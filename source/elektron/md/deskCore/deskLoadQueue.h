#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <optional>

namespace deskCore
{
	// Reading documents from a machine, one request in flight (P1: a burst of requests
	// costs step jitter, and a dump answer is large). Urgent reads go to the front; a
	// request without an answer is sent again (Policy::retries), then given up. Pure: the
	// caller gives the time and sends what next() returns.
	template<typename Ref>
	class LoadQueue
	{
	public:
		struct Policy
		{
			double gapMs = 30;		// between two requests
			int retries = 1;		// resends of one request before it is given up
		};

		// Read _ref. Already queued or in flight: urgent moves it to the front.
		void want(const Ref& _ref, const bool _urgent)
		{
			const auto it = std::find(m_queue.begin(), m_queue.end(), _ref);
			if(m_loading == _ref)
				return;
			if(it != m_queue.end())
			{
				if(!_urgent)
					return;
				m_queue.erase(it);
			}
			if(_urgent)
				m_queue.push_front(_ref);
			else
				m_queue.push_back(_ref);
		}

		bool contains(const Ref& _ref) const
		{
			return m_loading == _ref || std::find(m_queue.begin(), m_queue.end(), _ref) != m_queue.end();
		}

		// A document arrived: the request for it is answered.
		void arrived(const Ref& _ref)
		{
			if(m_loading == _ref)
				m_loading.reset();
		}

		// The request to send now, if any: the one in flight again after _timeoutMs without
		// an answer, or (when _mayStart and the gap has passed) the next queued one.
		std::optional<Ref> next(const double _nowMs, const double _timeoutMs, const bool _mayStart, const Policy& _policy)
		{
			if(m_loading)
			{
				if(_nowMs - m_sentMs < _timeoutMs)
					return {};
				if(m_retries++ < _policy.retries)
				{
					m_sentMs = m_lastRequestMs = _nowMs;
					return m_loading;
				}
				m_loading.reset();
			}
			if(m_queue.empty() || !_mayStart || _nowMs - m_lastRequestMs < _policy.gapMs)
				return {};
			m_loading = m_queue.front();
			m_queue.pop_front();
			m_retries = 0;
			m_sentMs = m_lastRequestMs = _nowMs;
			return m_loading;
		}

		const std::optional<Ref>& loading() const { return m_loading; }
		// Queued plus in flight.
		size_t pending() const { return m_queue.size() + (m_loading ? 1 : 0); }
		bool idle() const { return !pending(); }

	private:
		std::deque<Ref> m_queue;
		std::optional<Ref> m_loading;
		double m_sentMs = 0;
		double m_lastRequestMs = -1e9;
		int m_retries = 0;
	};
}
