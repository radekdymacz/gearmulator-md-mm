#pragma once

#include "elektronData/mmScreen.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace mmDesk
{
	// Front-panel keys the desk presses (the edge maps them to md::PanelControl).
	enum class Key : uint8_t
	{
		Exit,
		Enter,
		Up,
		Down,
		Left,
		Right,
		Global,		// FUNCTION + KIT/SONG: the GLOBAL menu
		Play,
		Stop
	};

	using Screen = elektronData::MmScreen;

	// The machine as the desk sees it, from the audio thread (md::MmTelemetry).
	struct Telemetry
	{
		bool valid = false;
		int step = -1;
		bool running = false;
		Screen screen = Screen::Unknown;
		uint32_t recvCount = 0;
		uint32_t recvErrors = 0;
		bool recvActive = false;	// SYSEX RECV takes dumps (RAM 0x26a3c3)
		int tempo = 0;				// BPM x 24 (RAM 0x2bc2a6), 0 = unknown
	};

	// On SYSEX RECV and taking dumps: the screen word says GLOBAL EDIT and the
	// receive flag is set (MM-P2-RESULT §2).
	inline bool onSysexRecv(const Telemetry& _t) { return _t.screen == Screen::GlobalEdit && _t.recvActive; }

	// The SYSEX RECV session. The Monomachine takes a dump only on GLOBAL > FILE >
	// SYSEX RECV. The session drives the panel there when there is something to
	// send, sends while parked (the machine keeps playing, MM-P0 §3), and leaves
	// after an idle time. Pure: feed it the time and the screen word, it returns
	// the keys to press and the dumps to send.
	class RecvSession
	{
	public:
		enum class State
		{
			Idle,		// not on SYSEX RECV, nothing to send
			ToMain,		// EXIT until the main screen, then the macro
			Entering,	// the macro runs
			Parked,		// on SYSEX RECV: dumps go out
			Leaving,	// EXIT back to the main screen
			Failed		// the screen never came; retried after a pause
		};

		// A dump to send and whose it is (the caller's tag: which push it answers).
		struct Send
		{
			std::vector<uint8_t> bytes;
			uint32_t tag = 0;
		};

		struct Out
		{
			std::vector<Key> keys;
			std::vector<Send> sends;
		};

		// The path: GLOBAL, ENTER, cursor to a known place, FILE, SYSEX RECV, ORIG.
		static std::vector<Key> enterMacro();
		static std::vector<Key> exitKeys();

		void want(std::vector<uint8_t> _dump, const uint32_t _tag = 0) { m_queue.push_back({std::move(_dump), _tag}); }
		// Keeps the session parked a while longer (an edit is coming).
		void touch(const double _now) { m_lastActivity = _now; }
		Out tick(double _now, const Telemetry& _t);

		State state() const { return m_state; }
		const char* stateName() const;
		size_t queued() const { return m_queue.size(); }
		bool parked() const { return m_state == State::Parked; }

		double idleMs = 3000;		// parked and quiet this long -> leave
		double timeoutMs = 3000;	// a screen that does not come -> retry

	private:
		void go(State _s, double _now) { m_state = _s; m_since = _now; }

		std::deque<Send> m_queue;
		State m_state = State::Idle;
		double m_since = 0;
		double m_lastActivity = 0;
		int m_attempts = 0;
		double m_keysDone = 0;	// the keys pressed last are through by then
	};
}
