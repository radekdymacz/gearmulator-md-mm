// P7: the standalone window on its screen (jucePluginEditorLib/windowFit.h), with Radek's screen:
// 1800 x 1169 points, the menu bar 39 and the Dock 74 (visible 0,39 1800 x 1056), a 28 point title bar.
#include "jucePluginEditorLib/windowFit.h"

#include <cstdio>
#include <cstdlib>

namespace
{
	int g_failures = 0;
	using jucePluginEditorLib::windowFit::Rect;

	void expect(const Rect& _got, const Rect& _want, const char* _what)
	{
		const bool ok = _got == _want;
		std::printf("  %s %s: %d,%d %dx%d\n", ok ? "ok  " : "FAIL", _what, _got.x, _got.y, _got.w, _got.h);
		if(!ok)
		{
			std::printf("       want %d,%d %dx%d\n", _want.x, _want.y, _want.w, _want.h);
			++g_failures;
		}
	}
}

int main()
{
	using namespace jucePluginEditorLib::windowFit;
	const Rect visible{0, 39, 1800, 1056};
	const Frame title{28, 0, 0, 0};
	// A window whose bottom is under the Dock (content 1596 x 1024 at 153,99: bottom 1123, the Dock from 1095).
	expect(fit({153, 99, 1596, 1024}, title, visible, 864, 554, false), {153, 71, 1596, 1024}, "a free window moved up, above the Dock");
	// Too tall for the screen: shorter, same width (a free page), top under the menu bar and title bar.
	expect(fit({100, 80, 1600, 1300}, title, visible, 864, 554, false), {100, 67, 1600, 1028}, "a free window no taller than the visible area");
	// A fixed skin keeps its aspect ratio: 1440 x 924 at 140 % is 2016 x 1294.
	expect(fit({0, 67, 2016, 1294}, title, visible, 144, 92, true), {0, 67, 1601, 1028}, "a fixed skin shrinks on both sides");
	// Hanging over the right edge, and above the menu bar.
	expect(fit({900, 0, 1200, 800}, title, visible, 864, 554, false), {600, 67, 1200, 800}, "moved in from the right and the top");
	// It fits: unchanged.
	expect(fit({200, 200, 1440, 924}, title, visible, 864, 554, false), {200, 171, 1440, 924}, "a window that fits stays (moved up only as far as needed)");
	expect(fit({200, 100, 1440, 924}, title, visible, 864, 554, false), {200, 100, 1440, 924}, "a window that fits is left alone");
	// A screen smaller than the minimum: the minimum, at the visible area's corner.
	expect(fit({0, 0, 1440, 924}, title, {0, 25, 800, 500}, 864, 554, false), {0, 53, 864, 554}, "never below the minimum");
	std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
