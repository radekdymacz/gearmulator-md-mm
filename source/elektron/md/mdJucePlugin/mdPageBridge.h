#pragma once

#include "elektronData/json.h"

#include <cstddef>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mdJucePlugin::pageBridge
{
	// The page bridge's transport on the plug-in's side (DESIGN-REVIEW-2026-10-02 finding 13), pure: the one place
	// that knows how a message travels between the page and the plug-in, so JUCE 8's native bridge replaces this
	// file only (the page's half is BridgeTransport in skins/shared/deskBridge.js).
	//   page -> plug-in: a navigation to gmbridge://c/<escaped JSON batch>, or a batch too long for one URL in
	//     pieces, gmbridge://p/<seq>/<i>/<n>/<escaped piece> (cut between escapes, so they are joined before they
	//     are decoded); gmbridge://log/<escaped text> is a line for the log.
	//   plug-in -> page: javascript:window.gm&&gm.recv([...]); an outbox longer than g_maxRecvBytes goes as several
	//     calls, split at message boundaries (one message longer than that goes alone), in order.
	constexpr const char* g_command = "gmbridge://c/";
	constexpr const char* g_piece = "gmbridge://p/";
	constexpr const char* g_log = "gmbridge://log/";
	constexpr size_t g_maxRecvBytes = 1 << 20;

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

	// The outbox as the scripts that hand it to the page, in order.
	inline std::vector<std::string> recvScripts(const std::vector<elektronData::json::Value>& _outbox, const size_t _maxBytes = g_maxRecvBytes)
	{
		std::vector<std::string> scripts;
		std::string batch;
		const auto close = [&]
		{
			if(batch.empty())
				return;
			scripts.push_back("javascript:window.gm&&gm.recv([" + batch + "])");
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
