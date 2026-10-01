#pragma once

#include "elektronData/mdSamples.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mdDesk
{
	// P9: one SDS (MIDI Sample Dump Standard) transfer to the Machinedrum UW, paced by its handshake.
	// Pure: the bytes go out through _send, the machine's replies come in through onReply, time is
	// the caller's. The header, then (after its ACK) the name and the packets one by one, each after
	// the ACK of the one before. NAK sends a packet again (3 times at most); WAIT holds (30 s at most);
	// CANCEL ends it. A machine that never answers the header in 2 s gets the packets without a
	// handshake (open loop, the SDS rule), one every g_openLoopMs. MD OS 1.63 answers every packet
	// (measured, mdDeskFirmwareTest samples) and, when an ACK was lost, answers the packet sent again
	// with a NAK for the next one: that counts as the ACK.
	class SdsSender
	{
	public:
		using Bytes = std::vector<uint8_t>;
		using Send = std::function<void(const Bytes&)>;

		static constexpr double g_replyMs = 2000;
		static constexpr double g_waitMs = 30000;
		static constexpr double g_openLoopMs = 60;		// a 127-byte packet is 41 ms at DIN speed
		static constexpr uint32_t g_retries = 3;

		enum class State : uint8_t { Idle, Sending, Done, Failed, Cancelled };

		struct Progress
		{
			State state = State::Idle;
			uint8_t slot = 0;
			size_t sent = 0;			// packets the machine took
			size_t total = 0;
			bool handshake = true;		// false: open loop
			uint32_t retries = 0;
			std::string text;			// what happened, in plain words (Done, Failed, Cancelled)
		};

		void start(uint8_t _slot, elektronData::MdSdsDump _dump, double _nowMs, const Send& _send);
		// A message from the machine: true when it was this transfer's reply.
		bool onReply(const Bytes& _message, double _nowMs, const Send& _send);
		// Timeouts and the open-loop pace.
		void pump(double _nowMs, const Send& _send);
		// The user stopped it: CANCEL to the machine.
		void cancel(const Send& _send);

		bool active() const { return m_progress.state == State::Sending; }
		const Progress& progress() const { return m_progress; }

	private:
		enum class Waiting : uint8_t { Header, Packet };

		void sendCurrent(double _nowMs, const Send& _send);
		void next(double _nowMs, const Send& _send);
		void finish(State _state, std::string _text);
		uint8_t packetNumber() const { return static_cast<uint8_t>(m_packet & 0x7f); }

		elektronData::MdSdsDump m_dump;
		Progress m_progress;
		Waiting m_waiting = Waiting::Header;
		size_t m_packet = 0;			// the packet on its way (Waiting::Packet)
		uint32_t m_packetRetries = 0;
		bool m_held = false;			// WAIT
		double m_sentMs = 0;
	};
}
