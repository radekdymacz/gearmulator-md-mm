#pragma once

#include <algorithm>

namespace jucePluginEditorLib
{
	// Where a standalone window goes on its screen, as plain values (P7). No JUCE, so it is tested on
	// its own (windowFitTest). All in screen points, top-left origin.
	namespace windowFit
	{
		struct Rect
		{
			int x = 0, y = 0, w = 0, h = 0;
			bool operator==(const Rect& _o) const { return x == _o.x && y == _o.y && w == _o.w && h == _o.h; }
		};

		// The window's own frame around its content (the native title bar on top).
		struct Frame
		{
			int top = 0, left = 0, bottom = 0, right = 0;
		};

		// The content (the editor) of a window with this frame, placed so the whole window, frame
		// included, is inside the visible area (the screen without the menu bar and the Dock): no
		// larger than it (never smaller than _min), and moved in when it hangs over an edge.
		// _keepAspect: shrink both sides by the same factor (an editor drawn to a fixed size).
		inline Rect fit(const Rect& _content, const Frame& _frame, const Rect& _visible, const int _minW, const int _minH, const bool _keepAspect)
		{
			const int maxW = std::max(_minW, _visible.w - _frame.left - _frame.right);
			const int maxH = std::max(_minH, _visible.h - _frame.top - _frame.bottom);
			Rect r = _content;
			if(_keepAspect && _content.w > 0 && _content.h > 0)
			{
				const double s = std::min({1.0, static_cast<double>(maxW) / _content.w, static_cast<double>(maxH) / _content.h});
				r.w = std::max(_minW, static_cast<int>(_content.w * s));
				r.h = std::max(_minH, static_cast<int>(_content.h * s));
			}
			else
			{
				r.w = std::clamp(_content.w, _minW, maxW);
				r.h = std::clamp(_content.h, _minH, maxH);
			}
			// The whole window inside: its right and bottom edges first, then its left and top ones win.
			const int left = _visible.x + _frame.left, top = _visible.y + _frame.top;
			const int right = _visible.x + _visible.w - _frame.right, bottom = _visible.y + _visible.h - _frame.bottom;
			r.x = std::max(left, std::min(r.x, right - r.w));
			r.y = std::max(top, std::min(r.y, bottom - r.h));
			return r;
		}

		// A plug-in's editor (B-001): the host places its window and draws its own bars around it (a title bar,
		// Live's device bar: g_hostBars tall, a guess that holds for the hosts we know), so the editor only
		// chooses its size: no larger than the visible area less those bars, never smaller than _min. A window
		// the size of the design (1440 x 924) on a 1440 x 900 screen otherwise hangs off the screen, its
		// right and bottom parts and its resize corner out of reach. The position is the content's own.
		inline constexpr int g_hostBars = 64;

		inline Rect fitPluginSize(const Rect& _content, const Rect& _visible, const int _minW, const int _minH, const bool _keepAspect)
		{
			auto r = fit(_content, {g_hostBars, 0, 0, 0}, _visible, _minW, _minH, _keepAspect);
			r.x = _content.x;
			r.y = _content.y;
			return r;
		}
	}
}
