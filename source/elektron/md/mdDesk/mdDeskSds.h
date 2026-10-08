#pragma once

#include "elektronData/mdSamples.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
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
	// B-014: how fast the editor's messages may go to the machine, as a real MIDI cable would carry them. A
	// bulk message (a dump: at least g_bulkBytes) takes bytesPerSecond's time on the "wire": the next
	// bulk waits until it has passed. Any message after a bulk one waits settleMs more than the
	// machine takes to read it (ingestBytesPerSecond), so a request (a read-back) never reaches the
	// firmware while it still applies a dump. Values (CCs) share a budget of valueBytesPerSecond with a burst
	// of valueBurstBytes (a whole-kit randomise is spread over a fraction of a second, the newest value of a
	// parameter wins meanwhile); a value-setting SysEx (tempo, an LFO, a master effect) goes at most every
	// latestIntervalMs per what it sets (the newest wins). bytesPerSecond 0: no pacing (every message at once).
	struct StreamPolicy
	{
		static constexpr size_t g_bulkBytes = 256;
		double bytesPerSecond = 0;
		double ingestBytesPerSecond = 0;	// how fast the machine reads (0: as bytesPerSecond)
		double settleMs = 0;
		double valueBytesPerSecond = 0;		// 0: values are not budgeted
		double valueBurstBytes = 0;
		double latestIntervalMs = 0;

		double wireMs(const size_t _bytes) const { return bytesPerSecond > 0 ? double(_bytes) * 1000.0 / bytesPerSecond : 0.0; }
		double ingestMs(const size_t _bytes) const
		{
			const double rate = ingestBytesPerSecond > 0 ? ingestBytesPerSecond : bytesPerSecond;
			return rate > 0 ? double(_bytes) * 1000.0 / rate : 0.0;
		}
	};

	// P9, B-014: the one way the editor's messages leave the adapter for the machine: its single choke point. A
	// sample transfer's own packets (sample) go straight out: nothing may come between them. Everything else goes
	// through the stream (StreamPolicy), in order, in lanes:
	//   send     SysEx (held while a sample is on its way). A bulk message waits until the one before it has had
	//            its time on the wire, and a newer dump of the same document replaces one still waiting (latest
	//            wins); anything after a bulk message waits until the machine has read and applied it.
	//   latest   a value-setting SysEx (tempo, LFO, master effect...): at most one every latestIntervalMs per key,
	//            the newest waiting value replaces an older one.
	//   value    a CC (a kit value, a mute): the value budget; the newest value of a key replaces a waiting one.
	//   priority notes and a key's held value: never wait for the value budget (they pass waiting values), but
	//            keep their place after SysEx.
	//   after    work that must follow what was sent before it (the working kit's edits after a pattern dump).
	// Pure: the caller gives the time and whether a sample is active.
	class SysexOut
	{
	public:
		using Bytes = SdsSender::Bytes;
		using Send = SdsSender::Send;

		explicit SysexOut(Send _wire = {}, StreamPolicy _policy = {}) : m_wire(std::move(_wire)), m_policy(_policy)
		{
			m_tokens = m_policy.valueBurstBytes;
		}

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
			Item i;
			i.lane = Lane::Sysex;
			i.bytes = _message;
			enqueue(std::move(i));
			pump(_nowMs);
		}

		// A SysEx that sets one value (_key: what it sets): the newest goes, at most one every latestIntervalMs.
		void sendLatest(const int _key, const Bytes& _message, const bool _sampleActive, const double _nowMs)
		{
			if(!m_wire)
				return;
			if(_sampleActive || m_policy.latestIntervalMs <= 0)
			{
				send(_message, _sampleActive, _nowMs);
				return;
			}
			Item i;
			i.lane = Lane::Latest;
			i.key = _key;
			i.bytes = _message;
			enqueue(std::move(i));
			pump(_nowMs);
		}

		// A CC (_key: the parameter it sets, _bytes its size): the value budget, the newest value of a key wins.
		void value(const int _key, const size_t _bytes, std::function<void()> _send, const double _nowMs)
		{
			Item i;
			i.lane = Lane::Value;
			i.key = _key;
			i.cost = _bytes;
			i.then = std::move(_send);
			enqueue(std::move(i));
			pump(_nowMs);
		}

		// A note (or a key's held value): first, past waiting values, after waiting SysEx.
		void priority(std::function<void()> _send, const double _nowMs)
		{
			Item i;
			i.lane = Lane::Priority;
			i.then = std::move(_send);
			enqueue(std::move(i));
			pump(_nowMs);
		}

		// _then runs once everything sent before it has gone (at once when nothing waits): it keeps its place after
		// them on the way to the machine.
		void after(std::function<void()> _then, const double _nowMs)
		{
			Item i;
			i.lane = Lane::Then;
			i.then = std::move(_then);
			enqueue(std::move(i));
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
				Item i;
				i.lane = Lane::Sysex;
				i.bytes = std::move(m_held.front());
				enqueue(std::move(i));
				m_held.pop_front();
			}
			pump(_nowMs);
		}

		// What may go by _nowMs goes.
		void pump(const double _nowMs)
		{
			if(m_policy.valueBytesPerSecond > 0)
			{
				m_tokens = std::min(m_policy.valueBurstBytes, m_tokens + std::max(0.0, _nowMs - m_tokensAtMs) * m_policy.valueBytesPerSecond / 1000.0);
				m_tokensAtMs = _nowMs;
			}
			while(!m_queue.empty())
			{
				if(!mayGo(m_queue.front(), _nowMs))
				{
					// values wait for the budget: what has priority behind them goes (up to the next SysEx)
					if(m_queue.front().lane == Lane::Value)
						passValues(_nowMs);
					return;
				}
				auto taken = std::move(m_queue.front());
				m_queue.pop_front();
				go(taken, _nowMs);
			}
		}

		size_t held() const { return m_held.size(); }
		// Messages waiting their turn (not the held ones).
		size_t waiting() const { return m_queue.size(); }
		size_t waitingBytes() const
		{
			size_t n = 0;
			for(const auto& i : m_queue)
				n += i.bytes.size() + i.cost;
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
				if(i.lane != Lane::Sysex || !isBulk(i.bytes))
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
			m_latestAtMs.clear();
			m_tokens = m_policy.valueBurstBytes;
		}

		static bool isBulk(const Bytes& _m) { return _m.size() >= StreamPolicy::g_bulkBytes; }
		// A dump's document: its message id and slot (Elektron: F0 00 20 3C <product> 00 <id> <ver> <rev> <slot>).
		static int documentKey(const Bytes& _m)
		{
			if(!isBulk(_m) || _m[0] != 0xf0 || _m[1] != 0x00 || _m[2] != 0x20 || _m[3] != 0x3c)
				return -1;
			return (_m[6] << 8) | _m[9];
		}

		// What was replaced while it waited (latest wins): dumps, value SysEx, values. For tests and diagnostics.
		size_t coalesced() const { return m_coalesced; }

	private:
		enum class Lane : uint8_t { Sysex, Latest, Value, Priority, Then };
		struct Item
		{
			Lane lane = Lane::Sysex;
			int key = -1;
			Bytes bytes;
			std::function<void()> then;
			size_t cost = 0;
		};

		bool paced() const { return m_policy.bytesPerSecond > 0; }

		bool mayGo(const Item& _i, const double _nowMs) const
		{
			switch(_i.lane)
			{
			case Lane::Sysex:
				return !paced() || (_nowMs >= m_settledAtMs && (!isBulk(_i.bytes) || _nowMs >= m_bulkFreeAtMs));
			case Lane::Latest:
			{
				if(paced() && _nowMs < m_settledAtMs)
					return false;
				const auto it = m_latestAtMs.find(_i.key);
				return it == m_latestAtMs.end() || _nowMs >= it->second + m_policy.latestIntervalMs;
			}
			case Lane::Value:
				return m_policy.valueBytesPerSecond <= 0 || m_tokens >= double(_i.cost);
			case Lane::Priority:
			case Lane::Then:
				return true;
			}
			return true;
		}

		void go(Item& _i, const double _nowMs)
		{
			switch(_i.lane)
			{
			case Lane::Sysex:
				if(paced() && isBulk(_i.bytes))
				{
					m_bulkFreeAtMs = std::max(m_bulkFreeAtMs, _nowMs) + m_policy.wireMs(_i.bytes.size());
					m_settledAtMs = _nowMs + m_policy.ingestMs(_i.bytes.size()) + m_policy.settleMs;
				}
				m_wire(_i.bytes);
				break;
			case Lane::Latest:
				m_latestAtMs[_i.key] = _nowMs;
				m_wire(_i.bytes);
				break;
			case Lane::Value:
				if(m_policy.valueBytesPerSecond > 0)
					m_tokens -= double(_i.cost);
				_i.then();
				break;
			case Lane::Priority:
			case Lane::Then:
				_i.then();
				break;
			}
		}

		// The front waits for the value budget: priority items behind the waiting values go now.
		void passValues(const double _nowMs)
		{
			for(auto it = m_queue.begin(); it != m_queue.end();)
			{
				if(it->lane == Lane::Value)
				{
					++it;
					continue;
				}
				if(it->lane != Lane::Priority)
					return;
				auto taken = std::move(*it);
				it = m_queue.erase(it);
				go(taken, _nowMs);
			}
		}

		void enqueue(Item _item)
		{
			// latest wins: a newer dump of a document replaces one still waiting (nothing queued behind
			// the old one may depend on it but its own after-work, which stays in place)
			if(_item.lane == Lane::Sysex && paced())
			{
				if(const int key = documentKey(_item.bytes); key >= 0)
				{
					for(auto& i : m_queue)
					{
						if(i.lane == Lane::Sysex && documentKey(i.bytes) == key)
						{
							i.bytes = std::move(_item.bytes);
							++m_coalesced;
							return;
						}
					}
				}
			}
			// a value (CC or value SysEx) replaces the waiting one of its key, unless SysEx or after-work lies between
			if(_item.lane == Lane::Value || _item.lane == Lane::Latest)
			{
				for(auto it = m_queue.rbegin(); it != m_queue.rend(); ++it)
				{
					if(it->lane == Lane::Sysex || it->lane == Lane::Then)
						break;
					if(it->lane == _item.lane && it->key == _item.key)
					{
						it->bytes = std::move(_item.bytes);
						it->then = std::move(_item.then);
						++m_coalesced;
						return;
					}
				}
			}
			m_queue.push_back(std::move(_item));
		}

		Send m_wire;
		StreamPolicy m_policy;
		std::deque<Bytes> m_held;
		std::deque<Item> m_queue;
		double m_bulkFreeAtMs = 0;
		double m_settledAtMs = 0;
		std::map<int, double> m_latestAtMs;
		double m_tokens = 0;
		double m_tokensAtMs = 0;
		size_t m_coalesced = 0;
	};
}
