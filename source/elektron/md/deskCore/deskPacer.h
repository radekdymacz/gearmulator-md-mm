#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace deskCore
{
	// MIDI at DIN speed (P4, HW MIDI): 31250 baud, 10 bits a byte, so 3125 bytes a second;
	// a 5410-byte pattern dump takes 1.73 s on the wire. The pacer lets messages out no
	// faster than that, in order, so the desk never floods a real interface and its timing
	// (TX, timeouts) matches the wire. Pure: the caller gives the time.
	class DinPacer
	{
	public:
		using Bytes = std::vector<uint8_t>;
		static constexpr double g_bytesPerSecond = 3125.0;

		static double wireMs(const size_t _bytes) { return static_cast<double>(_bytes) * 1000.0 / g_bytesPerSecond; }

		void push(Bytes _message) { m_queue.push_back(std::move(_message)); }

		// The messages whose turn has come by _nowMs (each starts when the one before has
		// finished on the wire).
		std::vector<Bytes> take(const double _nowMs)
		{
			std::vector<Bytes> out;
			if(m_freeAtMs < _nowMs)
				m_freeAtMs = _nowMs;
			while(!m_queue.empty() && m_freeAtMs <= _nowMs + 0.0001)
			{
				m_freeAtMs += wireMs(m_queue.front().size());
				out.push_back(std::move(m_queue.front()));
				m_queue.pop_front();
			}
			return out;
		}

		bool idle(const double _nowMs) const { return m_queue.empty() && m_freeAtMs <= _nowMs; }
		size_t queuedBytes() const
		{
			size_t n = 0;
			for(const auto& m : m_queue)
				n += m.size();
			return n;
		}
		// When the wire is free again.
		double freeAtMs() const { return m_freeAtMs; }

	private:
		std::deque<Bytes> m_queue;
		double m_freeAtMs = 0;
	};
}
