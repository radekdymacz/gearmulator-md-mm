#pragma once

#include "elektronData/json.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace elektronData
{
	struct MdGlobal;
	struct MmGlobal;
	struct MmKit;
}

namespace deskController
{
	// A controller profile (doc/modern-ux/DESIGN-tr06.md): a drum machine on one MIDI channel plays the
	// editor's machine. Its notes trigger tracks, its knobs move the selected track's parameters; what
	// its own MIDI OFFLINE sends (All Sound Off, Reset All Controllers) and Active Sensing never reach
	// the machine. Pure: no JUCE, no device. The facts of the controller and the default mapping are
	// data (tr06.json, compiled in); the user's mapping is a Setup, kept with the editor's state.
	using Value = elektronData::json::Value;

	enum class Machine : uint8_t
	{
		Md,
		Mm
	};

	const char* machineName(Machine _m);	// "md", "mm"
	int tracksOf(Machine _m);				// the tracks a voice or a knob can reach: 16, 6

	constexpr size_t g_voices = 7;

	// The controller's facts, from tr06.json.
	struct Profile
	{
		struct Voice
		{
			std::string id;					// "BD"
			std::string label;				// "BASS DRUM"
			std::vector<uint8_t> notes;		// what it sends first, then the aliases it is also heard as
		};
		struct Knob
		{
			uint8_t cc = 0;
			std::string label;
		};
		std::string id, label, about;
		uint8_t channel = 9;							// 0-based: the controller's own default
		std::array<Voice, g_voices> voices;
		std::vector<Knob> knobs;
		std::array<bool, 128> blockedCc{};				// never reach the machine while the profile is on
		std::vector<uint8_t> blockedRealtime;			// 0xfe Active Sensing
		bool relative = true;							// the knobs' default mode (knobMode): relative or absolute
		std::array<std::string, 2> defaultsAbout;		// per machine
		std::array<Value, 2> defaults;					// per machine: {voices, knobs}
	};
	const Profile& tr06();

	// What a knob moves on the selected track. Machinedrum: pg -1, i the kit parameter 0-23 or 24 (the
	// track level). Monomachine: pg 0-6 the DATA page (SYN, AMP, FLTR, EFX, LFO1-3), i 0-7; pg 7, i 0
	// the track level. Both: pg 8, i 0 NOTE (the Monomachine: the note the voices on the track play; the
	// Machinedrum, whose TRIGs have no pitch: the track's machine's pitch parameter, if it has one).
	// Unmapped: i -1.
	struct Target
	{
		static constexpr int g_notePg = 8;

		int pg = -1;
		int i = -1;

		static Target note() { return {g_notePg, 0}; }
		bool mapped() const { return i >= 0; }
		bool isNote() const { return pg == g_notePg && i == 0; }
		bool operator==(const Target& _o) const { return pg == _o.pg && i == _o.i; }
		bool operator!=(const Target& _o) const { return !(*this == _o); }
		bool operator<(const Target& _o) const { return pg != _o.pg ? pg < _o.pg : i < _o.i; }
	};

	struct TargetInfo
	{
		Target at;
		std::string name;
	};
	// Every target a knob can have on the machine (the page's list to reassign from).
	const std::vector<TargetInfo>& targets(Machine _m);
	bool validTarget(Machine _m, const Target& _t);
	std::string targetName(Machine _m, const Target& _t);
	// The machine's own name for a target on a track holding it (_model: the MD's kit model, the MM's
	// machine id; -1 not known), where it differs from the slot's (targetName): "TUNE" for the MM's SYN H
	// on a SWAVE-SAW, "PTCH" for the MD's SYN 1 on a TRX-BD and for NOTE there, "no pitch on this machine"
	// for the MD's NOTE on a machine without one, "-" for a slot the machine does not use, "" otherwise.
	// ASCII: the page joins the two ("SYN H · TUNE").
	std::string ownName(Machine _m, const Target& _t, int _model);
	// The machine on a track, by name ("SWAVE-SAW", "TRX-BD"; "" not known).
	std::string modelName(Machine _m, int _model);
	// The Machinedrum's pitch parameter for a machine (the first synthesis slot named PTCH, PITCH or
	// TUNE), -1 when it has none: what NOTE moves there.
	int mdPitchParam(int _model);

	// A voice's track, and (Monomachine) the note it plays there; -1: the Machinedrum's keymap decides.
	struct VoiceMap
	{
		int t = 0;
		int note = -1;

		bool operator==(const VoiceMap& _o) const { return t == _o.t && note == _o.note; }
	};

	// The user's side: on or off, the controller's channel, how the knobs apply, the mapping.
	struct Setup
	{
		bool on = false;
		uint8_t channel = 9;							// 0-based
		bool relative = true;							// knobMode: a turn moves the value from where it is (relative), or to the knob's position (absolute)
		std::array<VoiceMap, g_voices> voices{};
		std::array<Target, 128> knobs{};				// by CC

		bool operator==(const Setup& _o) const { return on == _o.on && channel == _o.channel && relative == _o.relative && voices == _o.voices && knobs == _o.knobs; }
		bool operator!=(const Setup& _o) const { return !(*this == _o); }
	};

	// The shipped mapping for the machine (off).
	Setup defaults(Machine _m);
	// {"schema":"desk/controller","version":1,"machine","profile":"off"|"tr06","channel":1-16,"knobMode":"relative"|"absolute","voices":[...],"knobs":[...]}.
	// Errors carry JSON paths; a setup for the other machine is refused.
	std::optional<Setup> setupFromJson(const Value& _doc, Machine _m, std::vector<std::string>& _errors);
	Value setupToJson(const Setup& _s, Machine _m);

	// How the voices reach the machine and where it listens: facts of its active global.
	struct Route
	{
		bool known = false;								// no global yet: notes wait (they are dropped)
		std::array<int, g_voices> channel{};			// 0-based; -1: this voice goes nowhere
		std::array<int, g_voices> note{};
		std::vector<std::pair<int, std::string>> listens;	// a channel the machine listens on, and why

		bool operator==(const Route& _o) const { return known == _o.known && channel == _o.channel && note == _o.note && listens == _o.listens; }
		bool operator!=(const Route& _o) const { return !(*this == _o); }
	};
	// The Machinedrum: every voice on the base channel, the first note its global's keymap gives the track
	// (a real Machinedrum over MIDI). _pads: the emulated Machinedrum, which turns every note 36-51 on any
	// channel into a press of TRIG key (note - 36) (upstream's mdhardware.cpp, pumpScheduledMidi), so a
	// keymap note there would press another track's key: the voice goes as its track's TRIG note, 36 + track.
	Route mdRoute(const Setup& _s, const elektronData::MdGlobal* _global, bool _pads = false);
	// The Monomachine: the track's channel (base + track), the voice's note.
	Route mmRoute(const Setup& _s, const elektronData::MmGlobal* _global);
	// A warning when the controller's channel is one the machine listens on (empty: none).
	std::string overlapWarning(const Setup& _s, const Route& _r, Machine _m);

	// The input side (any MIDI input thread: the audio thread for the host's MIDI, the ports' thread for
	// the plug-in's own ports): one message in, what becomes of it out. No lock, no allocation: the
	// message thread writes the tables (configure), the input reads them, each entry an atomic.
	class Input
	{
	public:
		enum class Verdict : uint8_t
		{
			Pass,		// not the controller's: the processor goes on as without the profile
			Block,		// the controller's, and it goes nowhere
			Note,		// the controller's voice: a, b, c is the machine's note (same velocity, on or off)
			Knob		// the controller's knob: kept (takeKnob), the machine gets it from the edit path
		};
		struct Result
		{
			Verdict verdict = Verdict::Pass;
			uint8_t a = 0, b = 0, c = 0;
		};

		Input();

		// The message thread: the setup and its route. Off passes everything.
		void configure(const Setup& _setup, const Route& _route, const Profile& _profile = tr06());
		bool isOn() const { return m_on.load(std::memory_order_acquire); }

		// One MIDI message (status, data, data; realtime and system common as one byte plus what
		// follows). SysEx is never the controller's (the TR-06 sends none): the caller passes it.
		Result translate(uint8_t _a, uint8_t _b, uint8_t _c);

		// The message thread: whether any knob moved since the last call, then each knob's newest value
		// since its last take (-1: none).
		bool knobsMoved() { return m_moved.exchange(false, std::memory_order_acq_rel); }
		int takeKnob(uint8_t _cc) { return m_latest[_cc & 0x7f].exchange(-1, std::memory_order_acq_rel); }

		// Diagnostics: what the input took.
		uint32_t notes() const { return m_notes.load(std::memory_order_relaxed); }
		uint32_t blocked() const { return m_blocked.load(std::memory_order_relaxed); }

	private:
		static constexpr uint16_t g_route = 0x8000;
		enum Cc : uint8_t { CcBlock = 0, CcKnob = 1 };

		std::atomic<bool> m_on{false};
		std::atomic<uint8_t> m_channel{9};
		std::atomic<bool> m_blockSensing{true};
		std::array<std::atomic<uint16_t>, 128> m_noteOut;	// by input note: g_route | channel << 8 | note; 0 blocks
		std::array<std::atomic<uint16_t>, 128> m_sounding;	// where a note-on went, for its note-off
		std::array<std::atomic<uint8_t>, 128> m_cc;
		std::array<std::atomic<int16_t>, 128> m_latest;
		std::atomic<bool> m_moved{false};
		std::atomic<uint32_t> m_notes{0}, m_blocked{0};
	};

	// Whether a MIDI input's name is the controller's: "TR-06" or "TR06", any case, anywhere in the name
	// (macOS names a USB TR-06 "TR-06", a driver may add to it).
	bool looksLikeTr06(const std::string& _name);

	// What arrives on each MIDI channel, for the MIDI monitor, whether the profile is on or not
	// (DESIGN-tr06.md, Activity). The input side (seen: any MIDI input thread) keeps each channel's last
	// channel message and a count, every CC's latest value per channel (a fixed 16 x 128 table) and the
	// last notes (a ring of g_notes), in atomics: no lock, no allocation. The message thread reads them.
	class Monitor
	{
	public:
		static constexpr size_t g_notes = 8;

		struct Last
		{
			uint32_t count = 0;
			uint8_t a = 0, b = 0, c = 0;
		};
		struct Note
		{
			uint32_t n = 0;					// the how-manieth note seen (newest highest)
			uint8_t ch = 0, note = 0, velocity = 0;
		};

		// A channel message (0x80-0xef); anything else is not a channel's and is ignored.
		void seen(uint8_t _a, uint8_t _b, uint8_t _c)
		{
			if(_a < 0x80 || _a >= 0xf0)
				return;
			const auto ch = _a & 0x0f;
			const auto type = _a & 0xf0;
			if(type == 0xb0)
				m_ccs[static_cast<size_t>(ch) << 7 | (_b & 0x7f)].store(static_cast<uint8_t>(0x80 | (_c & 0x7f)), std::memory_order_relaxed);
			else if(type == 0x90 && (_c & 0x7f))
			{
				const auto n = m_noteCount.fetch_add(1, std::memory_order_relaxed) + 1;
				m_notes[n % g_notes].store(static_cast<uint64_t>(n) << 32 | static_cast<uint32_t>(ch) | static_cast<uint32_t>(_b & 0x7f) << 8 | static_cast<uint32_t>(_c & 0x7f) << 16, std::memory_order_relaxed);
			}
			m_msg[ch].store(static_cast<uint32_t>(_a) | static_cast<uint32_t>(_b & 0x7f) << 8 | static_cast<uint32_t>(_c & 0x7f) << 16, std::memory_order_relaxed);
			m_count[ch].fetch_add(1, std::memory_order_release);
		}
		// A CC's latest value on a channel (0-based), -1 when none arrived since the last clear.
		int cc(const uint8_t _ch, const uint8_t _cc) const
		{
			const auto v = m_ccs[static_cast<size_t>(_ch & 0x0f) << 7 | (_cc & 0x7f)].load(std::memory_order_relaxed);
			return v & 0x80 ? v & 0x7f : -1;
		}
		// The last notes (note-ons), newest first, since the last clear.
		std::vector<Note> notes() const;
		// The message thread: forget the CCs and notes seen (the monitor's CLEAR, a new look).
		void clear();

		Monitor()
		{
			for(auto& c : m_ccs)
				c.store(0, std::memory_order_relaxed);
			for(auto& n : m_notes)
				n.store(0, std::memory_order_relaxed);
		}
		Last last(const uint8_t _ch) const
		{
			Last l;
			l.count = m_count[_ch & 0x0f].load(std::memory_order_acquire);
			const auto m = m_msg[_ch & 0x0f].load(std::memory_order_relaxed);
			l.a = static_cast<uint8_t>(m);
			l.b = static_cast<uint8_t>(m >> 8);
			l.c = static_cast<uint8_t>(m >> 16);
			return l;
		}

	private:
		std::array<std::atomic<uint32_t>, 16> m_msg{};
		std::array<std::atomic<uint32_t>, 16> m_count{};
		std::array<std::atomic<uint8_t>, 16 * 128> m_ccs;		// 0x80 | value; 0 none
		std::array<std::atomic<uint64_t>, g_notes> m_notes;		// n << 32 | velocity << 16 | note << 8 | channel; 0 none
		std::atomic<uint32_t> m_noteCount{0};
	};

	// The page's view of the Monitor (message thread): the last message on the controller's channel, a
	// channel the controller seems to send on instead (something arrived there lately, nothing on its own
	// channel), every CC seen with its latest value (the controller's channel first, then the others, each
	// by CC number) and the last notes. As JSON: {seq, last: {kind "note"|"off"|"cc"|"other", n, v} | null,
	// elsewhere: 1-16 | null, ccs: [{ch 1-16, cc, v}], notes: [{ch 1-16, n, v}]}.
	class Activity
	{
	public:
		static constexpr double g_elsewhereMs = 2000;

		// From now on: what the monitor counted so far is old (it is cleared).
		void start(Monitor& _monitor, double _now);
		// One look at the monitor (_channel 0-based). True when the page's view changed.
		bool update(const Monitor& _monitor, uint8_t _channel, double _now);
		Value toJson() const;

	private:
		struct Cc
		{
			uint8_t ch, cc, v;
			bool operator==(const Cc& _o) const { return ch == _o.ch && cc == _o.cc && v == _o.v; }
		};
		std::vector<Cc> m_ccs;
		std::vector<Monitor::Note> m_notes;
		std::array<uint32_t, 16> m_counts{};
		std::array<double, 16> m_at{};
		uint32_t m_seq = 0;
		bool m_hasLast = false;
		Monitor::Last m_last;
		int m_channel = -1;
		int m_elsewhere = -1;
	};

	// A MIDI input of the standalone app, and whether it is enabled in AUDIO / MIDI.
	struct MidiInput
	{
		std::string name;
		bool on = false;

		bool operator==(const MidiInput& _o) const { return name == _o.name && on == _o.on; }
	};

	// What the editor sees of the MIDI inputs and of the activity, for the page's document: whether it
	// sees the inputs by name (the standalone app; in a DAW the host owns them), every input (the CONTROL
	// workspace's device tiles), the enabled input that looks like the controller ("" none), and the
	// activity (null: the page is not watching).
	struct Seen
	{
		bool named = false;
		std::vector<MidiInput> inputs;
		std::string device;
		Value activity;
	};

	// The page's document (the "controller" message's doc, doc/modern-ux/DESIGN-tr06.md): the setup, what
	// each voice reaches now, every knob with its target (named for the machine on the selected track,
	// _model: its kit model or machine id, -1 not known), the targets to choose from, the selected track,
	// the last control the controller moved (_last: {cc, v} or {voice}, or null) and what is seen (_seen).
	Value pageDocument(const Setup& _s, Machine _m, const Route& _r, int _selected, const Value& _last, const Seen& _seen = {}, int _model = -1);

	// One value for the edit path: a target of a track, and the gesture (one undo step) it belongs to.
	struct Edit
	{
		uint8_t track = 0;
		Target at;
		uint8_t value = 0;
		uint32_t gesture = 0;

		bool operator==(const Edit& _o) const { return track == _o.track && at == _o.at && value == _o.value && gesture == _o.gesture; }
	};

	// Knob moves to single-parameter edits (the message thread), paced as the Control All pump
	// (DESIGN-edit-flow.md): what came in since the last round goes out at most every g_intervalMs,
	// never a value the target got last in the same gesture. A pause of g_quietMs ends the gesture: the
	// next move is a new undo step. Two ways in, one per knob mode:
	//  - absolute (in): a value; the newest of each target only (latest wins);
	//  - relative (add): a change; the changes of a round add up (a fast turn loses nothing) and go from the
	//    target's value: the one it got last in this gesture, else the machine's (take's _current), clamped
	//    to 0-127. A target whose value is not known yet gets nothing.
	class KnobPump
	{
	public:
		static constexpr double g_intervalMs = 50;
		static constexpr double g_quietMs = 400;

		// The target's value on the machine now, -1 when not known.
		using Current = std::function<int(uint8_t _track, const Target& _at)>;

		void in(uint8_t _track, const Target& _at, uint8_t _value, double _now);
		void add(uint8_t _track, const Target& _at, int _delta, double _now);
		std::vector<Edit> take(double _now, const Current& _current = {});
		bool pending() const;
		uint32_t gesture() const { return m_gesture; }

	private:
		struct Key
		{
			uint8_t track;
			Target at;
			bool operator<(const Key& _o) const { return track != _o.track ? track < _o.track : at < _o.at; }
		};
		struct Slot
		{
			int pending = -1;		// absolute: the newest value
			int delta = 0;			// relative: the changes since the last round
			bool relative = false;
			int sent = -1;
			uint32_t gesture = 0;
		};
		Slot& slot(uint8_t _track, const Target& _at, double _now);
		std::map<Key, Slot> m_slots;
		double m_lastOut = -1e9;
		double m_lastIn = -1e9;
		uint32_t m_gesture = 0;
	};

	// What a knob does on the selected track (_model: its machine, -1 not known): moves a parameter (at),
	// transposes the voices on the track (Note: the Monomachine's NOTE), or nothing (unmapped; the
	// Machinedrum's NOTE on a machine without a pitch parameter).
	struct KnobAction
	{
		enum class Kind : uint8_t { None, Param, Note };
		Kind kind = Kind::None;
		Target at;
	};
	KnobAction knobAction(const Setup& _s, Machine _m, uint8_t _cc, int _model);
	// NOTE on the Monomachine: the voices on _track play _delta semitones away (relative) or at _value
	// (absolute), held at 0-127.
	Setup transposed(const Setup& _s, int _track, int _delta, int _value);

	// Relative knobs (the knob mode relative): a knob that sends its absolute position (0-127) as a change.
	// Per CC the value it sent last is the reference; the next value moves the target by the difference,
	// never to the knob's position. The first value after a reset (the start, a track or mapping change)
	// or after g_idleMs without that CC only sets the reference.
	class RelativeKnobs
	{
	public:
		static constexpr double g_idleMs = 2000;

		RelativeKnobs() { reset(); }
		// The change a new value means (0: none, also when it only set the reference).
		int in(uint8_t _cc, uint8_t _value, double _now);
		void reset();

	private:
		std::array<int16_t, 128> m_ref{};
		std::array<double, 128> m_at{};
	};

	// The gesture ids of the controller's edits (one undo step per burst of knob moves), apart from the
	// page's (from 1) and a SysEx import's (0x40000000).
	constexpr uint32_t g_gestures = 0x50000000u;

	// The knobs' edits as the page's own commands, for the machine's edit path (the plug-in's session
	// hands them to the desk). The Machinedrum: one param (or level, i 24) command each, on the kit that
	// plays (_kit, its slot): the desk sends each as the live kit edit it is, a CC.
	std::vector<Value> mdCommands(const std::vector<Edit>& _edits, uint8_t _kit);
	// The Monomachine: one working-kit set with them all (the adapter sends what changed as CCs); none
	// when nothing changes.
	std::optional<Value> mmCommand(const std::vector<Edit>& _edits, const elektronData::MmKit& _working);
}
