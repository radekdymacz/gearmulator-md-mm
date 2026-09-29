#include "deskHost.h"

#include <array>

namespace deskHost
{
	namespace
	{
		constexpr std::array<const char*, static_cast<size_t>(AudioSetting::Count)> g_settings{
			"driver", "output", "input", "mute", "outputChannel", "sampleRate", "bufferSize", "midiInput", "midiOutput"};
		constexpr std::array<const char*, static_cast<size_t>(AudioAction::Count)> g_actions{"test", "bluetooth"};

		template<size_t N>
		std::vector<const char*> names(const std::array<const char*, N>& _a)
		{
			return {_a.begin(), _a.end()};
		}

		template<typename E, size_t N>
		std::optional<E> find(const std::array<const char*, N>& _a, const std::string& _name)
		{
			for(size_t i = 0; i < N; ++i)
				if(_name == _a[i])
					return static_cast<E>(i);
			return std::nullopt;
		}
	}

	const char* audioSettingName(const AudioSetting _s) { return g_settings[static_cast<size_t>(_s)]; }
	const char* audioActionName(const AudioAction _a) { return g_actions[static_cast<size_t>(_a)]; }
	std::optional<AudioSetting> audioSettingOf(const std::string& _name) { return find<AudioSetting>(g_settings, _name); }
	std::optional<AudioAction> audioActionOf(const std::string& _name) { return find<AudioAction>(g_actions, _name); }

	const Table& commands()
	{
		using deskCore::Arg;
		using deskCore::ArgType;
		using deskCore::CoreOp;
		using deskCore::Gate;
		using deskCore::Owner;
		const Arg t{"t", ArgType::Integer, 0, 15};
		const Arg pg{"pg", ArgType::Integer, 0, 7, true};
		const Arg i{"i", ArgType::Integer, 0, 24, true};
		// An entry of the learn document's mappings; the handler checks it against the list.
		const Arg index{"index", ArgType::Integer, 0, 65535};
		const Arg on{"on", ArgType::Bool, 0, 0, true};
		// A MIDI channel message: status 0x80-0xef, then two data bytes.
		Arg bytes{"b", ArgType::Bytes, 0, 0xef};
		bytes.length = 3;
		// "device" names a device (the request's own "id" is the result's).
		Arg set{"set", ArgType::Text, 0, 0, true, names(g_settings)};
		Arg act{"do", ArgType::Text, 0, 0, true, names(g_actions)};
		const Arg device{"device", ArgType::Text, 0, 0, true};
		const Arg channel{"index", ArgType::Integer, 0, 255, true};	// an output channel of the device
		const Arg value{"value", ArgType::Number, 0, 1e6, true};		// a sample rate or a buffer size
		const auto row = [](const char* _op, std::vector<Arg> _args, const char* _help, const Action _a, const Actor _who = Actor::Session)
		{
			return deskCore::Command<Handler>{_op, Owner::Host, Gate::None, -1, std::move(_args), _help, CoreOp::Edit, 0, {_a, _who}};
		};
		static const Table table({
			row("engine", {{"kind", ArgType::Text}}, "an entry of the engine map (machine.engines)", Action::Engine),
			row("recheckFirmware", {}, "look for the ROM again", Action::RecheckFirmware),
			row("revealRomFolder", {}, "", Action::RevealRomFolder),
			row("chooseRom", {}, "choose the firmware file (.bin, or a .zip with it): the window's native file chooser; the file stays on this computer",
				Action::ChooseRom, Actor::Window),
			row("chooseSyx", {}, "choose a .syx to import: the window's native file chooser; a preview follows (syxPreview)", Action::ChooseSyx, Actor::Window),
			row("syxImport", {{"kinds", ArgType::Array}}, "import the previewed .syx: the kinds chosen (global, kit, pattern, song), as document writes, one undo step",
				Action::SyxImport),
			row("syxCancel", {}, "stop an import between items", Action::SyxCancel),
			row("syxExport", {}, "every document the editor holds, as one .syx: the window's native save dialog", Action::SyxExport, Actor::Window),
			row("midi", {bytes}, "a channel message from the page: [status 0x80-0xef, data, data]", Action::Midi),
			row("openMenu", {}, "the editor's menu", Action::Menu, Actor::Window),
			row("learnStart", {t, pg, i}, "MIDI learn a track's parameter (learn.doc.limits: which)", Action::LearnStart),
			row("learnAdd", {{"cc", ArgType::Integer, 0, 127}, t, pg, i, {"ch", ArgType::Integer, 0, 15, true}},
				"a CC mapping without learning, on channel ch (none: all channels)", Action::LearnAdd),
			row("learnSetCc", {{"from", ArgType::Integer, 0, 127}, {"to", ArgType::Integer, 0, 127}},
				"a controller row's CC changed: its mappings follow", Action::LearnSetCc),
			row("learnCancel", {}, "", Action::LearnCancel),
			row("learnRemove", {index}, "", Action::LearnRemove),
			row("learnInvert", {index}, "", Action::LearnInvert),
			row("audio", {}, "the standalone's audio and MIDI devices", Action::AudioPublish, Actor::Window),
			row("audioSet", {set, act, device, on, channel, value}, "change one audio or MIDI setting (set), or do something (do)",
				Action::AudioSet, Actor::Window),
			row("audioMeter", {on}, "", Action::AudioMeter, Actor::Window),
			// The controller profile (doc/modern-ux/DESIGN-tr06.md): a drum machine on one channel plays the machine.
			row("ctlSet", {{"profile", ArgType::Text, 0, 0, true, {"off", "tr06"}}, {"channel", ArgType::Integer, 1, 16, true},
					{"knobMode", ArgType::Text, 0, 0, true, {"relative", "absolute"}}},
				"the controller profile: off or the Roland TR-06, the controller's MIDI channel, and how its knobs apply (relative: a turn moves the value from where it is; absolute: to the knob's position) (controller.doc)", Action::CtlSet),
			row("ctlVoice", {{"voice", ArgType::Text, 0, 0, false, {"BD", "SD", "LT", "HT", "CY", "OH", "CH"}}, t, {"note", ArgType::Integer, 0, 127, true}},
				"a controller voice's track (controller.doc.tracks) and, on the Monomachine, the note it plays", Action::CtlVoice),
			row("ctlKnob", {{"cc", ArgType::Integer, 0, 127}, {"pg", ArgType::Integer, 0, 8, true}, {"i", ArgType::IntegerOrNull, 0, 24}},
				"a controller knob's target on the selected track (controller.doc.targets; pg 8, i 0: NOTE); i null: none", Action::CtlKnob),
			row("ctlReset", {}, "the controller's shipped mapping and channel", Action::CtlReset),
			row("ctlTrack", {t}, "the page's selected track: the controller's knobs move its parameters", Action::CtlTrack),
			row("ctlWatch", {{"on", ArgType::Bool, 0, 0, false}}, "while the TR-06's view shows: the controller document reports what arrives (activity)", Action::CtlWatch),
			row("ctlClear", {}, "empty the MIDI monitor (controller.doc.activity: its CCs and notes)", Action::CtlClear),
		});
		return table;
	}

	elektronData::json::Value contractCommands(const elektronData::json::Value& _modelSchema)
	{
		auto out = _modelSchema;
		auto* one = out.find("oneOf");
		if(!one)
			return out;
		const auto host = commands().schema();
		auto merged = elektronData::json::Value::array();
		for(const auto& c : one->asArray())
			merged.push(c);
		for(const auto& c : host.find("oneOf")->asArray())
			merged.push(c);
		*one = std::move(merged);
		out.put("description", "Generated from the model's command table and the plug-in's (deskHost), P6: every command the page may send.");
		return out;
	}
}
