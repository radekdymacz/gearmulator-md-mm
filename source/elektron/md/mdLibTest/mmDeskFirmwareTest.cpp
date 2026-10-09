// MM-P2 smoke test: the Monomachine Editor's desk (mmDesk::Desk) against the
// real MM OS 1.32B firmware, headless. The same Desk code as the plug-in; the
// Port here drives an emulated machine. Manual: needs a user-supplied ROM.
//
//   mmDeskFirmwareTest <MM-ROM>
//
// Exits 77 (skip) without arguments.

#include "contractCheck.h"
#include "mdFirmwareSession.h"

#include "elektronData/mmCommands.h"
#include "elektronData/mmJson.h"
#include "elektronData/mmKit.h"
#include "elektronData/mmPattern.h"
#include "elektronData/mmValidate.h"
#include "elektronData/syxImport.h"

#include "mdLib/mdautomation.h"
#include "mdLib/mmtelemetry.h"

#include <functional>

#include "mmDesk/mmDesk.h"
#include "mmDesk/mmDeskWirePort.h"
#include "mmDesk/mmRecv.h"

#include "deskWire/mmWire.h"

#include "../mdJucePlugin/mdSyxSession.h"	// the session's .syx import, header only (B-019)

#include <cmath>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>

using namespace mdFirmwareSession;
namespace ed = elektronData;
using ed::json::Value;

namespace
{
	int g_failures = 0;
	contractCheck::Checker g_contract(MMDESK_SCHEMA);
	constexpr auto g_mm = md::MachineModel::Monomachine;

	void check(const bool _ok, const std::string& _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++g_failures;
	}

	std::optional<md::PanelControl> control(const mmDesk::Key _k)
	{
		using K = mmDesk::Key;
		using C = md::PanelControl;
		switch(_k)
		{
		case K::Exit: return C::Exit;
		case K::Enter: return C::Enter;
		case K::Up: return C::Up;
		case K::Down: return C::Down;
		case K::Left: return C::Left;
		case K::Right: return C::Right;
		case K::Play: return C::Play;
		case K::Stop: return C::Stop;
		case K::Global: return C::Kit;	// with FUNCTION
		case K::Record: return C::Record;
		case K::LiveRecord: return C::Play;	// with RECORD held
		case K::MuteWindow: return C::BankGroup;	// with FUNCTION
		case K::BankGroup: return C::BankGroup;
		case K::Trig9: case K::Trig10: case K::Trig11: case K::Trig12: case K::Trig13: case K::Trig14:
			return static_cast<C>(static_cast<int>(C::Trigger1) + 8 + (static_cast<int>(_k) - static_cast<int>(K::Trig9)));
		}
		return std::nullopt;
	}

	struct Rig
	{
		Machine m;
		std::deque<Bytes> out;							// MIDI to send, in order
		std::deque<std::pair<uint64_t, md::PanelPacket>> panel;	// row states at machine frames
		std::vector<Value> page;
		std::unique_ptr<mmDesk::Desk> desk;
		md::MmTelemetry tel;
		uint8_t channel = 0;	// the machine's base channel (the adapter's fact)

		// _hw (MM-P4): the desk drives the emulated machine as a real Monomachine over MIDI: the plug-in's
		// wire port (deskWire, DIN speed both ways), no panel keys, no telemetry, no memory. The panel
		// queue stays, for the person at the machine (userKeys).
		explicit Rig(const Bytes& _rom, const bool _hw = false) : m(_rom, "mm", {}, true, g_mm), hw(_hw)
		{
			mmDesk::Desk::Port port;
			port.device.sendSysex = [this](const Bytes& _b) { out.push_back(_b); };
			// Parameters and NRPN as the plug-in's wire encodes them (deskWire), on the base channel.
			port.device.sendParam = [this](const uint8_t _t, const uint8_t _p, const uint8_t _i, const uint8_t _v)
			{
				if(const auto cc = deskWire::mm::param(channel, _t, _p, _i, _v))
					out.push_back(*cc);
			};
			port.device.sendNrpn = [this](const uint8_t _t, const uint8_t _p, const uint8_t _v)
			{
				for(auto& b : deskWire::mm::nrpn(channel, _t, _p, _v))
					out.push_back(std::move(b));
			};
			port.device.baseChannel = [this](const uint8_t _ch) { channel = _ch; };
			// The keyboard's notes (noteOn) as the plug-in's wire encodes them.
			port.device.sendNote = [this](const uint8_t _ch, const uint8_t _n, const uint8_t _v)
			{
				if(const auto b = deskWire::note(_ch, _n, _v))
					out.push_back(*b);
			};
			port.device.pressKeys = [this](const std::vector<mmDesk::Key>& _keys) { return userKeys(_keys); };
			port.device.pressBankTrigs = [this](const uint8_t _b, const std::vector<uint8_t>& _t) { return userBankTrigs(_b, _t); };
			port.device.nowMs = [this] { return ms(); };
			if(hw)
				port.device = mmDesk::wirePort(wire, channel, [this] { return ms(); });
			port.toPage = [this](const Value& _v) { g_contract(_v); page.push_back(_v); };
			desk = hw ? std::make_unique<mmDesk::Desk>(port, mmDesk::wireProfile()) : std::make_unique<mmDesk::Desk>(port);
			m.onSysex = [this](const Bytes& _b)
			{
				if(!hw)
					replies.push_back(_b);
				else if(!unplugged)
					toDesk.send(ms(), _b);	// the machine's MIDI out is DIN too
			};
			m.onBlock = [this]
			{
				while(!panel.empty() && panel.front().first <= m.now())
				{
					m.hardware().trySendPanelEvent(panel.front().second.row, panel.front().second.mask);
					panel.pop_front();
				}
			};
		}

		// Keys on the machine's panel: the desk's (the emulator engine) or the person's (HW MIDI tests).
		bool userKeys(const std::vector<mmDesk::Key>& _keys)
		{
			uint64_t at = std::max(m.now(), panel.empty() ? 0 : panel.back().first) + 64;
			const auto hold = static_cast<uint64_t>(g_rate / 100);	// 10 ms
			const auto fn = *md::panelPacket(g_mm, md::PanelControl::Function);
			const auto rec = *md::panelPacket(g_mm, md::PanelControl::Record);
			for(const auto k : _keys)
			{
				const auto c = control(k);
				const auto pk = *md::panelPacket(g_mm, *c);
				if(mmDesk::isChord(k))
				{
					const auto held = mmDesk::heldIsRecord(k) ? rec : fn;
					md::PanelRowState rows;
					for(const auto st : {rows.press(held), rows.press(pk), rows.release(pk), rows.release(held)})
					{
						panel.emplace_back(at, st);
						at += hold;
					}
					continue;
				}
				panel.emplace_back(at, pk);
				at += hold;
				panel.emplace_back(at, md::PanelPacket{pk.row, 0});
				at += hold;
			}
			return true;
		}

		// MM-P8: BANK held, the TRIG keys in order, held (the plug-in's md::panelKeySequence "chain:").
		bool userBankTrigs(const uint8_t _bank, const std::vector<uint8_t>& _trigs)
		{
			std::string spec = "chain:" + std::to_string(_bank & 3) + ":";
			for(size_t i = 0; i < _trigs.size(); ++i)
				spec += (i ? "," : "") + std::to_string(_trigs[i]);
			const auto states = md::panelKeySequence(g_mm, spec);
			if(states.empty())
				return false;
			uint64_t at = std::max(m.now(), panel.empty() ? 0 : panel.back().first) + 64;
			const auto hold = static_cast<uint64_t>(g_rate / 100);	// 10 ms
			for(const auto& st : states)
			{
				panel.emplace_back(at, st);
				at += hold;
			}
			return true;
		}

		// HW MIDI: a message reaches the other side when its last byte has (the cable at DIN speed).
		struct Cable
		{
			double freeAt = 0;
			std::deque<std::pair<double, Bytes>> flight;
			void send(const double _now, Bytes _b)
			{
				freeAt = std::max(freeAt, _now) + deskCore::DinPacer::wireMs(_b.size());
				flight.emplace_back(freeAt, std::move(_b));
			}
			std::vector<Bytes> arrived(const double _now)
			{
				std::vector<Bytes> r;
				while(!flight.empty() && flight.front().first <= _now)
				{
					r.push_back(std::move(flight.front().second));
					flight.pop_front();
				}
				return r;
			}
		};
		bool hw = false;
		bool unplugged = false;
		Cable toMachine, toDesk;
		size_t hwBytesOut = 0, hwBytesIn = 0;
		deskWire::MidiWire wire{[this](const Bytes& _b) { toMachine.send(ms(), _b); }, [this]
		{
			auto r = toDesk.arrived(ms());
			for(const auto& b : r) hwBytesIn += b.size();
			return r;
		}};

		std::vector<Bytes> replies;
		size_t bytesSent = 0, dumpsSent = 0, ccsSent = 0;	// B-014: what the desk sent the machine
		double ms() const { return m.now() * 1000.0 / g_rate; }

		mmDesk::Telemetry readTelemetry()
		{
			tel.publish([this](const uint32_t _a) { return m.read8(_a); });
			mmDesk::Telemetry t;
			t.valid = true;
			t.step = tel.step.load();
			t.running = tel.running.load() == 1;
			t.screen = md::MmTelemetry::screenOf(tel.screen.load());
			t.recvCount = tel.recvCount.load();
			t.recvErrors = tel.recvErrors.load();
			t.recvActive = tel.recvActive.load() == 1;
			t.tempo = tel.tempo.load();
			t.mutes = tel.mutes.load();
			t.recording = tel.recording.load();
			t.bankGroup = tel.bankGroup.load();
			t.chainKnown = tel.readChain(t.chain.active, t.chain.next, t.chain.patterns);
			return t;
		}

		// 10 ms of desk ticks and machine time.
		void run(const double _ms)
		{
			const auto end = m.now() + static_cast<uint64_t>(_ms * g_rate / 1000);
			while(m.now() < end)
			{
				if(hw)
				{
					// the wire engine's step: out at DIN speed, what arrived in whole; no telemetry, no memory
					readTelemetry();	// for the test's own checks
					wire.pump(ms(), [this](const Bytes& _b) { desk->onDeviceSysex(_b); });
					for(auto& b : toMachine.arrived(ms()))
					{
						hwBytesOut += b.size();
						if(!unplugged)
							out.push_back(std::move(b));
					}
					desk->tick();
				}
				else
				{
					desk->onTelemetry(readTelemetry());
					Bytes region;
					uint32_t seq = 0;
					if(tel.readWorkingKit(region, seq) && seq != m_lastSeq)
					{
						m_lastSeq = seq;
						desk->onWorkingKit(region);
					}
					desk->tick();
				}
				while(!out.empty())
				{
					synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
					const auto& b = out.front();
					bytesSent += b.size();
					dumpsSent += b[0] == 0xf0 && b.size() >= 256;
					ccsSent += (b[0] & 0xf0) == 0xb0;
					if(b[0] == 0xf0)
						e.sysex.assign(b.begin(), b.end());
					else
					{
						e.a = b[0];
						e.b = b[1];
						e.c = b.size() > 2 ? b[2] : 0;
					}
					m.hardware().sendMidi(e);
					out.pop_front();
				}
				m.run(10);
				auto r = std::move(replies);
				replies.clear();
				for(const auto& x : r)
					desk->onDeviceSysex(x);
			}
		}

		void msg(const std::string& _json) { g_contract.command(*ed::json::parse(_json)); desk->onPageMessage(*ed::json::parse(_json)); }

		// A command the user confirms, as the page does: an ask it raises is answered by sending the
		// ask's command again with force.
		void msgConfirmed(const std::string& _json)
		{
			const auto from = page.size();
			msg(_json);
			for(size_t i = from; i < page.size(); ++i)
				if(page[i].find("type")->asString() == "ask")
				{
					std::printf("  ask %s: %s (confirmed)\n", page[i].find("ask")->asString().c_str(), page[i].find("message")->asString().c_str());
					Value c = *page[i].find("command");
					c.put("force", true);
					msg(ed::json::write(c));
					return;
				}
		}

		Value lastResult() const
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == "result")
					return *it;
			return {};
		}

		double rms(const double _ms) const
		{
			const auto n = static_cast<size_t>(_ms * g_rate / 1000);
			const auto& l = m.left();
			const auto from = l.size() > n ? l.size() - n : 0;
			double e = 0;
			for(size_t i = from; i < l.size(); ++i)
				e += double(l[i]) * l[i];
			return std::sqrt(e / double(std::max<size_t>(1, l.size() - from)));
		}

		uint32_t m_lastSeq = 0;
	};

	void smoke(const Bytes& _rom)
	{
		Rig r(_rom);
		r.msg(R"({"op":"ready"})");
		r.desk->setProbe(mmDesk::Desk::Probe::Running);
		r.run(600);
		check(r.desk->currentPattern() == 0 && r.desk->currentKit() >= 0, "status: current pattern and kit");
		check(r.desk->pattern(0).has_value(), "the current pattern is loaded");
		check(r.desk->workingKit().has_value(), "the working kit comes from memory");
		const auto t0 = r.ms();
		while(r.desk->loaded() < 288 && r.ms() - t0 < 30000)
			r.run(100);
		std::printf("  all 288 documents loaded after %.0f ms\n", r.ms() - t0);
		check(r.desk->loaded() == 288, "every pattern, kit, song and global loads");

		// The library's names (MM-PORT-PLAN f): a fresh machine's kit slots. Written ones hold their name
		// as text up to a NUL; a never-written one is marked by a first name byte of 0xff (what the page's
		// kitEmpty reads). Anything else (other bytes outside 0x20..0x7e before the NUL) would be junk the
		// page shows as a name, as the Machinedrum's never-written K17-K64 are (its kitNameText).
		{
			int text = 0, unused = 0, junk = 0;
			std::string which, sample;
			for(uint8_t s = 0; s < 128; ++s)
			{
				const auto k = r.desk->kit(s);
				if(!k)
					continue;
				const auto& n = k->name;
				if(n[0] == 0xff)
				{
					++unused;
					if(sample.empty())
					{
						char b[64];
						std::snprintf(b, sizeof(b), "K%02d %02x %02x %02x %02x machines %d %d", s + 1, n[0], n[1], n[2], n[3], k->machines[0], k->machines[1]);
						sample = b;
					}
					continue;
				}
				bool ok = true;
				for(const auto c : n)
				{
					if(c == 0)
						break;
					if(c < 0x20 || c >= 0x7f)
						ok = false;
				}
				if(ok)
					++text;
				else
				{
					++junk;
					if(which.size() < 60)
						which += " K" + std::to_string(s + 1);
				}
			}
			std::printf("  kit names: %d text, %d unused (first byte 0xff%s%s), %d other bytes%s\n", text, unused, sample.empty() ? "" : ", e.g. ", sample.c_str(), junk, which.c_str());
			check(junk == 0, "every kit slot's name is text, or the slot is marked unused (no junk name bytes)");
		}

		// The keyboard (the note intent): a key plays synth track 1 on its own channel while stopped.
		{
			r.run(300);
			const double silent = r.rms(200);
			r.msg(R"({"op":"noteOn","id":90,"t":0,"vel":127,"pitch":12})");
			check(r.lastResult().find("ok")->asBool(), "noteOn accepted");
			r.run(300);
			const double sounding = r.rms(250);
			r.msg(R"({"op":"noteOff","id":91,"t":0,"pitch":12})");
			r.run(300);
			std::printf("  keyboard: stopped rms %.4f, a key held %.4f\n", silent, sounding);
			check(sounding > 0.002 && sounding > silent * 4, "noteOn: a key plays T1 (rms " + std::to_string(sounding) + ")");
		}

		// Play pattern 1 (a factory demo), then edit it through the desk.
		r.msgConfirmed(R"({"op":"select","p":1})");
		r.run(300);
		r.msg(R"({"op":"play"})");
		r.run(1500);
		const double playing = r.rms(800);
		check(playing > 0.01, "pattern 1 plays (rms " + std::to_string(playing) + ")");
		auto p = *r.desk->pattern(1);
		for(size_t t = 0; t < 6; ++t)
			p.amp[t] = p.filter[t] = p.lfo[t] = p.pitch[t] = p.chord[t] = 0;
		for(auto& t : p.notes) t.fill(0xff);
		for(auto& m : p.lockMasks) m.fill(0);		// locks need trigs to sit on
		p.lockRowCount = 0;
		p.chordNoteCount = 0;
		std::printf("  before the edit: screen %08x recv %s\n", r.tel.screen.load(), r.desk->recvState().c_str());
		const auto tEdit = r.ms();
		r.msg(R"({"op":"set","id":1,"kind":"pattern","doc":)" + ed::json::write(ed::mmPatternToJson(p)) + "}");
		check(r.lastResult().find("ok")->asBool(), "the edit is accepted");
		if(!r.lastResult().find("ok")->asBool())
			std::printf("    %s\n", ed::json::write(r.lastResult()).c_str());
		double confirmed = -1;
		bool parkedThen = false;
		while(r.ms() - tEdit < 3000)
		{
			r.run(10);
			if(r.desk->lastRoundTripMs() > 0 && confirmed < 0)
			{
				confirmed = r.ms() - tEdit;
				parkedThen = r.desk->recvParked();
			}
			if(std::getenv("MMDESK_TRACE") && (r.ms() - tEdit < 120 || static_cast<int>(r.ms() - tEdit) % 100 < 10))
				std::printf("    +%.0f recv %s screen %08x count %u\n", r.ms() - tEdit, r.desk->recvState().c_str(), r.tel.screen.load(), r.tel.recvCount.load());
		}
		std::printf("  edit -> SYSEX RECV -> stored -> read back: %.0f ms (desk round trip %.0f ms)\n", confirmed, r.desk->lastRoundTripMs());
		check(confirmed > 0, "confirmed by read-back");
		check(parkedThen, "parked on SYSEX RECV when confirmed");
		const double cleared = r.rms(500);
		check(cleared < playing * 0.3, "the cleared pattern is heard while it plays (rms " + std::to_string(cleared) + ")");
		check(r.tel.running.load() == 1, "still playing");

		// Kit: a live CC edit and a structure edit (TRIG POS: dump + LOAD KIT).
		auto k = *r.desk->workingKit();
		const auto kitSlot = k.position;
		k.tracks[0].pages[2][0] = static_cast<uint8_t>(k.tracks[0].pages[2][0] ^ 0x20);	// FLT BASE
		r.msg(R"({"op":"set","id":2,"kind":"workingKit","doc":)" + ed::json::write(ed::mmKitToJson(k)) + "}");
		r.run(400);
		check(r.desk->workingKit() && r.desk->workingKit()->tracks[0].pages[2][0] == k.tracks[0].pages[2][0],
			"a kit value reaches the working kit by CC");
		k.trigPos[2] = 1;
		r.msg(R"({"op":"set","id":3,"kind":"workingKit","doc":)" + ed::json::write(ed::mmKitToJson(k)) + "}");
		const auto note = r.lastResult().find("note")->asString();
		r.run(1500);
		check(r.desk->workingKit() && r.desk->workingKit()->trigPos[2] == 1, "TRIG POS reaches the working kit (" + note + ")");
		check(r.desk->kit(kitSlot) && r.desk->kit(kitSlot)->trigPos[2] == 1, "and the stored slot");
		k.machines[3] = 3;	// SID
		r.msg(R"({"op":"set","id":4,"kind":"workingKit","doc":)" + ed::json::write(ed::mmKitToJson(k)) + "}");
		r.run(500);
		check(r.desk->workingKit() && r.desk->workingKit()->machines[3] == 3, "a machine change by 0x5B");

		// Song 2: a row edit.
		auto s = *r.desk->song(2);
		s.rows[0].bytes = {};
		s.rows[0].bytes[0] = 7;
		s.rows[0].bytes[ed::mmSongRow::g_length] = 16;
		s.rows[0].bytes[22] = s.rows[0].bytes[23] = 0xff;
		s.rows[1].bytes = {};
		s.rows[1].bytes[0] = 0xff;
		s.rows[1].bytes[22] = s.rows[1].bytes[23] = 0xff;	// END keeps the tempo, as the firmware writes it
		r.msg(R"({"op":"set","id":5,"kind":"song","doc":)" + ed::json::write(ed::mmSongToJson(s)) + "}");
		r.run(800);
		check(r.desk->song(2) && r.desk->song(2)->rows[0].bytes[0] == 7, "a song dump through SYSEX RECV");

		// Idle: back to the main screen, still playing. 0.3.4: the idle time starts once the stream has delivered the
		// dumps (cable speed while playing), so the session never leaves SYSEX RECV before a dump arrives.
		r.run(2500);
		for(int i = 0; i < 100 && md::MmTelemetry::screenOf(r.tel.screen.load()) != md::MmScreen::Main; ++i)
			r.run(100);
		check(md::MmTelemetry::screenOf(r.tel.screen.load()) == md::MmScreen::Main, "left SYSEX RECV after the idle time");
		check(r.tel.running.load() == 1, "still playing after leaving");

		// Queue a pattern while playing.
		r.msgConfirmed(R"({"op":"select","p":2})");
		r.run(200);
		bool sawQueued = false;
		for(auto it = r.page.rbegin(); it != r.page.rend(); ++it)
			if(it->find("type")->asString() == "machine")
			{
				sawQueued = it->find("doc")->find("pattern")->find("queued")->isNumber();
				break;
			}
		check(sawQueued, "the queued pattern is shown");
		const auto tq = r.ms();
		while(r.desk->currentPattern() != 2 && r.ms() - tq < 12000)
			r.run(50);
		std::printf("  queued switch after %.0f ms (at the end of the 64-step pattern)\n", r.ms() - tq);
		check(r.desk->currentPattern() == 2, "the queue switches at the pattern end");
		r.msg(R"({"op":"stop"})");
		r.run(600);
		check(r.tel.running.load() == 0, "STOP");
	}

	// The last machine document the desk published.
	Value lastMachine(const Rig& _r)
	{
		for(auto it = _r.page.rbegin(); it != _r.page.rend(); ++it)
			if(it->find("type")->asString() == "machine")
				return *it->find("doc");
		return {};
	}

	std::string kitWorking(const Rig& _r)
	{
		const auto m = lastMachine(_r);
		return m.isObject() ? m.find("kit")->find("working")->asString() : "";
	}

	// The kit library's click (MM-PORT-PLAN f): LOAD KIT over unsaved edits asks, and its "Save and load"
	// (SAVE KIT to the current slot, then LOAD KIT with force, as the page sends them) keeps the edit in the
	// slot and loads the other kit, clean.
	void library(const Bytes& _rom)
	{
		std::puts("library");
		Rig r(_rom);
		r.msg(R"({"op":"ready"})");
		r.desk->setProbe(mmDesk::Desk::Probe::Running);
		r.run(600);
		while(r.desk->loaded() < 288)
			r.run(100);
		r.run(1500);
		const auto from = r.desk->currentKit();
		const int to = from == 3 ? 4 : 3;
		auto k = *r.desk->workingKit();
		const auto v = static_cast<uint8_t>(k.tracks[0].pages[2][0] == 97 ? 98 : 97);	// FLT BASE of track 1
		k.tracks[0].pages[2][0] = v;
		r.msg(R"({"op":"set","id":1,"kind":"workingKit","doc":)" + ed::json::write(ed::mmKitToJson(k)) + "}");
		r.run(800);
		check(kitWorking(r) == "edited", "an edit makes the kit that plays edited");
		const auto at = r.page.size();
		r.msg(R"({"op":"loadKit","id":2,"k":)" + std::to_string(to) + "}");
		const Value* q = nullptr;
		for(size_t i = at; i < r.page.size(); ++i)
			if(r.page[i].find("type")->asString() == "ask")
				q = &r.page[i];
		const auto* alts = q ? q->find("alternatives") : nullptr;
		check(q && q->find("ask")->asString() == "loadKit" && alts && alts->isArray() && alts->asArray().size() == 1
			&& alts->asArray()[0].find("label")->asString() == "Save and load", "LOAD KIT over the edit asks, with Save and load");
		check(r.desk->currentKit() == from, "nothing is done before the answer");
		if(q && alts)
		{
			const Value alt = alts->asArray()[0];
			Value c = *q->find("command");
			for(const auto& f : alt.find("first")->asArray())
				r.msg(ed::json::write(f));
			c.put("force", true);
			r.msg(ed::json::write(c));
		}
		const auto t0 = r.ms();
		while((r.desk->currentKit() != to || kitWorking(r) != "clean") && r.ms() - t0 < 8000)
			r.run(100);
		r.run(1500);
		const auto saved = r.desk->kit(static_cast<uint8_t>(from));
		check(r.desk->currentKit() == to && kitWorking(r) == "clean", "Save and load: K" + std::to_string(to + 1) + " plays, clean");
		check(saved && saved->tracks[0].pages[2][0] == v, "and the edit is in K" + std::to_string(from + 1) + " (read back from the slot)");
	}

	// P7: loading a pattern (which loads its kit) leaves the kit that plays clean, and no question
	// comes on the next switch; the current pattern's LEN changes and plays at the new length.
	void patterns(const Bytes& _rom)
	{
		std::puts("patterns");
		Rig r(_rom);
		r.msg(R"({"op":"ready"})");
		r.desk->setProbe(mmDesk::Desk::Probe::Running);
		r.run(600);
		while(r.desk->loaded() < 288)
			r.run(100);
		r.run(1500);
		std::printf("  after boot: kit %d %s\n", r.desk->currentKit(), kitWorking(r).c_str());
		int asks = 0, edited = 0, loads = 0;
		for(const int p : {1, 2, 3, 16, 17, 0})
		{
			const auto from = r.page.size();
			r.msg(R"({"op":"select","p":)" + std::to_string(p) + "}");
			for(size_t i = from; i < r.page.size(); ++i)
				if(r.page[i].find("type")->asString() == "ask")
				{
					++asks;
					std::printf("  select %d asks: %s\n", p, r.page[i].find("message")->asString().c_str());
					Value c = *r.page[i].find("command");
					c.put("force", true);
					r.msg(ed::json::write(c));
					break;
				}
			r.run(2500);
			++loads;
			const auto state = kitWorking(r);
			const auto k = r.desk->currentKit();
			std::printf("  select %d: pattern %d kit %d %s\n", p, r.desk->currentPattern(), k, state.c_str());
			if(state == "edited")
			{
				++edited;
				const auto w = r.desk->workingKit();
				const auto s = r.desk->kit(static_cast<uint8_t>(k));
				if(w && s)
				{
					const auto a = ed::mmKitRaw(*w), b = ed::mmKitRaw(*s);
					for(size_t i = 0; i < a.size() && i < b.size(); ++i)
						if(a[i] != b[i])
							std::printf("    raw 0x%03zx: working %02x stored %02x\n", i, a[i], b[i]);
				}
			}
		}
		check(edited == 0, "a loaded pattern's kit is clean (" + std::to_string(edited) + " of " + std::to_string(loads) + " edited)");
		check(asks == 0, "no question when nothing was edited (" + std::to_string(asks) + " asked)");

		// LEN of the pattern that plays: 64 -> 32 -> 17, each read back and played at that length.
		for(const int len : {32, 17})
		{
			const int cp = r.desk->currentPattern();
			auto p = *r.desk->pattern(static_cast<uint8_t>(cp));
			p.length = static_cast<uint8_t>(len);
			r.msg(R"({"op":"set","kind":"pattern","doc":)" + ed::json::write(ed::mmPatternToJson(p)) + "}");
			const auto ok = r.lastResult().find("ok")->asBool();
			if(!ok)
				std::printf("    %s\n", ed::json::write(r.lastResult()).c_str());
			r.run(3000);
			const auto back = r.desk->pattern(static_cast<uint8_t>(cp));
			std::printf("  LEN %d on %d: accepted %d, read back %d, kit %s\n", len, cp, ok, back ? back->length : -1, kitWorking(r).c_str());
			check(ok && back && back->length == len, "LEN " + std::to_string(len) + " of the pattern that plays is stored");
			r.msg(R"({"op":"play"})");
			int maxStep = -1;
			const auto t0 = r.ms();
			while(r.ms() - t0 < 64 * 125 + 1000)
			{
				r.run(10);
				maxStep = std::max(maxStep, static_cast<int>(r.tel.step.load()));
			}
			r.msg(R"({"op":"stop"})");
			r.run(600);
			std::printf("  played: steps up to %d\n", maxStep + 1);
			check(maxStep + 1 == len, "and it plays " + std::to_string(len) + " steps");
		}
	}

	// P7: a DAW's transport and tempo (MIDI Start, 24 clocks a quarter, Stop from synthLib::MidiClock): does the
	// machine follow a 100 BPM clock, as booted? Steps counted over 4 s.
	int clockedSteps(Rig& _r, const double _bpm, const double _seconds)
	{
		const auto midi = [&](const uint8_t _b) { synthLib::SMidiEvent e(synthLib::MidiEventSource::Host); e.a = _b; _r.m.hardware().sendMidi(e); };
		midi(0xfa);
		int steps = 0, last = _r.tel.step.load();
		const double every = 60000.0 / _bpm / 24;
		for(double t = 0; t < _seconds * 1000; t += every)
		{
			midi(0xf8);
			_r.run(every);
			const int st = _r.tel.step.load();
			if(st != last) { ++steps; last = st; }
		}
		midi(0xfc);
		_r.run(300);
		return steps;
	}

	void hostClock(const Bytes& _rom)
	{
		std::puts("host clock");
		Rig r(_rom);
		r.msg(R"({"op":"ready"})");
		r.desk->setProbe(mmDesk::Desk::Probe::Running);
		r.run(600);
		while(r.desk->loaded() < 288)
			r.run(100);
		r.run(1000);
		const auto g = r.desk->global(static_cast<uint8_t>(r.desk->currentGlobal()));
		std::printf("  global %d: tempo %.1f\n", r.desk->currentGlobal(), r.readTelemetry().tempo / 24.0);
		const int steps = clockedSteps(r, 100, 4);
		std::printf("  as booted: %.2f steps/s with a 100 BPM clock (the clock: %.2f)\n", steps / 4.0, 100.0 / 60 * 4);
		check(steps == 0, "as booted the machine ignores the host's Start and clock");
		// in a DAW the session sends followHost: CLOCK IN on the active global, without an undo step
		r.msg(R"({"op":"followHost"})");
		std::printf("  followHost: %s\n", r.lastResult().find("note")->asString().c_str());
		r.run(6000);
		const auto g2 = r.desk->global(static_cast<uint8_t>(r.desk->currentGlobal()));
		check(g2 && g2->tempoSync == 1 && g2->transportIn == 1, "followHost sets GLOBAL › MIDI SYNC CLOCK IN and TRANSPORT IN, read back");
		check(r.desk->coreState().history().size() == 0, "without an undo step");
		// following the clock, the step rate scales with it (the pattern's own speed, 1X..3/2X, on top)
		const double at100 = clockedSteps(r, 100, 4) / 4.0, at150 = clockedSteps(r, 150, 4) / 4.0;
		std::printf("  after followHost: %.2f steps/s at a 100 BPM clock, %.2f at 150 (ratio %.2f)\n", at100, at150, at100 > 0 ? at150 / at100 : 0);
		check(at100 > 1 && std::abs(at150 / at100 - 1.5) < 0.1, "the machine follows the host's Start and its clock (100 and 150 BPM)");
		const int t0 = r.tel.step.load();
		r.run(1000);
		check(r.tel.step.load() == t0, "and stops on the host's Stop");
		r.msg(R"({"op":"followHost"})");
		check(r.lastResult().find("note")->asString().empty(), "a second followHost changes nothing");
		// Clock and transport OUT (the leader of two apps over a MIDI bus): which bytes make the machine send them
		if(std::getenv("MM_SYNC_OUT"))
		{
			int f8 = 0, fa = 0, fc = 0;
			r.m.onMidi = [&](const synthLib::SMidiEvent& _e) { if(_e.a == 0xf8) ++f8; else if(_e.a == 0xfa) ++fa; else if(_e.a == 0xfc) ++fc; };
			auto g3 = *r.desk->global(static_cast<uint8_t>(r.desk->currentGlobal()));
			g3.tempoSync = g3.transportIn = 0;	// its own clock again (not following)
			for(const size_t i : {size_t{0x07}, size_t{0x08}, size_t{0x09}, size_t{0x0a}})
			{
				auto raw = ed::mmGlobalRaw(g3);
				const auto was = raw[i];
				raw[i] = raw[i] ? 0 : 1;
				r.msg(R"({"op":"set","kind":"global","doc":)" + ed::json::write(ed::mmGlobalToJson(*ed::mmGlobalFromRaw(raw, g3.position))) + "}");
				r.run(5000);
				f8 = fa = fc = 0;
				r.msg(R"({"op":"play"})"); r.run(1000); r.msg(R"({"op":"stop"})"); r.run(600);
				std::printf("  byte %02zx %02x -> %02x: out clocks %d, start %d, stop %d (internal play 1 s)\n", i, was, raw[i], f8, fa, fc);
				r.msg(R"({"op":"set","kind":"global","doc":)" + ed::json::write(ed::mmGlobalToJson(g3)) + "}");
				r.run(5000);
			}
			r.m.onMidi = nullptr;
		}
		if(!std::getenv("MM_SYNC_SEARCH"))
			return;
		// which undecoded global byte is the MIDI SYNC setting: flip each and look again
		const auto raw0 = ed::mmGlobalRaw(*g);
		std::printf("  raw x05..x11:"); for(size_t i = 0x05; i < 0x12; ++i) std::printf(" %02x", raw0[i]);
		std::printf("  x30..x35:"); for(size_t i = 0x30; i < 0x36; ++i) std::printf(" %02x", raw0[i]);
		std::printf("  xfd..x105:"); for(size_t i = 0xfd; i < 0x106; ++i) std::printf(" %02x", raw0[i]);
		std::printf("\n");
		std::vector<size_t> cand;
		for(size_t i = 0x05; i < 0x12; ++i) cand.push_back(i);
		for(size_t i = 0x30; i < 0x36; ++i) cand.push_back(i);
		for(size_t i = 0xfd; i < 0x106; ++i) cand.push_back(i);
		if(const char* only = std::getenv("MM_SYNC_BYTES")) { cand.clear(); for(const char* p = only; *p;) { cand.push_back(std::strtoul(p, const_cast<char**>(&p), 16)); if(*p == ',') ++p; } }
		for(const auto i : cand)
		{
			auto raw = raw0;
			raw[i] = raw0[i] ? 0 : 1;
			if(const char* v = std::getenv("MM_SYNC_VAL")) raw[i] = static_cast<uint8_t>(std::strtoul(v, nullptr, 16));
			if(const char* both = std::getenv("MM_SYNC_ALSO")) raw[std::strtoul(both, nullptr, 16)] = 1;
			const auto gg = ed::mmGlobalFromRaw(raw, g->position);
			r.msg(R"({"op":"set","kind":"global","doc":)" + ed::json::write(ed::mmGlobalToJson(*gg)) + "}");
			r.run(4000);
			r.out.push_back(ed::mmSetActiveGlobal(g->position));
			r.run(800);
			const int n = clockedSteps(r, 100, 2), n150 = clockedSteps(r, 150, 2);
			std::printf("  byte %03zx %02x -> %02x: %.2f steps/s at 100 BPM, %.2f at 150\n", i, raw0[i], raw[i], n / 2.0, n150 / 2.0);
			r.msg(R"({"op":"set","kind":"global","doc":)" + ed::json::write(ed::mmGlobalToJson(*g)) + "}");
			r.run(3000);
		}
	}

	// The session's import traits for the rig (mdSessionMm.cpp's, without the plug-in).
	struct MmSyxTraits
	{
		using Docs = ed::MmDocuments;
		static constexpr ed::SyxModel model = ed::SyxModel::Mm;
		static constexpr const char* name = "Monomachine";
		static const Docs& docs(const ed::SyxFile& _f) { return _f.mm; }
	};

	ed::MmDocuments machineDocs(const mmDesk::Desk& _desk)
	{
		const auto& v = _desk.documents();
		ed::MmDocuments o;
		o.patterns = v.patterns;
		o.kits = v.kits;
		o.songs = v.songs;
		o.globals = v.globals;
		return o;
	}

	// P7: Export SysEx as the session does (writeSyx of every document the desk holds) from a fresh machine, to
	// SYX_EXPORT_TO: a file of the owner's own, for syximport.
	void syxExport(const Bytes& _rom)
	{
		std::puts("syx export");
		const std::string to = std::getenv("SYX_EXPORT_TO") ? std::getenv("SYX_EXPORT_TO") : "mm-export.syx";
		Rig r(_rom);
		r.msg(R"({"op":"ready"})");
		r.desk->setProbe(mmDesk::Desk::Probe::Running);
		r.run(600);
		const auto t0 = r.ms();
		while(r.desk->loaded() < 288 && r.ms() - t0 < 60000) r.run(100);
		auto docs = machineDocs(*r.desk);
		// B-026: SYX_BASE_CHANNEL=n (0-15, or 127 = OFF) writes the export's globals with another MIDI base channel
		if(const char* ch = std::getenv("SYX_BASE_CHANNEL"))
			for(auto& [slot, g] : docs.globals)
				g.baseChannel = static_cast<uint8_t>(std::atoi(ch));
		const auto bytes = ed::writeSyx(docs);
		std::ofstream out(to, std::ios::binary);
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		check(out.good() && !bytes.empty(), "wrote " + std::to_string(bytes.size()) + " bytes to " + to);
	}

	// P7, B-019: a .syx imported as the session does (mdSyxSession.h): the file's messages to the firmware as they
	// are (the dumps on SYSEX RECV, as the editor's own), then every document read back and compared. The user's own
	// file, read in place (MM_SYX); skipped without it. SYX_KINDS=kit,pattern imports only those; SYX_HW=1 over the
	// HW MIDI engine (the person opens SYSEX RECV and presses SEND: the rig does both). SYX_EXPECT=all: every
	// document must be taken (as it is or in the machine's own form).
	void syxImport(const Bytes& _rom)
	{
		std::puts("syx import (as is)");
		const char* path = std::getenv("MM_SYX");
		const std::string file = path ? path : "";
		std::ifstream in(file, std::ios::binary);
		if(file.empty() || !in) { std::printf("  skip: no file (MM_SYX)\n"); return; }
		const Bytes bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		const bool hw = std::getenv("SYX_HW") && std::string(std::getenv("SYX_HW")) == "1";
		Rig r(_rom, hw);
		r.msg(R"({"op":"ready"})");
		if(!hw)
			r.desk->setProbe(mmDesk::Desk::Probe::Running);
		r.run(600);
		const auto tLoad = r.ms();
		while(r.desk->loaded() < 288 && r.ms() - tLoad < (hw ? 900000 : 60000)) r.run(100);
		r.run(1000);
		std::printf("  %zu documents loaded\n", r.desk->loaded());

		mdJucePlugin::SyxJob<MmSyxTraits> job;
		const auto preview = job.open(bytes, file, machineDocs(*r.desk), {r.desk->currentPattern(), r.desk->currentKit(), r.desk->currentSong(), r.desk->currentGlobal()});
		g_contract(preview);
		check(preview.find("ok")->asBool(), "the preview opens the file");
		std::printf("  preview: %d messages, %d left out;", static_cast<int>(preview.find("messages")->asNumber()), static_cast<int>(preview.find("skippedCount")->asNumber()));
		for(const auto& [k, list] : preview.find("items")->asObject())
		{
			std::map<std::string, int> formats;
			for(const auto& i : list.asArray())
				++formats[i.find("format")->asString()];
			std::printf(" %s %zu", k.c_str(), list.asArray().size());
			for(const auto& [f, n] : formats)
				std::printf(" [%s x%d]", f.c_str(), n);
		}
		std::printf("\n");
		std::vector<std::string> kinds;
		{
			const std::string k = std::getenv("SYX_KINDS") ? std::getenv("SYX_KINDS") : "global,kit,pattern,song";
			for(const char* kind : {"global", "kit", "pattern", "song", "other"})
				if(k.find(kind) != std::string::npos)
					kinds.emplace_back(kind);
		}
		r.desk->setSysexTap([&job](const Bytes& _m) { job.onMachineSysex(_m); });
		const auto why = job.start(kinds, {}, machineDocs(*r.desk));
		check(why.empty(), "the import starts" + (why.empty() ? std::string() : ": " + why));
		using Phase = mdJucePlugin::SyxJob<MmSyxTraits>::Phase;
		const auto t0 = r.ms();
		double sendAt = -1, readAt = -1;
		std::optional<Value> last;
		bool hwSent = false;
		while(job.running() && r.ms() - t0 < 3600000)
		{
			if(r.desk->isInputReady())
				if(auto p = job.step(r.desk->machine(), r.ms(), hw))
				{
					g_contract(*p);
					last = *p;
					// HW MIDI: the person puts the machine on SYSEX RECV and presses SEND once the import waits for it
					if(hw && !hwSent && !p->find("text")->asString().empty() && p->find("text")->asString().find("SYSEX RECV") != std::string::npos)
					{
						hwSent = true;
						std::puts("  the person opens SYSEX RECV and presses SEND");
						r.userKeys(mmDesk::RecvSession::enterMacro());
						r.run(3000);
						r.msg(R"({"op":"hwSend","id":881})");
					}
				}
			if(sendAt < 0 && job.phase() == Phase::Sending)
				sendAt = r.ms();
			if(readAt < 0 && job.phase() == Phase::Reading)
				readAt = r.ms();
			r.run(10);
		}
		std::printf("  import: read before %.1f s, sent in %.1f s, read back in %.1f s (emulated time)\n", (sendAt - t0) / 1000,
			(readAt - sendAt) / 1000, (r.ms() - readAt) / 1000);
		check(last && last->find("phase")->asString() == "done", "the import ends with a report");
		if(!last)
			return;
		std::printf("  report: %s\n", last->find("text")->asString().c_str());
		int n = 0;
		for(const auto& i : last->find("report")->find("items")->asArray())
			if(n++ < 24)
				std::printf("    %s\n", i.find("text")->asString().c_str());
		std::map<std::string, int> outcomes;
		std::map<std::string, std::map<std::string, int>> perKind;
		for(const auto& [item, o] : job.outcomes())
		{
			++outcomes[ed::syxOutcomeName(o)];
			++perKind[ed::syxKindName(item.kind)][ed::syxOutcomeName(o)];
		}
		for(const auto& [k, m] : perKind)
		{
			std::printf("  %s:", k.c_str());
			for(const auto& [o, c] : m)
				std::printf(" %s %d", o.c_str(), c);
			std::printf("\n");
		}
		// SYX_DIFF=1: what differs between the file's document and the machine's, per JSON field (the first few)
		if(std::getenv("SYX_DIFF"))
		{
			const auto f = ed::parseSyx(bytes);
			int shown = 0;
			for(const auto& [item, o] : job.outcomes())
			{
				if(o == ed::SyxOutcome::Taken || shown >= 6)
					continue;
				Value a, b;
				if(item.kind == ed::SyxKind::Pattern && f.mm.patterns.count(item.slot) && r.desk->pattern(item.slot)) { a = ed::mmPatternToJson(f.mm.patterns.at(item.slot)); b = ed::mmPatternToJson(*r.desk->pattern(item.slot)); }
				else if(item.kind == ed::SyxKind::Kit && f.mm.kits.count(item.slot) && r.desk->kit(item.slot)) { a = ed::mmKitToJson(f.mm.kits.at(item.slot)); b = ed::mmKitToJson(*r.desk->kit(item.slot)); }
				else if(item.kind == ed::SyxKind::Global && f.mm.globals.count(item.slot) && machineDocs(*r.desk).globals.count(item.slot)) { a = ed::mmGlobalToJson(f.mm.globals.at(item.slot)); b = ed::mmGlobalToJson(machineDocs(*r.desk).globals.at(item.slot)); }
				else continue;
				++shown;
				std::string d;
				for(const auto& [key, v] : a.asObject())
					if(const auto* w = b.find(key); !w || *w != v)
						d += " " + key + (d.size() < 300 && v.isNumber() && w && w->isNumber() ? "=" + std::to_string(static_cast<int>(v.asNumber())) + "->" + std::to_string(static_cast<int>(w->asNumber())) : "");
				std::printf("    diff %s %d (%s):%s\n", ed::syxKindName(item.kind), item.slot + 1, ed::syxOutcomeName(o), d.c_str());
			}
		}
		check(outcomes["no reply"] == 0, "every document of the file is read back (" + std::to_string(outcomes["no reply"]) + " without a reply)");
		check(outcomes["taken"] > 0, "documents are imported (" + std::to_string(outcomes["taken"]) + ")");
		if(std::getenv("SYX_EXPECT") && std::string(std::getenv("SYX_EXPECT")) == "all")
			check(outcomes["ignored"] == 0 && outcomes["differs"] == 0 && outcomes["unknown"] == 0, "the machine took every document of the file");
		// the desk's documents are what the machine took
		r.run(2000);
		size_t same = 0, total = 0;
		std::string off;
		const auto f = ed::parseSyx(bytes);
		for(const auto& [item, o] : job.outcomes())
		{
			if(o != ed::SyxOutcome::Taken)
				continue;
			++total;
			if(ed::syxCanonical(machineDocs(*r.desk), item.kind, item.slot) == ed::syxCanonical(f.mm, item.kind, item.slot))
				++same;
			else if(off.size() < 80)
				off += std::string(" ") + ed::syxKindName(item.kind) + std::to_string(item.slot + 1);
		}
		check(same == total, "the desk's documents are what the machine took (" + std::to_string(same) + " of " + std::to_string(total) + ")" + off);
		check(r.desk->coreState().history().size() == 0, "an import is no undo step (the machine took a dump, nothing was edited)");
		// B-026: the editor still plays the machine after the import (a synth track's mute is CC 3 on its channel)
		if(!hw)
		{
			r.run(1000);
			const auto& gs = r.desk->documents().globals;
			const auto doc = lastMachine(r);
			const auto* g = doc.find("global");
			const auto* active = g ? g->find("current") : nullptr;
			int base = -1, span = 16;
			if(active && active->isNumber() && gs.count(static_cast<uint8_t>(active->asNumber())))
			{
				base = gs.at(static_cast<uint8_t>(active->asNumber())).baseChannel;
				span = gs.at(static_cast<uint8_t>(active->asNumber())).channelSpan;
			}
			r.msg(R"({"op":"mute","t":2,"on":true})");
			const bool taken = r.lastResult().find("ok")->asBool();
			r.run(600);
			const bool landed = ((r.tel.mutes.load() >> 2) & 1) == 1;
			std::printf("  after the import: base channel %d, span %d, mute %s, machine mutes %04x\n", base, span, taken ? "taken" : "refused", unsigned(r.tel.mutes.load()));
			if(base > 15 || span <= 2)
				check(!taken && r.tel.mutes.load() == 0, "no channel for T3 (base OFF or CHANNEL SPAN): the page's mute is refused with the reason, nothing muted");
			else
			{
				check(taken && landed, "after the import a page mute lands in the machine's mute set");
				r.msg(R"({"op":"play"})");
				r.run(1500);
				check(((r.tel.mutes.load() >> 2) & 1) == 1, "and stays muted while the machine plays");
				r.msg(R"({"op":"stop"})");
				r.run(400);
				r.msg(R"({"op":"mute","t":2,"on":false})");
				r.run(600);
			}
		}
	}

	// Zero crossings per second / 2 over a window of the left channel.
	double frequency(const std::vector<float>& _l, const size_t _from, const size_t _n)
	{
		int crossings = 0;
		for(size_t i = _from + 1; i < _from + _n && i < _l.size(); ++i)
			crossings += (_l[i - 1] < 0) != (_l[i] < 0);
		return crossings * 0.5 * g_rate / static_cast<double>(_n);
	}

	double rmsAt(const std::vector<float>& _l, const size_t _from, const size_t _n)
	{
		double e = 0;
		for(size_t i = _from; i < _from + _n && i < _l.size(); ++i)
			e += double(_l[i]) * _l[i];
		return std::sqrt(e / static_cast<double>(_n));
	}

	// Trigless and pitchless trigs as the firmware plays them (a sine on track 1).
	void trigKinds(const Bytes& _rom)
	{
		std::puts("trig kinds");
		Rig r(_rom);
		r.msg(R"({"op":"ready"})");
		r.desk->setProbe(mmDesk::Desk::Probe::Running);
		r.run(600);
		while(r.desk->loaded() < 288)
			r.run(100);
		// Kit: T1 GND-SIN, a short amp envelope; the other tracks silent.
		auto k = *r.desk->workingKit();
		k.machines[0] = 1;
		k.tracks[0].pages[0] = {0, 0, 0, 0, 0, 0, 0, 64};		// GND-SIN: TUNE centre
		k.tracks[0].pages[1] = {0, 0, 24, 0, 0, 110, 64, 0};	// ATK HOLD DEC REL DIST VOL PAN PORT
		k.tracks[0].pages[2] = {0, 127, 0, 0, 0, 0, 64, 64};	// open filter
		k.tracks[0].pages[3] = {64, 64, 0, 0, 64, 0, 0, 0};		// no delay
		for(size_t pg = 4; pg < 7; ++pg)
			k.tracks[0].pages[pg][7] = 0;						// LFO depth 0
		for(size_t t = 1; t < 6; ++t)
			k.levels[t] = 0;
		k.levels[0] = 120;
		r.msg(R"({"op":"set","kind":"workingKit","doc":)" + ed::json::write(ed::mmKitToJson(k)) + "}");
		r.run(800);
		{
			const auto& w = *r.desk->workingKit();
			std::printf("  working kit: T1 machine %d levels %d %d %d %d %d %d, AMP %d %d %d\n", w.machines[0], w.levels[0], w.levels[1],
				w.levels[2], w.levels[3], w.levels[4], w.levels[5], w.tracks[0].pages[1][0], w.tracks[0].pages[1][2], w.tracks[0].pages[1][5]);
		}
		// Pattern E01 (empty): 16 steps.
		auto p = *r.desk->pattern(64);
		p.length = 16;
		const auto full = [&](const size_t _s, const uint8_t _note)
		{
			p.pitch[0] |= ed::mmStepBit(_s);
			p.amp[0] |= ed::mmStepBit(_s);
			p.filter[0] |= ed::mmStepBit(_s);
			p.lfo[0] |= ed::mmStepBit(_s);
			p.notes[0][_s] = _note;
		};
		full(0, 48);							// C-3, a full trig
		p.pitch[0] |= ed::mmStepBit(4);			// step 5: trigless, pitch 72
		p.notes[0][4] = 72;
		p.pitch[0] |= ed::mmStepBit(8);			// step 9: pitchless (envelopes, no note)
		p.amp[0] |= ed::mmStepBit(8);
		p.filter[0] |= ed::mmStepBit(8);
		p.lfo[0] |= ed::mmStepBit(8);
		full(12, 60);							// step 13: C-4
		check(ed::validate(p).empty(), "the trig-kind pattern validates");
		r.msg(R"({"op":"set","kind":"pattern","doc":)" + ed::json::write(ed::mmPatternToJson(p)) + "}");
		r.run(1500);
		check(r.desk->pattern(64) && ed::encodeMmPattern(*r.desk->pattern(64)) == ed::encodeMmPattern(p), "stored as sent");
		r.msgConfirmed(R"({"op":"select","p":64})");
		r.run(300);
		r.msg(R"({"op":"tempo","bpm":133})");
		r.run(100);
		r.desk->onTelemetry(r.readTelemetry());
		check(r.readTelemetry().tempo == 133 * 24, "tempo 133 read back from RAM (0x2bc2a6)");
		r.msg(R"({"op":"tempo","bpm":120})");
		r.run(100);
		check(r.readTelemetry().tempo == 120 * 24, "tempo 120 read back from RAM");
		// Play; skip the first pass, then note where each step of the second pass starts.
		r.msg(R"({"op":"play"})");
		int last = -1, passes = 0;
		uint64_t stepStart[16]{};
		while(passes < 3)
		{
			r.run(2);
			const int st = r.tel.step.load();
			if(st == last || st < 0 || st > 15)
				continue;
			if(st == 0)
				++passes;
			if(passes == 2)
				stepStart[st] = r.m.now();
			last = st;
		}
		const auto& l = r.m.left();
		if(std::getenv("MMDESK_TRACE"))
			for(uint64_t f = stepStart[0]; f < stepStart[15] + g_rate / 8; f += g_rate / 80)
			{
				int st = 0;
				for(int k = 0; k < 16; ++k) if(stepStart[k] && stepStart[k] <= f) st = k;
				std::printf("    t %6.1f ms step %2d rms %.4f f %.0f\n", (f - stepStart[0]) * 1000.0 / g_rate, st,
					rmsAt(l, static_cast<size_t>(f), g_rate / 80), frequency(l, static_cast<size_t>(f), g_rate / 80));
			}
		// Measured (MM-P2-RESULT §3): an envelope's attack is heard about a step and 50 ms after
		// the playhead byte reaches its step; a pitch change on a trigless step is heard at once.
		const size_t slice = g_rate / 80;	// 12.5 ms
		const auto maxRms = [&](const uint64_t _a, const uint64_t _b)
		{
			double m = 0;
			for(auto f = _a; f + slice <= _b; f += slice)
				m = std::max(m, rmsAt(l, static_cast<size_t>(f), slice));
			return m;
		};
		const auto freq = [&](const uint64_t _a, const uint64_t _b)
		{
			return frequency(l, static_cast<size_t>(_a), static_cast<size_t>(_b - _a));
		};
		const auto step = g_rate / 8;	// 125 ms at 120 BPM
		const double before4 = rmsAt(l, static_cast<size_t>(stepStart[4] - slice), slice);
		const double trigless = maxRms(stepStart[5], stepStart[8]);	// after the pitch change
		const double fTrigless = freq(stepStart[5], stepStart[8]);
		const double before8 = rmsAt(l, static_cast<size_t>(stepStart[9] - slice), slice);
		const double pitchless = maxRms(stepStart[9], stepStart[11]);
		const double fPitchless = freq(stepStart[10], stepStart[12]);
		const double fFull = freq(stepStart[14], stepStart[15] + step);
		const double fullRms = maxRms(stepStart[13], stepStart[15]);
		std::printf("  trigless step 5: rms %.4f before, max %.4f after, %.0f Hz | pitchless step 9: %.4f -> %.4f, %.0f Hz | full step 13: max %.4f, %.0f Hz\n",
			before4, trigless, fTrigless, before8, pitchless, fPitchless, fullRms, fFull);
		check(trigless < before4 * 1.2, "a trigless trig fires no envelope: the note keeps decaying");
		check(std::abs(fTrigless - 520) < 60, "a trigless trig still changes the pitch (to C-5)");
		check(pitchless > before8 * 2.5, "a pitchless trig fires the envelopes");
		check(std::abs(fPitchless - fTrigless) < 60, "a pitchless trig keeps the pitch before it");
		check(fullRms > before8 * 2.5 && std::abs(fFull - fTrigless) > 150, "a full trig plays its own note");
	}

	// The last message of a type the page got (a null Value when none).
	Value lastOf(const Rig& _r, const char* _type)
	{
		for(auto it = _r.page.rbegin(); it != _r.page.rend(); ++it)
			if(it->find("type")->asString() == _type)
				return *it;
		return {};
	}

	// MM-P4 (ported onto the P6 desk in P8): POLY, MIDI track mutes, RECORD, MULTI TRIG and PORTAMENTO in
	// the kit, the MULTI MAP, another song.
	void intents(Rig& r);

	void p4(const Bytes& _rom)
	{
		std::puts("p4");
		Rig r(_rom);
		std::map<int, int> noteOns;	// channel -> note ons from the machine's MIDI out
		r.m.onMidi = [&](const synthLib::SMidiEvent& _e) { if((_e.a & 0xf0) == 0x90 && _e.c) ++noteOns[_e.a & 0x0f]; };
		r.msg(R"({"op":"ready"})");
		r.desk->setProbe(mmDesk::Desk::Probe::Running);
		r.run(600);
		while(r.desk->loaded() < 288)
			r.run(100);

		// POLY: SET STATUS 0x20, read back by status.
		r.msg(R"({"op":"poly","on":true})");
		r.run(1500);
		const auto poly = lastMachine(r).find("poly");
		check(poly && poly->isBool() && poly->asBool(), "POLY on (SET STATUS 0x20, status reads 1)");
		r.msg(R"({"op":"poly","on":false})");
		r.run(1500);
		check(lastMachine(r).find("poly")->isBool() && !lastMachine(r).find("poly")->asBool(), "POLY off again");
		// 0.3.5: the Song page's PATTERN | SONG switch: SET STATUS 0x10, the status read back
		const auto songMode = [&] { const auto d = lastMachine(r); const auto* s = d.find("song"); const auto* m = s ? s->find("songMode") : nullptr; return m && m->isBool() ? (m->asBool() ? 1 : 0) : -1; };
		r.msg(R"({"op":"seqMode","song":true})");
		r.run(1500);
		const int inSong = songMode();
		r.msg(R"({"op":"seqMode","song":false})");
		r.run(1500);
		std::printf("  sequencer mode: SONG -> %d, PATTERN -> %d\n", inSong, songMode());
		check(inSong == 1 && songMode() == 0, "seqMode: SONG mode and back to PATTERN mode, read back by status");

		// A pattern with MIDI track 1 notes every 4 steps (E01).
		auto p = *r.desk->pattern(64);
		p.length = 16;
		for(size_t t = 0; t < 6; ++t)
			p.amp[t] = p.filter[t] = p.lfo[t] = p.pitch[t] = p.chord[t] = p.midiTrig[t] = p.midiNote[t] = 0;
		for(auto& m : p.lockMasks) m.fill(0);
		p.lockRowCount = 0;
		p.chordNoteCount = 0;
		p.midiNoteCount = 0;
		for(uint8_t st = 0; st < 16; st += 4)
		{
			p.midiTrig[0] |= ed::mmStepBit(st);
			p.midiNote[0] |= ed::mmStepBit(st);
			p.midiNotes[p.midiNoteCount++] = ed::mmNoteEntryWord({0, st, 60});
		}
		const auto problems = ed::validate(p);
		check(problems.empty(), "the MIDI pattern validates" + (problems.empty() ? std::string() : ": " + problems.front()));
		r.msg(R"({"op":"set","kind":"pattern","doc":)" + ed::json::write(ed::mmPatternToJson(p)) + "}");
		r.run(1500);
		r.msgConfirmed(R"({"op":"select","p":64})");
		r.run(300);
		const auto g = *r.desk->global(static_cast<uint8_t>(std::max(0, r.desk->currentGlobal())));
		const int ch = g.midiSeqChannels[0] & 15;
		r.msg(R"({"op":"play"})");
		r.run(2000);
		noteOns.clear();
		r.run(2000);
		const int before = noteOns[ch];
		check(before >= 3, "MIDI track 1 plays on channel " + std::to_string(ch + 1) + " (" + std::to_string(before) + " notes in 2 s)");
		r.msg(R"({"op":"muteMidi","t":0,"on":true})");
		r.run(800);
		check(((r.tel.mutes.load() >> 6) & 1) == 1, "MIDI track 1 muted through the MUTE window (RAM 0x2bfedd)");
		check(lastMachine(r).find("mutes")->find("midi")->asNumber() == 1, "and the machine document shows it");
		noteOns.clear();
		r.run(2000);
		check(noteOns[ch] == 0, "muted: no notes on its channel (" + std::to_string(noteOns[ch]) + ")");
		r.msg(R"({"op":"muteMidi","t":0,"on":false})");
		r.run(800);
		noteOns.clear();
		r.run(2000);
		check(((r.tel.mutes.load() >> 6) & 1) == 0 && noteOns[ch] >= 3, "unmuted: its notes again (" + std::to_string(noteOns[ch]) + ")");
		// Taken back at once (the page's self-test does this): the second press follows the first's keys, so the
		// track ends unmuted (it stayed muted while the desk compared with memory alone).
		r.msg(R"({"op":"muteMidi","t":0,"on":true})");
		r.msg(R"({"op":"muteMidi","t":0,"on":false})");
		r.run(1200);
		check(((r.tel.mutes.load() >> 6) & 1) == 0 && lastMachine(r).find("mutes")->find("midi")->asNumber() == 0,
			"a mute taken back at once: unmuted in RAM and in the machine document");
		// Synth track mute: CC 3 through the mute parameter; the RAM shows it.
		r.msg(R"({"op":"mute","t":2,"on":true})");
		r.run(300);
		check(((r.tel.mutes.load() >> 2) & 1) == 1 && lastMachine(r).find("mutes")->find("synth")->asNumber() == 4,
			"synth track 3 muted (CC 3, RAM 0x2bfeb5), shown in the machine document");
		r.msg(R"({"op":"mute","t":2,"on":false})");
		r.run(300);

		// RECORD while playing: a note on track 1's channel is recorded into the pattern.
		const auto trigsBefore = r.desk->pattern(64)->pitch[0];
		r.msg(R"({"op":"record","mode":"live"})");
		r.run(500);
		check(r.tel.recording.load() == 2, "LIVE RECORDING on (RECORD + PLAY; RAM 0x2bff01)");
		check(lastOf(r, "telemetry").find("record")->asString() == "live", "the telemetry says live");
		const uint8_t base = g.baseChannel & 15;
		r.out.push_back({static_cast<uint8_t>(0x90 | base), 67, 100});
		r.run(120);
		r.out.push_back({static_cast<uint8_t>(0x80 | base), 67, 0});
		r.run(1500);
		r.msg(R"({"op":"record","mode":"off"})");
		r.run(2500);
		check(r.tel.recording.load() == 0, "recording off (RECORD twice)");
		const auto trigsAfter = r.desk->pattern(64)->pitch[0];
		check(trigsAfter != trigsBefore, "the live-recorded note is in the pattern read back (T1 trigs " + std::to_string(trigsBefore) + " -> " + std::to_string(trigsAfter) + ")");
		r.msg(R"({"op":"stop"})");
		r.run(500);
		// GRID RECORDING (stopped): the machine's TRIG keys write steps; the desk reads them back.
		r.msg(R"({"op":"record","mode":"grid"})");
		r.run(500);
		check(r.tel.recording.load() == 1, "GRID RECORDING on (RECORD; RAM 0x26bbb3)");
		const auto gridBefore = r.desk->pattern(64)->pitch[0];
		{
			const auto pk = *md::panelPacket(g_mm, md::PanelControl::Trigger12);
			r.panel.emplace_back(r.m.now() + 64, pk);
			r.panel.emplace_back(r.m.now() + 64 + 441, md::PanelPacket{pk.row, 0});
		}
		r.run(2500);
		check(r.desk->pattern(64)->pitch[0] == (gridBefore ^ ed::mmStepBit(11)), "a TRIG key in GRID RECORDING is read back while recording");
		r.msg(R"({"op":"record","mode":"off"})");
		r.run(600);
		check(r.tel.recording.load() == 0, "GRID RECORDING off");

		// MULTI TRIG and PORTAMENTO: kit fields without a live message (dump + LOAD KIT).
		auto k = *r.desk->workingKit();
		k.multiTrigMode = 1;
		k.multiTrigTiming = 3;
		k.splitKey = 48;
		k.splitTrack = 2;
		k.portamentoMask = static_cast<uint8_t>(k.portamentoMask & ~1);
		r.msg(R"({"op":"set","kind":"workingKit","doc":)" + ed::json::write(ed::mmKitToJson(k)) + "}");
		r.run(3000);
		const auto& w = *r.desk->workingKit();
		check(w.multiTrigMode == 1 && w.multiTrigTiming == 3 && w.splitKey == 48 && w.splitTrack == 2, "MULTI TRIG mode, timing and zones in the working kit");
		check((w.portamentoMask & 1) == 0, "PORTAMENTO ONLY LEGATO on track 1 in the working kit");

		// MULTI MAP: range 1 offset 2, length 8, transpose -3, timing 4/16, pattern A03.
		auto gl = g;
		gl.multiMap[1][0] = 2;
		gl.multiMap[2][0] = 2;
		gl.multiMap[3][0] = 8;
		gl.multiMap[4][0] = static_cast<uint8_t>(-3);
		gl.multiMap[5][0] = 3;
		r.msg(R"({"op":"set","kind":"global","doc":)" + ed::json::write(ed::mmGlobalToJson(gl)) + "}");
		r.run(3000);
		check(r.desk->global(gl.position) && *r.desk->global(gl.position) == gl, "MULTI MAP fields stored and read back");

		// Another song than the machine's: its slot is edited, LOAD SONG makes it the machine's.
		auto s = *r.desk->song(23);
		s.rows[0].bytes = {};
		s.rows[0].bytes[0] = 5;
		s.rows[0].bytes[ed::mmSongRow::g_length] = 16;
		s.rows[0].bytes[22] = s.rows[0].bytes[23] = 0xff;
		s.rows[1].bytes = {};
		s.rows[1].bytes[0] = 0xff;
		s.rows[1].bytes[22] = s.rows[1].bytes[23] = 0xff;
		r.msg(R"({"op":"set","kind":"song","doc":)" + ed::json::write(ed::mmSongToJson(s)) + "}");
		r.run(1500);
		check(r.desk->song(23) && r.desk->song(23)->rows[0].bytes[0] == 5, "song 24 (not the machine's) edited and read back");
		r.msg(R"({"op":"loadSong","s":23})");
		r.run(1500);
		check(r.desk->currentSong() == 23, "LOAD SONG: song 24 is the machine's");

		// Undo reaches a library slot write (P6: the core's one history).
		const auto k99 = *r.desk->kit(99);
		auto k99b = k99;
		k99b.name = {'U', 'N', 'D', 'O', 'M', 'E'};
		r.msg(R"({"op":"set","g":900,"kind":"kit","doc":)" + ed::json::write(ed::mmKitToJson(k99b)) + "}");
		r.run(2500);
		check(r.desk->kit(99)->name == k99b.name, "a kit written into K100 (a library slot)");
		r.msg(R"({"op":"undo"})");
		r.run(2500);
		check(r.desk->kit(99)->name == k99.name, "undo writes K100 back");

		intents(r);
	}

	// DESIGN-UNIFY.md phases 4-7 on the firmware: the Sound, Perform, global, Song and library gestures as intents
	// (mmDeskEdit.cpp), each read back from the machine (memory for the kit that plays, dumps for the slots).
	void intents(Rig& r)
	{
		std::puts("intents: Sound, Perform, the global, Song and the library");
		const auto kitId = r.desk->currentKit();
		const auto k = std::to_string(kitId);
		// MACHINE: a machine on track 5 with its start values (assign machine 0x5B, then its SYN page)
		r.msg(R"({"op":"machine","g":910,"k":)" + k + R"(,"t":4,"model":3,"keepFx":true})");
		r.run(3000);
		{
			const auto& w = *r.desk->workingKit();
			check(w.machines[4] == 3 && w.tracks[4].pages[0][7] == 64, "machine: SID-6581 on track 5 in the working kit (memory), TUNE at its start");
		}
		// ASSIGN, MULTI TRIG, PORTAMENTO: no live message (a dump to the kit's slot, LOAD KIT), one gesture each
		r.msg(R"({"op":"assign","g":911,"k":)" + k + R"(,"t":0,"src":2,"row":0,"page":3,"dest":1,"add":20})");
		r.run(4000);
		{
			const auto& w = *r.desk->workingKit();
			check(w.assignPage[0][4] == 3 && w.assignDest[0][4] == 1 && w.assignAdd[0][4] == 20, "assign: JOY U row 1 of track 1 in the working kit (memory)");
		}
		r.msg(R"({"op":"multiTrig","g":912,"k":)" + k + R"(,"mode":1,"splitKey":50,"splitTrack":3,"timing":3})");
		r.run(4000);
		{
			const auto& w = *r.desk->workingKit();
			check(w.multiTrigMode == 1 && w.splitKey == 50 && w.splitTrack == 3 && w.multiTrigTiming == 3,
				"multiTrig in the working kit (memory): mode " + std::to_string(w.multiTrigMode) + " key " + std::to_string(w.splitKey)
				+ " track " + std::to_string(w.splitTrack) + " timing " + std::to_string(w.multiTrigTiming));
		}
		const auto port0 = r.desk->workingKit()->portamentoMask;
		r.msg(R"({"op":"portamento","g":913,"k":)" + k + R"(,"t":1,"v":"legato"})");
		r.run(4000);
		check(r.desk->workingKit()->portamentoMask == (port0 & ~2), "portamento: track 2 ONLY LEGATO in the working kit (memory): "
			+ std::to_string(port0) + " -> " + std::to_string(r.desk->workingKit()->portamentoMask));
		// two kit edits without a live message one after the other (two gestures, the second before the first's dump is
		// back): both are in the working kit
		r.msg(R"({"op":"multiTrig","g":9121,"k":)" + k + R"(,"mode":2})");
		r.run(50);
		r.msg(R"({"op":"multiTrig","g":9122,"k":)" + k + R"(,"timing":5})");
		r.run(5000);
		check(r.desk->workingKit()->multiTrigMode == 2 && r.desk->workingKit()->multiTrigTiming == 5, "two kit edits in quick succession: both in the working kit (memory): mode "
			+ std::to_string(r.desk->workingKit()->multiTrigMode) + " timing " + std::to_string(r.desk->workingKit()->multiTrigTiming));
		// MULTI ENV: NRPN, every track's copy
		r.msg(R"({"op":"multiEnv","g":9130,"k":)" + k + R"(,"i":0,"v":30})");
		r.run(2000);
		{
			const auto& w = *r.desk->workingKit();
			bool all = true;
			for(const auto& tr : w.tracks)
				all = all && tr.multiEnv[0] == 30;
			check(all, "multiEnv: ATK 30 on every track (NRPN, memory)");
		}
		// MUTATE: many values at once
		r.msg(R"({"op":"params","g":914,"k":)" + k + R"(,"values":[[0,1,5,77],[2,2,0,33]]})");
		r.run(2000);
		check(r.desk->workingKit()->tracks[0].pages[1][5] == 77 && r.desk->workingKit()->tracks[2].pages[2][0] == 33, "params: two values at once (CC, memory)");
		// CLEAR MACHINE, then undo it in one step
		const auto before = *r.desk->workingKit();
		r.msg(R"({"op":"clearSound","g":915,"k":)" + k + R"(,"t":1})");
		r.run(2500);
		check(r.desk->workingKit()->machines[1] == 1, "clearSound: track 2 is GND-SIN (memory)");
		r.msg(R"({"op":"undo"})");
		r.run(3000);
		check(r.desk->workingKit()->machines[1] == before.machines[1] && r.desk->workingKit()->tracks[1].pages == before.tracks[1].pages,
			"undo: track 2's machine and pages again");

		// the MULTI MAP of the active global
		const auto gi = static_cast<uint8_t>(std::max(0, r.desk->currentGlobal()));
		r.msg(R"({"op":"multiMap","g":916,"i":0,"len":4,"tim":2})");
		r.run(3000);
		check(r.desk->global(gi)->multiMap[3][0] == 4 && r.desk->global(gi)->multiMap[5][0] == 2, "multiMap: range 1's length and timing read back");
		const auto ranges = [&] { const auto& h = r.desk->global(gi)->multiMap[0]; size_t n = 1; while(n < 32 && h[n] > h[n - 1]) ++n; return n; };
		const auto n0 = ranges();
		r.msg(R"({"op":"multiMapSplit","g":917,"i":0})");
		r.run(3000);
		check(ranges() == n0 + 1, "multiMapSplit: one range more (" + std::to_string(n0) + " -> " + std::to_string(ranges()) + "), read back");

		// Song rows on song 24 (not the machine's)
		Value row = Value::object();
		for(const auto& [key, v] : std::vector<std::pair<std::string, Value>>{{"kind", "pattern"}, {"pattern", 6}, {"target", 0}, {"repeats", 1},
			{"mutes", 0}, {"midiMutes", 0}, {"offset", 0}, {"length", 16}, {"transpose", 0}, {"tempo", Value()}, {"x3", 0}, {"x21", 0}})
			row.set(key, v);
		Value zeros = Value::array();
		for(int i = 0; i < 6; ++i) zeros.push(0);
		row.set("trackTranspose", zeros);
		row.set("midiTranspose", zeros);
		const auto rows = [&] { return ed::mmSongUsedRows(*r.desk->song(23)); };
		const auto r0 = rows();
		r.msg(R"({"op":"rowInsert","g":918,"s":23,"i":0,"row":)" + ed::json::write(row) + "}");
		r.run(2500);
		check(rows() == r0 + 1 && r.desk->song(23)->rows[0].bytes[0] == 6, "rowInsert: song 24 has the row, read back");
		r.msg(R"({"op":"rowDelete","g":919,"s":23,"i":0})");
		r.run(2500);
		check(rows() == r0, "rowDelete: the row goes, read back");

		// the library: a stored kit copied over another (it asks), renamed, undone; a pattern copied
		const auto k98 = *r.desk->kit(98), k99 = *r.desk->kit(99);
		r.msgConfirmed(R"({"op":"kitCopyTo","g":920,"from":98,"to":99})");
		r.run(2500);
		check(r.desk->kit(99)->name == k98.name && r.desk->kit(99)->machines == k98.machines, "kitCopyTo: K99 into K100, read back");
		r.msg(R"({"op":"kitRename","g":921,"k":99,"name":"intent"})");
		r.run(2500);
		{
			const auto named = *r.desk->kit(99);
			check(std::string(named.name.begin(), named.name.begin() + 6) == "INTENT", "kitRename: K100 is INTENT, read back (no question)");
		}
		r.msg(R"({"op":"undo"})");
		r.run(2500);
		r.msg(R"({"op":"undo"})");
		r.run(2500);
		check(r.desk->kit(99)->name == k99.name, "undo twice: K100 as it was");
		r.msgConfirmed(R"({"op":"patCopyTo","g":922,"from":64,"to":66})");
		r.run(3000);
		check(r.desk->pattern(66)->midiTrig[0] == r.desk->pattern(64)->midiTrig[0] && r.desk->pattern(66)->midiNoteCount == r.desk->pattern(64)->midiNoteCount,
			"patCopyTo: E01 into E03, read back");
		// into the kit that plays: it is loaded too (the slot's dump, LOAD KIT)
		r.msgConfirmed(R"({"op":"kitCopyTo","g":923,"from":98,"to":)" + k + "}");
		r.run(4000);
		{
			const auto w = *r.desk->workingKit();
			const auto slot = *r.desk->kit(static_cast<uint8_t>(kitId));
			check(slot.name == k98.name && slot.machines == k98.machines, "kitCopyTo into the kit that plays: its slot holds K99 (read back)");
			const auto text = [](const ed::MmKit& _k) { std::string n; for(const auto c : _k.name) if(c >= 0x20 && c < 0x7f) n += static_cast<char>(c); return n; };
			bool same = w.machines == k98.machines && w.levels == k98.levels;
			for(size_t t = 0; t < 6; ++t)
				same = same && w.tracks[t].pages == k98.tracks[t].pages;
			check(r.desk->currentKit() == kitId && same, "and it plays K99's sound (LOAD KIT; memory): \"" + text(w) + "\" vs \"" + text(k98)
				+ "\", level 1 " + std::to_string(w.levels[0]) + " vs " + std::to_string(k98.levels[0]));
			// K99 is a never-written slot (name byte 0 is 0xff): LOAD KIT plays it as NEW KIT (measured: the memory
			// image's name bytes 0-7 are "NEW KIT", the rest as stored). Loaded and untouched, the kit is clean.
			check(w.name == ed::mmKitAsLoaded(k98).name && (k98.name[0] != 0xff || text(w).rfind("NEW KIT", 0) == 0),
				"the kit that plays has the name LOAD KIT gives it (\"" + text(w) + "\")");
			check(kitWorking(r) == "clean", "loaded and untouched, the kit that plays is clean (not edited)");
		}
	}

	// MM-P8: pattern chaining through the desk on the firmware (hold BANK, press the TRIG keys; manual
	// 1-46): from stopped and while playing, sent again quickly (the latest wins), CLEAR, the other bank
	// group, a pick that ends a chain, and from song mode.
	struct Plays
	{
		std::vector<int> patterns;
		std::string text() const
		{
			std::string t;
			for(const auto p : patterns)
				t += (t.empty() ? "" : " ") + (p < 0 ? std::string("?") : ed::mmPatternName(static_cast<uint8_t>(p)));
			return t;
		}
	};

	// The pattern the machine plays after each of the next _wraps pattern ends (status asked at the wrap).
	Plays wraps(Rig& _r, const int _wraps)
	{
		Plays out;
		int last = _r.tel.step.load();
		for(double t = 0; static_cast<int>(out.patterns.size()) < _wraps && t < 40000; t += 10)
		{
			_r.run(10);
			const int s = _r.tel.step.load();
			if(s < last)
			{
				_r.run(60);
				_r.out.push_back(ed::mmStatusRequest(ed::MmStatus::Pattern));
				_r.run(60);
				out.patterns.push_back(_r.desk->currentPattern());
			}
			last = s;
		}
		return out;
	}

	std::vector<int> chainOf(const Rig& _r, bool& _active)
	{
		_active = false;
		const auto m = lastMachine(_r);
		const auto* c = m.find("desk") ? m.find("desk")->find("chain") : nullptr;
		std::vector<int> v;
		if(!c || !c->isObject())
			return v;
		_active = c->find("active")->asBool();
		for(const auto& p : c->find("patterns")->asArray())
			v.push_back(static_cast<int>(p.asNumber()));
		return v;
	}

	void chains(const Bytes& _rom)
	{
		std::puts("chain");
		Rig r(_rom);
		r.msg(R"({"op":"ready"})");
		r.desk->setProbe(mmDesk::Desk::Probe::Running);
		r.run(600);
		while(r.desk->loaded() < 288)
			r.run(100);
		check(lastMachine(r).find("capabilities")->find("can")->find("chains")->asBool(), "the emulator can chain");
		bool active = false;
		check(chainOf(r, active).empty() && !active && lastMachine(r).find("desk")->find("bankGroup")->asNumber() == 0,
			"no chain at boot, BANK GROUP A-D (RAM)");

		// from stopped: the first pattern is picked, PLAY plays the chain
		r.msg(R"({"op":"chain","patterns":[2,4]})");
		r.run(600);
		auto c = chainOf(r, active);
		check(active && c == std::vector<int>{2, 4}, "stopped: chain A03 A05 is the machine's (RAM 0x2bc2c4)");
		check(r.desk->currentPattern() == 2, "and A03 is the current pattern");
		r.msg(R"({"op":"play"})");
		r.run(300);
		auto w = wraps(r, 3);
		check(w.patterns == std::vector<int>{4, 2, 4}, "PLAY: A03, then A05, A03, A05 (" + w.text() + ")");

		// sent again quickly, as the page does at every pad: the latest wins
		r.msg(R"({"op":"chain","patterns":[5,6]})");
		r.msg(R"({"op":"chain","patterns":[5,6,7]})");
		r.msg(R"({"op":"chain","patterns":[7,5]})");
		r.run(800);
		c = chainOf(r, active);
		check(active && c == std::vector<int>{7, 5}, "three chains in a row: the machine holds the last, A08 A06");
		w = wraps(r, 3);
		check(w.patterns == std::vector<int>{7, 5, 7}, "and plays it from the pattern end (" + w.text() + ")");

		// CLEAR: the pattern that plays goes on
		r.msg(R"({"op":"chainClear"})");
		r.run(600);
		chainOf(r, active);
		const int on = r.desk->currentPattern();
		check(!active, "CLEAR ends the chain (BANK + the TRIG key of the pattern that plays)");
		w = wraps(r, 2);
		check(w.patterns == std::vector<int>{on, on}, "and " + ed::mmPatternName(static_cast<uint8_t>(on)) + " plays on (" + w.text() + ")");

		// a chain, then CLEAR at once (it waits for the chain's keys)
		r.msg(R"({"op":"chain","patterns":[9,10]})");
		r.msg(R"({"op":"chainClear"})");
		r.run(800);
		chainOf(r, active);
		check(!active, "CLEAR right after a chain: no chain once its keys are through");

		// E-H: BANK GROUP first
		r.msg(R"({"op":"chain","patterns":[66,64,65]})");
		r.run(800);
		c = chainOf(r, active);
		check(active && c == std::vector<int>{66, 64, 65} && lastMachine(r).find("desk")->find("bankGroup")->asNumber() == 1,
			"chain E03 E01 E02: BANK GROUP pressed to E-H first");
		w = wraps(r, 3);
		check(w.patterns == std::vector<int>{66, 64, 65} || w.patterns == std::vector<int>{64, 65, 66}, "it plays (" + w.text() + ")");

		// a pick while chained asks, and ends the chain
		const auto from = r.page.size();
		r.msg(R"({"op":"select","p":3})");
		bool asked = false;
		for(size_t i = from; i < r.page.size(); ++i)
			asked = asked || (r.page[i].find("type")->asString() == "ask" && r.page[i].find("ask")->asString() == "breakChain");
		check(asked, "a pick while chained asks first (breakChain)");
		r.msgConfirmed(R"({"op":"select","p":3,"force":true})");
		r.run(800);
		chainOf(r, active);
		check(!active && lastMachine(r).find("desk")->find("bankGroup")->asNumber() == 0, "confirmed: the chain ends (BANK GROUP back to A-D)");
		w = wraps(r, 2);
		check(w.patterns == std::vector<int>{3, 3}, "and A04 plays, the chain is gone (" + w.text() + ")");

		// from song mode: pattern mode first, then the chain plays
		r.msg(R"({"op":"stop"})");
		r.run(400);
		r.out.push_back(ed::mmSetStatus(ed::MmStatus::SongMode, 1));
		r.run(1500);
		check(lastMachine(r).find("song")->find("songMode")->asBool(), "song mode (SET STATUS)");
		r.msg(R"({"op":"play"})");
		r.run(500);
		r.msg(R"({"op":"chain","patterns":[12,13]})");
		r.run(1500);
		c = chainOf(r, active);
		check(active && c == std::vector<int>{12, 13} && !lastMachine(r).find("song")->find("songMode")->asBool(),
			"a chain from song mode: pattern mode, chain A13 A14");
		w = wraps(r, 3);
		check(w.patterns.size() == 3 && w.patterns[1] != w.patterns[2] && (w.patterns[2] == 12 || w.patterns[2] == 13), "the chain plays, not the song (" + w.text() + ")");
		r.msg(R"({"op":"stop"})");
		r.run(300);
		r.msg(R"({"op":"stop"})");
		r.run(500);
		chainOf(r, active);
		check(!active, "STOP twice ends it, as on the machine");
	}

	// MM-P4 (ported in P8): HW MIDI. The desk drives the emulated machine through the plug-in's wire port as if
	// it were a real one on MIDI: DIN speed, no panel keys, no telemetry, no memory. The test plays the person
	// at the machine.
	void hwLink(const Bytes& _rom)
	{
		std::puts("hw");
		Rig r(_rom, true);
		const auto lifecycle = [&] { const auto m = lastMachine(r); return m.isObject() ? m.find("lifecycle")->asString() : std::string(); };
		r.msg(R"({"op":"ready"})");
		const auto t0 = r.ms();
		while(r.ms() - t0 < 8000 && !(r.desk->currentPattern() >= 0 && r.desk->pattern(static_cast<uint8_t>(r.desk->currentPattern()))
			&& r.desk->workingKit()))
			r.run(50);
		check(lifecycle() == "ready", "connected: the machine answers status requests");
		check(r.desk->workingKit().has_value(), "the current pattern and its kit arrive over DIN (" + std::to_string(int(r.ms() - t0)) + " ms)");
		const auto tk = r.ms();
		while(r.ms() - tk < 180000)
		{
			bool all = true;
			for(uint8_t i = 0; i < 128 && all; ++i)
				all = r.desk->kit(i).has_value();
			if(all)
				break;
			r.run(200);
		}
		std::printf("  all 128 kits after %.1f s at DIN speed\n", (r.ms() - tk) / 1000);

		// A kit value goes out as a CC; the machine plays it (its working kit in RAM).
		auto k = *r.desk->workingKit();
		k.tracks[0].pages[1][5] = static_cast<uint8_t>(k.tracks[0].pages[1][5] == 90 ? 91 : 90);
		r.msg(R"({"op":"set","kind":"workingKit","doc":)" + ed::json::write(ed::mmKitToJson(k)) + "}");
		r.run(300);
		Bytes region;
		uint32_t seq = 0;
		r.tel.readWorkingKit(region, seq);
		check(region.size() > 5 && region[5 + 0x11 + 8 + 5] == k.tracks[0].pages[1][5], "a kit value as a CC reaches the machine's working kit");

		// A pattern edit waits for SYSEX RECV, which only the person can open.
		const auto slot = static_cast<uint8_t>(r.desk->currentPattern());
		auto p = *r.desk->pattern(slot);
		p.amp[0] ^= ed::mmStepBit(15);
		p.pitch[0] |= p.amp[0] & ed::mmStepBit(15);
		if(!(p.amp[0] & ed::mmStepBit(15)))
			p.pitch[0] &= ~ed::mmStepBit(15);
		p.filter[0] &= ~ed::mmStepBit(15);
		p.lfo[0] &= ~ed::mmStepBit(15);
		r.msg(R"({"op":"set","kind":"pattern","doc":)" + ed::json::write(ed::mmPatternToJson(p)) + "}");
		r.run(500);
		const auto recv = [&] { return *lastMachine(r).find("recv"); };
		check(recv().find("state")->asString() == "waitingUser" && recv().find("waiting")->asNumber() == 1, "the dump waits for SYSEX RECV (SEND 1)");
		r.userKeys(mmDesk::RecvSession::enterMacro());
		r.run(1500);
		check(r.tel.recvActive.load() == 1, "the person opened GLOBAL > FILE > SYSEX RECV");
		const auto ts = r.ms();
		r.msg(R"({"op":"hwSend"})");
		std::printf("  hwSend: %s\n", r.lastResult().find("note")->asString().c_str());
		while(r.ms() - ts < 10000 && r.desk->lastRoundTripMs() < 0)
			r.run(50);
		check(r.desk->lastRoundTripMs() > 0 && r.desk->pattern(slot)->amp[0] == p.amp[0], "sent and confirmed by read-back (" + std::to_string(int(r.ms() - ts)) + " ms)");
		check(recv().find("waiting")->asNumber() == 0, "nothing waits any more");
		r.userKeys(mmDesk::RecvSession::exitKeys());
		r.run(500);

		// POLY over MIDI: SET STATUS.
		r.msg(R"({"op":"poly","on":true})");
		r.run(2500);
		check(lastMachine(r).find("poly")->isBool() && lastMachine(r).find("poly")->asBool(), "POLY over MIDI (SET STATUS)");
		r.msg(R"({"op":"poly","on":false})");
		r.run(2500);
		// What has no MIDI message says so, from the capabilities.
		const auto caps = *lastMachine(r).find("capabilities");
		check(!caps.find("can")->find("midiMutes")->asBool() && !caps.find("can")->find("gridRecord")->asBool()
			&& caps.find("reasons")->find("midiMutes") && caps.find("reasons")->find("gridRecord"), "MIDI track mutes and RECORD disabled over MIDI, with the reasons");
		r.msg(R"({"op":"muteMidi","t":0,"on":true})");
		check(!r.lastResult().find("ok")->asBool(), "MIDI track mutes refused over MIDI");
		r.msg(R"({"op":"record","mode":"grid"})");
		check(!r.lastResult().find("ok")->asBool(), "RECORD refused over MIDI");
		// MIDI Start is ignored while CONTROL IN TRANSPORT is IGNORE (the factory setting)...
		r.wire.send(Bytes{0xfa});
		r.run(1500);
		check(r.tel.running.load() == 0, "MIDI Start is ignored with TRANSPORT IGNORE (global 0x06 = 0)");
		r.wire.send(Bytes{0xfc});
		r.run(300);
		// ... so PLAY asks to set it
		const auto asked = r.page.size();
		r.msg(R"({"op":"play"})");
		bool ask = false;
		for(size_t i = asked; i < r.page.size(); ++i)
			ask = ask || (r.page[i].find("type")->asString() == "ask" && r.page[i].find("ask")->asString() == "transportIgnore");
		check(ask, "PLAY over MIDI with TRANSPORT IGNORE offers TRANSPORT ACCEPT");
		r.msgConfirmed(R"({"op":"play"})");
		r.run(300);
		check(recv().find("waiting")->asNumber() == 1, "confirmed: the global waits for SYSEX RECV");
		r.userKeys(mmDesk::RecvSession::enterMacro());
		r.run(1500);
		r.msg(R"({"op":"hwSend"})");
		r.run(3000);
		r.userKeys(mmDesk::RecvSession::exitKeys());
		r.run(800);
		check(r.desk->global(static_cast<uint8_t>(r.desk->currentGlobal()))->transportIn == 1, "TRANSPORT ACCEPT stored (read back)");
		r.msg(R"({"op":"play"})");
		r.run(1500);
		check(r.tel.running.load() == 1, "PLAY as MIDI Start plays the machine");
		r.msg(R"({"op":"stop"})");
		r.run(800);
		check(r.tel.running.load() == 0, "STOP as MIDI Stop");

		// Unplugged, then back.
		r.unplugged = true;
		r.run(5000);
		check(lifecycle() == "hwLost", "unplugged: HW NO MIDI");
		r.unplugged = false;
		r.run(2500);
		check(lifecycle() == "ready", "back: HW MIDI");
		std::printf("  %zu bytes out, %zu bytes in, at DIN speed both ways\n", r.hwBytesOut, r.hwBytesIn);
	}

	// The result the page got for a command id (a null Value when none came yet).
	Value resultFor(const Rig& _r, const int _id)
	{
		for(auto it = _r.page.rbegin(); it != _r.page.rend(); ++it)
			if(it->find("type")->asString() == "result" && it->find("id") && static_cast<int>(it->find("id")->asNumber()) == _id)
				return *it;
		return {};
	}
	bool resultOk(const Rig& _r, const int _id)
	{
		const auto v = resultFor(_r, _id);
		if(v.isObject() && !v.find("ok")->asBool())
			std::printf("    result %d: %s\n", _id, v.find("errors")->asArray().empty() ? "" : v.find("errors")->asArray()[0].asString().c_str());
		return v.isObject() && v.find("ok")->asBool();
	}

	// DESIGN-UNIFY.md 1 and phase 3, the lost update: the machine writes a step itself (LIVE RECORDING from the
	// keyboard, GRID RECORDING from its TRIG keys); the page then edits other steps as intents (step, lock). The core
	// applies them to its own pattern, which holds the recorded step (read back while the machine records), so the
	// pattern it pushes keeps it. Before the intents the page sent its whole copy of the pattern, which did not.
	// Bug 4 (the user journeys, 2026-10-05): the panel keys while the desk is parked on SYSEX RECV. The keys go to the
	// panel directly here (as the RECV session's own do), so the desk's rule does not decide what is seen.
	// Measured: a key pressed while the machine is still taking the dump is lost; once the dump is taken (the RECV
	// count moved) RECORD, PLAY, STOP, the MUTE window and BANK GROUP all work on SYSEX RECV. Then the desk: RECORD
	// while the dump is on its way is refused as busy (it was answered ok and lost), and taken once the dump is in.
	void parkedKeys(const Bytes& _rom)
	{
		std::puts("parked");
		Rig r(_rom);
		r.msg(R"({"op":"ready"})");
		r.desk->setProbe(mmDesk::Desk::Probe::Running);
		r.run(600);
		while(r.desk->loaded() < 288)
			r.run(100);
		int flip = 0;
		uint32_t count0 = 0;
		// An edit of the current pattern (one SLIDE step): its dump parks the machine on SYSEX RECV.
		const auto park = [&]
		{
			const auto cur = static_cast<uint8_t>(lastMachine(r).find("pattern")->find("current")->asNumber());
			auto p = *r.desk->pattern(cur);
			p.slide[0] ^= ed::mmStepBit(static_cast<size_t>(1 + (flip++ & 7)));
			count0 = r.tel.recvCount.load();
			r.msg(R"({"op":"set","kind":"pattern","doc":)" + ed::json::write(ed::mmPatternToJson(p)) + "}");
			for(int i = 0; i < 300 && !r.desk->recvParked(); ++i)
				r.run(10);
			return r.desk->recvParked();
		};
		const auto taken = [&]
		{
			const auto t0 = r.ms();
			for(int i = 0; i < 300 && r.tel.recvCount.load() == count0; ++i)
				r.run(10);
			return r.ms() - t0;
		};
		const auto onRecv = [&] { return r.tel.recvActive.load() == 1 && md::MmTelemetry::screenOf(r.tel.screen.load()) == elektronData::MmScreen::GlobalEdit; };
		const auto leave = [&] { r.run(4000); };	// the session leaves (idle)

		// a key while the dump is still being taken
		check(park(), "parked on SYSEX RECV");
		r.userKeys({mmDesk::Key::Record});
		const double tookMs = taken();
		r.run(800);
		std::printf("  RECORD pressed while the dump is taken (%.0f ms more): recording %d (on SYSEX RECV: %s)\n", tookMs, r.tel.recording.load(), onRecv() ? "yes" : "no");
		check(r.tel.recording.load() == 0, "a key pressed while the machine takes a dump is lost");
		leave();

		// keys once the dump is taken
		const auto afterTaken = [&](const char* _what, const std::vector<mmDesk::Key>& _keys, const std::function<int()>& _read, const int _want)
		{
			check(park(), std::string("parked on SYSEX RECV before ") + _what);
			taken();
			r.run(300);
			const int before = _read();
			r.userKeys(_keys);
			r.run(800);
			const int after = _read();
			std::printf("  dump taken, %s: %d -> %d (on SYSEX RECV: %s)\n", _what, before, after, onRecv() ? "yes" : "no");
			check(after == _want, std::string(_what) + " works on SYSEX RECV once the dump is taken");
			leave();
		};
		afterTaken("RECORD", {mmDesk::Key::Record}, [&] { return r.tel.recording.load(); }, 1);
		afterTaken("RECORD again (off)", {mmDesk::Key::Record}, [&] { return r.tel.recording.load(); }, 0);
		afterTaken("PLAY (the step moves)", {mmDesk::Key::Play}, [&] { return r.tel.step.load() > 0 ? 1 : 0; }, 1);
		afterTaken("STOP", {mmDesk::Key::Stop}, [&] { const int s = r.tel.step.load(); r.run(300); return r.tel.step.load() != s ? 1 : 0; }, 0);
		const int bg = r.tel.bankGroup.load();
		afterTaken("BANK GROUP", {mmDesk::Key::BankGroup}, [&] { return r.tel.bankGroup.load(); }, bg ^ 1);
		afterTaken("BANK GROUP back", {mmDesk::Key::BankGroup}, [&] { return r.tel.bankGroup.load(); }, bg);
		afterTaken("MUTE window + TRIG 9 + EXIT", {mmDesk::Key::MuteWindow, mmDesk::Key::Trig9, mmDesk::Key::Exit}, [&] { return (r.tel.mutes.load() >> 6) & 1; }, 1);
		afterTaken("MUTE window + TRIG 9 + EXIT (back)", {mmDesk::Key::MuteWindow, mmDesk::Key::Trig9, mmDesk::Key::Exit}, [&] { return (r.tel.mutes.load() >> 6) & 1; }, 0);

		// the desk: RECORD while the dump is on its way, then once it is taken
		check(park(), "parked again");
		r.run(10);	// the session sends the dump on its next step; the machine takes it over about 40 ms
		check(r.tel.recvCount.load() == count0, "the dump not taken yet");
		r.msg(R"({"op":"record","mode":"grid"})");
		const auto first = r.lastResult();
		std::printf("  RECORD while the dump is taken: %s\n", ed::json::write(first).c_str());
		check(!first.find("ok")->asBool() && first.find("errors")->asArray()[0].asString() == "The panel is busy (SYSEX RECV); try again.",
			"the desk refuses it as busy (it was answered ok and lost)");
		taken();
		r.run(300);
		r.msg(R"({"op":"record","mode":"grid"})");
		const auto again = r.lastResult();
		r.run(800);
		std::printf("  RECORD once the dump is taken: %s, recording %d, recv %s\n", ed::json::write(again).c_str(), r.tel.recording.load(), r.desk->recvState().c_str());
		check(again.find("ok")->asBool() && r.tel.recording.load() == 1, "once the dump is taken RECORD is taken: GRID RECORDING");
		r.msg(R"({"op":"record","mode":"off"})");
		r.run(800);
		check(r.tel.recording.load() == 0, "GRID RECORDING off");
	}

	void recording(const Bytes& _rom)
	{
		std::puts("recording");
		Rig r(_rom);
		r.msg(R"({"op":"ready"})");
		r.desk->setProbe(mmDesk::Desk::Probe::Running);
		r.run(600);
		while(r.desk->loaded() < 288)
			r.run(100);

		// E02: T1 trigs on steps 0 and 8 (C-3), nothing else
		constexpr uint8_t slot = 65;
		auto p = *r.desk->pattern(slot);
		p.length = 16;
		for(auto* m : {&p.amp, &p.filter, &p.lfo, &p.noteOff, &p.midiTrig, &p.midiNoteOff, &p.pitch, &p.chord, &p.midiNote, &p.slide, &p.midiSlide})
			m->fill(0);
		for(auto& n : p.notes)
			n.fill(ed::MmPattern::g_noNote);
		for(auto& m : p.lockMasks)
			m.fill(0);
		for(auto& row : p.lockRows)
			row.fill(ed::MmPattern::g_noLock);
		p.lockRowCount = 0;
		p.chordNotes.fill(0xffff);
		p.chordNoteCount = 0;
		p.midiNotes.fill(0xffff);
		p.midiNoteCount = 0;
		for(const uint8_t st : {0, 8})
		{
			for(auto* m : {&p.pitch, &p.amp, &p.filter, &p.lfo})
				(*m)[0] |= ed::mmStepBit(st);
			p.notes[0][st] = 48;
		}
		const auto problems = ed::validate(p);
		check(problems.empty(), "the pattern validates" + (problems.empty() ? std::string() : ": " + problems.front()));
		r.msg(R"({"op":"set","kind":"pattern","doc":)" + ed::json::write(ed::mmPatternToJson(p)) + "}");
		r.run(2000);
		r.msgConfirmed(R"({"op":"select","p":65})");
		r.run(500);
		const auto g = *r.desk->global(static_cast<uint8_t>(std::max(0, r.desk->currentGlobal())));
		const uint8_t base = g.baseChannel & 15;

		// LIVE RECORDING: a note from the keyboard on T1's channel lands on a step
		r.msg(R"({"op":"play"})");
		r.run(1500);
		r.msg(R"({"op":"record","mode":"live"})");
		r.run(500);
		check(r.tel.recording.load() == 2, "LIVE RECORDING on");
		// what T1 holds now, step by step: its trig and its note (a recorded note may land on a step that had a trig)
		const auto t1 = [&r] { const auto q = *r.desk->pattern(slot); std::map<size_t, int> m; for(size_t s = 0; s < 64; ++s) if(ed::mmStepSet(q.pitch[0], s)) m[s] = q.notes[0][s]; return m; };
		const auto before = t1();
		r.out.push_back({static_cast<uint8_t>(0x90 | base), 67, 100});
		r.run(120);
		r.out.push_back({static_cast<uint8_t>(0x80 | base), 67, 0});
		r.run(2500);
		std::map<size_t, int> recorded;
		for(const auto& [st, n] : t1())
			if(!before.count(st) || before.at(st) != n)
				recorded[st] = n;
		std::string where;
		for(const auto& [st, n] : recorded)
			where += " step " + std::to_string(st + 1) + " note " + std::to_string(n);
		check(!recorded.empty(), "the machine recorded the note on T1 and the core read it back:" + (where.empty() ? std::string(" nothing") : where));
		const auto survived = [&recorded](const ed::MmPattern& _q)
		{
			for(const auto& [st, n] : recorded)
				if(!ed::mmStepSet(_q.pitch[0], st) || _q.notes[0][st] != n)
					return false;
			return true;
		};
		// the page, which may not have seen that step yet, edits other steps: T2 step 5 a note, a FILTER lock on T1 step 0
		r.msg(R"({"op":"step","id":9001,"g":901,"p":65,"t":1,"s":5,"v":{"n":[60],"a":1,"f":1,"l":1}})");
		r.msg(R"({"op":"lock","id":9002,"g":901,"p":65,"t":0,"page":2,"i":1,"s":0,"v":99})");
		r.run(300);
		check(resultOk(r, 9001) && resultOk(r, 9002), "the page's step and lock intents are taken while the machine records");
		r.run(3000);
		r.msg(R"({"op":"record","mode":"off"})");
		r.run(1500);
		r.msg(R"({"op":"stop"})");
		r.run(4000);
		// the machine's own pattern, read again
		r.msg(R"({"op":"load","kind":"pattern","slot":65})");
		r.run(3000);
		auto after = *r.desk->pattern(slot);
		check(!recorded.empty() && survived(after), "LIVE: the recorded note survived the page's edits of other steps (no lost update)");
		check(ed::mmStepSet(after.pitch[1], 5) && after.notes[1][5] == 60, "LIVE: the page's step is on the machine (T2 step 6, C-4)");
		const auto row = ed::mmLockRow(after, {0, 2, 1});
		check(row >= 0 && after.lockRows[static_cast<size_t>(row)][0] == 99, "LIVE: the page's lock is on the machine (T1 step 1, FILTER)");
		check(r.desk->pattern(slot)->pitch[0] == after.pitch[0], "the core's pattern is the machine's");

		// GRID RECORDING (stopped): a TRIG key writes a step; the page then edits another track's step
		r.msg(R"({"op":"record","mode":"grid"})");
		r.run(600);
		check(r.tel.recording.load() == 1, "GRID RECORDING on");
		const auto gridBefore = r.desk->pattern(slot)->pitch[0];
		{
			const auto pk = *md::panelPacket(g_mm, md::PanelControl::Trigger12);
			r.panel.emplace_back(r.m.now() + 64, pk);
			r.panel.emplace_back(r.m.now() + 64 + 441, md::PanelPacket{pk.row, 0});
		}
		r.run(2500);
		const auto gridRecorded = r.desk->pattern(slot)->pitch[0] ^ gridBefore;
		check(gridRecorded == ed::mmStepBit(11), "a TRIG key in GRID RECORDING wrote T1 step 12; the core read it back");
		r.msg(R"({"op":"step","id":9003,"g":902,"p":65,"t":2,"s":3,"v":{"n":[55],"a":1,"f":1,"l":1}})");
		r.run(300);
		check(resultOk(r, 9003), "the page's step intent is taken while the machine grid-records");
		r.run(3000);
		r.msg(R"({"op":"record","mode":"off"})");
		r.run(4000);
		r.msg(R"({"op":"load","kind":"pattern","slot":65})");
		r.run(3000);
		after = *r.desk->pattern(slot);
		check((after.pitch[0] & gridRecorded) == gridRecorded && survived(after),
			"GRID: the recorded steps survived the page's edit of another track's step");
		check(ed::mmStepSet(after.pitch[2], 3) && after.notes[2][3] == 55, "GRID: the page's step is on the machine (T3 step 4)");
		// one undo step per gesture: undoing the GRID gesture takes T3's step away, the recorded steps stay
		r.msg(R"({"op":"undo","id":9004})");
		r.run(4000);
		after = *r.desk->pattern(slot);
		check(resultOk(r, 9004) && !ed::mmStepSet(after.pitch[2], 3) && (after.pitch[0] & gridRecorded) == gridRecorded,
			"undo: the page's last gesture goes, the recorded steps stay");
		// for contrast, the path the intents replace: the page's whole copy of the pattern from before the recording
		// (a set, as the page sent every Sequence gesture) overwrites what the machine recorded
		r.msg(R"({"op":"set","id":9005,"g":903,"kind":"pattern","doc":)" + ed::json::write(ed::mmPatternToJson(p)) + "}");
		r.run(4000);
		r.msg(R"({"op":"load","kind":"pattern","slot":65})");
		r.run(3000);
		check(resultOk(r, 9005) && !survived(*r.desk->pattern(slot)), "the old whole-document set from the page's stale copy loses the recorded note (the bug the intents fix)");
	}
}

namespace
{
	// B-014: what every kind of editing costs the audio thread on the Monomachine (mdDeskFirmwareTest actions is the
	// Machinedrum's): the pattern plays; each phase repeats one user action at a person's rate (the page's commands);
	// per action the traffic, the 128/256-frame buffers a second over 50 / 100 M instructions, the hot time a second
	// (a 5-block mean over 1.3x idle) and the worst 256-frame buffer. ACTIONS / ACTIONS_BUDGET as on the MD.
	void actions(const Bytes& _rom)
	{
		Rig r(_rom);
		r.msg(R"({"op":"ready"})");
		r.desk->setProbe(mmDesk::Desk::Probe::Running);
		r.run(600);
		for(int i = 0; i < 600 && r.desk->loaded() < 288; ++i)
			r.run(100);
		auto& m = r.m;
		const int p = std::max(0, r.desk->currentPattern());
		const auto ps = std::to_string(p);
		const auto k = std::to_string(std::max(0, r.desk->currentKit()));
		r.msg(R"({"op":"tempo","bpm":120})");
		r.msg(R"({"op":"play"})");
		r.run(2000);
		for(int i = 0; i < 20; ++i)
		{
			const auto b = r.bytesSent;
			r.run(500);
			if(r.bytesSent - b < 64)
				break;
		}
		const char* only = std::getenv("ACTIONS");
		const double budget = std::getenv("ACTIONS_BUDGET") ? std::atof(std::getenv("ACTIONS_BUDGET")) : -1;
		uint32_t rng = 777;
		const auto rnd = [&](const int _n) { rng = rng * 1664525u + 1013904223u; return static_cast<int>((rng >> 8) % static_cast<uint32_t>(_n)); };
		int gesture = 4000;
		double idleMean = 0;
		std::printf("== B-014 MM actions (stream %s): action | edits | dumps CCs bytes/s | heavy 128 / 256 a second | hot ms/s | worst 256 (M instr)\n",
			mmDesk::emulatorProfile().stream.valueBytesPerSecond > 0 ? "on" : "off");
		const auto action = [&](const char* _name, const double _ms, const double _everyMs, const std::function<void(int)>& _edit)
		{
			if(only && std::string(_name) != "idle" && ("," + std::string(only) + ",").find("," + std::string(_name) + ",") == std::string::npos)
				return;
			const auto b0 = r.bytesSent, d0 = r.dumpsSent, c0 = r.ccsSent;
			m.resetAudioTime();
			const auto start = m.now();
			int n = 0;
			while(double(m.now() - start) * 1000.0 / g_rate < _ms)
			{
				const double t = double(m.now() - start) * 1000.0 / g_rate;
				if(_edit && t >= n * _everyMs)
					_edit(n++);
				r.run(10);
			}
			if(_edit)
				r.run(3000);	// what the edits leave: the last dump, its read-back
			const double secs = double(m.now() - start) / g_rate;
			const auto& v = m.blockMInstr;
			double mean = 0;
			for(const auto x : v)
				mean += x;
			mean /= double(std::max<size_t>(1, v.size()));
			if(!_edit)
				idleMean = mean;
			const double base = idleMean > 0 ? idleMean : mean;
			size_t h2 = 0, h4 = 0, hot = 0;
			float worst4 = 0;
			for(size_t i = 0; i + 2 <= v.size(); i += 2)
				h2 += v[i] + v[i + 1] > 50;
			for(size_t i = 0; i + 4 <= v.size(); i += 4)
			{
				const float w = v[i] + v[i + 1] + v[i + 2] + v[i + 3];
				h4 += w > 100;
				worst4 = std::max(worst4, w);
			}
			for(size_t i = 0; i + 5 <= v.size(); ++i)
				hot += (v[i] + v[i + 1] + v[i + 2] + v[i + 3] + v[i + 4]) / 5 > 1.3 * base;
			const double hotMs = double(hot) * g_block * 1000.0 / g_rate / secs;
			std::printf("  %-12s %4d | %4zu %5zu %6.0f | %5.1f %5.1f | %5.0f | %5.1f\n", _name, n, r.dumpsSent - d0, r.ccsSent - c0, double(r.bytesSent - b0) / secs,
				double(h2) / secs, double(h4) / secs, hotMs, worst4);
			if(budget >= 0 && _edit)
				check(hotMs <= budget, std::string(_name) + ": hot time within the budget (" + std::to_string(int(hotMs)) + " ms/s, budget " + std::to_string(int(budget)) + ")");
		};
		const auto noteRow = [&](const int _t)
		{
			std::string st;
			for(int s = 0; s < 16; ++s)
				if(rnd(3) == 0)
					st += std::string(st.empty() ? "" : ",") + "[" + std::to_string(s) + ",{\"n\":[" + std::to_string(48 + rnd(24)) + "],\"a\":1,\"f\":1,\"l\":1}]";
			return "{\"t\":" + std::to_string(_t) + ",\"steps\":[" + st + "]}";
		};
		const auto params = [&](const int _tracks, const int _g)
		{
			std::string vals;
			for(int t = 0; t < _tracks; ++t)
				for(int pg = 0; pg < 6; ++pg)
					for(int i = 0; i < 8; ++i)
						if(rnd(3))
							vals += std::string(vals.empty() ? "" : ",") + "[" + std::to_string(t) + "," + std::to_string(pg) + "," + std::to_string(i) + "," + std::to_string(rnd(128)) + "]";
			return "{\"op\":\"params\",\"k\":" + k + ",\"values\":[" + vals + "],\"g\":" + std::to_string(_g) + "}";
		};
		action("idle", 6000, 0, {});
		{ const int g = ++gesture; action("lockDrag", 6000, 16, [&](const int _n) { r.msg("{\"op\":\"lock\",\"g\":" + std::to_string(g) + ",\"p\":" + ps + ",\"t\":0,\"page\":2,\"i\":1,\"s\":" + std::to_string((_n % 4) * 4) + ",\"v\":" + std::to_string((_n * 7) % 128) + "}"); }); }
		action("genR", 6000, 400, [&](const int _n) { r.msg("{\"op\":\"steps\",\"g\":" + std::to_string(++gesture) + ",\"p\":" + ps + ",\"from\":0,\"to\":16,\"rows\":[" + noteRow(_n % 6) + "]}"); });
		action("genAllR", 6000, 800, [&](const int) { std::string rows; for(int t = 0; t < 6; ++t) rows += (t ? "," : "") + noteRow(t); r.msg("{\"op\":\"steps\",\"g\":" + std::to_string(++gesture) + ",\"p\":" + ps + ",\"from\":0,\"to\":16,\"rows\":[" + rows + "]}"); });
		{ const int g = ++gesture; action("pianoPaint", 6000, 50, [&](const int _n) { r.msg("{\"op\":\"step\",\"g\":" + std::to_string(g) + ",\"p\":" + ps + ",\"t\":1,\"s\":" + std::to_string(_n % 16) + ",\"v\":{\"n\":[" + std::to_string(48 + _n % 24) + "],\"a\":1,\"f\":1,\"l\":1}}"); }); }
		{ const int g = ++gesture; action("mutR", 6000, 400, [&](const int) { r.msg(params(1, g)); }); }
		{ const int g = ++gesture; action("mutAllR", 6000, 800, [&](const int) { r.msg(params(6, g)); }); }
		{ const int g = ++gesture; action("paramDrag", 6000, 16, [&](const int _n) { r.msg("{\"op\":\"param\",\"g\":" + std::to_string(g) + ",\"k\":" + k + ",\"t\":2,\"page\":1,\"i\":3,\"v\":" + std::to_string(_n % 128) + "}"); }); }
		{ const int g = ++gesture; action("levelDrag", 6000, 16, [&](const int _n) { r.msg("{\"op\":\"level\",\"g\":" + std::to_string(g) + ",\"k\":" + k + ",\"t\":3,\"v\":" + std::to_string(_n % 128) + "}"); }); }
		action("machine", 6000, 500, [&](const int _n) { r.msg("{\"op\":\"machine\",\"g\":" + std::to_string(++gesture) + ",\"k\":" + k + ",\"t\":4,\"model\":" + std::to_string(1 + _n % 4) + ",\"keepFx\":true}"); });
		action("muteBurst", 6000, 30, [&](const int _n) { r.msg("{\"op\":\"mute\",\"t\":" + std::to_string(_n % 6) + ",\"on\":" + ((_n / 6) % 2 ? "false" : "true") + "}"); });
		action("copyPaste", 6000, 500, [&](const int _n) { r.msg("{\"op\":\"copySteps\",\"p\":" + ps + ",\"t\":" + std::to_string(_n % 3) + ",\"from\":0,\"to\":16}"); r.msg("{\"op\":\"pasteSteps\",\"g\":" + std::to_string(++gesture) + ",\"p\":" + ps + ",\"t\":" + std::to_string(3 + _n % 3) + ",\"from\":0}"); });
		action("clearUndo", 6000, 700, [&](const int _n) { if(_n % 2 == 0) r.msgConfirmed("{\"op\":\"clearPattern\",\"g\":" + std::to_string(++gesture) + ",\"p\":" + ps + "}"); else r.msg(R"({"op":"undo"})"); });
		{ const int g = ++gesture; action("lengthDrag", 6000, 50, [&](const int _n) { r.msg("{\"op\":\"length\",\"g\":" + std::to_string(g) + ",\"p\":" + ps + ",\"v\":" + std::to_string(16 + _n % 48) + "}"); }); }
		r.msg("{\"op\":\"length\",\"p\":" + ps + ",\"v\":16}");
		{ const int g = ++gesture; action("transpose", 6000, 50, [&](const int _n) { r.msg("{\"op\":\"transpose\",\"g\":" + std::to_string(g) + ",\"p\":" + ps + ",\"v\":" + std::to_string(_n % 24 - 12) + "}"); }); }
		{ const int g = ++gesture; action("tempoDrag", 6000, 33, [&](const int _n) { r.msg("{\"op\":\"tempo\",\"g\":" + std::to_string(g) + ",\"bpm\":" + std::to_string(100 + _n % 60) + "}"); }); }
		r.msg(R"({"op":"tempo","bpm":120})");
		action("keyboard", 6000, 125, [&](const int _n) { if(_n % 2 == 0) r.msg("{\"op\":\"noteOn\",\"t\":" + std::to_string(_n % 6) + ",\"vel\":100,\"pitch\":0}"); else r.msg("{\"op\":\"noteOff\",\"t\":" + std::to_string((_n - 1) % 6) + ",\"pitch\":0}"); });
		action("undoRedo", 6000, 500, [&](const int _n) { r.msg(_n % 2 ? R"({"op":"redo"})" : R"({"op":"undo"})"); });
		action("select", 8000, 1000, [&](const int _n) { r.msgConfirmed("{\"op\":\"select\",\"p\":" + std::to_string(_n % 2 ? p : (p + 1) % 128) + ",\"now\":true}"); });
		r.msgConfirmed("{\"op\":\"select\",\"p\":" + ps + ",\"now\":true}");
		action("loadKit", 8000, 1000, [&](const int _n) { r.msgConfirmed("{\"op\":\"loadKit\",\"k\":" + std::to_string(_n % 2 ? std::stoi(k) : (std::stoi(k) + 1) % 64) + ",\"force\":true}"); });
		action("after", 4000, 0, {});
		r.msg(R"({"op":"stop"})");
		r.run(300);
	}
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 2)
	{
		std::puts("usage: mmDeskFirmwareTest <MM-ROM>");
		return 77;
	}
	try
	{
		const auto rom = load(_argv[1]);
		require(rom.size() == md::g_romSize, "ROM must be the 8 MiB MM 1.32B image");
		const std::string only = _argc > 2 ? _argv[2] : "";
		if(only == "actions")
		{
			actions(rom);
			std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
			return g_failures ? 1 : 0;
		}
		if(only.empty() || only == "smoke")
			smoke(rom);
		if(only.empty() || only == "trigkinds")
			trigKinds(rom);
		if(only == "syximport")
			syxImport(rom);
		if(only == "syxexport")
			syxExport(rom);
		if(only.empty() || only == "hostclock")
			hostClock(rom);
		if(only.empty() || only == "patterns")
			patterns(rom);
		if(only.empty() || only == "library")
			library(rom);
		if(only.empty() || only == "p4")
			p4(rom);
		if(only.empty() || only == "hw")
			hwLink(rom);
		if(only.empty() || only == "chain")
			chains(rom);
		if(only.empty() || only == "record")
			recording(rom);
		if(only.empty() || only == "parked")
			parkedKeys(rom);
		check(g_contract.loaded() && g_contract.bad() == 0, g_contract.summary());
		std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
		return g_failures ? 1 : 0;
	}
	catch(const std::exception& _e)
	{
		std::fprintf(stderr, "mmDeskFirmwareTest FAIL: %s\n", _e.what());
		return 1;
	}
}
