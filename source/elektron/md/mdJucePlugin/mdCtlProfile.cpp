#include "mdCtlProfile.h"

#include "mdAudioMidiLink.h"
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
		// While the page watches: what arrives, at most about 15 times a second; the MIDI inputs' names
		// (a TR-06 plugged in or enabled) once a second.
		constexpr double g_watchMs = 66;
		constexpr double g_inputsMs = 1000;

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

	void CtlInputFilter::configure(const dc::Setup& _setup, const dc::Route& _route, const bool _watch)
	{
		m_input.configure(_setup, _route);
		const bool want = _setup.on || _watch;
		if(want == m_installed)
			return;
		// Off and not watched: out of the processor's path altogether (setInputFilter waits for a call in flight).
		m_processor.getExternalMidi().setInputFilter(want ? this : nullptr);
		m_installed = want;
	}

	bool CtlInputFilter::filterIn(const synthLib::SMidiEvent& _ev)
	{
		// Only what comes from outside: the host's MIDI and the plug-in's own ports. Never the page's.
		if(_ev.source != synthLib::MidiEventSource::Host && _ev.source != synthLib::MidiEventSource::Physical)
			return false;
		if(!_ev.sysex.empty())
			return false;
		// What arrives, for the page (two relaxed stores); with the profile off the input passes everything.
		m_monitor.seen(_ev.a, _ev.b, _ev.c);
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
		if(!m_hooks.inputs)
			m_hooks.inputs = [this] { return AudioMidiLink::enabledMidiInputs(m_processor); };
	}

	void ControllerProfile::configure()
	{
		m_filter.configure(m_setup, m_route, m_watch);
	}

	void ControllerProfile::watch(const bool _on)
	{
		if(_on == m_watch)
			return;
		m_watch = _on;
		// What came before the page looked is not news.
		if(m_watch)
			m_activity.start(m_filter.monitor(), sessionNowMs());
		configure();
	}

	void ControllerProfile::detach()
	{
		watch(false);
	}

	bool ControllerProfile::lookAtInputs()
	{
		const auto names = m_hooks.inputs ? m_hooks.inputs() : std::nullopt;
		std::string device;
		if(names)
			for(const auto& n : *names)
				if(device.empty() && dc::looksLikeTr06(n))
					device = n;
		const bool named = names.has_value();
		if(named == m_inputs.named && device == m_inputs.device)
			return false;
		m_inputs.named = named;
		m_inputs.device = device;
		return true;
	}

	void ControllerProfile::reply(const Value& _message, const bool _ok, const std::string& _note) const
	{
		m_publish(deskCore::resultMessage(_message, _ok ? std::vector<std::string>{} : std::vector<std::string>{_note}, _note));
	}

	void ControllerProfile::change(const dc::Setup& _setup)
	{
		m_setup = _setup;
		m_route = m_hooks.route ? m_hooks.route(m_setup) : m_route;
		configure();
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
		configure();
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
		case A::CtlWatch:
		{
			const auto* on = _message.find("on");
			watch(on && on->isBool() && on->asBool());
			reply(_message, true, {});
			publish();
			return;
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
				configure();
				publish();
			}
		}
		if(_now - m_lastInputsMs >= g_inputsMs)
		{
			m_lastInputsMs = _now;
			if(lookAtInputs())
				publish();
		}
		if(m_watch && _now - m_lastActivityMs >= g_watchMs)
		{
			m_lastActivityMs = _now;
			if(m_activity.update(m_filter.monitor(), m_setup.channel, _now))
				publish();
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
		auto seen = m_inputs;
		seen.activity = m_watch ? m_activity.toJson() : Value();
		msg.set("doc", dc::pageDocument(m_setup, m_hooks.machine, m_route, m_selected, m_last, seen));
		m_publish(msg);
	}
}
