#pragma once

// Private to the MmMachine sources (mmDeskMachine.cpp, mmDeskDelivery.cpp, mmDeskCommands.cpp, mmDeskChain.cpp,
// mmDeskNotes.cpp, mmDeskRecord.cpp, mmDeskLoad.cpp): their shared constants and small helpers. Not part of
// mmDesk's interface.

#include "mmDeskMachine.h"

#include "elektronData/mmJson.h"

#include "deskCore/deskKinds.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>

namespace mmDesk::parts
{
	namespace ed = elektronData;
	using Value = ed::json::Value;
	using deskCore::Outcome;

	constexpr double g_readBackTimeoutMs = 8000;
	constexpr double g_wireReadBackTimeoutMs = 15000;	// DIN speed, behind the library's background read
	constexpr double g_recordReadMs = 1000;				// while recording: the current pattern read back this often
	constexpr deskCore::LoadQueue<Ref>::Policy g_loadPolicy{25, 2};
	constexpr double g_loadTimeoutMs = 400;
	constexpr double g_telemetryMinMs = 25;				// the playhead message at most this often
	// A panel key state is held 10 ms (DevicePort::pressKeys); the keys are through after their states
	// and a little more (the telemetry shows what they did).
	constexpr double g_keyStateMs = 10;
	constexpr double g_keysMarginMs = 60;
	// The keyboard's pitch 0: MIDI note 48 (C3, the home row's A at octave 0).
	constexpr int g_noteC = 48;
	constexpr const char* g_noChains = "A chain is made with the Monomachine's keys (hold BANK, press the TRIG keys; manual 1-46), and over"
		" MIDI no message reaches them: Appendix C has no SysEx for chaining or for the keys. Chain on the machine's panel.";
	constexpr const char* g_panelBusy = "The panel is busy (SYSEX RECV); try again.";
	// 0.3.4: PLAY / STOP while the panel takes an edit wait for it (pumpTransport), this long at most
	constexpr const char* g_transportWaits = "The machine is taking an edit (SYSEX RECV): it follows in a moment.";
	constexpr double g_transportWaitMs = 10000;

	// The dump a request brings back, for timeouts at DIN speed (the kind's record).
	inline size_t replyBytes(const Kind _k)
	{
		const auto* s = deskCore::kindSpec<MmModel>(_k);
		return s ? s->replyBytes : 0;
	}

	inline int num(const Value& _m, const char* _key, const int _default = -1)
	{
		const auto* v = _m.find(_key);
		return v && v->isNumber() ? static_cast<int>(v->asNumber()) : _default;
	}

	inline bool flag(const Value& _m, const char* _key)
	{
		const auto* v = _m.find(_key);
		return v && v->isBool() && v->asBool();
	}

	inline Outcome ok(std::string _note = {}) { return {{}, std::move(_note), {}}; }
	inline Outcome refuse(std::string _error) { return {{std::move(_error)}, {}, {}}; }

	// A question the model declares (MmModel::asks()); the core sends the command again with force.
	inline Outcome ask(const char* _what, std::string _message, const char* _confirm)
	{
		assert(std::find(MmModel::asks().begin(), MmModel::asks().end(), _what) != MmModel::asks().end() && "a question the model does not declare");
		Outcome o;
		o.ask = deskCore::Ask{_what, std::move(_message), _confirm, Value::object()};
		return o;
	}

	inline std::string kitLabel(const Documents& _view, const int _slot)
	{
		char k[8];
		std::snprintf(k, sizeof(k), "K%03d", _slot + 1);
		const auto* w = _view.workingKitOf(_slot);
		const auto it = _view.kits.find(static_cast<uint8_t>(_slot));
		const auto* kit = w ? w : it != _view.kits.end() ? &it->second : nullptr;
		std::string name;
		if(kit)
			for(const auto c : kit->name)
				if(c)
					name += static_cast<char>(c);
		return std::string(k) + (name.empty() ? std::string() : " " + name);
	}

	inline const ed::MmGlobal* activeGlobal(const Documents& _view, const int _current)
	{
		const auto it = _current < 0 ? _view.globals.end() : _view.globals.find(static_cast<uint8_t>(_current & 7));
		return it == _view.globals.end() ? nullptr : &it->second;
	}
}
