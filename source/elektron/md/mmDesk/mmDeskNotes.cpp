// MmMachine: the page's keyboard, the note intent (deskCore/deskNotes.h) as a MIDI note on the track's
// channel; the sounding notes are a value (SoundingNotes, mmDeskWatch.h).
#include "mmDeskMachineParts.h"

namespace mmDesk
{
	using namespace parts;

	// The page's keyboard (the home row, the piano roll's keys, the transpose keyboard): synth track t's
	// note on its own MIDI channel (elektronData::mmTrackChannel: base + t, while t < CHANNEL SPAN and up to channel 15), MIDI note
	// 48 + pitch. A second note on of the same pitch ends the first; the note off goes where its note on
	// went. Notes are the machine's to sound: nothing in the documents changes.
	Outcome MmMachine::cmdNoteOn(const Value& _m, const Documents& _view)
	{
		if(!m_port.sendNote)
			return refuse("This engine cannot play notes");
		const auto on = deskCore::noteOnOf(_m);
		const int note = g_noteC + on.pitch;
		if(note < 0 || note > 127)
			return refuse("pitch: MIDI note " + std::to_string(note) + " is not a note 0-127");
		const auto* g = activeGlobal(_view, m_curGlobal);
		if(!g)
			return refuse("The global is not read yet.");
		const auto channel = on.track < 6 ? ed::mmTrackChannel(*g, static_cast<uint8_t>(on.track)) : std::nullopt;
		if(const auto why = noChannelReason(_view, on.track); !why.empty() || !channel)
			return refuse(why.empty() ? "T" + std::to_string(on.track + 1) + " has no MIDI channel of its own." : why);
		const int ch = *channel;
		auto r = pressNote(std::move(m_notes), on.track, on.pitch, static_cast<uint8_t>(ch), static_cast<uint8_t>(note), on.velocity);
		m_notes = std::move(r.next);
		for(const auto& n : r.sends)
			m_port.sendNote(n.channel, n.note, n.velocity);
		return ok();
	}

	Outcome MmMachine::cmdNoteOff(const Value& _m, const Documents&)
	{
		const auto off = deskCore::noteOffOf(_m);
		auto r = releaseNotes(std::move(m_notes), off);
		m_notes = std::move(r.next);
		if(m_port.sendNote)
			for(const auto& n : r.sends)
				m_port.sendNote(n.channel, n.note, n.velocity);
		return ok();
	}
}
