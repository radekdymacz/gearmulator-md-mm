// The in-app updater's pure parts (updateCore.h, doc/modern-ux/DESIGN-updates.md): version order, latest.json
// (valid, malformed, an OS missing), the daily schedule, SHA-256 (FIPS 180-4 vectors), ed25519 (RFC 8032 vectors
// and a statement signed by scripts/release/mdmm_ed25519.py, so the release signer and the app agree), and the
// verdict on a download.
#include "updateCore.h"
#include "updateKey.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
	int g_failures = 0;

	void check(const bool _ok, const char* _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what);
		if(!_ok)
			++g_failures;
	}

	using namespace mdmmUpdate;

	template<size_t N> std::array<uint8_t, N> hex(const char* _s)
	{
		std::array<uint8_t, N> out{};
		for(size_t i = 0; i < N; ++i)
		{
			unsigned v = 0;
			std::sscanf(_s + i * 2, "%2x", &v);
			out[i] = static_cast<uint8_t>(v);
		}
		return out;
	}

	std::string sha256Hex(const std::string& _s)
	{
		return toHex(Sha256::of(_s.data(), _s.size()));
	}

	// A test key pair (seed = SHA-256 of "mdmm-update test key, never a release key"); the signature below was
	// made by scripts/release/mdmm_ed25519.py over updateMessage("Machinedrum-Editor-macOS.pkg", 0.3.3, SHA-256("hello update")).
	const char* g_testPub = "e6e477dbe6e37718980cab5abee3f905278c5dc76cfea0cb79da63588699a1bc";
	const char* g_testSig = "OqUWLRFl4MVug1zsUDdct7X1U8A2yoFW1HqR55IISFMYIJqjEL0vwqeCNRW66x3Q2S4st+ScYSFCpRmKn7i6CQ==";
	const char* g_helloSha = "ed70ed98d448ff57ee2b137e52bfcc54729095de8dd049f8432c1bbc06581687";

	std::string asset(const std::string& _name, const std::string& _tag, const std::string& _extra = {})
	{
		return "{\"name\": \"" + _name + "\", \"url\": \"https://github.com/radekdymacz/gearmulator-md-mm/releases/download/" + _tag + "/" + _name
			+ "\", \"size\": 12, \"sha256\": \"" + g_helloSha + "\"" + _extra + "}";
	}

	std::string manifest(const std::string& _assets, const std::string& _version = "0.3.3")
	{
		return "{\"schema\": 1, \"version\": \"" + _version + "\", \"tag\": \"mdmm-v" + _version + "\", \"date\": \"2026-10-08\","
			" \"notes\": \"https://github.com/radekdymacz/gearmulator-md-mm/releases/tag/mdmm-v" + _version + "\","
			" \"page\": \"https://mdmm.dev/get/\", \"assets\": " + _assets + ", \"future\": [1, 2]}";
	}

	std::string signedMac()
	{
		return "{\"mac\": {\"md\": " + asset("Machinedrum-Editor-macOS.pkg", "mdmm-v0.3.3", std::string(", \"sig\": \"") + g_testSig + "\"")
			+ ", \"mm\": " + asset("Monomachine-Editor-macOS.pkg", "mdmm-v0.3.3") + "}}";
	}
}

int main()
{
	std::printf("mdmmUpdateTest\n");

	// versions
	{
		const auto v = parseVersion("0.3.2");
		check(v && v->major == 0 && v->minor == 3 && v->patch == 2, "0.3.2 parses");
		check(parseVersion("10.20.300") && parseVersion("10.20.300")->patch == 300, "several digits");
		for(const char* bad : {"", "0.3", "0.3.2.1", "v0.3.2", "0.3.2-alpha", "0..2", "a.b.c", " 0.3.2", "0.3.2 ", "1.2.99999999999"})
			check(!parseVersion(bad), (std::string("refused: '") + bad + "'").c_str());
		check(compare(*parseVersion("0.3.10"), *parseVersion("0.3.9")) > 0, "0.3.10 is newer than 0.3.9 (numbers, not text)");
		check(compare(*parseVersion("0.4.0"), *parseVersion("0.3.99")) > 0, "minor beats patch");
		check(compare(*parseVersion("1.0.0"), *parseVersion("0.99.99")) > 0, "major beats minor");
		check(compare(*parseVersion("0.3.2"), *parseVersion("0.3.2")) == 0, "equal");
		check(compare(*parseVersion("0.3.1"), *parseVersion("0.3.2")) < 0, "older");
		check(toString(*parseVersion("1.22.333")) == "1.22.333", "round trip");
	}

	// SHA-256 (FIPS 180-4 examples), and the streaming update across block edges
	{
		check(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 of nothing");
		check(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 of abc");
		check(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
			== "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA-256 of the two-block example");
		std::string million(1000000, 'a');
		Sha256 s;
		for(size_t i = 0; i < million.size(); i += 777)
			s.update(million.data() + i, std::min<size_t>(777, million.size() - i));
		check(toHex(s.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA-256 of a million a's, in odd pieces");
		check(sha256Hex("hello update") == g_helloSha, "SHA-256 of the test file");
	}

	// ed25519: RFC 8032 section 7.1 tests 1 to 3, through Monocypher
	{
		struct V { const char* pub; const char* msg; size_t len; const char* sig; };
		const V vectors[] = {
			{"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "", 0,
				"e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
			{"3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "\x72", 1,
				"92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
			{"fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "\xaf\x82", 2,
				"6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"}};
		int i = 1;
		for(const auto& v : vectors)
		{
			const auto key = hex<32>(v.pub);
			auto sig = hex<64>(v.sig);
			const std::string msg(v.msg, v.len);
			const std::string n = "RFC 8032 test " + std::to_string(i++);
			check(verifySignature(key, msg, sig), (n + ": verifies").c_str());
			check(!verifySignature(key, msg + "x", sig), (n + ": another message does not").c_str());
			sig[5] ^= 0x10;
			check(!verifySignature(key, msg, sig), (n + ": a flipped bit does not").c_str());
		}
	}

	// keys: the placeholder is no key; hex in either case is
	{
		check(!parseKey(g_updatePublicKeyHex) || std::strlen(g_updatePublicKeyHex) == 64, "updateKey.h: the placeholder parses to no key");
		check(parseKey(g_testPub).has_value(), "64 hex characters are a key");
		check(!parseKey(std::string(g_testPub).substr(1)), "63 are not");
		check(!parseKey(std::string(63, '0') + "g"), "a non-hex character is not");
	}

	// latest.json
	{
		const auto p = parseManifest(manifest(signedMac()));
		check(p.manifest.has_value(), ("a valid manifest parses " + p.error).c_str());
		if(p.manifest)
		{
			const auto& m = *p.manifest;
			check(toString(m.version) == "0.3.3" && m.tag == "mdmm-v0.3.3" && m.page == "https://mdmm.dev/get/", "its version, tag, page");
			const auto* md = m.find(Os::Mac, Machine::Md);
			check(md && md->size == 12 && toHex(md->sha256) == g_helloSha && md->sig, "the MD's mac asset, signed");
			const auto* mm = m.find(Os::Mac, Machine::Mm);
			check(mm && !mm->sig, "the MM's mac asset, unsigned");
			check(!m.find(Os::Win, Machine::Md) && !m.find(Os::Linux, Machine::Mm), "an OS missing: no asset, no error");

			// what the editor offers
			const auto cur = *parseVersion("0.3.2");
			const auto key = parseKey(g_testPub).has_value();
			check(offerFor(m, cur, Os::Mac, Machine::Md, true, key) == Offer::Install, "standalone, signed, key: Install");
			check(offerFor(m, cur, Os::Mac, Machine::Md, false, key) == Offer::Download, "plug-in: Download");
			check(offerFor(m, cur, Os::Mac, Machine::Md, true, false) == Offer::Download, "no key in the build: Download");
			check(offerFor(m, cur, Os::Mac, Machine::Mm, true, key) == Offer::Download, "unsigned asset: Download");
			check(offerFor(m, cur, Os::Win, Machine::Md, true, key) == Offer::Download, "no build for this OS: Download");
			check(offerFor(m, *parseVersion("0.3.3"), Os::Mac, Machine::Md, true, key) == Offer::None, "same version: nothing");
			check(offerFor(m, *parseVersion("0.4.0"), Os::Mac, Machine::Md, true, key) == Offer::None, "older: nothing");

			// the download's verdict
			const auto pub = parseKey(g_testPub);
			const auto good = Sha256::of("hello update", 12);
			check(verifyDownload(*md, m.version, 12, good, pub) == Verdict::Ok, "the signed file verifies");
			check(verifyDownload(*md, m.version, 12, good, std::nullopt) == Verdict::NoKey, "no key: refused");
			check(verifyDownload(*mm, m.version, 12, good, pub) == Verdict::Unsigned, "no signature: refused");
			check(verifyDownload(*md, m.version, 13, good, pub) == Verdict::WrongSize, "wrong size: refused");
			check(verifyDownload(*md, m.version, 12, Sha256::of("hello updatE", 12), pub) == Verdict::WrongHash, "wrong hash: refused");
			check(verifyDownload(*md, *parseVersion("0.3.4"), 12, good, pub) == Verdict::BadSignature, "another version (a replay): refused");
			auto renamed = *md;
			renamed.name = "Monomachine-Editor-macOS.pkg";
			check(verifyDownload(renamed, m.version, 12, good, pub) == Verdict::BadSignature, "the other machine's name: refused");
			auto otherKey = *pub;
			otherKey[0] ^= 1;
			check(verifyDownload(*md, m.version, 12, good, otherKey) == Verdict::BadSignature, "another key: refused");
		}

		const auto win = parseManifest(manifest("{\"win\": {\"mm\": " + asset("Monomachine-Editor-Windows-x64-not-tested.zip", "mdmm-v0.3.3") + "}}"));
		check(win.manifest && win.manifest->find(Os::Win, Machine::Mm) && !win.manifest->find(Os::Win, Machine::Md), "one machine on one OS");
		check(parseManifest(manifest("{}")).manifest.has_value(), "no assets at all: parses, offers the site");

		struct Bad { const char* what; std::string text; };
		const Bad bad[] = {
			{"not JSON", "{\"schema\": 1,"},
			{"an array", "[1, 2]"},
			{"empty", ""},
			{"schema 2", manifest("{}").replace(manifest("{}").find("\"schema\": 1"), 11, "\"schema\": 2")},
			{"a bad version", manifest("{}", "0.3")},
			{"the tag not the version", manifest("{}").replace(manifest("{}").find("mdmm-v0.3.3"), 11, "mdmm-v0.3.4")},
			{"assets not an object", manifest("[]")},
			{"an OS not an object", manifest("{\"mac\": 3}")},
			{"another repository's URL", manifest("{\"mac\": {\"md\": {\"name\": \"Machinedrum-Editor-macOS.pkg\", \"url\": \"https://evil.example/Machinedrum-Editor-macOS.pkg\", \"size\": 12, \"sha256\": \"" + std::string(g_helloSha) + "\"}}}")},
			{"another tag's URL", manifest("{\"mac\": {\"md\": " + asset("Machinedrum-Editor-macOS.pkg", "mdmm-v0.3.2") + "}}")},
			{"the wrong asset name", manifest("{\"mac\": {\"md\": " + asset("Monomachine-Editor-macOS.pkg", "mdmm-v0.3.3") + "}}")},
			{"the dmg instead of the pkg", manifest("{\"mac\": {\"md\": " + asset("Machinedrum-Editor-macOS.dmg", "mdmm-v0.3.3") + "}}")},
			{"an upper-case hash", manifest("{\"mac\": {\"md\": " + asset("Machinedrum-Editor-macOS.pkg", "mdmm-v0.3.3") + "}}").replace(
				manifest("{\"mac\": {\"md\": " + asset("Machinedrum-Editor-macOS.pkg", "mdmm-v0.3.3") + "}}").find(g_helloSha), 2, "ED")},
			{"a short signature", manifest("{\"mac\": {\"md\": " + asset("Machinedrum-Editor-macOS.pkg", "mdmm-v0.3.3", ", \"sig\": \"AAAA\"") + "}}")},
			{"a signature with a stray bit in its padding", manifest("{\"mac\": {\"md\": " + asset("Machinedrum-Editor-macOS.pkg", "mdmm-v0.3.3",
				", \"sig\": \"" + std::string(g_testSig).replace(85, 1, "D") + "\"") + "}}")},
			{"a signature that is a number", manifest("{\"mac\": {\"md\": " + asset("Machinedrum-Editor-macOS.pkg", "mdmm-v0.3.3", ", \"sig\": 5") + "}}")},
		};
		for(const auto& b : bad)
		{
			const auto r = parseManifest(b.text);
			check(!r.manifest && !r.error.empty(), (std::string("malformed, refused: ") + b.what + " (" + r.error + ")").c_str());
		}
		auto big = manifest("{}");
		big.insert(1, "\"pad\": \"" + std::string(70000, 'x') + "\", ");
		check(!parseManifest(big).manifest, "larger than 64 KB: refused");
	}

	// the daily schedule
	{
		const int64_t day = g_checkIntervalSeconds, t = 1790000000;
		check(checkDue(t, 0, true), "never checked: due");
		check(!checkDue(t, 0, false), "setting off: never due");
		check(!checkDue(t + day * 10, t, false), "setting off: not even after ten days");
		check(!checkDue(t + 60, t, true), "a minute later: not due");
		check(!checkDue(t + day - 1, t, true), "a second short of a day: not due");
		check(checkDue(t + day, t, true), "a day later: due");
		check(!checkDue(t - 3600, t, true), "the clock an hour back: not due (at most once a day)");
		check(checkDue(t - day - 1, t, true), "the last check more than a day in the future: due");
		// a day of polls every minute starts exactly one check
		int64_t last = 0;
		int checks = 0;
		for(int64_t now = t; now < t + day; now += 60)
			if(checkDue(now, last, true)) { ++checks; last = now; }
		check(checks == 1, "a day of polls every minute: one check");
	}

	// the swap helpers: quoting, and (POSIX) the sh helper run for real on files with awkward names
	{
		check(quoteSh("a b") == "'a b'" && quoteSh("it's") == "'it'\\''s'", "sh quoting");
		check(quotePowerShell("C:\\a b\\x.exe") == "'C:\\a b\\x.exe'" && quotePowerShell("it's") == "'it''s'", "PowerShell quoting");
		check(quotePowerShell("a\xE2\x80\x99" "b") == "'a\xE2\x80\x99\xE2\x80\x99" "b'", "PowerShell: a typographic quote is doubled too");
		SwapPlan plan;
		plan.pid = 1234;
		plan.copies = {{"C:\\Temp\\new\\Machinedrum Editor.exe", "C:\\Apps\\it's here\\Machinedrum Editor.exe"}};
		plan.app = "C:\\Apps\\it's here\\Machinedrum Editor.exe";
		plan.log = "C:\\Temp\\swap.log";
		const auto ps = swapScriptPowerShell(plan);
		check(ps.find("Wait-Process -Id 1234") != std::string::npos && ps.find("'C:\\Apps\\it''s here\\Machinedrum Editor.exe'") != std::string::npos
			&& ps.find("Start-Process") == std::string::npos, "PowerShell helper: waits, quoted target, no restart unless asked");
		plan.restart = true;
		check(swapScriptPowerShell(plan).find("Start-Process -FilePath 'C:\\Apps\\it''s here\\Machinedrum Editor.exe'") != std::string::npos,
			"PowerShell helper: restarts after Restart now");
#ifndef _WIN32
		char dirTemplate[] = "/tmp/mdmmUpdateTest.XXXXXX";
		if(const char* dir = mkdtemp(dirTemplate))
		{
			const std::string d = dir;
			const std::string staged = d + "/new it's.bin", installed = d + "/old $(touch pwned) it's.bin", log = d + "/swap.log";
			std::FILE* f = std::fopen(staged.c_str(), "wb");
			std::fputs("new", f);
			std::fclose(f);
			f = std::fopen(installed.c_str(), "wb");
			std::fputs("old", f);
			std::fclose(f);
			const pid_t child = fork();
			if(child == 0)
				_exit(0);
			int status = 0;
			waitpid(child, &status, 0);	// a process that has quit: the helper does not wait
			SwapPlan sh;
			sh.pid = child;
			sh.copies = {{staged, installed}};
			sh.log = log;
			const std::string script = d + "/swap.sh";
			f = std::fopen(script.c_str(), "wb");
			const auto text = swapScriptSh(sh);
			std::fwrite(text.data(), 1, text.size(), f);
			std::fclose(f);
			const int rc = std::system(("/bin/sh " + quoteSh(script)).c_str());
			char buf[16] = {};
			f = std::fopen(installed.c_str(), "rb");
			const size_t n = f ? std::fread(buf, 1, sizeof(buf) - 1, f) : 0;
			if(f) std::fclose(f);
			check(rc == 0 && std::string(buf, n) == "new", "sh helper: the installed file is replaced");
			check(access((d + "/pwned").c_str(), F_OK) != 0 && access((installed + ".mdmm-new").c_str(), F_OK) != 0,
				"sh helper: a name is data, never run; no leftover");
			std::system(("rm -rf " + quoteSh(d)).c_str());
		}
		else
			check(false, "a temporary folder for the sh helper");
#endif
	}

	std::printf(g_failures ? "mdmmUpdateTest: %d FAILED\n" : "mdmmUpdateTest: all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
