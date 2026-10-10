#include "updateCore.h"
#include "updateKey.h"

#include "elektronData/json.h"

#include "monocypher/monocypher-ed25519.h"

#include <algorithm>
#include <cstring>

namespace mdmmUpdate
{
	namespace json = elektronData::json;

	// ---- versions ----

	namespace
	{
		bool parseNumber(const std::string& _s, size_t& _pos, uint32_t& _out)
		{
			const size_t start = _pos;
			uint64_t v = 0;
			while(_pos < _s.size() && _s[_pos] >= '0' && _s[_pos] <= '9')
			{
				v = v * 10 + static_cast<uint64_t>(_s[_pos] - '0');
				if(v > 0xffffffffull || _pos - start >= 9)
					return false;
				++_pos;
			}
			if(_pos == start)
				return false;
			_out = static_cast<uint32_t>(v);
			return true;
		}
	}

	std::optional<Version> parseVersion(const std::string& _text)
	{
		Version v;
		size_t pos = 0;
		if(!parseNumber(_text, pos, v.major) || pos >= _text.size() || _text[pos++] != '.')
			return {};
		if(!parseNumber(_text, pos, v.minor) || pos >= _text.size() || _text[pos++] != '.')
			return {};
		if(!parseNumber(_text, pos, v.patch) || pos != _text.size())
			return {};
		return v;
	}

	int compare(const Version& _a, const Version& _b)
	{
		if(_a.major != _b.major)
			return _a.major < _b.major ? -1 : 1;
		if(_a.minor != _b.minor)
			return _a.minor < _b.minor ? -1 : 1;
		if(_a.patch != _b.patch)
			return _a.patch < _b.patch ? -1 : 1;
		return 0;
	}

	std::string toString(const Version& _v)
	{
		return std::to_string(_v.major) + "." + std::to_string(_v.minor) + "." + std::to_string(_v.patch);
	}

	// ---- names ----

	const char* osKey(const Os _os)
	{
		switch(_os)
		{
		case Os::Mac: return "mac";
		case Os::Win: return "win";
		case Os::Linux: return "linux";
		}
		return "";
	}

	const char* machineKey(const Machine _m)
	{
		return _m == Machine::Md ? "md" : "mm";
	}

	const char* assetName(const Os _os, const Machine _m)
	{
		const bool md = _m == Machine::Md;
		switch(_os)
		{
		case Os::Mac: return md ? "Machinedrum-Editor-macOS.pkg" : "Monomachine-Editor-macOS.pkg";
		case Os::Win: return md ? "Machinedrum-Editor-Windows-x64-not-tested.zip" : "Monomachine-Editor-Windows-x64-not-tested.zip";
		case Os::Linux: return md ? "Machinedrum-Editor-Linux-x64-not-tested.tar.gz" : "Monomachine-Editor-Linux-x64-not-tested.tar.gz";
		}
		return "";
	}

	// ---- hex and base64 ----

	namespace
	{
		int hexDigit(const char _c)
		{
			if(_c >= '0' && _c <= '9') return _c - '0';
			if(_c >= 'a' && _c <= 'f') return _c - 'a' + 10;
			if(_c >= 'A' && _c <= 'F') return _c - 'A' + 10;
			return -1;
		}

		template<size_t N> std::optional<std::array<uint8_t, N>> fromHex(const std::string& _hex, const bool _lowerOnly)
		{
			if(_hex.size() != N * 2)
				return {};
			std::array<uint8_t, N> out{};
			for(size_t i = 0; i < N; ++i)
			{
				const char hi = _hex[i * 2], lo = _hex[i * 2 + 1];
				if(_lowerOnly && ((hi >= 'A' && hi <= 'F') || (lo >= 'A' && lo <= 'F')))
					return {};
				const int a = hexDigit(hi), b = hexDigit(lo);
				if(a < 0 || b < 0)
					return {};
				out[i] = static_cast<uint8_t>(a * 16 + b);
			}
			return out;
		}

		int base64Digit(const char _c)
		{
			if(_c >= 'A' && _c <= 'Z') return _c - 'A';
			if(_c >= 'a' && _c <= 'z') return _c - 'a' + 26;
			if(_c >= '0' && _c <= '9') return _c - '0' + 52;
			if(_c == '+') return 62;
			if(_c == '/') return 63;
			return -1;
		}

		// Standard base64 with padding, exactly 64 bytes (88 characters, "==" at the end).
		std::optional<std::array<uint8_t, 64>> signatureFromBase64(const std::string& _s)
		{
			if(_s.size() != 88 || _s[86] != '=' || _s[87] != '=')
				return {};
			std::array<uint8_t, 66> buf{};
			size_t n = 0;
			for(size_t i = 0; i < 88; i += 4)
			{
				int d[4];
				for(int k = 0; k < 4; ++k)
					d[k] = (i + k >= 86) ? 0 : base64Digit(_s[i + k]);
				if(d[0] < 0 || d[1] < 0 || d[2] < 0 || d[3] < 0)
					return {};
				const uint32_t v = (static_cast<uint32_t>(d[0]) << 18) | (static_cast<uint32_t>(d[1]) << 12)
					| (static_cast<uint32_t>(d[2]) << 6) | static_cast<uint32_t>(d[3]);
				buf[n++] = static_cast<uint8_t>(v >> 16);
				buf[n++] = static_cast<uint8_t>(v >> 8);
				buf[n++] = static_cast<uint8_t>(v);
			}
			// the last group holds one byte: the bits the padding covers must be zero (one encoding only)
			if(buf[64] != 0 || buf[65] != 0)
				return {};
			std::array<uint8_t, 64> out{};
			std::memcpy(out.data(), buf.data(), 64);
			return out;
		}
	}

	std::string toHex(const uint8_t* _data, const size_t _size)
	{
		static const char* digits = "0123456789abcdef";
		std::string s;
		s.reserve(_size * 2);
		for(size_t i = 0; i < _size; ++i)
		{
			s.push_back(digits[_data[i] >> 4]);
			s.push_back(digits[_data[i] & 15]);
		}
		return s;
	}

	// ---- latest.json ----

	const Asset* Manifest::find(const Os _os, const Machine _m) const
	{
		const auto it = assets.find({_os, _m});
		return it == assets.end() ? nullptr : &it->second;
	}

	namespace
	{
		const std::string* stringOf(const json::Value& _o, const char* _key)
		{
			const auto* v = _o.find(_key);
			return v && v->isString() ? &v->asString() : nullptr;
		}

		Parsed fail(std::string _why)
		{
			return {std::nullopt, std::move(_why)};
		}
	}

	Parsed parseManifest(const std::string& _json)
	{
		if(_json.size() > g_maxManifestBytes)
			return fail("too large");
		std::string error;
		const auto doc = json::parse(_json, &error);
		if(!doc)
			return fail("not JSON: " + error);
		if(!doc->isObject())
			return fail("not an object");
		const auto* schema = doc->find("schema");
		if(!schema || !schema->isNumber() || schema->asNumber() != 1)
			return fail("schema is not 1");

		Manifest m;
		const auto* version = stringOf(*doc, "version");
		const auto parsedVersion = version ? parseVersion(*version) : std::nullopt;
		if(!parsedVersion)
			return fail("no version");
		m.version = *parsedVersion;
		const auto* tag = stringOf(*doc, "tag");
		if(!tag || *tag != "mdmm-v" + toString(m.version))
			return fail("tag does not match the version");
		m.tag = *tag;
		const std::pair<const char*, std::string*> texts[] = {{"date", &m.date}, {"notes", &m.notes}, {"page", &m.page}};
		for(const auto& text : texts)
		{
			const auto* s = stringOf(*doc, text.first);
			if(!s)
				return fail(std::string("no ") + text.first);
			*text.second = *s;
		}

		const auto* assets = doc->find("assets");
		if(!assets || !assets->isObject())
			return fail("no assets");
		for(const Os os : {Os::Mac, Os::Win, Os::Linux})
		{
			const auto* perOs = assets->find(osKey(os));
			if(!perOs)
				continue;	// no build for this OS
			if(!perOs->isObject())
				return fail(std::string("assets.") + osKey(os) + " is not an object");
			for(const Machine machine : {Machine::Md, Machine::Mm})
			{
				const auto* a = perOs->find(machineKey(machine));
				if(!a)
					continue;
				const std::string where = std::string("assets.") + osKey(os) + "." + machineKey(machine);
				if(!a->isObject())
					return fail(where + " is not an object");
				Asset asset;
				const auto* name = stringOf(*a, "name");
				if(!name || *name != assetName(os, machine))
					return fail(where + ": not the release's name for it");
				asset.name = *name;
				const auto* url = stringOf(*a, "url");
				const auto suffix = m.tag + "/" + asset.name;
				if(!url || (*url != std::string(g_repoDownloads) + suffix && *url != std::string(g_repoDownloadsRenamed) + suffix))
					return fail(where + ": not this repository's release URL");
				asset.url = *url;
				const auto* size = a->find("size");
				if(!size || !size->isNumber() || size->asNumber() < 1 || size->asNumber() > static_cast<double>(g_maxAssetBytes)
					|| size->asNumber() != static_cast<double>(static_cast<uint64_t>(size->asNumber())))
					return fail(where + ": bad size");
				asset.size = static_cast<uint64_t>(size->asNumber());
				const auto* sha = stringOf(*a, "sha256");
				const auto digest = sha ? fromHex<32>(*sha, true) : std::nullopt;
				if(!digest)
					return fail(where + ": bad sha256");
				asset.sha256 = *digest;
				if(const auto* sig = a->find("sig"))
				{
					const auto bytes = sig->isString() ? signatureFromBase64(sig->asString()) : std::nullopt;
					if(!bytes)
						return fail(where + ": bad sig");
					asset.sig = *bytes;
				}
				m.assets.emplace(std::make_pair(os, machine), std::move(asset));
			}
		}
		return {std::move(m), {}};
	}

	// ---- the schedule ----

	bool checkDue(const int64_t _now, const int64_t _last, const bool _enabled)
	{
		if(!_enabled)
			return false;
		if(_last <= 0)
			return true;
		if(_now >= _last)
			return _now - _last >= g_checkIntervalSeconds;
		return _last - _now > g_checkIntervalSeconds;	// the last check is in the future: the clock went back
	}

	Offer offerFor(const Manifest& _m, const Version& _current, const Os _os, const Machine _machine, const bool _standalone, const bool _haveKey)
	{
		if(compare(_m.version, _current) <= 0)
			return Offer::None;
		const auto* asset = _m.find(_os, _machine);
		if(_standalone && _haveKey && asset && asset->sig)
			return Offer::Install;
		return Offer::Download;
	}

	// ---- SHA-256 ----

	namespace
	{
		constexpr uint32_t g_k[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

		constexpr uint32_t rotr(const uint32_t _x, const int _n) { return (_x >> _n) | (_x << (32 - _n)); }
	}

	Sha256::Sha256()
		: m_h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}
	{
	}

	void Sha256::block(const uint8_t* _p)
	{
		uint32_t w[64];
		for(int i = 0; i < 16; ++i)
			w[i] = (static_cast<uint32_t>(_p[i * 4]) << 24) | (static_cast<uint32_t>(_p[i * 4 + 1]) << 16)
				| (static_cast<uint32_t>(_p[i * 4 + 2]) << 8) | static_cast<uint32_t>(_p[i * 4 + 3]);
		for(int i = 16; i < 64; ++i)
		{
			const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3], e = m_h[4], f = m_h[5], g = m_h[6], h = m_h[7];
		for(int i = 0; i < 64; ++i)
		{
			const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
			const uint32_t ch = (e & f) ^ (~e & g);
			const uint32_t t1 = h + s1 + ch + g_k[i] + w[i];
			const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
			const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
			const uint32_t t2 = s0 + maj;
			h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
		}
		m_h[0] += a; m_h[1] += b; m_h[2] += c; m_h[3] += d; m_h[4] += e; m_h[5] += f; m_h[6] += g; m_h[7] += h;
	}

	void Sha256::update(const void* _data, size_t _size)
	{
		const auto* p = static_cast<const uint8_t*>(_data);
		m_total += _size;
		while(_size > 0)
		{
			const size_t take = std::min(_size, m_buf.size() - m_used);
			std::memcpy(m_buf.data() + m_used, p, take);
			m_used += take;
			p += take;
			_size -= take;
			if(m_used == m_buf.size())
			{
				block(m_buf.data());
				m_used = 0;
			}
		}
	}

	std::array<uint8_t, 32> Sha256::finish()
	{
		const uint64_t bits = m_total * 8;
		const uint8_t one = 0x80;
		update(&one, 1);
		const uint8_t zero = 0;
		while(m_used != 56)
			update(&zero, 1);
		uint8_t len[8];
		for(int i = 0; i < 8; ++i)
			len[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
		update(len, 8);
		std::array<uint8_t, 32> out{};
		for(int i = 0; i < 8; ++i)
		{
			out[i * 4] = static_cast<uint8_t>(m_h[i] >> 24);
			out[i * 4 + 1] = static_cast<uint8_t>(m_h[i] >> 16);
			out[i * 4 + 2] = static_cast<uint8_t>(m_h[i] >> 8);
			out[i * 4 + 3] = static_cast<uint8_t>(m_h[i]);
		}
		*this = Sha256();
		return out;
	}

	std::array<uint8_t, 32> Sha256::of(const void* _data, const size_t _size)
	{
		Sha256 s;
		s.update(_data, _size);
		return s.finish();
	}

	// ---- the signature ----

	std::optional<std::array<uint8_t, 32>> parseKey(const std::string& _hex)
	{
		return fromHex<32>(_hex, false);
	}

	std::optional<std::array<uint8_t, 32>> buildKey()
	{
		return parseKey(g_updatePublicKeyHex);
	}

	std::string updateMessage(const std::string& _name, const Version& _version, const std::array<uint8_t, 32>& _sha256)
	{
		return "mdmm-update-v1\n" + _name + "\n" + toString(_version) + "\n" + toHex(_sha256) + "\n";
	}

	bool verifySignature(const std::array<uint8_t, 32>& _key, const std::string& _message, const std::array<uint8_t, 64>& _sig)
	{
		return crypto_ed25519_check(_sig.data(), _key.data(), reinterpret_cast<const uint8_t*>(_message.data()), _message.size()) == 0;
	}

	Verdict verifyDownload(const Asset& _asset, const Version& _version, const uint64_t _size,
		const std::array<uint8_t, 32>& _sha256, const std::optional<std::array<uint8_t, 32>>& _key)
	{
		if(!_key)
			return Verdict::NoKey;
		if(!_asset.sig)
			return Verdict::Unsigned;
		if(_size != _asset.size)
			return Verdict::WrongSize;
		if(_sha256 != _asset.sha256)
			return Verdict::WrongHash;
		if(!verifySignature(*_key, updateMessage(_asset.name, _version, _sha256), *_asset.sig))
			return Verdict::BadSignature;
		return Verdict::Ok;
	}

	const char* verdictText(const Verdict _v)
	{
		switch(_v)
		{
		case Verdict::Ok: return "verified";
		case Verdict::NoKey: return "this build has no update key";
		case Verdict::Unsigned: return "the release is not signed";
		case Verdict::WrongSize: return "the file has the wrong size";
		case Verdict::WrongHash: return "the file's SHA-256 is not the published one";
		case Verdict::BadSignature: return "the signature does not match";
		}
		return "";
	}

	// ---- the swap after exit ----

	std::string quotePowerShell(const std::string& _s)
	{
		// In a single-quoted PowerShell string only quotes are special; PowerShell also takes the typographic
		// single quotes (U+2018, U+2019, U+201A, U+201B) as quotes: each is doubled.
		std::string out = "'";
		for(size_t i = 0; i < _s.size(); ++i)
		{
			const char c = _s[i];
			if(c == '\'')
			{
				out += "''";
				continue;
			}
			if(static_cast<unsigned char>(c) == 0xE2 && i + 2 < _s.size() && static_cast<unsigned char>(_s[i + 1]) == 0x80)
			{
				const auto third = static_cast<unsigned char>(_s[i + 2]);
				if(third >= 0x98 && third <= 0x9B)
				{
					const std::string q = _s.substr(i, 3);
					out += q + q;
					i += 2;
					continue;
				}
			}
			out += c;
		}
		return out + "'";
	}

	std::string quoteSh(const std::string& _s)
	{
		std::string out = "'";
		for(const char c : _s)
		{
			if(c == '\'')
				out += "'\\''";
			else
				out += c;
		}
		return out + "'";
	}

	std::string swapScriptPowerShell(const SwapPlan& _plan)
	{
		std::string from, to;
		for(const auto& [src, dst] : _plan.copies)
		{
			from += (from.empty() ? "" : ", ") + quotePowerShell(src);
			to += (to.empty() ? "" : ", ") + quotePowerShell(dst);
		}
		std::string s;
		s += "# The editor's update: put the new files in place once the app has quit (doc/modern-ux/DESIGN-updates.md).\r\n";
		s += "$log = " + quotePowerShell(_plan.log) + "\r\n";
		s += "function Say($t) { Add-Content -LiteralPath $log -Value $t }\r\n";
		s += "try { Wait-Process -Id " + std::to_string(_plan.pid) + " -Timeout 300 -ErrorAction SilentlyContinue } catch {}\r\n";
		s += "$from = @(" + from + ")\r\n";
		s += "$to = @(" + to + ")\r\n";
		s += "for ($k = 0; $k -lt $from.Count; $k++) {\r\n";
		s += "  $done = $false\r\n";
		s += "  for ($i = 0; $i -lt 120 -and -not $done; $i++) {\r\n";
		s += "    try { Copy-Item -LiteralPath $from[$k] -Destination $to[$k] -Force -ErrorAction Stop; $done = $true }\r\n";
		s += "    catch { Start-Sleep -Milliseconds 250 }\r\n";
		s += "  }\r\n";
		s += "  if (-not $done) { Say ('failed: ' + $to[$k]); exit 1 }\r\n";
		s += "  Say ('replaced: ' + $to[$k])\r\n";
		s += "}\r\n";
		s += "Say 'done'\r\n";
		if(_plan.restart)
			s += "Start-Process -FilePath " + quotePowerShell(_plan.app) + "\r\n";
		return s;
	}

	std::string swapScriptSh(const SwapPlan& _plan)
	{
		std::string s;
		s += "#!/bin/sh\n";
		s += "# The editor's update: put the new files in place once the app has quit (doc/modern-ux/DESIGN-updates.md).\n";
		s += "pid=" + std::to_string(_plan.pid) + "\n";
		s += "log=" + quoteSh(_plan.log) + "\n";
		s += "n=0\n";
		s += "while kill -0 \"$pid\" 2>/dev/null; do\n";
		s += "  n=$((n + 1)); if [ \"$n\" -gt 1500 ]; then echo 'the app did not quit' >> \"$log\"; exit 1; fi\n";
		s += "  sleep 0.2\n";
		s += "done\n";
		s += "put() {\n";
		s += "  if cp -f \"$1\" \"$2.mdmm-new\" && chmod 755 \"$2.mdmm-new\" && mv -f \"$2.mdmm-new\" \"$2\"; then echo \"replaced: $2\" >> \"$log\";\n";
		s += "  else rm -f \"$2.mdmm-new\"; echo \"failed: $2\" >> \"$log\"; exit 1; fi\n";
		s += "}\n";
		for(const auto& [src, dst] : _plan.copies)
			s += "put " + quoteSh(src) + " " + quoteSh(dst) + "\n";
		s += "echo done >> \"$log\"\n";
		if(_plan.restart)
			s += "nohup " + quoteSh(_plan.app) + " >/dev/null 2>&1 &\n";
		return s;
	}
}
