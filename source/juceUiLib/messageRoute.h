#pragma once

// Where a message box goes. The Machinedrum/Monomachine Editors show no native alert: their window is a
// page, and a question or a warning is the page's (a modal in its own style). An editor turns the route on
// (enable) and each open window takes messages (attach). genericUI::MessageBox offers each message here
// first (doc/modern-ux/UPSTREAM.md); when the route is off, or for the products that do not use it, the
// native box shows as before.
//
// Several windows can be open at once (two instances in a DAW), so a window is one of several sinks, each with
// its owner (its plug-in instance) and a token (Attachment) that removes that sink only (release review
// 2026-10-04, S4: one global sink, cleared by whichever window closed). A message goes to:
//   - its owner's window, when the code that shows it says whose it is (OwnerScope on the calling thread);
//   - else the window attached last.
// A message whose window is not open waits for it (at most g_maxWaiting); past that, and never silently, the
// native box shows.

#include "messageBox.h"

#include <cstddef>
#include <cstdint>
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
		using Owner = const void*;	// a plug-in instance (its processor); nullptr: nobody's in particular

		constexpr size_t g_maxWaiting = 16;

		struct State
		{
			struct Entry
			{
				uint64_t token;
				Owner owner;
				Sink sink;
			};
			struct Waiting
			{
				Owner owner;
				Notice notice;
			};
			std::mutex mutex;
			bool enabled = false;
			uint64_t nextToken = 1;
			std::vector<Entry> sinks;		// in the order they were attached
			std::vector<Waiting> waiting;
		};

		inline State& state()
		{
			static State s;
			return s;
		}

		// The owner of the messages shown on this thread now (OwnerScope); nullptr outside one.
		inline Owner& currentOwner()
		{
			thread_local Owner owner = nullptr;
			return owner;
		}

		// While it lives, the messages this thread shows are _owner's (they go to its window).
		class OwnerScope
		{
		public:
			explicit OwnerScope(const Owner _owner) : m_previous(currentOwner()) { currentOwner() = _owner; }
			~OwnerScope() { currentOwner() = m_previous; }
			OwnerScope(const OwnerScope&) = delete;
			OwnerScope& operator=(const OwnerScope&) = delete;
		private:
			const Owner m_previous;
		};

		inline void enable()
		{
			const std::lock_guard<std::mutex> lock(state().mutex);
			state().enabled = true;
		}

		inline void detach(const uint64_t _token)
		{
			const std::lock_guard<std::mutex> lock(state().mutex);
			auto& sinks = state().sinks;
			for(auto it = sinks.begin(); it != sinks.end(); ++it)
			{
				if(it->token != _token)
					continue;
				sinks.erase(it);
				return;
			}
		}

		// A window's hold on the route: destroying it (or reset) removes that window's sink, and only that one.
		class Attachment
		{
		public:
			Attachment() = default;
			explicit Attachment(const uint64_t _token) : m_token(_token) {}
			~Attachment() { reset(); }
			Attachment(Attachment&& _other) noexcept : m_token(std::exchange(_other.m_token, 0)) {}
			Attachment& operator=(Attachment&& _other) noexcept
			{
				if(this != &_other)
				{
					reset();
					m_token = std::exchange(_other.m_token, 0);
				}
				return *this;
			}
			Attachment(const Attachment&) = delete;
			Attachment& operator=(const Attachment&) = delete;

			void reset()
			{
				if(m_token)
					detach(std::exchange(m_token, 0));
			}
			bool attached() const { return m_token != 0; }

		private:
			uint64_t m_token = 0;
		};

		// _owner's window takes messages (its own, and nobody's while it is the newest window); the ones that
		// waited for it go to it first.
		[[nodiscard]] inline Attachment attach(const Owner _owner, Sink _sink)
		{
			std::vector<Notice> waiting;
			uint64_t token;
			{
				const std::lock_guard<std::mutex> lock(state().mutex);
				auto& s = state();
				token = s.nextToken++;
				s.sinks.push_back({token, _owner, _sink});
				for(auto it = s.waiting.begin(); it != s.waiting.end();)
				{
					if(it->owner == nullptr || it->owner == _owner)
					{
						waiting.push_back(std::move(it->notice));
						it = s.waiting.erase(it);
					}
					else
						++it;
				}
			}
			for(auto& n : waiting)
				_sink(std::move(n));
			return Attachment(token);
		}

		// _owner is gone (its plug-in instance closes): the messages still waiting for its window are dropped.
		inline void forget(const Owner _owner)
		{
			if(_owner == nullptr)
				return;
			std::vector<Notice> dropped;	// destroyed outside the lock (their callbacks may hold anything)
			{
				const std::lock_guard<std::mutex> lock(state().mutex);
				auto& waiting = state().waiting;
				for(auto it = waiting.begin(); it != waiting.end();)
				{
					if(it->owner == _owner)
					{
						dropped.push_back(std::move(it->notice));
						it = waiting.erase(it);
					}
					else
						++it;
				}
			}
		}

		// true: taken (shown by a window, or waiting for one), the caller shows nothing. false: the caller shows
		// the native box (the route is off, or too many wait).
		inline bool offer(Notice _n)
		{
			const auto owner = currentOwner();
			Sink sink;
			{
				const std::lock_guard<std::mutex> lock(state().mutex);
				auto& s = state();
				if(!s.enabled)
					return false;
				for(auto it = s.sinks.rbegin(); it != s.sinks.rend() && !sink; ++it)
					if(it->owner == owner)
						sink = it->sink;
				if(!sink && owner == nullptr && !s.sinks.empty())
					sink = s.sinks.back().sink;
				if(!sink)
				{
					// its window is not open: it waits for it, a few at most
					if(s.waiting.size() >= g_maxWaiting)
						return false;
					s.waiting.push_back({owner, std::move(_n)});
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
