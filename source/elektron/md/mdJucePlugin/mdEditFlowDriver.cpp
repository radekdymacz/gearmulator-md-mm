#include "mdEditFlowDriver.h"

#include "mdController.h"
#include "mdDeskSession.h"
#include "mdEditFlowCounters.h"
#include "mdPluginProcessor.h"

#include "mdLib/mddeskdevice.h"

#include "elektronData/json.h"
#include "elektronData/mdJson.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mmJson.h"

#include "juce_events/juce_events.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace mdJucePlugin
{
	namespace
	{
		namespace ed = elektronData;
		using Value = ed::json::Value;

		double now() { return juce::Time::getMillisecondCounterHiRes(); }
		int intAt(const Value* _v, const char* _a, const char* _b = nullptr)
		{
			if(!_v) return -1;
			const auto* x = _v->find(_a);
			if(_b && x) x = x->find(_b);
			return x && x->isNumber() ? static_cast<int>(x->asNumber()) : -1;
		}

		// A counter snapshot: what the phase's numbers are the difference of.
		struct Snap
		{
			double ms = 0;
			uint64_t ccs = 0, syncs = 0, rx = 0, sysexBytes = 0;
			std::array<uint32_t, 128> sysex{};
			uint32_t panel = 0;
			int undo = 0;
		};

		struct Phase
		{
			std::string name;
			double rate = 0;		// edits a second
			double activeMs = 0, tailMs = 0;
			int kind = 0;			// 0 idle, 1 tweak, 2 lock
		};

		class Driver final : juce::Timer
		{
		public:
			Driver(AudioPluginAudioProcessor& _p, DeskSession& _s, std::string _out, const bool _oldPage)
				: m_p(_p), m_s(_s), m_out(std::move(_out)), m_oldPage(_oldPage)
			{
				m_mm = _p.getModel() == md::MachineModel::Monomachine;
				auto& c = dynamic_cast<Controller&>(_p.getController());
				m_sysexListener = std::make_unique<baseLib::EventListener<pluginLib::SysEx>>(c.evDeviceSysex, [this](const pluginLib::SysEx& _m)
				{
					++m_replies[_m.size() > 6 ? _m[6] : 0];
				});
				m_s.attach([this](const Value& _m) { onPage(_m); });
				m_phases = {{"idle", 0, 4000, 0, 0}, {"tweak", 60, 4000, 1000, 1}, {"idle2", 0, 2000, 0, 0},
					{"lock", 60, 4000, 1500, 2}, {"idle3", 0, 2000, 0, 0}};
				m_file = std::fopen(m_out.c_str(), "w");
				line("start model=%s page=%s", m_mm ? "mm" : "md", m_oldPage ? "old" : "new");
				m_t0 = now();
				startTimer(2);
			}

			~Driver() override
			{
				stopTimer();
				m_s.detach();
				if(m_file) std::fclose(m_file);
			}

		private:
			void line(const char* _fmt, ...)
			{
				if(!m_file) return;
				va_list a;
				va_start(a, _fmt);
				std::vfprintf(m_file, _fmt, a);
				va_end(a);
				std::fputc('\n', m_file);
				std::fflush(m_file);
			}

			void send(const Value& _m)
			{
				const auto t = now();
				m_s.onPageMessage(_m);
				const auto us = (now() - t) * 1000.0;
				m_sessionUs += us;
				m_sessionMaxUs = std::max(m_sessionMaxUs, us);
				++m_sent;
			}
			void sendText(const std::string& _json)
			{
				if(auto v = ed::json::parse(_json)) send(*v);
			}

			void onPage(const Value& _m)
			{
				const auto* t = _m.find("type");
				const std::string type = t && t->isString() ? t->asString() : "";
				++m_pubMsgs;
				m_pubBytes += ed::json::write(_m).size();
				if(type == "reset") m_reset = true;
				else if(type == "machine") m_machine = *_m.find("doc");
				else if(type == "doc")
				{
					const auto kind = _m.find("kind")->asString();
					++m_docs[kind];
					if(kind == "workingKit") m_workingKit = *_m.find("doc");
					else if(kind == "pattern") m_patterns[intAt(&_m, "slot")] = *_m.find("doc");
				}
				else if(type == "result")
				{
					if(const auto* ok = _m.find("ok"); ok && ok->isBool() && !ok->asBool())
					{
						++m_errors;
						if(m_errors < 4) line("error %s", ed::json::write(_m).substr(0, 300).c_str());
					}
				}
			}

			std::string lifecycle() const
			{
				const auto* l = m_machine ? m_machine->find("lifecycle") : nullptr;
				return l && l->isString() ? l->asString() : "";
			}
			// What the background read has left: MD machine.desk.loading, MM machine.loading {done, total}.
			int loading() const
			{
				if(!m_machine) return -1;
				if(!m_mm) return intAt(&*m_machine, "desk", "loading");
				const int done = intAt(&*m_machine, "loading", "done"), total = intAt(&*m_machine, "loading", "total");
				return done < 0 || total < 0 ? -1 : std::max(0, total - done);
			}
			int kit() const { return m_machine ? intAt(&*m_machine, "kit", "current") : -1; }
			int pattern() const { return m_machine ? intAt(&*m_machine, "pattern", "current") : -1; }
			int undoCount() const { return m_machine ? intAt(&*m_machine, "history", "undoCount") : -1; }

			uint64_t rxBytes() const
			{
				return m_p.getPlugin().withDeviceLocked([](synthLib::Device* _d) -> uint64_t
				{
					auto* d = dynamic_cast<md::DeskDevice*>(_d);
					return d ? d->getHardware().midiRxConsumedCount() : 0;
				});
			}

			Snap snap() const
			{
				Snap s;
				s.ms = now();
				auto& c = dynamic_cast<Controller&>(m_p.getController());
				s.ccs = c.getTransmittedAutomationChangeCount();
				s.syncs = c.getSynchronizationRequestCount();
				s.rx = rxBytes();
#if MDMM_EDITFLOW_DRIVER
				auto& k = editFlow::counters();
				s.sysexBytes = k.sysexBytes.load();
				for(size_t i = 0; i < 128; ++i) s.sysex[i] = k.sysexByCmd[i].load();
				s.panel = k.panelPackets.load();
#endif
				s.undo = undoCount();
				return s;
			}

			void timerCallback() override
			{
				const auto t = now();
				if(m_state == 0)
				{
					// settle: the page's ready, the machine ready, the library read, a kit known for 3 s
					if(m_reset) { m_reset = false; sendText(R"({"op":"ready"})"); }
					if(!m_readySent || (!m_machine && t - m_readyMs > 2000)) { m_readySent = true; m_readyMs = t; sendText(R"({"op":"ready"})"); }
					const bool ok = lifecycle() == "ready" && loading() == 0 && kit() >= 0 && pattern() >= 0;
					if(!ok) m_stable = -1;
					else if(m_stable < 0) m_stable = t;
					else if(t - m_stable > 3000)
					{
						line("settled ms=%.0f kit=%d pattern=%d", t - m_t0, kit(), pattern());
						sendText(R"({"op":"play"})");
						prepareLockLane();
						m_state = 1;
						m_next = t + 3000;
					}
					if(t - m_t0 > 240000 && m_state == 0)
					{
						// Measure anyway, and say so: the background read may still run.
						const auto* l = m_machine ? m_machine->find("loading") : nullptr;
						line("settle timeout lifecycle=%s loading=%d (%s) kit=%d pattern=%d: measuring anyway", lifecycle().c_str(), loading(),
							l ? ed::json::write(*l).c_str() : "-", kit(), pattern());
						sendText(R"({"op":"play"})");
						prepareLockLane();
						m_state = 1;
						m_next = t + 3000;
					}
					return;
				}
				if(m_state == 1)
				{
					if(t < m_next) return;
					startPhase(t);
					return;
				}
				if(m_state == 2)
				{
					auto& ph = m_phases[m_phase];
					const double el = t - m_phaseT0;
					if(el < ph.activeMs && ph.rate > 0)
						while(m_n < el / 1000.0 * ph.rate)
							edit(ph, m_n++);
					if(el >= ph.activeMs && m_activeEnd < 0)
					{
						m_activeEnd = t;
						m_atActiveEnd = snapPage();
					}
					if(el >= ph.activeMs + ph.tailMs)
						endPhase(t);
				}
			}

			struct PageSnap { int sent; double sessionUs; size_t pubMsgs, pubBytes; std::map<std::string, int> docs; std::map<int, int> replies; };
			PageSnap snapPage() const { return {m_sent, m_sessionUs, m_pubMsgs, m_pubBytes, m_docs, m_replies}; }

			void startPhase(const double _t)
			{
				m_state = 2;
				m_phaseT0 = _t;
				m_n = 0;
				m_activeEnd = -1;
				m_start = snap();
				m_startPage = snapPage();
				m_sessionMaxUs = 0;
				m_g = 5000 + m_phase;
				m_base.reset();
			}

			void endPhase(const double _t)
			{
				const auto e = snap();
				const auto p = snapPage();
				const auto& ph = m_phases[m_phase];
				const auto& a = m_activeEnd >= 0 ? m_atActiveEnd : p;
				const int edits = m_n;
				std::string sx, rp, dc;
				for(size_t i = 0; i < 128; ++i)
					if(const auto n = e.sysex[i] - m_start.sysex[i]) sx += " " + std::to_string(i) + ":" + std::to_string(n);
				for(const auto& [c, n] : p.replies)
				{
					const auto it = m_startPage.replies.find(c);
					if(const auto d = n - (it == m_startPage.replies.end() ? 0 : it->second)) rp += " " + std::to_string(c) + ":" + std::to_string(d);
				}
				for(const auto& [k, n] : p.docs)
				{
					const auto it = m_startPage.docs.find(k);
					if(const auto d = n - (it == m_startPage.docs.end() ? 0 : it->second)) dc += " " + k + ":" + std::to_string(d);
				}
				line("phase name=%s t0=%.3f activeEnd=%.3f t1=%.3f edits=%d pageMsgs=%d sessionUsTotal=%.0f sessionUsMax=%.0f"
					" ccs=%llu syncs=%llu rxBytes=%llu sysexOutBytes=%llu panelPackets=%u undoSteps=%d toPageMsgs=%zu toPageBytes=%zu"
					" activeToPageBytes=%zu sysexOut=[%s ] deviceReplies=[%s ] docs=[%s ]",
					ph.name.c_str(), m_phaseT0, m_activeEnd >= 0 ? m_activeEnd : _t, _t, edits, p.sent - m_startPage.sent,
					p.sessionUs - m_startPage.sessionUs, m_sessionMaxUs,
					static_cast<unsigned long long>(e.ccs - m_start.ccs), static_cast<unsigned long long>(e.syncs - m_start.syncs),
					static_cast<unsigned long long>(e.rx - m_start.rx), static_cast<unsigned long long>(e.sysexBytes - m_start.sysexBytes),
					e.panel - m_start.panel, e.undo - m_start.undo, p.pubMsgs - m_startPage.pubMsgs, p.pubBytes - m_startPage.pubBytes,
					a.pubBytes - m_startPage.pubBytes, sx.c_str(), rp.c_str(), dc.c_str());
				if(++m_phase >= m_phases.size())
				{
					finish();
					return;
				}
				startPhase(_t);
			}

			void finish()
			{
				line("done errors=%d", m_errors);
				m_state = 3;
				stopTimer();
			}

			// A triangle 0..20 in steps of one: the move's delta is +-1.
			static int tri(const int _n) { const int k = _n % 40; return k < 20 ? k : 40 - k; }

			void edit(const Phase& _ph, const int _n)
			{
				if(_ph.kind == 1) tweak(_n);
				else if(_ph.kind == 2) lock(_n);
			}

			void tweak(const int _n)
			{
				const int d = tri(_n + 1) - tri(_n);
				if(m_mm)
				{
					// The MM page's Control All: the whole working kit, the knob on all six synth tracks.
					if(!m_workingKit) return;
					std::vector<std::string> errors;
					if(!m_mmBase)
						if(auto k = ed::mmKitFromJson(*m_workingKit, errors)) m_mmBase = *k;
					if(!m_mmBase) return;
					auto k = *m_mmBase;
					for(auto& tr : k.tracks)
						tr.pages[2][0] = static_cast<uint8_t>(std::clamp(tr.pages[2][0] + tri(_n + 1), 0, 127));	// FLT BASE
					Value m = Value::object();
					m.set("op", "set");
					m.set("kind", "workingKit");
					m.set("g", m_g);
					m.set("doc", ed::mmKitToJson(k));
					send(m);
					return;
				}
				if(!m_oldPage)
				{
					sendText("{\"op\":\"tweak\",\"k\":" + std::to_string(kit()) + ",\"group\":\"syn\",\"knob\":1,\"d\":" + std::to_string(d)
						+ ",\"g\":" + std::to_string(m_g) + "}");
					return;
				}
				// Before: mdDeskLive.js sent the same delta for every track as its own param message.
				if(!m_base && m_workingKit)
				{
					std::vector<std::string> errors;
					if(auto k = ed::kitFromJson(*m_workingKit, errors)) m_base = *k;
				}
				if(!m_base) return;
				for(uint8_t t = 0; t < 16; ++t)
				{
					const auto name = ed::mdMachineName(m_base->models[t]);
					if(name.rfind("RAM", 0) == 0 || name.rfind("MID", 0) == 0 || name.rfind("CTR", 0) == 0) continue;
					const int v = std::clamp(m_base->params[t][1] + tri(_n + 1), 0, 127);
					sendText("{\"op\":\"param\",\"k\":" + std::to_string(kit()) + ",\"t\":" + std::to_string(t) + ",\"i\":1,\"v\":"
						+ std::to_string(v) + ",\"g\":" + std::to_string(m_g) + "}");
				}
			}

			// The lock lane is drawn over track 1's steps 1-16: the MD holds locks on trigs only, so they
			// get trigs first (before the phases).
			void prepareLockLane()
			{
				if(m_mm)
				{
					// The MM holds notes on trigs only: track 1 gets trigs on steps 1-16, one set.
					const auto it = m_patterns.find(pattern());
					if(it == m_patterns.end())
						return;
					std::vector<std::string> errors;
					auto q = ed::mmPatternFromJson(it->second, errors);
					if(!q)
						return;
					for(size_t st = 0; st < 16; ++st)
					{
						const uint64_t bit = uint64_t(1) << st;
						q->pitch[0] |= bit; q->amp[0] |= bit; q->filter[0] |= bit; q->lfo[0] |= bit;
						if(q->notes[0][st] == ed::MmPattern::g_noNote) q->notes[0][st] = 60;
					}
					Value m = Value::object();
					m.set("op", "set");
					m.set("kind", "pattern");
					m.set("doc", ed::mmPatternToJson(*q));
					send(m);
					line("prepared lock lane: trigs on track 1 steps 1-16");
					return;
				}
				const auto it = m_patterns.find(pattern());
				if(it == m_patterns.end())
					return;
				std::vector<std::string> errors;
				const auto p = ed::patternFromJson(it->second, errors);
				if(!p)
					return;
				int added = 0;
				for(size_t s = 0; s < 16; ++s)
					if(!ed::hasTrig(*p, 0, s))
					{
						++added;
						sendText("{\"op\":\"trig\",\"p\":" + std::to_string(pattern()) + ",\"t\":0,\"s\":" + std::to_string(s) + ",\"on\":true}");
					}
				line("prepared lock lane: %d trigs added on track 1", added);
			}

			void lock(const int _n)
			{
				const int s = (_n * 16 / 60) % 16, v = (_n * 7) % 128;
				if(!m_mm)
				{
					sendText("{\"op\":\"lock\",\"p\":" + std::to_string(pattern()) + ",\"t\":0,\"i\":1,\"s\":" + std::to_string(s) + ",\"v\":"
						+ std::to_string(v) + ",\"g\":" + std::to_string(m_g) + "}");
					return;
				}
				// The MM page sends the whole pattern at the gesture, one value changed.
				const auto it = m_patterns.find(pattern());
				if(it == m_patterns.end()) return;
				std::vector<std::string> errors;
				auto q = ed::mmPatternFromJson(it->second, errors);
				if(!q) return;
				int row = -1;
				for(int i = 0; i < q->lockRowCount && row < 0; ++i)
					for(size_t st = 0; st < 16; ++st)
						if(q->lockRows[i][st] != ed::MmPattern::g_noLock) { row = i; break; }
				if(row >= 0) q->lockRows[static_cast<size_t>(row)][static_cast<size_t>(s)] = static_cast<uint8_t>(v);
				else q->notes[0][static_cast<size_t>(s)] = static_cast<uint8_t>(36 + _n % 24);
				Value m = Value::object();
				m.set("op", "set");
				m.set("kind", "pattern");
				m.set("g", m_g);
				m.set("doc", ed::mmPatternToJson(*q));
				send(m);
			}

			AudioPluginAudioProcessor& m_p;
			DeskSession& m_s;
			std::string m_out;
			bool m_oldPage, m_mm = false;
			FILE* m_file = nullptr;
			std::unique_ptr<baseLib::EventListener<pluginLib::SysEx>> m_sysexListener;
			std::optional<Value> m_machine, m_workingKit;
			std::map<int, Value> m_patterns;
			std::optional<ed::MdKit> m_base;
			std::optional<ed::MmKit> m_mmBase;
			std::map<std::string, int> m_docs;
			std::map<int, int> m_replies;
			size_t m_pubMsgs = 0, m_pubBytes = 0;
			int m_sent = 0, m_errors = 0;
			double m_sessionUs = 0, m_sessionMaxUs = 0;
			bool m_reset = false, m_readySent = false;
			double m_readyMs = 0, m_stable = -1, m_t0 = 0, m_next = 0;
			int m_state = 0;
			std::vector<Phase> m_phases;
			size_t m_phase = 0;
			double m_phaseT0 = 0, m_activeEnd = -1;
			int m_n = 0;
			int64_t m_g = 0;
			Snap m_start;
			PageSnap m_startPage, m_atActiveEnd;
		};
	}

	std::shared_ptr<void> startEditFlowDriver(AudioPluginAudioProcessor& _processor, DeskSession& _session)
	{
		const auto* out = std::getenv("GEARMULATOR_EDITFLOW_DRIVE");
		if(!out || !*out)
			return {};
		const auto* page = std::getenv("GEARMULATOR_EDITFLOW_PAGE");
		return std::make_shared<Driver>(_processor, _session, out, page && std::string(page) == "old");
	}
}
