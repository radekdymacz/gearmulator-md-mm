#include "mdDeskHistory.h"

namespace mdDesk
{
	void History::record(const std::vector<Change>& _changes, const uint64_t _gesture)
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

	std::vector<Change> History::inverted(const std::vector<Change>& _changes)
	{
		std::vector<Change> out;
		for(auto it = _changes.rbegin(); it != _changes.rend(); ++it)
			out.push_back({it->after, it->before, it->slotWrite});
		return out;
	}

	std::optional<std::vector<Change>> History::undo()
	{
		if(m_undo.empty())
			return {};
		auto step = std::move(m_undo.back());
		m_undo.pop_back();
		auto changes = inverted(step.changes);
		m_redo.push_back(std::move(step));
		return changes;
	}

	std::optional<std::vector<Change>> History::redo()
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

	void History::clear()
	{
		m_undo.clear();
		m_redo.clear();
	}
}
