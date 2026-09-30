#pragma once

// Where a message box goes. The Machinedrum/Monomachine Editors show no native alert: their window is a
// page, and a question or a warning is the page's (a modal in its own style). An editor turns the route on
// (enable) and its window takes the messages (setSink). genericUI::MessageBox offers each message here
// first (doc/modern-ux/UPSTREAM.md); when the route is off, or for the products that do not use it, the
// native box shows as before. A message that comes while no window is open waits for the next one.

#include "messageBox.h"

#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace genericUI
{
	namespace messageRoute
	{
		struct Notice
		{
			std::string title;
			std::string text;
			std::vector<std::string> buttons;		// the last one is the safe answer (No, Cancel, OK)
			std::function<void(int)> answered;		// the button pressed (its index); never called if the window closes
		};

		using Sink = std::function<void(Notice)>;

		struct State
		{
			std::mutex mutex;
			bool enabled = false;
			Sink sink;
			std::vector<Notice> waiting;
		};

		inline State& state()
		{
			static State s;
			return s;
		}

		inline void enable()
		{
			const std::lock_guard<std::mutex> lock(state().mutex);
			state().enabled = true;
		}

		// A window takes the messages (or none, with an empty sink); the ones that waited go to it first.
		inline void setSink(Sink _sink)
		{
			std::vector<Notice> waiting;
			Sink sink;
			{
				const std::lock_guard<std::mutex> lock(state().mutex);
				state().sink = std::move(_sink);
				sink = state().sink;
				if(sink)
					waiting.swap(state().waiting);
			}
			for(auto& n : waiting)
				sink(std::move(n));
		}

		// true: taken, the caller shows nothing.
		inline bool offer(Notice _n)
		{
			Sink sink;
			{
				const std::lock_guard<std::mutex> lock(state().mutex);
				if(!state().enabled)
					return false;
				sink = state().sink;
				if(!sink)
				{
					if(state().waiting.size() < 16)
						state().waiting.push_back(std::move(_n));
					return true;
				}
			}
			sink(std::move(_n));
			return true;
		}

		inline bool offerAsk(const std::string& _title, const std::string& _text, std::vector<std::string> _buttons, const MessageBox::Callback& _callback)
		{
			return offer({_title, _text, std::move(_buttons), [_callback](const int _i) { if(_callback) _callback(_i == 0 ? MessageBox::Result::Yes : MessageBox::Result::No); }});
		}

		inline bool offerOk(const std::string& _title, const std::string& _text, const std::function<void()>& _callback = {})
		{
			return offer({_title, _text, {"OK"}, [_callback](int) { if(_callback) _callback(); }});
		}
	}
}
