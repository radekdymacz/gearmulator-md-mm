#pragma once

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace deskCore
{
	// A panel sequence as data (P6): each step's action runs, then the runner waits for an
	// observed fact before the next step, or for the step's timeout. It replaces the fixed
	// delays (stop, 150 ms, load, 300 ms, play) with what the machine reports. MM's timed
	// key lists are the same idea one level down.
	enum class Wait : uint8_t
	{
		None,			// go on at once
		Stopped,		// telemetry: the sequencer stopped
		Playing,		// telemetry: it plays
		StatusReply,	// a status reply came after the action (MIDI is taken in order)
		PatternIs		// the machine reports pattern `waitArg` as current
	};

	struct SeqFacts
	{
		bool playing = false;
		uint32_t statusReplies = 0;		// a counter: every status reply adds one
		int pattern = -1;
	};

	template<typename Action>
	struct SeqStep
	{
		Action action{};
		int arg = 0;
		Wait until = Wait::None;
		int waitArg = 0;
		double timeoutMs = 1000;
	};

	template<typename Action>
	class Sequencer
	{
	public:
		struct Due
		{
			Action action{};
			int arg = 0;
			bool afterTimeout = false;	// the previous step's fact never came
		};

		// Starts _steps; a sequence still running is dropped.
		void start(std::vector<SeqStep<Action>> _steps)
		{
			m_steps.assign(_steps.begin(), _steps.end());
			m_waiting = false;
		}

		// The actions due by _nowMs with these facts, in order. Call it every tick.
		std::vector<Due> due(const double _nowMs, const SeqFacts& _facts)
		{
			std::vector<Due> out;
			bool timedOut = false;
			while(!m_steps.empty())
			{
				if(m_waiting)
				{
					const auto& s = m_steps.front();
					const bool holds = s.until == Wait::None
						|| (s.until == Wait::Stopped && !_facts.playing)
						|| (s.until == Wait::Playing && _facts.playing)
						|| (s.until == Wait::StatusReply && _facts.statusReplies != m_repliesAtAction)
						|| (s.until == Wait::PatternIs && _facts.pattern == s.waitArg);
					timedOut = !holds && _nowMs - m_actionMs >= s.timeoutMs;
					if(!holds && !timedOut)
						break;
					m_steps.pop_front();
					m_waiting = false;
					continue;
				}
				const auto& s = m_steps.front();
				out.push_back({s.action, s.arg, timedOut});
				timedOut = false;
				m_waiting = true;
				m_actionMs = _nowMs;
				m_repliesAtAction = _facts.statusReplies;
			}
			return out;
		}

		bool running() const { return !m_steps.empty(); }
		void clear() { m_steps.clear(); m_waiting = false; }

	private:
		std::deque<SeqStep<Action>> m_steps;
		bool m_waiting = false;
		double m_actionMs = 0;
		uint32_t m_repliesAtAction = 0;
	};
}
