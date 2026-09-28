#include "mdDeskSession.h"

#include "mdSessions.h"

#include "mdPluginProcessor.h"

namespace mdJucePlugin
{
	namespace json = elektronData::json;

	namespace
	{
		// The session's pace: the playhead (the app modulators move on its steps) is looked at every
		// 8 ms, faster than the fastest step (300 BPM at 2X: 25 ms); the desk's own work runs at
		// about 30 Hz (every fourth step).
		constexpr int g_stepMs = 8;
	}

	DeskSession::DeskSession(AudioPluginAudioProcessor& _processor) : m_processor(_processor)
	{
		startTimer(g_stepMs);
	}

	DeskSession::~DeskSession()
	{
		stopTimer();
	}

	std::unique_ptr<DeskSession> DeskSession::create(AudioPluginAudioProcessor& _processor)
	{
		if(_processor.getModel() == md::MachineModel::Monomachine)
			return makeMmSession(_processor);
		return makeMdSession(_processor);
	}

	void DeskSession::attach(ToPage _toPage)
	{
		m_toPage = std::move(_toPage);
		onAttach();
	}

	void DeskSession::detach()
	{
		onDetach();
		m_toPage = nullptr;
	}

	void DeskSession::toPage(const Value& _message) const
	{
		if(m_toPage)
			m_toPage(_message);
	}

	void DeskSession::reply(const Value& _message, const bool _ok, const std::string& _note) const
	{
		json::Value r = json::Value::object();
		r.set("type", "result");
		const auto* op = _message.find("op");
		r.set("op", op ? *op : json::Value(""));
		if(const auto* id = _message.find("id"); id && id->isNumber())
			r.set("id", *id);
		r.set("ok", _ok);
		json::Value errors = json::Value::array();
		if(!_ok)
			errors.push(_note);
		r.set("errors", std::move(errors));
		r.set("note", _ok ? _note : std::string());
		toPage(r);
	}
}
