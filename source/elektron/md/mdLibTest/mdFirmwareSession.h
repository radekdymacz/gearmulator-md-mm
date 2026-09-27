#pragma once

// Headless MD OS 1.63 session for firmware harnesses: boot, render audio, send
// MIDI and wait for the UART to drain, collect SysEx replies. Test-only.

#include "mdLib/mdhardware.h"
#include "mdLib/mdpanel.h"
#include "mdLib/mdstate.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace mdFirmwareSession
{
	using Bytes = std::vector<uint8_t>;

	constexpr uint32_t g_block = 64;
	constexpr uint32_t g_rate = 44100;
	constexpr uint32_t g_playheadAddress = 0x261aa7;	// P0: current step, 0-based

	inline void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	inline Bytes load(const std::string& _path)
	{
		std::ifstream s(_path, std::ios::binary);
		require(s.good(), "cannot read " + _path);
		return {std::istreambuf_iterator<char>(s), std::istreambuf_iterator<char>()};
	}

	inline void save(const std::string& _path, const Bytes& _data)
	{
		std::ofstream s(_path, std::ios::binary);
		s.write(reinterpret_cast<const char*>(_data.data()), static_cast<std::streamsize>(_data.size()));
	}

	// Patch RAM out of a saved plug-in device state (the "MDST" payload).
	inline Bytes patchRamFromState(const std::string& _path, const Bytes& _rom)
	{
		const auto state = load(_path);
		md::DecodedState decoded;
		for(const auto type : {synthLib::StateTypeGlobal, synthLib::StateTypeCurrentProgram})
			if(md::decodeState(decoded, state, _rom, md::MachineModel::Machinedrum, type))
				return decoded.patchRam;
		throw std::runtime_error("device state did not decode: " + _path);
	}

	class Machine
	{
	public:
		Machine(const Bytes& _rom, const std::string& _romName, const Bytes& _patchRam = {})
			: m_hw(_rom, _romName, md::MachineModel::Machinedrum, _patchRam)
		{
			require(m_hw.isValid(), "firmware did not construct a valid machine");
			for(auto& o : m_out)
				o.resize(g_block);
			for(size_t c = 0; c < m_out.size(); ++c)
				m_outputs[c] = m_out[c].data();
			uint32_t frames = 0;
			while(!m_hw.isFirmwareMidiReady())
			{
				m_hw.advance(g_block);
				frames += g_block;
				require(frames < g_rate * 60, "MD firmware boot timed out");
			}
			// The splash animation keeps running for ~20 s after MIDI is ready.
			for(uint32_t i = 0; i < g_rate * 25 / g_block; ++i)
				m_hw.advance(g_block);
		}

		// Machine time since the capture started, in frames.
		uint64_t now() const { return m_frames; }
		const std::vector<float>& left() const { return m_left; }
		const std::vector<float>& right() const { return m_right; }
		md::Hardware& hardware() { return m_hw; }

		// Called once per rendered block (after audio, after SysEx collection).
		std::function<void()> onBlock;
		// Every complete SysEx message the firmware sends.
		std::function<void(const Bytes&)> onSysex;

		void step()
		{
			m_hw.processAudio(m_outputs, g_block, 0);
			m_left.insert(m_left.end(), m_out[0].begin(), m_out[0].end());
			m_right.insert(m_right.end(), m_out[1].begin(), m_out[1].end());
			m_frames += g_block;
			m_events.clear();
			m_hw.readMidiOut(m_events);
			for(const auto& e : m_events)
			{
				if(e.sysex.empty())
					continue;
				m_rx.emplace_back(e.sysex.begin(), e.sysex.end());
				if(onSysex)
					onSysex(m_rx.back());
			}
			if(onBlock)
				onBlock();
		}

		void run(const double _ms)
		{
			const auto end = m_frames + static_cast<uint64_t>(_ms * g_rate / 1000.0);
			while(m_frames < end)
				step();
		}

		void runUntil(const uint64_t _frame)
		{
			while(m_frames < _frame)
				step();
		}

		// Queue a message; returns the frames until the firmware UART consumed it.
		uint64_t send(const Bytes& _bytes)
		{
			synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
			if(!_bytes.empty() && _bytes[0] == 0xf0)
				e.sysex.assign(_bytes.begin(), _bytes.end());
			else
			{
				e.a = _bytes.size() > 0 ? _bytes[0] : 0;
				e.b = _bytes.size() > 1 ? _bytes[1] : 0;
				e.c = _bytes.size() > 2 ? _bytes[2] : 0;
			}
			const auto start = m_frames;
			m_hw.sendMidi(e);
			while(!m_hw.isMidiIngressIdle())
			{
				step();
				require(m_frames - start < g_rate * 10, "MIDI ingress did not drain");
			}
			return m_frames - start;
		}

		// Send a request and wait for the first reply with the given command byte.
		// _frames gets request-queued -> reply-complete time.
		Bytes request(const Bytes& _request, const uint8_t _reply, uint64_t* _frames = nullptr)
		{
			m_rx.clear();
			const auto start = m_frames;
			send(_request);
			while(m_frames - start < g_rate * 5)
			{
				for(const auto& m : m_rx)
				{
					if(m.size() > 7 && m[6] == _reply)
					{
						if(_frames)
							*_frames = m_frames - start;
						auto result = m;
						m_rx.clear();
						return result;
					}
				}
				step();
			}
			return {};
		}

		void panel(const md::PanelControl _control)
		{
			const auto packet = md::panelPacket(md::MachineModel::Machinedrum, _control);
			require(packet.has_value(), "no panel packet");
			m_hw.trySendPanelEvent(packet->row, packet->mask);
			run(40);
			m_hw.trySendPanelEvent(packet->row, 0);
			run(40);
		}

		uint8_t read8(const uint32_t _address) { return m_hw.getUC().read8(_address); }
		uint8_t playhead() { return read8(g_playheadAddress); }

		Bytes snapshotRam()
		{
			Bytes ram;
			ram.reserve(0x100000);
			for(uint32_t a = 0x200000; a < 0x300000; ++a)
				ram.push_back(m_hw.getUC().read8(a));
			return ram;
		}

	private:
		md::Hardware m_hw;
		std::array<std::vector<float>, 6> m_out;
		synthLib::TAudioOutputs m_outputs{};
		std::vector<float> m_left, m_right;
		uint64_t m_frames = 0;
		std::vector<synthLib::SMidiEvent> m_events;
		std::vector<Bytes> m_rx;
	};
}
