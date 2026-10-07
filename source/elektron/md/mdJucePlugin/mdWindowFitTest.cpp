// P7: the standalone window on its screen (jucePluginEditorLib/windowFit.h), with Radek's screen:
// 1800 x 1169 points, the menu bar 39 and the Dock 74 (visible 0,39 1800 x 1056), a 28 point title bar.
#include "jucePluginEditorLib/windowFit.h"
#include "mdPageZoom.h"

#include <cmath>
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

	// B-001: a plug-in's editor picks its size only (the host places its window and adds its bars, g_hostBars).
	// An M1 MacBook Air: 1440 x 900 points, the menu bar 25 (visible 0,25 1440 x 875). The design size, 1440 x 924,
	// does not fit: as tall as the visible area less the host's bars, as wide as the screen; where it is stays.
	const Rect air{0, 25, 1440, 875};
	expect(fitPluginSize({300, 120, 1440, 924}, air, 864, 554, false), {300, 120, 1440, 811}, "a plug-in on a 1440 x 900 screen: shorter, its place kept");
	expect(fitPluginSize({0, 0, 1800, 1100}, air, 864, 554, false), {0, 0, 1440, 811}, "a remembered large size: no larger than the screen");
	expect(fitPluginSize({50, 60, 1200, 780}, air, 864, 554, false), {50, 60, 1200, 780}, "a plug-in that fits is left alone");
	expect(fitPluginSize({0, 0, 1440, 924}, {0, 25, 1024, 600}, 864, 554, false), {0, 0, 1024, 554}, "never below the minimum");

	// The page's zoom (mdPageZoom.h): the design width fits, the user's zoom multiplies it, the steps.
	{
		using namespace mdJucePlugin::pageZoom;
		const auto near = [](const double _a, const double _b) { return std::abs(_a - _b) < 1e-9; };
		const auto zoomCheck = [&](const bool _ok, const char* _what)
		{
			std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what);
			if(!_ok)
				++g_failures;
		};
		zoomCheck(near(effective(1440, 924, 1.0), 1.0), "the design size: 100 %");
		zoomCheck(near(effective(1440, 800, 1.0), 1.0) && near(effective(1600, 900, 1.0), 1.0), "between the design and its minimum height: 100 %, laid out in the window");
		zoomCheck(near(effective(864, 554, 1.0), 0.6), "the minimum window: 60 %, the whole page");
		zoomCheck(near(effective(1000, 1300, 1.0), 1000.0 / 1440), "a tall narrow window: the width fits");
		zoomCheck(near(effective(1440, 540, 1.0), 0.75), "a short window: zoomed out to the page's minimum height (720 px), nothing cut");
		zoomCheck(near(effective(2560, 1440, 1.0), 1440.0 / 924), "a large window: zoomed up by the smaller side, the design's proportions kept");
		zoomCheck(near(effective(3440, 1440, 1.0), 1440.0 / 924), "an ultrawide window: zoomed up by its height, laid out wider");
		zoomCheck(near(effective(1920, 1080, 1.0), 1080.0 / 924), "1920 x 1080: by its height");
		zoomCheck(near(effective(1080, 693, 0.8), 0.6), "the user's 80 % on a 75 % fit: 60 %");
		zoomCheck(near(effective(1440, 924, 1.5), 1.5), "the user's 150 %");
		zoomCheck(near(clampUser(0.0), 1.0) && near(clampUser(-1), 1.0) && near(clampUser(9), 2.0) && near(clampUser(0.1), 0.5),
			"a stored zoom out of range: 100 % when it is none, the nearest end otherwise");
		zoomCheck(near(step(1.0, 1), 1.1) && near(step(1.0, -1), 0.9) && near(step(1.3, -1), 1.25) && near(step(1.3, 1), 1.5),
			"the steps: one up, one down, from between two steps to the next in that direction");
		zoomCheck(near(step(2.0, 1), 2.0) && near(step(0.5, -1), 0.5) && near(step(1.75, 0), 1.0), "the ends hold; 0 is 100 %");
	}
	std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
