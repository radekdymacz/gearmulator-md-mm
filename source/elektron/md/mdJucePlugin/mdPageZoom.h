#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace mdJucePlugin
{
	// The page's zoom in its window, as plain values (B-001, B-004): no JUCE, tested by mdWindowFitTest.
	// The page is designed for 1440 x 924 CSS px and lays itself out down to 720 px tall. The window shows it
	// scaled so it fits:
	//   - a window narrower than the design, or shorter than the page's minimum height, zooms the whole page
	//     out (nothing is cut: no part of the header or the tracks is ever off the window);
	//   - a window larger than the design both ways zooms it up as far as the smaller side allows, so the
	//     page keeps its proportions on a large screen (no stretched faders, no empty band under the
	//     panels); a wider window than that lays the page out wider at that zoom;
	//   - between the two, 100 %: the page lays itself out in the window.
	// The user's own zoom (the editor's menu, Cmd - / Cmd + / Cmd 0, remembered in the editor's config)
	// multiplies it: below 100 % the page has more room, above it is larger.
	namespace pageZoom
	{
		struct Design
		{
			int width = 1440;		// the page's design size
			int height = 924;
			int minHeight = 720;	// below it the page is zoomed out rather than squeezed
		};

		// The user's zoom steps, as a browser has them.
		inline constexpr std::array<double, 11> g_steps{0.5, 0.67, 0.75, 0.8, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0};

		inline double clampUser(const double _user)
		{
			if(!std::isfinite(_user) || _user <= 0)
				return 1.0;
			return std::clamp(_user, g_steps.front(), g_steps.back());
		}

		// The zoom that fits the design into a window _width x _height points.
		inline double fit(const int _width, const int _height, const Design& _d = {})
		{
			if(_width <= 0 || _d.width <= 0)
				return 1.0;
			const double w = static_cast<double>(_width) / _d.width;
			const double hMin = _height > 0 && _d.minHeight > 0 ? static_cast<double>(_height) / _d.minHeight : w;
			const double down = std::min(w, hMin);
			if(down < 1.0)
				return down;
			const double h = _height > 0 && _d.height > 0 ? static_cast<double>(_height) / _d.height : 1.0;
			return std::max(1.0, std::min(w, h));
		}

		// The web view's zoom for a window _width x _height points with the user's zoom.
		inline double effective(const int _width, const int _height, const double _user, const Design& _d = {})
		{
			return fit(_width, _height, _d) * clampUser(_user);
		}

		// The next step from _user: _step -1 smaller, +1 larger, 0 back to 100 %. A zoom between steps goes to
		// the nearest step in that direction.
		inline double step(const double _user, const int _step)
		{
			if(_step == 0)
				return 1.0;
			const double u = clampUser(_user);
			if(_step > 0)
			{
				for(const double s : g_steps)
					if(s > u + 0.001)
						return s;
				return g_steps.back();
			}
			for(auto it = g_steps.rbegin(); it != g_steps.rend(); ++it)
				if(*it < u - 0.001)
					return *it;
			return g_steps.front();
		}
	}
}
