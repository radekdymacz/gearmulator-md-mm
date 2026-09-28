#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace deskCore
{
	// Undo/redo over documents. A step is the list of changes one command (or one drag
	// gesture) made. Change needs `before`, `after` and `ref()`; inverting swaps before and after
	// and keeps every other member. Pure values, no clocks.
	//
	// Undo and redo are two steps (P6): next() says what to deliver (the caller refreshes each
	// change's `before` from what it shows now), done() records what was delivered. So a step
	// only ever holds changes that reached the machine.
	template<typename Change>
	class History
	{
	public:
		enum class Direction : uint8_t { Undo, Redo };

		explicit History(const size_t _limit = 200) : m_limit(_limit) {}

		// _gesture != 0 merges into the previous step when that step came from the
		// same gesture: a drag is one undo step. The merged step keeps the first
		// "before" of every document and the latest "after".
		void record(const std::vector<Change>& _changes, const uint64_t _gesture)
		{
			if(_changes.empty())
				return;
			m_redo.clear();
			if(_gesture && !m_undo.empty() && m_undo.back().gesture == _gesture)
			{
				auto& step = m_undo.back();
				for(const auto& c : _changes)
				{
					bool merged = false;
					for(auto& existing : step.changes)
					{
						if(existing.ref() == c.ref())
						{
							existing.after = c.after;
							merged = true;
							break;
						}
					}
					if(!merged)
						step.changes.push_back(c);
				}
				return;
			}
			push(m_undo, {_changes, _gesture});
		}

		// The changes to deliver for an undo (inverted, in reverse order) or a redo, or none.
		std::optional<std::vector<Change>> next(const Direction _d) const
		{
			const auto& from = _d == Direction::Undo ? m_undo : m_redo;
			if(from.empty())
				return {};
			return _d == Direction::Undo ? inverted(from.back().changes) : from.back().changes;
		}

		// The step next() gave is done: what of it was delivered (as delivered) moves to the other
		// list; what was not is dropped.
		void done(const Direction _d, const std::vector<Change>& _delivered)
		{
			auto& from = _d == Direction::Undo ? m_undo : m_redo;
			auto& to = _d == Direction::Undo ? m_redo : m_undo;
			if(from.empty())
				return;
			from.pop_back();
			if(_delivered.empty())
				return;
			// Undo delivered inverted changes: the redo step is their inversion again.
			push(to, {_d == Direction::Undo ? inverted(_delivered) : _delivered, 0});
		}

		bool canUndo() const { return !m_undo.empty(); }
		bool canRedo() const { return !m_redo.empty(); }
		size_t size() const { return m_undo.size(); }
		size_t redoSize() const { return m_redo.size(); }
		void clear()
		{
			m_undo.clear();
			m_redo.clear();
		}

	private:
		struct Step
		{
			std::vector<Change> changes;
			uint64_t gesture = 0;
		};

		void push(std::deque<Step>& _list, Step _step)
		{
			_list.push_back(std::move(_step));
			while(_list.size() > m_limit)
				_list.pop_front();
		}

		static std::vector<Change> inverted(const std::vector<Change>& _changes)
		{
			std::vector<Change> out;
			for(auto it = _changes.rbegin(); it != _changes.rend(); ++it)
			{
				auto c = *it;
				std::swap(c.before, c.after);
				out.push_back(std::move(c));
			}
			return out;
		}

		size_t m_limit;
		std::deque<Step> m_undo;
		std::deque<Step> m_redo;
	};
}
