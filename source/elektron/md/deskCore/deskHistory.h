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
	// gesture) made; undo hands back the inverted changes and the caller delivers them
	// like any other edit. Change needs `before`, `after` and `ref()`; inverting swaps
	// before and after and keeps every other member. Pure values, no clocks.
	template<typename Change>
	class History
	{
	public:
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
			m_undo.push_back({_changes, _gesture});
			while(m_undo.size() > m_limit)
				m_undo.pop_front();
		}

		// The changes to apply, as before -> after pairs in the undo direction.
		std::optional<std::vector<Change>> undo()
		{
			if(m_undo.empty())
				return {};
			auto step = std::move(m_undo.back());
			m_undo.pop_back();
			auto changes = inverted(step.changes);
			m_redo.push_back(std::move(step));
			return changes;
		}

		std::optional<std::vector<Change>> redo()
		{
			if(m_redo.empty())
				return {};
			auto step = std::move(m_redo.back());
			m_redo.pop_back();
			auto changes = step.changes;
			step.gesture = 0;	// a redone step never merges with a new gesture
			m_undo.push_back(std::move(step));
			return changes;
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
