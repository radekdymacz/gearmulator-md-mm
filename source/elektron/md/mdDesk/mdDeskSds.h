#pragma once

#include "elektronData/mdSamples.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
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
	// B-014: how fast the editor's SysEx may go to the machine, as a real MIDI cable would carry it. A
	// bulk message (a dump: at least g_bulkBytes) takes bytesPerSecond's time on the "wire": the next
	// bulk waits until it has passed. Any message after a bulk one waits settleMs more than the
	// machine takes to read it (ingestBytesPerSecond), so a request (a read-back) never reaches the
	// firmware while it still applies a dump. bytesPerSecond 0: no pacing (every message at once).
	struct StreamPolicy
	{
		static constexpr size_t g_bulkBytes = 256;
		double bytesPerSecond = 0;
		double ingestBytesPerSecond = 0;	// how fast the machine reads (0: as bytesPerSecond)
		double settleMs = 0;

		double wireMs(const size_t _bytes) const { return bytesPerSecond > 0 ? double(_bytes) * 1000.0 / bytesPerSecond : 0.0; }
		double ingestMs(const size_t _bytes) const
		{
			const double rate = ingestBytesPerSecond > 0 ? ingestBytesPerSecond : bytesPerSecond;
			return rate > 0 ? double(_bytes) * 1000.0 / rate : 0.0;
		}
	};

	// P9, B-014: the one way SysEx leaves the adapter: the editor's single choke point to the machine. A
	// sample transfer's own packets (sample) go straight out: nothing may come between them. Any other
	// SysEx (send) is held while a sample is on its way, and otherwise goes through the stream in order
	// (StreamPolicy): a bulk message waits until the one before it has had its time on the wire, and a
	// newer dump of the same document (the same message id and slot) replaces one still waiting (latest
	// wins); anything after a bulk message waits until the machine has read and applied it. Work that
	// must follow what waits (after) is queued with it. Live values (CCs, notes, panel keys) do not come
	// here: they go first. Pure: the caller gives the time (send, pump) and whether a sample is active.
	class SysexOut
	{
	public:
		using Bytes = SdsSender::Bytes;
		using Send = SdsSender::Send;

		explicit SysexOut(Send _wire = {}, StreamPolicy _policy = {}) : m_wire(std::move(_wire)), m_policy(_policy) {}

		// The engine can send SysEx at all.
		bool open() const { return static_cast<bool>(m_wire); }
		const StreamPolicy& policy() const { return m_policy; }

		// Any SysEx but the sample's.
		void send(const Bytes& _message, const bool _sampleActive, const double _nowMs)
		{
			if(!m_wire)
				return;
			if(_sampleActive)
			{
				m_held.push_back(_message);
				return;
			}
			enqueue(Item{_message, {}});
			pump(_nowMs);
		}

		// _then runs once everything sent before it has gone (at once when nothing waits): it keeps its place after
		// them on the way to the machine.
		void after(std::function<void()> _then, const double _nowMs)
		{
			if(m_queue.empty() && m_held.empty())
			{
				_then();
				return;
			}
			m_queue.push_back(Item{{}, std::move(_then)});
			pump(_nowMs);
		}

		// The sample transfer's own packets.
		void sample(const Bytes& _message) const
		{
			if(m_wire)
				m_wire(_message);
		}

		// The sample is over: what was held goes into the stream, in order.
		void release(const bool _sampleActive, const double _nowMs)
		{
			if(_sampleActive || m_held.empty())
				return;
			while(!m_held.empty())
			{
				enqueue(Item{std::move(m_held.front()), {}});
				m_held.pop_front();
			}
			pump(_nowMs);
		}

		// What may go by _nowMs goes.
		void pump(const double _nowMs)
		{
			while(!m_queue.empty())
			{
				auto& item = m_queue.front();
				if(!item.then)
				{
					const bool bulk = m_policy.bytesPerSecond > 0 && isBulk(item.bytes);
					if(_nowMs < m_settledAtMs || (bulk && _nowMs < m_bulkFreeAtMs))
						return;
					if(bulk)
					{
						m_bulkFreeAtMs = std::max(m_bulkFreeAtMs, _nowMs) + m_policy.wireMs(item.bytes.size());
						m_settledAtMs = _nowMs + m_policy.ingestMs(item.bytes.size()) + m_policy.settleMs;
						m_lastBulkMs = _nowMs;
					}
				}
				auto taken = std::move(item);
				m_queue.pop_front();
				if(taken.then)
					taken.then();
				else
					m_wire(taken.bytes);
			}
		}

		size_t held() const { return m_held.size(); }
		// Messages waiting their turn (not the held ones).
		size_t waiting() const { return m_queue.size(); }
		size_t waitingBytes() const
		{
			size_t n = 0;
			for(const auto& i : m_queue)
				n += i.bytes.size();
			return n;
		}
		// "Sending": something waits its turn, or the machine still reads or applies a dump.
		bool sending(const double _nowMs) const { return !m_queue.empty() || _nowMs < m_settledAtMs; }
		// When a bulk message may go next (the wire is free).
		double bulkFreeAtMs() const { return m_bulkFreeAtMs; }
		// How long a message sent now would wait: for what queues before it, and the dump on the wire.
		double delayMs(const double _nowMs) const
		{
			if(m_policy.bytesPerSecond <= 0)
				return 0;
			double t = std::max({_nowMs, m_settledAtMs});
			double bulkFree = m_bulkFreeAtMs;
			for(const auto& i : m_queue)
			{
				if(!isBulk(i.bytes))
					continue;
				const double start = std::max(t, bulkFree);
				bulkFree = start + m_policy.wireMs(i.bytes.size());
				t = start + m_policy.ingestMs(i.bytes.size()) + m_policy.settleMs;
			}
			return t - _nowMs;
		}
		// The machine started over: nothing waits any more.
		void clear()
		{
			m_queue.clear();
			m_bulkFreeAtMs = m_settledAtMs = 0;
		}

		static bool isBulk(const Bytes& _m) { return _m.size() >= StreamPolicy::g_bulkBytes; }
		// A dump's document: its message id and slot (Elektron: F0 00 20 3C <product> 00 <id> <ver> <rev> <slot>).
		static int documentKey(const Bytes& _m)
		{
			if(!isBulk(_m) || _m[0] != 0xf0 || _m[1] != 0x00 || _m[2] != 0x20 || _m[3] != 0x3c)
				return -1;
			return (_m[6] << 8) | _m[9];
		}

	private:
		struct Item
		{
			Bytes bytes;
			std::function<void()> then;
		};

		void enqueue(Item _item)
		{
			// latest wins: a newer dump of a document replaces one still waiting (nothing queued behind
			// the old one may depend on it but its own after-work, which stays in place)
			if(const int key = documentKey(_item.bytes); key >= 0 && m_policy.bytesPerSecond > 0)
			{
				for(auto& i : m_queue)
				{
					if(!i.then && documentKey(i.bytes) == key)
					{
						i.bytes = std::move(_item.bytes);
						++m_coalesced;
						return;
					}
				}
			}
			m_queue.push_back(std::move(_item));
		}

	public:
		// Dumps replaced while they waited (latest wins), for tests and diagnostics.
		size_t coalesced() const { return m_coalesced; }

	private:
		Send m_wire;
		StreamPolicy m_policy;
		std::deque<Bytes> m_held;
		std::deque<Item> m_queue;
		double m_bulkFreeAtMs = 0;
		double m_settledAtMs = 0;
		double m_lastBulkMs = -1e18;
		size_t m_coalesced = 0;
	};
}
