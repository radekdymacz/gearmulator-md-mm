#pragma once

#include "elektronData/json.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace mdJucePlugin
{
	// What the page shows for a notice: the notice message but its number (noticeMessage)
	struct NoticeShown
	{
		std::string title;
		std::string text;
		std::vector<std::string> buttons;	// none: the page shows one, OK
		bool modal = true;					// false: the banner (one at a time, at the window's bottom)
	};

	// The page's notice message for notice _notice (number 0: a banner that waits for no answer, or takes it away)
	inline elektronData::json::Value noticeMessage(const int _notice, const NoticeShown& _shown)
	{
		namespace json = elektronData::json;
		auto m = json::Value::object();
		m.set("type", "notice");
		m.set("id", _notice);
		m.set("title", _shown.title);
		m.set("text", _shown.text);
		auto buttons = json::Value::array();
		for(const auto& b : _shown.buttons)
			buttons.push(json::Value(b));
		m.set("buttons", std::move(buttons));
		if(!_shown.modal)
			m.set("modal", false);
		return m;
	}

	// The plug-in's notices a window's page shows and the answers they wait for (PageEditor: the notice route's
	// questions and the update banner), pure. A notice gets its number here, from 1 and never again in this book; the
	// page's answer names it ({"op":"noticeAnswer","notice":n,"button":b}, deskHost.cpp: "id" is the request's own,
	// the bridge numbers every request) and runs its callback once. An answer to a notice that is not waiting
	// (unknown, answered already, forgotten: a newer banner replaced it) or with a button the notice does not have
	// runs nothing and says why, for the request's result. A page that started again (Linux: WebPageHost's restart) has
	// lost what it showed: the notices still waiting are shown again by their numbers (waitingNotices).
	class NoticeBook
	{
	public:
		using Answered = std::function<void(int)>;

		// A notice as the page shows it (no buttons: the page shows one, OK, so one key); its number. _answered may be
		// empty.
		int add(NoticeShown _shown, Answered _answered)
		{
			const int notice = ++m_last;
			const auto keys = std::max<size_t>(1, _shown.buttons.size());
			m_waiting[notice] = {keys, std::move(_answered), std::move(_shown)};
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

		// The notices still waiting for an answer, oldest first, as the page showed them
		std::vector<std::pair<int, NoticeShown>> waitingNotices() const
		{
			std::vector<std::pair<int, NoticeShown>> result;
			for(const auto& [notice, waiting] : m_waiting)
				result.emplace_back(notice, waiting.shown);
			return result;
		}

	private:
		struct Notice
		{
			size_t buttons = 1;
			Answered answered;
			NoticeShown shown;
		};
		int m_last = 0;
		std::map<int, Notice> m_waiting;
	};
}
