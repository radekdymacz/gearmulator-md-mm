// 0.3.4: the editor menu's first line and the standalone's About box say the build's product and version
// (mdAbout.h; the names from scripts/mdmm-product.env, the version mdmm::editorVersion(), as the page shows it).
#include "mdAbout.h"

#include <cstdio>
#include <string>

namespace
{
	int g_failures = 0;
	void check(const bool _ok, const std::string& _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++g_failures;
	}
}

int main()
{
	using namespace mdJucePlugin;
	const std::string version = mdmm::editorVersion();
	check(version != "0.0.0" && !version.empty(), "the build compiles its version in (" + version + ")");
	check(about::title(false) == std::string(MDMM_PRODUCT_NAME_MD) + " " + version, "the menu's first line, MD: " + about::title(false));
	check(about::title(true) == std::string(MDMM_PRODUCT_NAME_MM) + " " + version, "the menu's first line, MM: " + about::title(true));
	const auto text = about::text();
	check(text.find("Version " + version) != std::string::npos, "the About box gives the version");
	check(text.find(MDMM_VENDOR) != std::string::npos, "the maker");
	check(text.find("GNU General Public License, version 3") != std::string::npos, "the licence");
	check(text.find("joelanders") != std::string::npos && text.find("Gearmulator") != std::string::npos, "the credits");
	std::printf("mdAboutTest: %s\n", g_failures ? "FAIL" : "PASS");
	return g_failures ? 1 : 0;
}
