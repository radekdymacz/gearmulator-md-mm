#pragma once

#include "mmDeskTelemetry.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace mmDesk
{
	// On SYSEX RECV and taking dumps: the screen word says GLOBAL EDIT and the
	// receive flag is set (MM-P2-RESULT §2).
	inline bool onSysexRecv(const Telemetry& _t) { return _t.screen == Screen::GlobalEdit && _t.recvActive; }

	// The SYSEX RECV session. The Monomachine takes a dump only on GLOBAL > FILE >
	// SYSEX RECV. The session drives the panel there when there is something to
	// send, sends while parked (the machine keeps playing, MM-P0 §3), and leaves
	// after an idle time. A screen that never comes is retried after a pause, maxFailures
	// times; then (or once the oldest dump waited maxWaitMs, telemetry or not) the session
	// gives up what it holds and says whose it was (Out::gaveUp), so the caller can fail
	// those pushes and the panel is left alone. Its times are the machine's: they move only
	// while the emulator runs (Telemetry::blocks moves), so a host that stops processing
	// (a DAW at rest) fails nothing that would go out on resume. Pure: feed it the time and
	// the telemetry, it returns the keys to press and the dumps to send.
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
			Failed		// the screen never came; retried after a pause (or given up: see maxFailures)
		};

		// A dump to send and whose it is (the caller's tag: which push it answers).
		struct Send
		{
			std::vector<uint8_t> bytes;
			uint32_t tag = 0;
			double queuedMs = -1;	// machine time it was first seen in the queue (-1: not yet)
		};

		struct Out
		{
			std::vector<Key> keys;
			std::vector<Send> sends;
			std::vector<uint32_t> gaveUp;	// the tags of the dumps given up (never sent): the screen never came
		};

		// The path: GLOBAL, ENTER, cursor to a known place, FILE, SYSEX RECV, ORIG.
		static std::vector<Key> enterMacro();
		static std::vector<Key> exitKeys();

		void want(std::vector<uint8_t> _dump, const uint32_t _tag = 0) { m_queue.push_back({std::move(_dump), _tag, -1}); }
		// Keeps the session parked a while longer (an edit is coming).
		void touch() { m_lastActivity = m_clock; }
		Out tick(double _now, const Telemetry& _t);

		State state() const { return m_state; }
		const char* stateName() const;
		size_t queued() const { return m_queue.size(); }
		bool parked() const { return m_state == State::Parked; }

		double idleMs = 3000;		// parked and quiet this long -> leave
		double timeoutMs = 3000;	// a screen that does not come -> retry
		int maxFailures = 2;		// failed tries (each three attempts, then a pause) before the queue is given up
		double maxWaitMs = 45000;	// a dump queued this long and not sent is given up, whatever the screen says

	private:
		void go(State _s, double _now) { m_state = _s; m_since = _now; }
		void failed(Out& _out, double _now);
		void giveUp(Out& _out, double _now);

		std::deque<Send> m_queue;
		State m_state = State::Idle;
		double m_since = 0;
		double m_lastActivity = 0;
		int m_attempts = 0;
		int m_failures = 0;			// failed tries since the screen last came
		double m_clock = 0;			// machine time: moves with the caller's clock only while the emulator runs
		double m_lastNowMs = -1;
		uint64_t m_lastBlocks = 0;
		double m_keysDone = 0;	// the keys pressed last are through by then
	};
}
