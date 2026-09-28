#pragma once

#include <optional>

namespace deskCore
{
	// Latest-wins delivery for one document with one push in flight: while the
	// firmware has not answered, newer edits only replace what goes next. A
	// knob-speed drag therefore costs one dump per read-back, not one per event.
	// T is the document value (MD) or the dump's bytes (MM).
	template<typename T>
	class PushSlot
	{
	public:
		enum class ReadBack
		{
			NotWaiting,			// nothing in flight: an ordinary refresh
			Confirmed,			// the firmware holds what we sent, nothing queued
			ConfirmedSendNext,	// confirmed; a newer value waits: send inFlight()
			Other				// something else (an older reply): keep waiting
		};

		// True: send _value now. False: it waits for the in-flight read-back.
		bool want(const T& _value)
		{
			if(!m_inFlight)
			{
				m_inFlight = _value;
				return true;
			}
			m_next = _value;
			return false;
		}

		ReadBack onReadBack(const T& _firmware)
		{
			if(!m_inFlight)
				return ReadBack::NotWaiting;
			if(!(_firmware == *m_inFlight))
				return ReadBack::Other;
			m_inFlight.reset();
			if(!m_next)
				return ReadBack::Confirmed;
			m_inFlight = m_next;
			m_next.reset();
			return ReadBack::ConfirmedSendNext;
		}

		// The value to send after ConfirmedSendNext (it is in flight now).
		const std::optional<T>& inFlight() const { return m_inFlight; }
		const std::optional<T>& next() const { return m_next; }
		bool busy() const { return m_inFlight.has_value(); }

		// The read-back never came: forget both; the caller reports the failure.
		void abandon()
		{
			m_inFlight.reset();
			m_next.reset();
		}

	private:
		std::optional<T> m_inFlight;
		std::optional<T> m_next;
	};
}
