#pragma once

#include "mdDeskEdit.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace mdDesk
{
	// Undo/redo over documents. A step is the list of changes one command (or
	// one drag gesture) made; undo hands back the "before" documents and the
	// caller delivers them like any other edit. Pure values, no clocks.
	class History
	{
	public:
		explicit History(size_t _limit = 200) : m_limit(_limit) {}

		// _gesture != 0 merges into the previous step when that step came from the
		// same gesture: a drag is one undo step. The merged step keeps the first
		// "before" of every document and the latest "after".
		void record(const std::vector<Change>& _changes, uint64_t _gesture);

		// The changes to apply, as before -> after pairs in the undo direction.
		std::optional<std::vector<Change>> undo();
		std::optional<std::vector<Change>> redo();

		bool canUndo() const { return !m_undo.empty(); }
		bool canRedo() const { return !m_redo.empty(); }
		size_t size() const { return m_undo.size(); }
		size_t redoSize() const { return m_redo.size(); }
		void clear();

	private:
		struct Step
		{
			std::vector<Change> changes;
			uint64_t gesture = 0;
		};

		static std::vector<Change> inverted(const std::vector<Change>& _changes);

		size_t m_limit;
		std::deque<Step> m_undo;
		std::deque<Step> m_redo;
	};

	// Latest-wins delivery for one document with one push in flight: while the
	// firmware has not answered, newer edits only replace what goes next. A
	// knob-speed drag therefore costs one dump per read-back, not one per event.
	template<typename T>
	class PushSlot
	{
	public:
		enum class ReadBack
		{
			NotWaiting,		// nothing in flight: an ordinary refresh
			Confirmed,		// the firmware holds what we sent, nothing queued
			ConfirmedSendNext,	// confirmed; a newer value waits: send next()
			Other			// something else (an older reply): keep waiting
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
