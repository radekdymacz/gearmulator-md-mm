#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <vector>

namespace deskCore
{
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
		// A dump after a dump waits for the settle too (true), or only until the machine has read the one before
		// (false: dumps back to back at the machine's read speed, as md::Hardware feeds them; a request still waits
		// for the settle after the last one).
		bool settleBetweenDumps = true;

		double wireMs(const size_t _bytes) const { return bytesPerSecond > 0 ? double(_bytes) * 1000.0 / bytesPerSecond : 0.0; }
		// 0.3.4: the same stream while the machine's sequencer stands. No audio timing to protect, so no cable:
		// dumps go back to back as fast as the machine reads them (as md::Hardware feeds them, 125 KB/s), still
		// nothing else over a dump it applies (a request waits for the read and the settle), still the newest dump
		// of a document wins while it waits; values unbudgeted, value SysEx at once. Measured (mdDeskFirmwareTest
		// syximport, a full backup's 128 patterns while stopped): 13.7 s, every pattern in the firmware as in the
		// file; with the settle between dumps too, 46.9 s and the same result.
		StreamPolicy stopped() const
		{
			if(bytesPerSecond <= 0)
				return *this;
			StreamPolicy s = *this;
			s.bytesPerSecond = ingestBytesPerSecond > 0 ? ingestBytesPerSecond : bytesPerSecond;
			s.valueBytesPerSecond = 0;
			s.latestIntervalMs = 0;
			s.settleBetweenDumps = false;
			return s;
		}
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
	// Two paces (0.3.4): the policy given (a MIDI cable's) while the machine plays, StreamPolicy::stopped() while
	// it stands (setPlaying). A transfer that is under way when play starts goes on at cable speed; when play
	// stops, what waits goes at the fast pace. Starts in the playing pace (the safe one) until told.
	// Pure: the caller gives the time, whether a sample is active and whether the machine plays.
	class Stream
	{
	public:
		using Bytes = std::vector<uint8_t>;
		using Send = std::function<void(const Bytes&)>;

		explicit Stream(Send _wire = {}, StreamPolicy _policy = {})
			: m_wire(std::move(_wire)), m_playingPolicy(_policy), m_stoppedPolicy(_policy.stopped()), m_policy(_policy)
		{
			m_tokens = m_policy.valueBurstBytes;
		}

		// The engine can send SysEx at all.
		bool open() const { return static_cast<bool>(m_wire); }
		// The pace in force now (the cable's while playing).
		const StreamPolicy& policy() const { return m_policy; }
		bool playing() const { return m_playing; }

		// The machine's sequencer started or stopped: the pace changes from now on.
		void setPlaying(const bool _playing, const double _nowMs)
		{
			if(_playing == m_playing)
				return;
			m_playing = _playing;
			m_policy = _playing ? m_playingPolicy : m_stoppedPolicy;
			if(_playing)
			{
				// values start with a full burst at cable speed
				m_tokens = m_policy.valueBurstBytes;
				m_tokensAtMs = _nowMs;
			}
			else if(m_bulkFreeAtMs > _nowMs && m_policy.bytesPerSecond > 0)
			{
				// the dump on the "wire" is already with the machine: its remaining cable time is not waited for
				m_bulkFreeAtMs = std::min(m_bulkFreeAtMs, std::max(_nowMs, m_readAtMs));
			}
			pump(_nowMs);
		}

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
			if(_sampleActive || m_playingPolicy.latestIntervalMs <= 0)
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
			m_bulkFreeAtMs = m_settledAtMs = m_readAtMs = 0;
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
				if(!paced())
					return true;
				if(!isBulk(_i.bytes))
					return _nowMs >= m_settledAtMs;
				return _nowMs >= m_bulkFreeAtMs && _nowMs >= (m_policy.settleBetweenDumps ? m_settledAtMs : m_readAtMs);
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
					m_readAtMs = std::max(m_readAtMs, _nowMs) + m_policy.ingestMs(_i.bytes.size());
					m_settledAtMs = m_readAtMs + m_policy.settleMs;
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
		StreamPolicy m_playingPolicy;
		StreamPolicy m_stoppedPolicy;
		StreamPolicy m_policy;		// the one in force
		bool m_playing = true;
		std::deque<Bytes> m_held;
		std::deque<Item> m_queue;
		double m_bulkFreeAtMs = 0;
		double m_settledAtMs = 0;
		double m_readAtMs = 0;
		std::map<int, double> m_latestAtMs;
		double m_tokens = 0;
		double m_tokensAtMs = 0;
		size_t m_coalesced = 0;
	};
}
