#pragma once

#include "elektronData/json.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mdJucePlugin::pageBridge
{
	// The page bridge's transport on the plug-in's side (DESIGN-REVIEW-2026-10-02 finding 13), pure: the one place
	// that knows how a message travels between the page and the plug-in, so JUCE 8's native bridge replaces this
	// file only (the page's half is BridgeTransport in skins/shared/deskBridge.js).
	//   page -> plug-in: a navigation to gmbridge://c/<escaped JSON batch>, or a batch too long for one URL in
	//     pieces, gmbridge://p/<seq>/<i>/<n>/<escaped piece> (cut between escapes, so they are joined before they
	//     are decoded); gmbridge://log/<escaped text> is a line for the log.
	//   plug-in -> page: javascript:window.gm&&gm.recv([...],<seq>); an outbox longer than g_maxRecvBytes goes as
	//     several calls, split at message boundaries (one message longer than that goes alone), in order. Each call
	//     has the next batch number: JUCE 7 keeps the last URL it went to, javascript: ones too, and goes to it again
	//     when the view is shown again (reloadLastURL), so the page drops a batch whose number it has already had
	//     (release review 2026-10-04, S5).
	//
	//   Linux (doc/release/LINUX.md): webkit2gtk's web process crashes (2.50, a null read) when a javascript: URL is
	//   loaded while the page's bridge iframes navigate, so there plug-in -> page goes as files instead: each call's
	//   script (the same gm.recv([...],<seq>) without "javascript:") is written beside the page file as
	//   <page file>.recv-<seq>.js (recvFileName), and the page, loaded with ?recv=file, loads them in order with
	//   <script> tags, polling for the next one. It says how far it got with gmbridge://a/<seq> (now and then; the
	//   plug-in then deletes those files), and gmbridge://a/0 when it starts (a page that started again reads from 1).
	//   The page waits for batch n before it reads n + 1, so the files are written strictly in order (FileOutbox): one
	//   that could not be written holds back every later one until it is.
	constexpr const char* g_command = "gmbridge://c/";
	constexpr const char* g_piece = "gmbridge://p/";
	constexpr const char* g_log = "gmbridge://log/";
	constexpr const char* g_ack = "gmbridge://a/";
	constexpr const char* g_javascriptUrl = "javascript:";
	constexpr const char* g_fileRecvQuery = "recv";	// ?recv=file
	constexpr size_t g_maxRecvBytes = 1 << 20;

	// The file the page loads for batch _seq (beside the page file, named after it).
	inline std::string recvFileName(const std::string& _pageFileName, const uint64_t _seq)
	{
		return _pageFileName + ".recv-" + std::to_string(_seq) + ".js";
	}

	// gmbridge://a/<seq>: the last batch the page has read, or nothing when _url is not one.
	inline std::optional<uint64_t> ackOf(const std::string& _url)
	{
		if(_url.rfind(g_ack, 0) != 0)
			return std::nullopt;
		const auto text = _url.substr(std::char_traits<char>::length(g_ack));
		if(text.empty() || text.find_first_not_of("0123456789") != std::string::npos || text.size() > 19)
			return std::nullopt;
		return std::stoull(text);
	}

	inline bool startsWith(const std::string& _s, const char* _prefix)
	{
		return _s.rfind(_prefix, 0) == 0;
	}

	// The pieces of the page's long batches, joined. A batch whose pieces never all come is dropped once a few
	// newer batches have come.
	class Pieces
	{
	public:
		// _url is a gmbridge://p/ URL. The whole batch (still escaped) once its last piece is in.
		std::optional<std::string> add(const std::string& _url)
		{
			if(!startsWith(_url, g_piece))
				return std::nullopt;
			size_t pos = std::char_traits<char>::length(g_piece);
			long fields[3];
			for(auto& f : fields)
			{
				const auto slash = _url.find('/', pos);
				if(slash == std::string::npos)
					return std::nullopt;
				char* end = nullptr;
				const auto text = _url.substr(pos, slash - pos);
				f = std::strtol(text.c_str(), &end, 10);
				if(text.empty() || *end || f < 0)
					return std::nullopt;
				pos = slash + 1;
			}
			const auto seq = fields[0], index = fields[1], count = fields[2];
			if(count < 1 || index >= count)
				return std::nullopt;
			auto& batch = m_batches[seq];
			batch.count = static_cast<size_t>(count);
			batch.pieces[static_cast<size_t>(index)] = _url.substr(pos);
			while(m_batches.size() > g_keep)
				m_batches.erase(m_batches.begin());
			const auto it = m_batches.find(seq);
			if(it == m_batches.end() || it->second.pieces.size() < it->second.count)
				return std::nullopt;
			std::string joined;
			for(const auto& [i, p] : it->second.pieces)
				joined += p;
			m_batches.erase(it);
			return joined;
		}

		size_t waiting() const { return m_batches.size(); }

	private:
		static constexpr size_t g_keep = 4;
		struct Batch
		{
			size_t count = 0;
			std::map<size_t, std::string> pieces;
		};
		std::map<long, Batch> m_batches;
	};

	// Linux: the batches' script files written strictly in order (the page reads batch n + 1 only after batch n). A script
	// that could not be written (a full disk for a moment) waits with every later one, and each pump tries again from the
	// first; only a written one is the page's (the writer registers it, for deletion once the page has read it). A
	// backlog past g_maxBacklogBytes, or one that has not moved for g_maxStallMs, is dropped: the page could never read
	// past the gap, so it is loaded again once a file can be written (resync; it starts over with a/0 and says ready,
	// and the session sends everything once more). Pure: the writer and the clock are the caller's.
	class FileOutbox
	{
	public:
		static constexpr size_t g_maxBacklogBytes = 8 << 20;
		static constexpr double g_maxStallMs = 2000.0;

		// _write(seq, script): true when the page can read the batch now (its file is there, whole)
		using Write = std::function<bool(uint64_t, const std::string&)>;

		// What a pump changed, for the log (a stall is said once, not on every try)
		enum class Change
		{
			None,
			Stalled,	// a write failed and nothing failed before: it waits (stalledAt) with what follows
			Recovered,	// every waiting batch is written now
			Dropped		// the backlog was too large or too old: dropped, resync() is set
		};

		explicit FileOutbox(Write _write) : m_write(std::move(_write)) {}

		// _scripts numbered from _firstSeq (recvScripts), behind what waits. Nothing while a resync is due: the page
		// that is loaded again gets everything anew.
		void add(const uint64_t _firstSeq, std::vector<std::string> _scripts)
		{
			if(m_resync)
				return;
			for(size_t i = 0; i < _scripts.size(); ++i)
			{
				m_bytes += _scripts[i].size();
				m_waiting.push_back({_firstSeq + i, std::move(_scripts[i])});
			}
		}

		// Writes what waits, in order, until a write fails. _nowMs: the caller's clock (how long a stall lasts).
		Change pump(const double _nowMs)
		{
			bool moved = false;
			while(!m_waiting.empty())
			{
				const auto& next = m_waiting.front();
				if(m_write(next.seq, next.script))
				{
					m_bytes -= next.script.size();
					m_waiting.pop_front();
					moved = true;
					continue;
				}
				const bool first = !m_stalledSince;
				if(first || moved)
					m_stalledSince = _nowMs;	// a stall lasts from the last batch that went
				if(m_bytes <= g_maxBacklogBytes && _nowMs - *m_stalledSince <= g_maxStallMs)
					return first ? Change::Stalled : Change::None;
				m_dropped = {m_waiting.size(), m_bytes};
				m_waiting.clear();
				m_bytes = 0;
				m_stalledSince.reset();
				m_resync = true;
				return Change::Dropped;
			}
			if(!m_stalledSince)
				return Change::None;
			m_stalledSince.reset();
			return Change::Recovered;
		}

		// The page started again (a/0): what waits was the old page's, the new one reads from batch 1.
		void restart()
		{
			m_waiting.clear();
			m_bytes = 0;
			m_stalledSince.reset();
			m_resync = false;
		}

		bool resync() const { return m_resync; }
		size_t waiting() const { return m_waiting.size(); }
		size_t bytes() const { return m_bytes; }
		// The batch that could not be written, while one waits.
		std::optional<uint64_t> stalledAt() const { return m_stalledSince && !m_waiting.empty() ? std::optional<uint64_t>(m_waiting.front().seq) : std::nullopt; }
		// The last drop: how many batches and bytes went.
		std::pair<size_t, size_t> dropped() const { return m_dropped; }

	private:
		struct Script
		{
			uint64_t seq;
			std::string script;
		};
		Write m_write;
		std::deque<Script> m_waiting;
		size_t m_bytes = 0;
		std::optional<double> m_stalledSince;
		bool m_resync = false;
		std::pair<size_t, size_t> m_dropped;
	};

	// Linux, after a drop (FileOutbox::resync): the page is loaded again only once none of the old page's batch files
	// is left (_deleteOldBatches) and its own file could be written (_writePage). The page that starts reads batch 1 at
	// once, before the plug-in has its a/0: an old file 1..k would pass for the new numbering's, and the page would
	// then wait for k + 1 and skip the new 1..k. Pure: the steps are the caller's. True when it was loaded again.
	inline bool reloadAfterDrop(const std::function<bool()>& _deleteOldBatches, const std::function<bool()>& _writePage,
		const std::function<void()>& _reload)
	{
		if(!_deleteOldBatches() || !_writePage())
			return false;
		_reload();
		return true;
	}

	// The outbox as the scripts that hand it to the page, in order, numbered from _firstSeq (one number a script;
	// the caller's next batch is _firstSeq + the number of scripts).
	// _prefix: "javascript:" for URLs, "" for the Linux script files.
	inline std::vector<std::string> recvScripts(const std::vector<elektronData::json::Value>& _outbox, const uint64_t _firstSeq,
		const size_t _maxBytes = g_maxRecvBytes, const std::string& _prefix = g_javascriptUrl)
	{
		std::vector<std::string> scripts;
		std::string batch;
		const auto close = [&]
		{
			if(batch.empty())
				return;
			scripts.push_back(_prefix + "window.gm&&gm.recv([" + batch + "]," + std::to_string(_firstSeq + scripts.size()) + ")");
			batch.clear();
		};
		for(const auto& m : _outbox)
		{
			const auto text = elektronData::json::write(m);
			if(!batch.empty() && batch.size() + 1 + text.size() > _maxBytes)
				close();
			batch += (batch.empty() ? "" : ",") + text;
		}
		close();
		return scripts;
	}
}
