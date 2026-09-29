#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "midiports.h"

#include "synthLib/midiTypes.h"

namespace pluginLib
{
	// An editor's input filter (doc/modern-ux/DESIGN-tr06.md, a controller profile): it sees every MIDI
	// event that comes in from outside (the host's and the physical ports'; never the device's own
	// output) before the processor routes it, and takes the ones that are its own. Called on whichever
	// thread the event arrives on (the audio thread, the MIDI ports' thread): no lock that waits, no
	// allocation.
	class MidiInputFilter
	{
	public:
		virtual ~MidiInputFilter() = default;
		// True: the filter took the event; the device, the ports and MIDI learn do not get it.
		virtual bool filterIn(const synthLib::SMidiEvent& _ev) = 0;
		// The audio block, while external MIDI is on: what the filter made for the MIDI out.
		virtual void flushOut(juce::MidiBuffer&, MidiPorts&) {}
	};

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

		// The editor's input filter, or nullptr. Message thread. Returns once no MIDI thread is inside
		// the filter it replaces, so the old one can go.
		void setInputFilter(MidiInputFilter* _filter)
		{
			m_filter.store(_filter, std::memory_order_seq_cst);
			while(m_filterUsers.load(std::memory_order_seq_cst) != 0)
				std::this_thread::yield();
		}

		// The processor's MIDI in: the input filter's events first (on or off), then SysEx from the
		// hardware is the editor's. True: kept, the device does not get it.
		bool takeIn(const synthLib::SMidiEvent& _ev)
		{
			if(_ev.source == synthLib::MidiEventSource::Device)
				return false;
			if(withFilter([&](MidiInputFilter& _f) { return _f.filterIn(_ev); }))
				return true;
			if(_ev.sysex.empty() || !isOn())
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
			withFilter([&](MidiInputFilter& _f) { _f.flushOut(_midiMessages, _ports); return false; });
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
		// The filter as long as the call runs: setInputFilter waits for the calls in flight.
		template<typename F> bool withFilter(const F& _f)
		{
			m_filterUsers.fetch_add(1, std::memory_order_seq_cst);
			auto* f = m_filter.load(std::memory_order_seq_cst);
			const bool taken = f && _f(*f);
			m_filterUsers.fetch_sub(1, std::memory_order_seq_cst);
			return taken;
		}

		std::atomic<MidiInputFilter*> m_filter{nullptr};
		std::atomic<int> m_filterUsers{0};
		std::atomic<bool> m_on{false};
		std::mutex m_mutex;
		std::vector<synthLib::SMidiEvent> m_in, m_out;
	};
}
