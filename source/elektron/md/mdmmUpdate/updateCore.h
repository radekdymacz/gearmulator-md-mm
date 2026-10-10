#pragma once

// The in-app updater's pure parts (doc/modern-ux/DESIGN-updates.md): versions, latest.json, the daily
// schedule, SHA-256 and the ed25519 check of a download. No JUCE, no I/O: the plug-in's side
// (mdJucePlugin/mdUpdater.*) fetches, stores and installs; everything it decides comes from here.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mdmmUpdate
{
	// ---- versions: MAJOR.MINOR.PATCH, numbers only (pre-releases never reach latest.json) ----
	struct Version
	{
		uint32_t major = 0;
		uint32_t minor = 0;
		uint32_t patch = 0;
	};

	std::optional<Version> parseVersion(const std::string& _text);
	// <0, 0, >0 as _a is older than, the same as, newer than _b.
	int compare(const Version& _a, const Version& _b);
	std::string toString(const Version& _v);

	// ---- what runs where ----
	enum class Os : uint8_t { Mac, Win, Linux };
	enum class Machine : uint8_t { Md, Mm };

	const char* osKey(Os _os);				// "mac", "win", "linux" (latest.json's and config.js's keys)
	const char* machineKey(Machine _m);		// "md", "mm"
	// The fixed release asset name for an OS and a machine (the site's download links use the same).
	const char* assetName(Os _os, Machine _m);
	// The repository was renamed gearmulator-md-mm -> mdmm (2026-10-10). Apps up to 0.4.0 accept only the old
	// prefix, so latest.json keeps writing it (GitHub redirects the old name); from 0.5 both are accepted.
	constexpr const char* g_repoDownloads = "https://github.com/radekdymacz/gearmulator-md-mm/releases/download/";
	constexpr const char* g_repoDownloadsRenamed = "https://github.com/radekdymacz/mdmm/releases/download/";
	constexpr const char* g_manifestUrl = "https://mdmm.dev/latest.json";
	constexpr const char* g_downloadPage = "https://mdmm.dev/get/";
	constexpr uint64_t g_maxManifestBytes = 64 * 1024;
	constexpr uint64_t g_maxAssetBytes = 1024ull * 1024 * 1024;

	// ---- latest.json (DESIGN-updates.md 3.1) ----
	struct Asset
	{
		std::string name;
		std::string url;
		uint64_t size = 0;
		std::array<uint8_t, 32> sha256{};
		std::optional<std::array<uint8_t, 64>> sig;	// absent: the release was not signed
	};

	struct Manifest
	{
		Version version;
		std::string tag;
		std::string date;
		std::string notes;
		std::string page;
		std::map<std::pair<Os, Machine>, Asset> assets;

		const Asset* find(Os _os, Machine _m) const;
	};

	struct Parsed
	{
		std::optional<Manifest> manifest;
		std::string error;		// why not, when manifest is empty
	};

	Parsed parseManifest(const std::string& _json);

	// ---- the daily check (DESIGN-updates.md 3.3) ----
	constexpr int64_t g_checkIntervalSeconds = 24 * 60 * 60;
	// Due when checks are on and a day has passed since the last one, or the last one is more than a day
	// ahead of now (a clock that was set back). _last 0: never checked.
	bool checkDue(int64_t _now, int64_t _last, bool _enabled);

	// ---- what the editor offers ----
	enum class Offer : uint8_t
	{
		None,		// not newer (or no manifest)
		Install,	// the standalone downloads, verifies and installs it
		Download	// open the download page: a plug-in, no build for this OS, an unsigned release, no key in this build
	};
	Offer offerFor(const Manifest& _m, const Version& _current, Os _os, Machine _machine, bool _standalone, bool _haveKey);

	// ---- SHA-256 (FIPS 180-4) ----
	class Sha256
	{
	public:
		Sha256();
		void update(const void* _data, size_t _size);
		std::array<uint8_t, 32> finish();

		static std::array<uint8_t, 32> of(const void* _data, size_t _size);
	private:
		void block(const uint8_t* _p);
		std::array<uint32_t, 8> m_h{};
		std::array<uint8_t, 64> m_buf{};
		size_t m_used = 0;
		uint64_t m_total = 0;
	};

	std::string toHex(const uint8_t* _data, size_t _size);
	template<size_t N> std::string toHex(const std::array<uint8_t, N>& _a) { return toHex(_a.data(), N); }

	// ---- the signature (DESIGN-updates.md 3.2) ----
	// The key in hex (updateKey.h); empty for the placeholder or anything that is not 64 hex characters.
	std::optional<std::array<uint8_t, 32>> parseKey(const std::string& _hex);
	// This build's key (updateKey.h), empty while it is the placeholder.
	std::optional<std::array<uint8_t, 32>> buildKey();
	// "mdmm-update-v1\n<name>\n<version>\n<sha256 hex>\n"
	std::string updateMessage(const std::string& _name, const Version& _version, const std::array<uint8_t, 32>& _sha256);
	bool verifySignature(const std::array<uint8_t, 32>& _key, const std::string& _message, const std::array<uint8_t, 64>& _sig);

	enum class Verdict : uint8_t
	{
		Ok,
		NoKey,			// this build has the placeholder key
		Unsigned,		// latest.json has no signature for it
		WrongSize,
		WrongHash,
		BadSignature
	};
	// A downloaded file (its size and SHA-256) against what latest.json promised, with _key.
	Verdict verifyDownload(const Asset& _asset, const Version& _version, uint64_t _size,
		const std::array<uint8_t, 32>& _sha256, const std::optional<std::array<uint8_t, 32>>& _key);
	const char* verdictText(Verdict _v);

	// ---- the swap after exit (Windows, Linux; DESIGN-updates.md 4) ----
	// A helper script, started detached as the app quits: it waits for the app's process to end, puts each staged
	// file in place of the installed one (POSIX: copy beside it, then rename over it; Windows: copy with retries
	// while the old .exe is still locked) and, after "Restart now", starts the app again. Paths go in quoted, so a
	// space or a quote in a folder name is data, never code.
	struct SwapPlan
	{
		int64_t pid = 0;
		std::vector<std::pair<std::string, std::string>> copies;	// staged file -> installed file
		std::string app;			// started again when restart is set
		bool restart = false;
		std::string log;			// what the helper did (a text file)
	};
	std::string swapScriptPowerShell(const SwapPlan& _plan);
	std::string swapScriptSh(const SwapPlan& _plan);
	std::string quotePowerShell(const std::string& _s);	// '...' with ' doubled
	std::string quoteSh(const std::string& _s);			// '...' with ' as '\''
}
