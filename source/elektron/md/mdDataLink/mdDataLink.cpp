#include "mdDataLink.h"

#include "elektronData/mdValidate.h"

namespace mdDataLink
{
	namespace ed = elektronData;

	namespace
	{
		constexpr std::array<ed::MdStatus, 7> g_allStatus{ed::MdStatus::GlobalSlot, ed::MdStatus::Kit,
			ed::MdStatus::Pattern, ed::MdStatus::Song, ed::MdStatus::SequencerMode, ed::MdStatus::LockMode,
			ed::MdStatus::Track};
	}

	bool Session::State::operator==(const State& _o) const
	{
		return globalSlot == _o.globalSlot && kit == _o.kit && pattern == _o.pattern && song == _o.song
			&& track == _o.track && songMode == _o.songMode && extendedMode == _o.extendedMode
			&& queuedPattern == _o.queuedPattern && workingKit == _o.workingKit
			&& songReloadNeeded == _o.songReloadNeeded && patternKits == _o.patternKits;
	}

	Session::Session(Send _send) : m_send(std::move(_send))
	{
	}

	void Session::send(const Bytes& _bytes) const
	{
		if(m_send)
			m_send(_bytes);
	}

	void Session::changed(const State& _before) const
	{
		if(onState && _before != m_state)
			onState(m_state);
	}

	void Session::requestStatus()
	{
		for(const auto s : g_allStatus)
			send(ed::mdStatusRequest(s));
	}

	void Session::requestPattern(const uint8_t _slot)
	{
		send(ed::mdPatternRequest(_slot));
	}

	void Session::requestKit(const uint8_t _slot)
	{
		send(ed::mdKitRequest(_slot));
	}

	void Session::requestSong(const uint8_t _slot)
	{
		send(ed::mdSongRequest(_slot));
	}

	void Session::requestGlobal(const uint8_t _slot)
	{
		send(ed::mdGlobalRequest(_slot));
	}

	std::vector<std::string> Session::pushPattern(const ed::MdPattern& _pattern, const bool _askBack)
	{
		auto problems = ed::validate(_pattern);
		if(!problems.empty())
			return problems;
		send(ed::encodeMdPattern(_pattern));
		if(_askBack)
			requestPattern(_pattern.position);
		return {};
	}

	std::vector<std::string> Session::pushKit(const ed::MdKit& _kit, const KitApply _apply)
	{
		auto problems = ed::validate(_kit);
		if(!problems.empty())
			return problems;
		const auto before = m_state;
		send(ed::encodeMdKit(_kit));
		if(_apply == KitApply::StoreAndLoad)
		{
			send(ed::mdLoadKit(_kit.position));
			m_state.kit = _kit.position;
			m_state.workingKit = WorkingKit::Clean;
		}
		else if(m_state.kit == _kit.position)
		{
			// The playing kit now differs from its slot until it is loaded or saved.
			m_state.workingKit = WorkingKit::Edited;
		}
		requestKit(_kit.position);
		changed(before);
		return {};
	}

	std::vector<std::string> Session::pushSong(const ed::MdSong& _song, const bool _askBack)
	{
		auto problems = ed::validate(_song);
		if(!problems.empty())
			return problems;
		const auto before = m_state;
		send(ed::encodeMdSong(_song));
		if(m_state.song == _song.position)
			m_state.songReloadNeeded = true;
		if(_askBack)
			requestSong(_song.position);
		changed(before);
		return {};
	}

	std::vector<std::string> Session::pushGlobal(const ed::MdGlobal& _global)
	{
		auto problems = ed::validate(_global);
		if(!problems.empty())
			return problems;
		send(ed::encodeMdGlobal(_global));
		requestGlobal(_global.position);
		return {};
	}

	void Session::selectPattern(const uint8_t _slot)
	{
		const auto before = m_state;
		send(ed::mdLoadPattern(_slot));
		if(m_state.pattern != _slot)
			m_state.queuedPattern = _slot;
		// Ask again; while playing the reply stays on the old pattern until the switch.
		send(ed::mdStatusRequest(ed::MdStatus::Pattern));
		send(ed::mdStatusRequest(ed::MdStatus::Kit));
		changed(before);
	}

	void Session::loadKit(const uint8_t _slot)
	{
		const auto before = m_state;
		send(ed::mdLoadKit(_slot));
		m_state.kit = _slot;
		m_state.workingKit = WorkingKit::Clean;
		changed(before);
	}

	void Session::saveKit(const uint8_t _slot)
	{
		const auto before = m_state;
		send(ed::mdSaveKit(_slot));
		m_state.kit = _slot;
		m_state.workingKit = WorkingKit::Clean;
		changed(before);
	}

	void Session::loadSong(const uint8_t _slot)
	{
		const auto before = m_state;
		send(ed::mdLoadSong(_slot));
		m_state.song = _slot;
		m_state.songReloadNeeded = false;
		changed(before);
	}

	void Session::saveSong(const uint8_t _slot)
	{
		const auto before = m_state;
		send(ed::mdSaveSong(_slot));
		m_state.song = _slot;
		changed(before);
	}

	void Session::noteWorkingKitEdited()
	{
		const auto before = m_state;
		m_state.workingKit = WorkingKit::Edited;
		changed(before);
	}

	void Session::noteWorkingKitObserved(const bool _matchesSlot)
	{
		const auto before = m_state;
		m_state.workingKit = _matchesSlot ? WorkingKit::Clean : WorkingKit::Edited;
		changed(before);
	}

	bool Session::selectWouldDiscardKitEdits(const uint8_t _slot) const
	{
		if(m_state.workingKit != WorkingKit::Edited || m_state.extendedMode != true || !m_state.kit)
			return false;
		const auto link = m_state.patternKits.find(_slot);
		return link != m_state.patternKits.end() && link->second != *m_state.kit;
	}

	void Session::applyStatus(const ed::MdStatusValue& _s)
	{
		switch(_s.param)
		{
		case ed::MdStatus::GlobalSlot:
			m_state.globalSlot = _s.value;
			break;
		case ed::MdStatus::Kit:
			// Another kit became current (linked switch, load by panel or program
			// change): the working kit is that slot's stored copy.
			if(m_state.kit && *m_state.kit != _s.value)
				m_state.workingKit = WorkingKit::Clean;
			m_state.kit = _s.value;
			break;
		case ed::MdStatus::Pattern:
			m_state.pattern = _s.value;
			if(m_state.queuedPattern == _s.value)
				m_state.queuedPattern.reset();
			break;
		case ed::MdStatus::Song:
			m_state.song = _s.value;
			break;
		case ed::MdStatus::SequencerMode:
			m_state.songMode = _s.value != 0;
			break;
		case ed::MdStatus::LockMode:
			m_state.extendedMode = _s.value != 0;
			break;
		case ed::MdStatus::Track:
			m_state.track = _s.value;
			break;
		}
	}

	void Session::onSysex(const Bytes& _message)
	{
		const auto before = m_state;
		if(const auto status = ed::parseMdStatusResponse(_message))
		{
			applyStatus(*status);
			changed(before);
			return;
		}
		switch(ed::mdDumpCommand(_message))
		{
		case ed::g_mdPatternDump:
			if(const auto p = ed::decodeMdPattern(_message))
			{
				m_state.patternKits[p->position] = p->kit;
				changed(before);
				if(onPattern)
					onPattern(*p);
			}
			break;
		case ed::g_mdKitDump:
			if(const auto k = ed::decodeMdKit(_message); k && onKit)
				onKit(*k);
			break;
		case ed::g_mdSongDump:
			if(const auto s = ed::decodeMdSong(_message); s && onSong)
				onSong(*s);
			break;
		case ed::g_mdGlobalDump:
			if(const auto g = ed::decodeMdGlobal(_message); g && onGlobal)
				onGlobal(*g);
			break;
		default:
			break;
		}
	}

	elektronData::json::Value Session::stateToJson(const State& _s)
	{
		using elektronData::json::Value;
		const auto opt = [](const std::optional<uint8_t>& _v) { return _v ? Value(static_cast<int>(*_v)) : Value(); };
		const auto flag = [](const std::optional<bool>& _v) { return _v ? Value(*_v) : Value(); };
		Value v = Value::object();
		v.set("schema", "md-desk/machine");
		v.set("version", 1);
		Value pattern = Value::object();
		pattern.set("current", opt(_s.pattern));
		pattern.set("queued", opt(_s.queuedPattern));
		v.set("pattern", std::move(pattern));
		Value kit = Value::object();
		kit.set("current", opt(_s.kit));
		kit.set("working", _s.workingKit == WorkingKit::Clean ? "clean"
			: _s.workingKit == WorkingKit::Edited ? "edited" : "unknown");
		v.set("kit", std::move(kit));
		Value song = Value::object();
		song.set("current", opt(_s.song));
		song.set("reloadNeeded", _s.songReloadNeeded);
		v.set("song", std::move(song));
		v.set("globalSlot", opt(_s.globalSlot));
		v.set("track", opt(_s.track));
		v.set("songMode", flag(_s.songMode));
		v.set("extendedMode", flag(_s.extendedMode));
		Value links = Value::array();
		for(const auto& [pat, k] : _s.patternKits)
			links.push(Value(Value::Array{static_cast<int>(pat), static_cast<int>(k)}));
		v.set("patternKits", std::move(links));
		return v;
	}
}
