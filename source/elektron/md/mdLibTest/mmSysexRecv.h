#pragma once

// The Monomachine's SYSEX RECV, driven headless the way the Monomachine Editor's adapter drives the emulator
// (mmDesk::RecvSession): the same key macro (GLOBAL, ENTER, ... FILE, SYSEX RECV, ORIG) and exit keys, the same
// screen test (the screen word says GLOBAL EDIT and the receive flag is set), the dumps back to back while parked,
// EXIT to leave. The MM OS 1.32B takes user-data dumps only on that screen (MM-P0 §3); requests and the short
// commands (0x56, 0x57, 0x6C, status) go on any screen. Test-only, deterministic: everything runs on machine time.

#include "mdFirmwareSession.h"

#include "mdLib/mmtelemetry.h"
#include "mdLib/mdpanel.h"

#include "mmDesk/mmRecv.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mmSysexRecv
{
	using mdFirmwareSession::Bytes;
	using mdFirmwareSession::Machine;
	using mdFirmwareSession::require;

	inline md::PanelControl control(const mmDesk::Key _key)
	{
		using K = mmDesk::Key;
		using C = md::PanelControl;
		switch(_key)
		{
		case K::Exit: return C::Exit;
		case K::Enter: return C::Enter;
		case K::Up: return C::Up;
		case K::Down: return C::Down;
		case K::Left: return C::Left;
		case K::Right: return C::Right;
		case K::Global: return C::Kit;	// with FUNCTION held
		default: break;
		}
		throw std::runtime_error("a key the SYSEX RECV macros do not use");
	}

	// Each key held 10 ms and released for 10 ms, a chord as FUNCTION down, key down, key up, FUNCTION up: the
	// edge's timing for the desk's keys (mmDeskFirmwareTest Rig::userKeys, the plug-in's panel queue).
	inline void pressKeys(Machine& _m, const std::vector<mmDesk::Key>& _keys)
	{
		constexpr double holdMs = 10;
		const auto model = md::MachineModel::Monomachine;
		const auto function = *md::panelPacket(model, md::PanelControl::Function);
		for(const auto key : _keys)
		{
			const auto packet = md::panelPacket(model, control(key));
			require(packet.has_value(), "no MM panel packet for a SYSEX RECV key");
			if(mmDesk::isChord(key))
			{
				md::PanelRowState rows;
				const md::PanelPacket states[] = {rows.press(function), rows.press(*packet), rows.release(*packet),
					rows.release(function)};
				for(const auto state : states)
				{
					_m.hardware().trySendPanelEvent(state.row, state.mask);
					_m.run(holdMs);
				}
				continue;
			}
			_m.hardware().trySendPanelEvent(packet->row, packet->mask);
			_m.run(holdMs);
			_m.hardware().trySendPanelEvent(packet->row, 0);
			_m.run(holdMs);
		}
	}

	inline uint32_t read32(Machine& _m, const uint32_t _address)
	{
		return (static_cast<uint32_t>(_m.read8(_address)) << 24) | (static_cast<uint32_t>(_m.read8(_address + 1)) << 16)
			| (static_cast<uint32_t>(_m.read8(_address + 2)) << 8) | _m.read8(_address + 3);
	}

	inline md::MmScreen screen(Machine& _m)
	{
		return md::MmTelemetry::screenOf(read32(_m, md::MmTelemetry::g_screenAddress));
	}

	// mmDesk::onSysexRecv on the machine's memory.
	inline bool onRecv(Machine& _m)
	{
		return screen(_m) == md::MmScreen::GlobalEdit && _m.read8(md::MmTelemetry::g_recvActiveAddress) == 1;
	}

	// Messages the machine has taken on SYSEX RECV, good and bad.
	inline uint32_t taken(Machine& _m)
	{
		return read32(_m, md::MmTelemetry::g_recvCountAddress) + read32(_m, md::MmTelemetry::g_recvErrorAddress);
	}

	inline uint32_t errors(Machine& _m)
	{
		return read32(_m, md::MmTelemetry::g_recvErrorAddress);
	}

	// Runs until _done or _ms of machine time have passed; whether _done came.
	template<typename Done>
	bool runUntil(Machine& _m, const double _ms, Done _done)
	{
		for(double t = 0; t < _ms; t += 10)
		{
			if(_done())
				return true;
			_m.run(10);
		}
		return _done();
	}

	// To SYSEX RECV as the adapter goes (RecvSession::tick): from the main screen the macro, from anywhere else EXIT
	// first; the screen must come within 3 s of the keys, three tries.
	inline void enter(Machine& _m)
	{
		for(int attempt = 0; attempt < 3; ++attempt)
		{
			if(onRecv(_m))
				return;
			if(screen(_m) != md::MmScreen::Main)
			{
				pressKeys(_m, mmDesk::RecvSession::exitKeys());
				runUntil(_m, 3000, [&] { return screen(_m) == md::MmScreen::Main; });
			}
			pressKeys(_m, mmDesk::RecvSession::enterMacro());
			if(runUntil(_m, 3000, [&] { return onRecv(_m); }))
				return;
		}
		throw std::runtime_error("the Monomachine did not reach SYSEX RECV");
	}

	inline void leave(Machine& _m)
	{
		pressKeys(_m, mmDesk::RecvSession::exitKeys());
		require(runUntil(_m, 3000, [&] { return screen(_m) == md::MmScreen::Main; }),
			"the Monomachine did not return to its main screen after SYSEX RECV");
	}

	struct Result
	{
		uint32_t taken = 0;
		uint32_t errors = 0;
	};

	// The dumps on SYSEX RECV, back to back at the pace the emulated UART reads them (125 KB/s, as the editor's
	// stream sends dumps to a machine that stands), then until the machine has taken every one (its count of
	// messages and errors) or _settleMs passed after the last; then EXIT. What it took, and how many were bad.
	inline Result send(Machine& _m, const std::vector<Bytes>& _dumps, const double _settleMs = 5000)
	{
		enter(_m);
		const auto base = taken(_m);
		const auto errorBase = errors(_m);
		for(const auto& dump : _dumps)
			_m.send(dump);
		runUntil(_m, _settleMs, [&] { return taken(_m) - base >= _dumps.size(); });
		Result r;
		r.taken = taken(_m) - base;
		r.errors = errors(_m) - errorBase;
		// the adapter leaves after an idle time: the last dump is stored by then
		_m.run(200);
		leave(_m);
		return r;
	}
}
