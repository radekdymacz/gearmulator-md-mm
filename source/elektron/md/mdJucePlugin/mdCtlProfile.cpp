#include "mdCtlProfile.h"

#include "mdDeskHost.h"
#include "mdDeskSession.h"
#include "mdPluginProcessor.h"

#include "deskCore/deskCommands.h"

#include "jucePluginLib/processor.h"
#include "synthLib/plugin.h"

namespace mdJucePlugin
{
	namespace dc = deskController;
	namespace json = elektronData::json;

	namespace
	{
		// How often the machine's route (its active global) is looked at, and how often the page hears
		// that a knob moved (the row it lights).
		constexpr double g_routeMs = 250;
		constexpr double g_activityMs = 250;

		int intOf(const json::Value& _m, const char* _key, const int _default = -1)
		{
			const auto* v = _m.find(_key);
			return v && v->isNumber() ? static_cast<int>(v->asNumber()) : _default;
		}
	}

	// ---- the input filter ----

	CtlInputFilter::CtlInputFilter(pluginLib::Processor& _processor) : m_processor(_processor)
	{
	}

	CtlInputFilter::~CtlInputFilter()
	{
		if(m_installed)
			m_processor.getExternalMidi().setInputFilter(nullptr);
	}

	void CtlInputFilter::configure(const dc::Setup& _setup, const dc::Route& _route)
	{
		m_input.configure(_setup, _route);
		if(_setup.on == m_installed)
			return;
		// Off: out of the processor's path altogether (setInputFilter waits for a call in flight).
		m_processor.getExternalMidi().setInputFilter(_setup.on ? this : nullptr);
		m_installed = _setup.on;
	}

	bool CtlInputFilter::filterIn(const synthLib::SMidiEvent& _ev)
	{
		// Only what comes from outside: the host's MIDI and the plug-in's own ports. Never the page's.
		if(_ev.source != synthLib::MidiEventSource::Host && _ev.source != synthLib::MidiEventSource::Physical)
			return false;
		if(!_ev.sysex.empty())
			return false;
		const auto r = m_input.translate(_ev.a, _ev.b, _ev.c);
		switch(r.verdict)
		{
		case dc::Input::Verdict::Pass:
			return false;
		case dc::Input::Verdict::Block:
		case dc::Input::Verdict::Knob:
			return true;
		case dc::Input::Verdict::Note:
			// A real machine on the plug-in's MIDI gets it with the next audio block (flushOut); the
			// emulated one now, at the event's offset. A full queue drops the note (it never blocks).
			if(m_processor.getExternalMidi().isOn())
				m_out.tryPush({r.a, r.b, r.c});
			else
				m_processor.getPlugin().addMidiEvent(synthLib::SMidiEvent(_ev.source, r.a, r.b, r.c, _ev.offset));
			return true;
		}
		return false;
	}

	void CtlInputFilter::flushOut(juce::MidiBuffer& _midiMessages, pluginLib::MidiPorts& _ports)
	{
		Short s;
		while(m_out.tryPop(s))
		{
			const synthLib::SMidiEvent e(synthLib::MidiEventSource::Editor, s.a, s.b, s.c);
			_midiMessages.addEvent(pluginLib::MidiPorts::toJuceMidiMessage(e), 0);
			_ports.send(e);
		}
	}

	// ---- the session's side ----

	ControllerProfile::ControllerProfile(AudioPluginAudioProcessor& _processor, Hooks _hooks, std::function<void(const Value&)> _publish)
		: m_processor(_processor)
		, m_hooks(std::move(_hooks))
		, m_publish(std::move(_publish))
		, m_setup(dc::defaults(m_hooks.machine))
		, m_filter(pluginProcessorOf(_processor))
	{
		m_route.channel.fill(-1);
		m_route.note.fill(-1);
	}

	void ControllerProfile::reply(const Value& _message, const bool _ok, const std::string& _note) const
	{
		m_publish(deskCore::resultMessage(_message, _ok ? std::vector<std::string>{} : std::vector<std::string>{_note}, _note));
	}

	void ControllerProfile::change(const dc::Setup& _setup)
	{
		m_setup = _setup;
		m_route = m_hooks.route ? m_hooks.route(m_setup) : m_route;
		m_filter.configure(m_setup, m_route);
		// Kept with the project; the session's own save is not a restore.
		auto* host = m_processor.getDeskHost();
		host->setController(json::write(dc::setupToJson(m_setup, m_hooks.machine)));
		m_seen = host->controllerVersion();
	}

	void ControllerProfile::restore()
	{
		auto* host = m_processor.getDeskHost();
		const auto version = host->controllerVersion();
		if(m_seen && *m_seen == version)
			return;
		m_seen = version;
		const auto text = host->controller();
		auto s = dc::defaults(m_hooks.machine);
		if(!text.empty())
		{
			std::vector<std::string> errors;
			if(const auto doc = json::parse(text))
				if(const auto read = dc::setupFromJson(*doc, m_hooks.machine, errors))
					s = *read;
		}
		m_setup = s;
		m_route = m_hooks.route ? m_hooks.route(m_setup) : m_route;
		m_filter.configure(m_setup, m_route);
		publish();
	}

	void ControllerProfile::handle(const deskHost::Action _action, const Value& _message)
	{
		using A = deskHost::Action;
		const auto m = m_hooks.machine;
		auto s = m_setup;
		switch(_action)
		{
		case A::CtlSet:
		{
			if(const auto* p = _message.find("profile"); p && p->isString())
				s.on = p->asString() != "off";
			if(const int ch = intOf(_message, "channel"); ch >= 1)
				s.channel = static_cast<uint8_t>(ch - 1);
			break;
		}
		case A::CtlVoice:
		{
			const auto id = _message.find("voice")->asString();
			const int t = intOf(_message, "t");
			size_t v = 0;
			while(v < dc::g_voices && dc::tr06().voices[v].id != id)
				++v;
			if(v == dc::g_voices || t < 0 || t >= dc::tracksOf(m))
			{
				reply(_message, false, "ctlVoice: a voice and a track 1-" + std::to_string(dc::tracksOf(m)));
				return;
			}
			s.voices[v].t = t;
			if(m == dc::Machine::Mm)
				s.voices[v].note = intOf(_message, "note", s.voices[v].note);
			break;
		}
		case A::CtlKnob:
		{
			const int cc = intOf(_message, "cc");
			const auto* i = _message.find("i");
			const dc::Target t{m == dc::Machine::Md ? -1 : intOf(_message, "pg"), i && i->isNumber() ? static_cast<int>(i->asNumber()) : -1};
			bool knob = false;
			for(const auto& k : dc::tr06().knobs)
				knob |= k.cc == cc;
			if(!knob || (t.mapped() && !dc::validTarget(m, t)))
			{
				reply(_message, false, "ctlKnob: a knob of the controller and a parameter of the machine (controller.doc.targets)");
				return;
			}
			s.knobs[static_cast<size_t>(cc)] = t;
			break;
		}
		case A::CtlReset:
		{
			const bool on = s.on;
			s = dc::defaults(m);
			s.on = on;
			break;
		}
		case A::CtlTrack:
		{
			const int t = intOf(_message, "t");
			if(t < 0 || t >= dc::tracksOf(m))
			{
				reply(_message, false, "ctlTrack: a track 1-" + std::to_string(dc::tracksOf(m)));
				return;
			}
			const bool same = t == m_selected;
			m_selected = t;
			reply(_message, true, {});
			if(!same)
				publish();
			return;
		}
		default:
			reply(_message, false, "not a controller command");
			return;
		}
		if(s != m_setup)
			change(s);
		reply(_message, true, {});
		publish();
	}

	void ControllerProfile::step(const double _now)
	{
		restore();
		if(_now - m_lastRouteMs >= g_routeMs && m_hooks.route)
		{
			m_lastRouteMs = _now;
			// The machine's global decides where a voice goes (its base channel, its keymap): follow it.
			const auto r = m_hooks.route(m_setup);
			if(r != m_route)
			{
				m_route = r;
				m_filter.configure(m_setup, m_route);
				publish();
			}
		}
		auto& in = m_filter.input();
		if(m_setup.on && in.knobsMoved())
		{
			for(const auto& k : dc::tr06().knobs)
			{
				const int v = in.takeKnob(k.cc);
				if(v < 0)
					continue;
				const auto& target = m_setup.knobs[k.cc];
				if(target.mapped())
					m_pump.in(static_cast<uint8_t>(m_selected), target, static_cast<uint8_t>(v), _now);
				Value last = Value::object();
				last.set("cc", static_cast<int>(k.cc));
				last.set("v", v);
				m_last = std::move(last);
				m_lastChanged = true;
			}
		}
		if(m_pump.pending())
		{
			const auto edits = m_pump.take(_now);
			if(!edits.empty() && m_hooks.apply)
				m_hooks.apply(edits);
		}
		if(m_lastChanged && _now - m_lastPublishMs >= g_activityMs)
			publish();
	}

	void ControllerProfile::publish()
	{
		m_lastPublishMs = sessionNowMs();
		m_lastChanged = false;
		Value msg = Value::object();
		msg.set("type", "controller");
		msg.set("doc", dc::pageDocument(m_setup, m_hooks.machine, m_route, m_selected, m_last));
		m_publish(msg);
	}
}
