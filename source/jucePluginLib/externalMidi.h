#pragma once

#include <atomic>
#include <mutex>
#include <vector>

#include "midiports.h"

#include "synthLib/midiTypes.h"

namespace pluginLib
{
	// External MIDI (P4, the Machinedrum and Monomachine Editors' HW MIDI; doc/modern-ux/UPSTREAM.md):
	// an editor drives real hardware through the plug-in's MIDI in and out (the host's and the
	// physical ports) instead of the emulated device. While it is on, SysEx that comes in is kept for
	// the editor (not given to the device), what the editor sends goes out in the next audio block,
	// and the device's own MIDI output is not passed on (it would reach the hardware). The processor
	// owns one and calls takeIn() and flushOut(); the editor calls set(), send() and drainIn().
	// Message thread, except the audio-thread side (takeIn, flushOut) inside processBlock.
	class ExternalMidi
	{
	public:
		void set(const bool _on)
		{
			const std::scoped_lock lock(m_mutex);
			m_on.store(_on, std::memory_order_release);
			m_in.clear();
			m_out.clear();
		}

		bool isOn() const { return m_on.load(std::memory_order_acquire); }

		void send(const synthLib::SMidiEvent& _ev)
		{
			const std::scoped_lock lock(m_mutex);
			if(m_on.load(std::memory_order_relaxed))
				m_out.push_back(_ev);
		}

		void drainIn(std::vector<synthLib::SMidiEvent>& _out)
		{
			const std::scoped_lock lock(m_mutex);
			_out.insert(_out.end(), m_in.begin(), m_in.end());
			m_in.clear();
		}

		// The processor's MIDI in: SysEx from the hardware is the editor's. True: kept, the
		// device does not get it.
		bool takeIn(const synthLib::SMidiEvent& _ev)
		{
			if(_ev.source == synthLib::MidiEventSource::Device || _ev.sysex.empty() || !isOn())
				return false;
			const std::scoped_lock lock(m_mutex);
			m_in.push_back(_ev);
			return true;
		}

		// The audio block: what the editor sent, to the host's MIDI out and the physical ports. Never
		// waits for the message thread; what cannot be taken now goes out with the next block.
		void flushOut(juce::MidiBuffer& _midiMessages, MidiPorts& _ports)
		{
			if(!isOn())
				return;
			std::unique_lock lock(m_mutex, std::try_to_lock);
			if(!lock.owns_lock())
				return;
			for(const auto& e : m_out)
			{
				_midiMessages.addEvent(MidiPorts::toJuceMidiMessage(e), 0);
				_ports.send(e);
			}
			m_out.clear();
		}

	private:
		std::atomic<bool> m_on{false};
		std::mutex m_mutex;
		std::vector<synthLib::SMidiEvent> m_in, m_out;
	};
}
