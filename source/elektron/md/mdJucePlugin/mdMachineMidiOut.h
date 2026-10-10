#pragma once

#include "synthLib/midiRoutingMatrix.h"

namespace mdJucePlugin
{
	// B-037: the emulated machine's own MIDI out goes where a real machine's MIDI OUT goes: the standalone's MIDI
	// output (AUDIO / MIDI) and, in a DAW, the plug-in's MIDI out to the host. What the machine's UART sends is tagged
	// Device (mdLib, mdmc.h). Its notes, CCs, program changes, pitch bend and pressure go, and its system messages:
	// clock, start, stop and continue (the only ones MD OS 1.63 and MM OS 1.32B send, measured; a song position or song
	// select would share the routing matrix's bucket for them, as on the real machine's MIDI OUT). Its SysEx never
	// goes: that is the dumps and answers it sends back to the editor's own requests (the desk talks to the machine in
	// SysEx), which would reach whatever is on the port, and a real Elektron there with SysEx receive
	// on would store them.
	//
	// jucePluginLib sends the device's output to the host by its MIDI routing matrix (processor.cpp, processBlock: the
	// Device -> Host cell), which has no such route by default, and a project's state carries the whole matrix (the
	// "MiRM" chunk). So the processor sets the route when it is made and again after every state load
	// (mdPluginProcessor.cpp; doc/modern-ux/UPSTREAM.md): the rule lives here, not in saved projects. Plain values,
	// tested by mdMachineMidiOutTest.
	namespace machineMidiOut
	{
		using Matrix = synthLib::MidiRoutingMatrix;
		using Kind = Matrix::EventType;
		using Source = synthLib::MidiEventSource;

		// The kinds of the machine's own MIDI that go to the host: everything but SysEx
		constexpr Kind g_toHost = Kind::Note | Kind::Controller | Kind::PolyPressure | Kind::Aftertouch
			| Kind::PitchBend | Kind::ProgramChange | Kind::Other;

		// The Device -> Host route, exactly: whatever a loaded state said, SysEx off
		inline void route(Matrix& _matrix)
		{
			_matrix.setEnabled(Source::Device, Source::Host, Kind::All, false);
			_matrix.setEnabled(Source::Device, Source::Host, g_toHost, true);
		}

		// Whether an event the device sent goes to the host, as processBlock decides it with the matrix. Only the
		// device's output passes there: the editor's own MIDI (Source::Editor) never reaches the host this way.
		inline bool toHost(const Matrix& _matrix, const synthLib::SMidiEvent& _event)
		{
			return _event.source == Source::Device && _matrix.enabled(_event, Source::Host);
		}

		// The route again when a state load is over, however it ends (a malformed chunk throws after "MiRM" was read)
		class RouteAfterLoad
		{
		public:
			explicit RouteAfterLoad(Matrix& _matrix) : m_matrix(_matrix) {}
			~RouteAfterLoad() { route(m_matrix); }

			RouteAfterLoad(const RouteAfterLoad&) = delete;
			RouteAfterLoad& operator=(const RouteAfterLoad&) = delete;

		private:
			Matrix& m_matrix;
		};
	}
}
