#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <utility>

namespace mdJucePlugin
{
	// The plug-in's notices a window's page shows and the answers they wait for (PageEditor: the notice route's
	// questions and the update banner), pure. A notice gets its number here, from 1 and never again in this book; the
	// page's answer names it ({"op":"noticeAnswer","notice":n,"button":b}, deskHost.cpp: "id" is the request's own,
	// the bridge numbers every request) and runs its callback once. An answer to a notice that is not waiting
	// (unknown, answered already, forgotten: a newer banner replaced it) or with a button the notice does not have
	// runs nothing and says why, for the request's result.
	class NoticeBook
	{
	public:
		using Answered = std::function<void(int)>;

		// A notice with _buttons keys (none: the page shows one, OK, so one); its number. _answered may be empty.
		int add(const size_t _buttons, Answered _answered)
		{
			const int notice = ++m_last;
			m_waiting[notice] = {std::max<size_t>(1, _buttons), std::move(_answered)};
			return notice;
		}

		// The page's answer: "" when the notice took it (its callback ran), else why not (nothing ran).
		std::string answer(const int _notice, const int _button)
		{
			const auto it = m_waiting.find(_notice);
			if(it == m_waiting.end())
				return "notice " + std::to_string(_notice) + " is not waiting for an answer";
			if(_button < 0 || static_cast<size_t>(_button) >= it->second.buttons)
				return "notice " + std::to_string(_notice) + " has no button " + std::to_string(_button);
			// out of the book before the callback runs: it may add or forget notices (the banner's next state)
			auto answered = std::move(it->second.answered);
			m_waiting.erase(it);
			if(answered)
				answered(_button);
			return {};
		}

		// The notice's answer is no longer wanted (a newer banner replaced it on the page).
		void forget(const int _notice) { m_waiting.erase(_notice); }

		bool waiting(const int _notice) const { return m_waiting.count(_notice) != 0; }
		size_t size() const { return m_waiting.size(); }

	private:
		struct Notice
		{
			size_t buttons = 1;
			Answered answered;
		};
		int m_last = 0;
		std::map<int, Notice> m_waiting;
	};
}
