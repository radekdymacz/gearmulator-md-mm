// doc/modern-ux/DESIGN-edit-flow.md: the plug-in path for one small edit, measured. The real processor
// (AudioPluginAudioProcessor, the code the VST3 wraps) with its desk session, the audio thread running
// processBlock at real time like a DAW, and the page's messages replayed on the message thread. Every
// hop is counted: page -> session, session -> page (bytes), host parameter notifications and gestures,
// updateHostDisplay, the controller's CCs and sync requests, MIDI bytes the firmware consumed, what
// the device sends out (tapped: Device -> Host routing on, so it shows in processBlock's MIDI out),
// parameter changes by origin, and processBlock time.
//
//   mdEditFlowPluginTest md|mm <ROM> [rate=48000] [block=128]
//
// Manual; exits 77 without a ROM. Isolated config (EphemeralConfig).

#include "mdPluginProcessor.h"
#include "mdController.h"
#include "mdDeskHost.h"
#include "mdDeskSession.h"

#include "mdLib/mddeskdevice.h"

#include "elektronData/json.h"
#include "mdDesk/mdDeskEdit.h"
#include "elektronData/mdJson.h"
#include "elektronData/mmPattern.h"
#include "elektronData/mmJson.h"
#include "elektronData/mmKit.h"

#include "juce_audio_utils/juce_audio_utils.h"
#include "jucePluginLib/controller.h"
#include "jucePluginLib/parameter.h"
#include "synthLib/midiRoutingMatrix.h"
#include "synthLib/romLoader.h"

#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

namespace
{
	using Value = elektronData::json::Value;
	using Clock = std::chrono::steady_clock;
	namespace ed = elektronData;

	Value parseJson(const std::string& _json)
	{
		auto v = ed::json::parse(_json);
		return v ? *v : Value::object();
	}

	double nowMs() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

	void pumpMessages(const double _ms)
	{
		const auto end = nowMs() + _ms;
		while(nowMs() < end)
		{
#if JUCE_MAC
			CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, false);
#else
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
#endif
		}
	}

	struct Counts
	{
		// session -> page
		std::map<std::string, int> pubByType;
		std::map<std::string, int> docByKind;
		size_t pubBytes = 0, pubMsgs = 0;
		// host
		std::atomic<int> hostParam{0}, gestureBegin{0}, gestureEnd{0}, hostChanged{0}, hostProgramChanged{0};
		// parameters by origin (onValueChanged)
		std::map<int, int> paramByOrigin;
		// audio thread
		std::atomic<int> outCc{0}, outSysex{0}, outOther{0};
		std::array<std::atomic<int>, 256> otherByStatus{};
		std::atomic<size_t> outSysexBytes{0};
		std::atomic<int> blocks{0}, overBlocks{0};
		std::atomic<double> blockUsSum{0}, blockUsMax{0};
		// device facts, read at start and end
		uint64_t rxStart = 0, rxEnd = 0, ccStart = 0, ccEnd = 0, syncStart = 0, syncEnd = 0;
		std::atomic<int> devSysex{0};
		std::atomic<size_t> devSysexBytes{0};
		std::map<int, int> devSysexByCmd;
		std::mutex devMutex;
		double editMs = 0;
	};
	Counts* g_c = nullptr;

	const char* originName(const int _o)
	{
		using O = pluginLib::Parameter::Origin;
		switch(static_cast<O>(_o))
		{
		case O::Ui: return "Ui";
		case O::HostAutomation: return "Host";
		case O::Midi: return "Midi";
		case O::PresetChange: return "Preset";
		case O::Derived: return "Derived";
		default: return "Unknown";
		}
	}

	struct HostListener final : juce::AudioProcessorListener
	{
		// DAW automation read: the host plays back what it recorded (on the audio thread, next block).
		std::atomic<bool> echo{false};
		// DAW automation read with lanes on these parameters: the value every block (constant here).
		std::atomic<int> readCount{0};
		std::mutex m;
		std::vector<std::pair<int, float>> recorded;

		void audioProcessorParameterChanged(juce::AudioProcessor*, const int _index, const float _v) override
		{
			if(g_c) ++g_c->hostParam;
			if(echo)
			{
				const std::lock_guard l(m);
				recorded.emplace_back(_index, _v);
			}
		}
		void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& _d) override
		{
			if(g_c) { ++g_c->hostChanged; if(_d.programChanged) ++g_c->hostProgramChanged; }
		}
		void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { if(g_c) ++g_c->gestureBegin; }
		void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { if(g_c) ++g_c->gestureEnd; }
	};

	struct Rig
	{
		md::MachineModel model;
		std::unique_ptr<mdJucePlugin::AudioPluginAudioProcessor> processor;
		juce::AudioProcessor* ap = nullptr;
		HostListener host;
		std::atomic<bool> run{true}, loopMidi{false};
		std::thread audio;
		std::vector<baseLib::EventListener<pluginLib::Parameter*>> paramListeners;
		std::unique_ptr<baseLib::EventListener<synthLib::SysexBuffer>> sysexListener;
		std::optional<Value> machine, workingKit, pattern;
		std::string lastDocJson;
		bool resetSeen = false;
		bool recording = false;
		std::vector<std::string> recorded;
		int errorsShown = 0;
		int rate, block;

		Rig(const md::MachineModel _m, const int _rate, const int _block) : model(_m), rate(_rate), block(_block)
		{
			mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig config;
			const auto home = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mdEditFlowPluginTest");
			home.createDirectory();
			config.deviceHomePath = home.getFullPathName().toStdString() + "/";
			processor = std::make_unique<mdJucePlugin::AudioPluginAudioProcessor>(model, config, false);
			ap = processor.get();
			ap->addListener(&host);
			ap->prepareToPlay(rate, block);
			// the tap: what the device sends shows in processBlock's MIDI out (off by default)
			processor->getMidiRoutingMatrix().setEnabled(synthLib::MidiEventSource::Device, synthLib::MidiEventSource::Host,
				synthLib::MidiRoutingMatrix::EventType::All, true);
			auto& controller = processor->getController();
			for(const auto& [idx, list] : controller.getExposedParameters())
				for(auto* p : list)
					paramListeners.emplace_back(p->onValueChanged, [](pluginLib::Parameter* _p)
					{
						if(g_c) ++g_c->paramByOrigin[static_cast<int>(_p->getChangeOrigin())];
					});
			auto& mdc = dynamic_cast<mdJucePlugin::Controller&>(controller);
			sysexListener = std::make_unique<baseLib::EventListener<synthLib::SysexBuffer>>(mdc.evDeviceSysex, [](const synthLib::SysexBuffer& _m)
			{
				if(!g_c) return;
				++g_c->devSysex;
				g_c->devSysexBytes += _m.size();
				const std::lock_guard l(g_c->devMutex);
				++g_c->devSysexByCmd[_m.size() > 6 ? _m[6] : -1];
			});
			audio = std::thread([this] { audioThread(); });
			auto* session = processor->getDeskHost()->session();
			session->attach([this](const Value& _m) { onPage(_m); });
		}

		~Rig()
		{
			processor->getDeskHost()->session()->detach();
			run = false;
			audio.join();
			ap->releaseResources();
			ap->removeListener(&host);
			paramListeners.clear();
			sysexListener.reset();
			processor.reset();
		}

		void audioThread()
		{
			juce::AudioBuffer<float> buf(ap->getTotalNumOutputChannels(), block);
			juce::MidiBuffer midi, loop;
			const double period = 1000.0 * block / rate;
			auto next = nowMs();
			while(run)
			{
				midi.clear();
				if(loopMidi)
					midi.swapWith(loop);
				loop.clear();
				if(host.echo)
				{
					std::vector<std::pair<int, float>> rec;
					{
						std::unique_lock l(host.m, std::try_to_lock);
						if(l.owns_lock())
							rec.swap(host.recorded);
					}
					auto& params = ap->getParameters();
					for(const auto& [i, v] : rec)
						if(i >= 0 && i < params.size())
							params[i]->setValue(v);	// the host's automation read, on the audio thread
				}
				if(const int n = host.readCount.load())
				{
					auto& params = ap->getParameters();
					for(int i = 0; i < n && i < params.size(); ++i)
						params[i]->setValue(params[i]->getValue());
				}
				buf.clear();
				const auto t = Clock::now();
				ap->processBlock(buf, midi);
				const double us = std::chrono::duration<double, std::micro>(Clock::now() - t).count();
				if(g_c)
				{
					++g_c->blocks;
					g_c->blockUsSum = g_c->blockUsSum + us;
					if(us > g_c->blockUsMax) g_c->blockUsMax = us;
					if(us > period * 1000.0) ++g_c->overBlocks;
					for(const auto meta : midi)
					{
						const auto msg = meta.getMessage();
						if(msg.isSysEx()) { ++g_c->outSysex; g_c->outSysexBytes += static_cast<size_t>(msg.getRawDataSize()); }
						else if(msg.isController()) ++g_c->outCc;
						else { ++g_c->outOther; const auto st = msg.getRawData()[0]; ++g_c->otherByStatus[st >= 0xf0 ? st : (st & 0xf0)]; }
						if(loopMidi)
							loop.addEvent(msg, 0);
					}
				}
				next += period;
				const auto wait = next - nowMs();
				if(wait > 0)
					std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(wait * 1000)));
				else if(wait < -100)
					next = nowMs();
			}
		}

		void onPage(const Value& _m)
		{
			const auto text = ed::json::write(_m);
			if(recording) recorded.push_back(text);
			const auto* t = _m.find("type");
			const std::string type = t && t->isString() ? t->asString() : "?";
			if(g_c)
			{
				++g_c->pubMsgs;
				g_c->pubBytes += text.size();
				++g_c->pubByType[type];
			}
			if(type == "reset") resetSeen = true;
			if(type == "result" && _m.find("ok") && !_m.find("ok")->asBool() && errorsShown < 5)
			{
				++errorsShown;
				std::printf("    result error: %s\n", text.substr(0, 200).c_str());
			}
			if(type == "machine")
				machine = *_m.find("doc");
			else if(type == "doc")
			{
				const auto kind = _m.find("kind")->asString();
				if(g_c) ++g_c->docByKind[kind];
				if(kind == "workingKit") { workingKit = *_m.find("doc"); lastDocJson = ed::json::write(*workingKit); }
				if(kind == "pattern" && _m.find("slot") && static_cast<int>(_m.find("slot")->asNumber()) == currentPattern()) pattern = *_m.find("doc");
			}
		}

		void pageText(const std::string& _json) { processor->getDeskHost()->session()->onPageMessage(parseJson(_json)); }
		void page(const Value& _v) { processor->getDeskHost()->session()->onPageMessage(_v); }

		template<typename T> T device(const std::function<T(md::DeskDevice&)>& _f, T _def)
		{
			return processor->getPlugin().withDeviceLocked([&](synthLib::Device* _d) { auto* d = dynamic_cast<md::DeskDevice*>(_d); return d ? _f(*d) : _def; });
		}
		uint64_t rxBytes() { return device<uint64_t>([](md::DeskDevice& _d) { return _d.getHardware().midiRxConsumedCount(); }, 0); }
		mdJucePlugin::Controller& controller() { return dynamic_cast<mdJucePlugin::Controller&>(processor->getController()); }

		std::string lifecycle() const
		{
			if(!machine) return "";
			const auto* l = machine->find("lifecycle");
			return l && l->isString() ? l->asString() : "";
		}
		int loading() const
		{
			if(!machine) return -1;
			const auto* d = machine->find("desk");
			const auto* l = d ? d->find("loading") : nullptr;
			return l && l->isNumber() ? static_cast<int>(l->asNumber()) : 0;
		}
		int currentPattern() const
		{
			if(!machine) return -1;
			const auto* k = machine->find("pattern");
			const auto* c = k ? k->find("current") : nullptr;
			return c && c->isNumber() ? static_cast<int>(c->asNumber()) : -1;
		}
		void prepareLockLane()
		{
			if(!pattern) { std::puts("  no pattern for the lock lane"); return; }
			std::vector<std::string> errors;
			if(model == md::MachineModel::Machinedrum)
			{
				const auto p = ed::patternFromJson(*pattern, errors);
				for(size_t st = 0; p && st < 16; ++st)
					if(!ed::hasTrig(*p, 0, st))
						pageText("{\"op\":\"trig\",\"p\":" + std::to_string(currentPattern()) + ",\"t\":0,\"s\":" + std::to_string(st) + ",\"on\":true}");
				return;
			}
			auto q = ed::mmPatternFromJson(*pattern, errors);
			if(!q) return;
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
			page(m);
		}
		int currentKit() const
		{
			if(!machine) return -1;
			const auto* k = machine->find("kit");
			const auto* c = k ? k->find("current") : nullptr;
			return c && c->isNumber() ? static_cast<int>(c->asNumber()) : -1;
		}
	};

	void begin(Rig& _r, Counts& _c)
	{
		_c.rxStart = _r.rxBytes();
		_c.ccStart = _r.controller().getTransmittedAutomationChangeCount();
		_c.syncStart = _r.controller().getSynchronizationRequestCount();
		g_c = &_c;
	}

	void end(Rig& _r, Counts& _c, const char* _name, const double _seconds, const int _changes)
	{
		g_c = nullptr;
		_c.rxEnd = _r.rxBytes();
		_c.ccEnd = _r.controller().getTransmittedAutomationChangeCount();
		_c.syncEnd = _r.controller().getSynchronizationRequestCount();
		const double n = std::max(1, _changes);
		const double period = 1000.0 * _r.block / _r.rate;
		const double load = _c.blocks ? 100.0 * (_c.blockUsSum / _c.blocks) / (period * 1000.0) : 0;
		std::printf("%-10s %4.1fs %3d changes | processBlock avg %5.1f %% of the block, max %6.0f us (block %4.0f us), over %d of %d\n",
			_name, _seconds, _changes, load, _c.blockUsMax.load(), period * 1000.0, _c.overBlocks.load(), _c.blocks.load());
		std::printf("    firmware MIDI in %6llu B (%6.0f /change) | controller CCs %4llu, sync requests %3llu | device out: SysEx %d (%zu B)",
			static_cast<unsigned long long>(_c.rxEnd - _c.rxStart), (_c.rxEnd - _c.rxStart) / n, static_cast<unsigned long long>(_c.ccEnd - _c.ccStart),
			static_cast<unsigned long long>(_c.syncEnd - _c.syncStart), _c.devSysex.load(), _c.devSysexBytes.load());
		for(const auto& [cmd, k] : _c.devSysexByCmd) std::printf(" [0x%02x x%d]", cmd, k);
		std::printf(", MIDI out CC %d SysEx %d other %d", _c.outCc.load(), _c.outSysex.load(), _c.outOther.load());
		for(int i = 0; i < 256; ++i) if(_c.otherByStatus[i]) std::printf(" [%02x x%d]", i, _c.otherByStatus[i].load());
		std::printf("\n");
		std::printf("    host: param notifications %d (%.1f /change), gestures %d/%d, updateHostDisplay %d (program %d) | params changed:",
			_c.hostParam.load(), _c.hostParam / n, _c.gestureBegin.load(), _c.gestureEnd.load(), _c.hostChanged.load(), _c.hostProgramChanged.load());
		for(const auto& [o, k] : _c.paramByOrigin) std::printf(" %s %d", originName(o), k);
		std::printf("\n    session onPageMessage %.0f us/change |", _c.editMs * 1000 / n);
		std::printf(" to page: %zu msgs, %zu B (%.0f B/change):", _c.pubMsgs, _c.pubBytes, _c.pubBytes / n);
		for(const auto& [t, k] : _c.pubByType) std::printf(" %s %d", t.c_str(), k);
		std::printf(" | docs:");
		for(const auto& [t, k] : _c.docByKind) std::printf(" %s %d", t.c_str(), k);
		std::printf("\n");
	}

	// One edit as the page sends it: MD a kit parameter value; MM the whole working kit with one value.
	void edit(Rig& _r, const int _n)
	{
		const int v = 40 + (_n % 40);
		if(_r.model == md::MachineModel::Machinedrum)
		{
			_r.pageText("{\"op\":\"param\",\"k\":" + std::to_string(_r.currentKit()) + ",\"t\":0,\"i\":12,\"v\":" + std::to_string(v) + ",\"g\":" + std::to_string(1000 + _n / 1000) + "}");
			return;
		}
		if(!_r.workingKit) { std::puts("  no working kit yet"); return; }
		std::vector<std::string> errors;
		auto k = ed::mmKitFromJson(*_r.workingKit, errors);
		if(!k) { std::puts("  working kit does not parse"); return; }
		k->tracks[0].pages[2][0] = static_cast<uint8_t>(v);	// FLT BASE
		Value m = Value::object();
		m.set("op", "set");
		m.set("kind", "workingKit");
		m.set("g", 1000 + _n / 1000);
		m.set("doc", ed::mmKitToJson(*k));
		_r.page(m);
	}

	// MD Control All (parameter tweaking) as the page sends it: one tweak per move (mdDeskLive.js since
	// DESIGN-edit-flow.md); EDITFLOW_OLDPAGE=1: as it did before, the knob on all 16 tracks as 16 params.
	void tweakMove(Rig& _r, const int _n)
	{
		if(_r.model == md::MachineModel::Monomachine)
		{
			// The MM page's Control All: the whole working kit, the knob on all six synth tracks, one set a frame.
			if(!_r.workingKit) return;
			std::vector<std::string> errors;
			auto k = ed::mmKitFromJson(*_r.workingKit, errors);
			if(!k) return;
			for(auto& tr : k->tracks)
				tr.pages[2][0] = static_cast<uint8_t>(std::clamp(tr.pages[2][0] + ((_n / 20) % 2 ? -1 : 1), 0, 127));
			Value m = Value::object();
			m.set("op", "set");
			m.set("kind", "workingKit");
			m.set("g", 7);
			m.set("doc", ed::mmKitToJson(*k));
			_r.page(m);
			return;
		}
		if(!std::getenv("EDITFLOW_OLDPAGE"))
		{
			_r.pageText("{\"op\":\"tweak\",\"k\":" + std::to_string(_r.currentKit()) + ",\"group\":\"syn\",\"knob\":1,\"d\":"
				+ std::to_string((_n / 20) % 2 ? -1 : 1) + ",\"t\":0,\"g\":7}");
			return;
		}
		for(int t = 0; t < 16; ++t)
			_r.pageText("{\"op\":\"param\",\"k\":" + std::to_string(_r.currentKit()) + ",\"t\":" + std::to_string(t) + ",\"i\":1,\"v\":" + std::to_string(20 + (_n + t) % 80) + ",\"g\":7}");
	}

	// A lock lane drawn over track 1's steps (MD: the lock op per cell; MM: the whole pattern per edit).
	void lockEdit(Rig& _r, const int _n)
	{
		const int s = (_n * 16 / 60) % 16, v = (_n * 7) % 128;
		if(_r.model == md::MachineModel::Machinedrum)
		{
			_r.pageText("{\"op\":\"lock\",\"p\":" + std::to_string(_r.currentPattern()) + ",\"t\":0,\"i\":1,\"s\":" + std::to_string(s)
				+ ",\"v\":" + std::to_string(v) + ",\"g\":" + std::to_string(3000 + _n / 1000) + "}");
			return;
		}
		if(!_r.pattern) return;
		std::vector<std::string> errors;
		auto q = ed::mmPatternFromJson(*_r.pattern, errors);
		if(!q) return;
		q->notes[0][static_cast<size_t>(s)] = static_cast<uint8_t>(36 + _n % 24);
		Value m = Value::object();
		m.set("op", "set");
		m.set("kind", "pattern");
		m.set("g", 3000);
		m.set("doc", ed::mmPatternToJson(*q));
		_r.page(m);
	}

	// Control All through the real processor: one tweak per case, the working kit from memory after it,
	// against the model's rule (mdDesk::controlAllReaches). Returns the failures.
	int controlAllTruth(Rig& _r)
	{
		struct Case { const char* group; int knob; int d; int t; };
		const Case cases[] = {{"rt", 1, 5, 0}, {"syn", 0, 6, 0}, {"fx", 0, 4, 0}, {"syn", 1, -3, 5}, {"rt", 5, -3, 2}, {"syn", 2, 2, 0}};
		int fails = 0;
		for(const auto& c : cases)
		{
			std::vector<std::string> errors;
			const auto before = _r.workingKit ? ed::kitFromJson(*_r.workingKit, errors) : std::nullopt;
			if(!before) { std::puts("  controlall: no working kit"); return 1; }
			_r.pageText("{\"op\":\"tweak\",\"k\":" + std::to_string(_r.currentKit()) + ",\"group\":\"" + c.group + "\",\"knob\":"
				+ std::to_string(c.knob) + ",\"d\":" + std::to_string(c.d) + ",\"t\":" + std::to_string(c.t) + ",\"g\":" + std::to_string(4000 + c.knob) + "}");
			pumpMessages(2500);
			const auto after = ed::kitFromJson(*_r.workingKit, errors);
			const size_t index = (std::string(c.group) == "fx" ? 8u : std::string(c.group) == "rt" ? 16u : 0u) + static_cast<size_t>(c.knob);
			int moved = 0, wrong = 0;
			for(size_t t = 0; t < 16; ++t)
			{
				const int want = mdDesk::controlAllReaches(before->models[t], index) ? std::clamp(before->params[t][index] + c.d, 0, 127) : before->params[t][index];
				moved += after->params[t][index] != before->params[t][index];
				wrong += after->params[t][index] != want;
			}
			const auto* d = _r.machine ? _r.machine->find("desk") : nullptr;
			std::printf("  controlall %s knob %d %+d: %d tracks moved, %d differ from the model (knobPage %d, tx %d)\n", c.group, c.knob, c.d, moved, wrong,
				d && d->find("knobPage") ? static_cast<int>(d->find("knobPage")->asNumber()) : -9, d && d->find("tx") && d->find("tx")->asBool());
			fails += wrong != 0;
		}
		std::printf("controlall: %s\n", fails ? "FAIL" : "PASS");
		return fails;
	}

	void scenario(Rig& _r, const char* _name, const int _changes, const double _spacingMs, const double _tailMs, const int _kind = 0)
	{
		Counts c;
		begin(_r, c);
		const auto t0 = nowMs();
		for(int i = 0; i < _changes; ++i)
		{
			const auto te = nowMs();
			if(_kind == 1) tweakMove(_r, i); else if(_kind == 2) lockEdit(_r, i); else edit(_r, i + static_cast<int>(t0) % 7);
			c.editMs += nowMs() - te;
			pumpMessages(_spacingMs);
		}
		pumpMessages(_tailMs);
		end(_r, c, _name, (nowMs() - t0) / 1000, _changes);
	}
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 3)
	{
		std::puts("usage: mdEditFlowPluginTest md|mm <ROM> [rate] [block]");
		return 77;
	}
	juce::ScopedJuceInitialiser_GUI juce;
	synthLib::RomLoader::addSearchPath(juce::File(_argv[2]).getParentDirectory().getFullPathName().toStdString());
	const auto model = std::string(_argv[1]) == "mm" ? md::MachineModel::Monomachine : md::MachineModel::Machinedrum;
	const int rate = _argc > 3 ? std::atoi(_argv[3]) : 48000;
	const int block = _argc > 4 ? std::atoi(_argv[4]) : 128;
	{
		Rig r(model, rate, block);
		std::printf("%s at %d Hz, %d-frame blocks\n", _argv[1], rate, block);
		// Boot (the processor may reboot the machine once for its factory flash), the page's ready,
		// the library read: settled when ready, nothing loading and a kit known for 3 s in a row.
		const auto t0 = nowMs();
		double stableSince = -1;
		int resets = 0;
		while(nowMs() - t0 < 120000)
		{
			if(r.resetSeen) { r.resetSeen = false; ++resets; r.pageText(R"({"op":"ready"})"); }
			if(!r.machine && static_cast<int>(nowMs() - t0) % 2000 < 25)
				r.pageText(R"({"op":"ready"})");
			const bool ok = r.lifecycle() == "ready" && r.loading() == 0 && r.currentKit() >= 0;
			if(!ok) stableSince = -1;
			else if(stableSince < 0) stableSince = nowMs();
			else if(nowMs() - stableSince > 3000) break;
			pumpMessages(25);
		}
		std::printf("settled after %.1f s (%d resets), kit %d\n", (nowMs() - t0) / 1000, resets, r.currentKit());
		if(const auto* rec = std::getenv("EDITFLOW_RECORD"))
		{
			// A fresh ready: everything a page gets, for replaying into the real page in a browser.
			r.recording = true;
			r.pageText(R"({"op":"ready"})");
			pumpMessages(2000);
			r.recording = false;
			std::string all = "[";
			for(size_t i = 0; i < r.recorded.size(); ++i) all += (i ? ",\n" : "") + r.recorded[i];
			all += "]";
			juce::File(rec).replaceWithText(all);
			std::printf("recorded %zu page messages to %s\n", r.recorded.size(), rec);
		}
		r.pageText(R"({"op":"play"})");
		pumpMessages(1500);
		std::printf("kit %d, lifecycle %s\n", r.currentKit(), r.lifecycle().c_str());
		scenario(r, "idle", 0, 0, 3000);
		scenario(r, "one", 1, 0, 3000);
		scenario(r, "steps10", 10, 300, 2000);
		scenario(r, "drag60", 120, 1000.0 / 60, 2000);
		scenario(r, "tweak60", 60, 1000.0 / 60, 2000, 1);
		if(model == md::MachineModel::Machinedrum && std::getenv("EDITFLOW_CONTROLALL") && controlAllTruth(r))
			return 1;
		// The lock lane: trigs on track 1's steps 1-16 first (the MD holds locks on trigs; the MM notes).
		r.prepareLockLane();
		pumpMessages(1500);
		scenario(r, "lock60", 240, 1000.0 / 60, 2000, 2);
		r.host.echo = true;
		scenario(r, "drag+echo", 120, 1000.0 / 60, 2000);
		r.host.echo = false;
		pumpMessages(500);
		r.loopMidi = true;
		scenario(r, "one+loop", 1, 0, 3000);
		scenario(r, "drag+loop", 120, 1000.0 / 60, 2000);
		r.loopMidi = false;
		pumpMessages(500);
		{
			auto& params = r.ap->getParameters();
			std::printf("host read on parameters 0-15: %s .. %s\n", params[0]->getName(40).toRawUTF8(), params[15]->getName(40).toRawUTF8());
		}
		r.host.readCount = 1;
		scenario(r, "read1", 0, 0, 3000);
		r.host.readCount = 16;
		scenario(r, "read16", 0, 0, 3000);
		r.host.readCount = 0;
		pumpMessages(500);
		scenario(r, "idle2", 0, 0, 3000);
		if(!r.lastDocJson.empty())
		{
			const auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(std::string("editflow-") + _argv[1] + "-workingKit.json");
			f.replaceWithText(r.lastDocJson);
			std::printf("last working kit JSON: %s\n", f.getFullPathName().toRawUTF8());
		}
	}
	return 0;
}
