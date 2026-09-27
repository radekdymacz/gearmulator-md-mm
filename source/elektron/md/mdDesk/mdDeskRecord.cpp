#include "mdDeskRecord.h"

#include <algorithm>

namespace mdDesk
{
	void KnobRecorder::want(const uint8_t _track, const uint8_t _index, const uint8_t _value)
	{
		if(_track >= elektronData::MdKit::g_tracks || _index >= elektronData::MdKit::g_paramsPerTrack)
			return;
		m_targets[{_track, _index}] = _value;
	}

	void KnobRecorder::reset()
	{
		m_targets.clear();
		m_selected.reset();
		m_lastMs = m_lastPageMs = -1e9;
	}

	std::optional<KnobStep> KnobRecorder::next(const double _nowMs, const int _knobPage,
		const elektronData::MdKit* _memory)
	{
		if(!_memory)
			return {};
		// Drop what already arrived.
		for(auto it = m_targets.begin(); it != m_targets.end();)
		{
			if(_memory->params[it->first.first][it->first.second] == it->second)
				it = m_targets.erase(it);
			else
				++it;
		}
		if(m_targets.empty())
			return {};
		// Finish the selected track first: selecting another one costs a round trip.
		auto target = m_targets.begin();
		if(m_selected)
		{
			const auto same = std::find_if(m_targets.begin(), m_targets.end(),
				[&](const auto& _t) { return _t.first.first == *m_selected; });
			if(same != m_targets.end())
				target = same;
		}
		const auto [track, index] = target->first;
		if(m_selected != track)
		{
			if(_nowMs - m_lastMs < g_selectGapMs)
				return {};
			m_selected = track;
			m_lastMs = _nowMs;
			return KnobStep{KnobStep::Kind::SelectTrack, track, 0, 0};
		}
		if(_nowMs - m_lastMs < g_selectGapMs)
			return {};
		const int page = index / 8;
		if(_knobPage != page)
		{
			if(_nowMs - m_lastPageMs < g_pageGapMs)
				return {};
			m_lastPageMs = m_lastMs = _nowMs;
			return KnobStep{KnobStep::Kind::PageKey, track, 0, 0};
		}
		if(_nowMs - m_lastMs < g_turnGapMs)
			return {};
		const int steps = std::clamp(int(target->second) - int(_memory->params[track][index]), -g_maxStepsPerTurn,
			g_maxStepsPerTurn);
		m_lastMs = _nowMs;
		return KnobStep{KnobStep::Kind::Turn, track, static_cast<uint8_t>(index % 8), steps};
	}
}
