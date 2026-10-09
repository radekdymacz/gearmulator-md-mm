// The page bridge's transport on the plug-in's side (mdPageBridge.h, review finding 13): the page's long batches
// joined from their pieces, the outbox split into numbered gm.recv calls at message boundaries, and Linux's batch
// files written strictly in order (FileOutbox). Pure. Also the route of the plug-in's notices to the page
// (juceUiLib/messageRoute.h): one sink per open window; and a window's notices and their answers (mdNoticeBook.h).
#include "mdNoticeBook.h"
#include "mdPageBridge.h"

#include "juceUiLib/messageRoute.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	int g_failures = 0;

	void check(const bool _ok, const char* _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what);
		if(!_ok)
			++g_failures;
	}

	namespace json = elektronData::json;
	namespace bridge = mdJucePlugin::pageBridge;
}

int main()
{
	std::printf("mdPageBridgeTest\n");
	// pieces: any order, joined before decoding (an escape's bytes of one character may span two pieces)
	{
		bridge::Pieces p;
		const std::string escaped = "%5B%7B%22op%22%3A%22kitName%22%2C%22n%22%3A%22%E2%82%AC%22%7D%5D";	// [{"op":"kitName","n":"€"}]
		const auto a = escaped.substr(0, 40), b = escaped.substr(40, 12), c = escaped.substr(52);
		check(!p.add(std::string(bridge::g_piece) + "7/2/3/" + c), "a last piece first: nothing yet");
		check(!p.add(std::string(bridge::g_piece) + "7/0/3/" + a), "the first piece: nothing yet");
		const auto joined = p.add(std::string(bridge::g_piece) + "7/1/3/" + b);
		check(joined && *joined == escaped && p.waiting() == 0, "all three: the batch, joined in index order");
		check(!p.add(std::string(bridge::g_piece) + "x/0/1/abc") && !p.add(std::string(bridge::g_piece) + "1/1/1/abc")
			&& !p.add(std::string(bridge::g_command) + "abc"), "a malformed piece is not a batch");
		for(int seq = 10; seq < 20; ++seq)
			p.add(std::string(bridge::g_piece) + std::to_string(seq) + "/0/2/x");
		check(p.waiting() <= 4, "batches whose pieces never all come are dropped");
		const auto one = p.add(std::string(bridge::g_piece) + "30/0/1/abc");
		check(one && *one == "abc", "a batch of one piece");
	}
	// the outbox: one call while it fits (as json::write of the array), else split at message boundaries
	{
		std::vector<json::Value> outbox;
		for(int i = 0; i < 5; ++i)
		{
			auto m = json::Value::object();
			m.set("type", "result");
			m.set("id", i);
			m.set("note", std::string(100, 'a'));
			outbox.push_back(m);
		}
		const auto all = bridge::recvScripts(outbox, 1);
		check(all.size() == 1 && all[0] == "javascript:window.gm&&gm.recv(" + json::write(json::Value(json::Value::Array(outbox.begin(), outbox.end()))) + ",1)",
			"an outbox that fits: one call, the messages as json::write of the array, then the batch number");
		const auto split = bridge::recvScripts(outbox, 7, 300);
		bool order = split.size() == 3;
		std::string rejoined;
		for(size_t i = 0; i < split.size(); ++i)
		{
			const auto& s = split[i];
			const auto open = std::string("javascript:window.gm&&gm.recv([");
			const auto close = "]," + std::to_string(7 + i) + ")";
			order = order && s.rfind(open, 0) == 0 && s.size() >= close.size() && s.compare(s.size() - close.size(), close.size(), close) == 0
				&& s.size() <= open.size() + 300 + close.size();
			rejoined += (rejoined.empty() ? "" : ",") + s.substr(open.size(), s.size() - open.size() - close.size());
		}
		check(order && "[" + rejoined + "]" == json::write(json::Value(json::Value::Array(outbox.begin(), outbox.end()))),
			"a long outbox: calls of at most the limit, in order, numbered on from the first, every message once");
		const auto big = bridge::recvScripts(outbox, 1, 10);
		check(big.size() == 5 && big[4].size() > 3 && big[4].compare(big[4].size() - 3, 3, ",5)") == 0, "a message longer than the limit goes alone, with its own number");
		// what the host does: the next flush goes on from the last number, so a batch the view replays (JUCE 7's
		// reloadLastURL) carries a number the page has had
		uint64_t next = 1;
		const auto first = bridge::recvScripts(outbox, next);
		next += first.size();
		const auto second = bridge::recvScripts(outbox, next, 300);
		check(next == 2 && second.size() == 3 && second[0].find("],2)") != std::string::npos && second[2].find("],4)") != std::string::npos,
			"successive flushes: the numbers go on, never repeat");
		check(bridge::recvScripts({}, 1).empty(), "nothing to send: no call (and no number used)");
		// Linux: the same calls as script files beside the page, and the page's progress
		const auto files = bridge::recvScripts(outbox, 3, bridge::g_maxRecvBytes, "");
		check(files.size() == 1 && files[0] == "window.gm&&gm.recv(" + json::write(json::Value(json::Value::Array(outbox.begin(), outbox.end()))) + ",3)",
			"a script file holds the same call, without javascript:");
		check(bridge::recvFileName("gearmulator-mdStudio-1a2b.html", 12) == "gearmulator-mdStudio-1a2b.html.recv-12.js",
			"a batch's file is named after the page file and the batch number");
		check(bridge::ackOf("gmbridge://a/17") == std::optional<uint64_t>(17) && bridge::ackOf("gmbridge://a/0") == std::optional<uint64_t>(0),
			"the page's progress: the last batch it read");
		check(!bridge::ackOf("gmbridge://a/") && !bridge::ackOf("gmbridge://a/1x") && !bridge::ackOf("gmbridge://c/1")
			&& !bridge::ackOf("gmbridge://a/99999999999999999999"), "anything else is not a progress report");
	}
	// Linux's batch files (codex review 2026-10, item 5): a file that could not be written holds back every later one
	// (the page reads in order) and is the page's only once written; a backlog too large or too old is dropped and the
	// page is loaded again
	{
		using Outbox = bridge::FileOutbox;
		std::vector<uint64_t> written;	// the files the page can read (the writer registers them), in write order
		uint64_t failAt = 3;			// the write of this batch fails, 0: none
		bool ownScripts = true;
		Outbox files([&](const uint64_t _seq, const std::string& _script)
		{
			if(_seq == failAt)
				return false;
			ownScripts = ownScripts && _script == "s" + std::to_string(_seq);
			written.push_back(_seq);
			return true;
		});
		const auto scripts = [](const uint64_t _first, const size_t _n)
		{
			std::vector<std::string> out;
			for(size_t i = 0; i < _n; ++i)
				out.push_back("s" + std::to_string(_first + i));
			return out;
		};
		const auto list = [&] { std::string t; for(const auto s : written) t += (t.empty() ? "" : ",") + std::to_string(s); return t; };
		files.add(1, scripts(1, 2));
		check(files.pump(0) == Outbox::Change::None && list() == "1,2" && files.waiting() == 0, "batches 1 and 2 written, in order");
		files.add(3, scripts(3, 1));
		check(files.pump(33) == Outbox::Change::Stalled && list() == "1,2" && files.waiting() == 1 && files.stalledAt() == std::optional<uint64_t>(3),
			"batch 3 cannot be written: it waits, nothing registered for it, the stall said once");
		files.add(4, scripts(4, 2));
		check(files.pump(66) == Outbox::Change::None && list() == "1,2" && files.waiting() == 3,
			"batches 4 and 5 wait behind 3 (the page reads in order): not written, not registered; said only once");
		check(files.pump(99) == Outbox::Change::None && list() == "1,2", "a pump with nothing new tries batch 3 again");
		failAt = 0;
		check(files.pump(132) == Outbox::Change::Recovered && list() == "1,2,3,4,5" && files.waiting() == 0 && files.bytes() == 0 && !files.stalledAt(),
			"writing works again: 3, 4 and 5 written in order, nothing waits");
		files.add(6, scripts(6, 1));
		check(files.pump(165) == Outbox::Change::None && list() == "1,2,3,4,5,6" && !files.resync(), "then on as before");
		// a stall measured from the last batch that went: progress starts it again
		failAt = 8;
		files.add(7, scripts(7, 2));
		check(files.pump(1000) == Outbox::Change::Stalled && list() == "1,2,3,4,5,6,7", "7 written, 8 waits");
		check(files.pump(2900) == Outbox::Change::None && !files.resync(), "under 2 s of stall: still waiting");
		check(files.pump(3001) == Outbox::Change::Dropped && files.resync() && files.waiting() == 0 && files.bytes() == 0
			&& files.dropped() == std::make_pair(size_t(1), std::string("s8").size()), "over 2 s without a batch going: dropped, resync due");
		files.add(9, scripts(9, 1));
		failAt = 0;
		check(files.pump(3100) == Outbox::Change::None && files.waiting() == 0 && list() == "1,2,3,4,5,6,7",
			"while a resync is due nothing is queued or written (the page is loaded again and gets everything anew)");
		files.restart();
		files.add(1, scripts(1, 1));
		check(!files.resync() && files.pump(3200) == Outbox::Change::None && written.back() == 1, "the page started again: from batch 1, written");
		check(ownScripts, "the writer got each batch's own script");
		// a backlog past 8 MiB while stalled is dropped at once
		failAt = 2;
		files.add(2, {std::string("s2")});
		check(files.pump(4000) == Outbox::Change::Stalled, "a new stall");
		files.add(3, {std::string(Outbox::g_maxBacklogBytes, 'x')});
		check(files.pump(4001) == Outbox::Change::Dropped && files.resync() && files.waiting() == 0 && files.bytes() == 0,
			"more than 8 MiB waiting: the queue is emptied and the resync flag set, without waiting 2 s");
		files.restart();
		check(!files.resync() && files.waiting() == 0, "restart clears the resync");
	}
	// a window's notices (codex review 2026-10, item 1): the page answers {"notice": n, "button": b}; an answer runs its
	// notice's callback once, and only for a notice waiting with that button
	{
		mdJucePlugin::NoticeBook book;
		std::vector<std::string> ran;
		const int a = book.add(2, [&](const int _b) { ran.push_back("a" + std::to_string(_b)); });
		const int b = book.add(0, [&](const int _b) { ran.push_back("b" + std::to_string(_b)); });
		check(a == 1 && b == 2 && book.size() == 2, "notices are numbered from 1 in the order they come");
		check(book.answer(99, 0) == "notice 99 is not waiting for an answer" && ran.empty(), "an unknown notice: refused, nothing runs");
		check(book.answer(a, 2) == "notice 1 has no button 2" && book.answer(a, -1) == "notice 1 has no button -1" && ran.empty() && book.waiting(a),
			"a button the notice does not have: refused, nothing runs, the notice still waits");
		check(book.answer(a, 1).empty() && ran.size() == 1 && ran[0] == "a1" && !book.waiting(a), "its own button: the callback runs with it, once");
		check(book.answer(a, 0) == "notice 1 is not waiting for an answer" && ran.size() == 1, "answered already: refused, nothing runs again");
		check(book.answer(b, 0).empty() && ran.back() == "b0" && book.answer(b, 1) == "notice 2 is not waiting for an answer",
			"a notice without buttons is the page's OK: button 0, once");
		// the update banner: a newer one replaces it, the old one's answer is not wanted
		int banner = book.add(3, [&](const int _b) { ran.push_back("old" + std::to_string(_b)); });
		book.forget(banner);
		banner = book.add(1, [&](const int _b) { ran.push_back("new" + std::to_string(_b)); });
		check(book.answer(banner - 1, 0) == "notice " + std::to_string(banner - 1) + " is not waiting for an answer" && ran.back() == "b0",
			"a replaced banner is forgotten: its answer runs nothing");
		check(book.answer(banner, 0).empty() && ran.back() == "new0", "the banner shown takes its answer");
		// a callback may add the next notice (the banner's next state) while it runs
		int next = 0;
		const int c = book.add(1, [&](const int) { next = book.add(1, {}); });
		check(book.answer(c, 0).empty() && next == c + 1 && book.waiting(next) && book.answer(next, 0).empty(),
			"a callback that adds a notice: the new one waits; one without a callback takes its answer");
		check(book.size() == 0, "nothing left waiting");
	}
	// notices: each window its own instance's; closing one never takes another's (release review 2026-10-04, S4)
	{
		namespace route = genericUI::messageRoute;
		int ownerA = 0, ownerB = 0;
		const void* a = &ownerA;
		const void* b = &ownerB;
		std::vector<std::string> toA, toB;
		const auto notice = [](const std::string& _t) { return route::Notice{_t, "", {"OK"}, {}}; };
		const auto from = [&](const void* _owner, const std::string& _t)
		{
			const route::OwnerScope scope(_owner);
			return route::offer(notice(_t));
		};
		check(!route::offer(notice("off")), "the route off: the native box shows");
		route::enable();
		check(from(a, "a1"), "no window open: the notice waits");
		auto winA = route::attach(a, [&](route::Notice _n) { toA.push_back(_n.title); });
		check(toA.size() == 1 && toA[0] == "a1", "A's window opens: A's waiting notice goes to it");
		auto winB = route::attach(b, [&](route::Notice _n) { toB.push_back(_n.title); });
		check(from(a, "a2") && from(b, "b1") && toA.back() == "a2" && toB.size() == 1 && toB[0] == "b1",
			"two windows: each instance's notices go to its own window");
		check(route::offer(notice("anon")) && toB.back() == "anon", "a notice of nobody's goes to the newest window");
		winA.reset();
		check(from(b, "b2") && toB.back() == "b2", "closing A's window leaves B's sink in place");
		check(from(a, "a3") && toA.back() == "a2" && toB.back() == "b2", "A's notice while its window is closed waits (not B's)");
		auto winA2 = route::attach(a, [&](route::Notice _n) { toA.push_back(_n.title); });
		check(toA.back() == "a3", "A's window opens again: the notice that waited reaches it");
		winA2.reset();
		winB.reset();
		route::forget(a);
		size_t taken = 0;
		while(taken < route::g_maxWaiting + 4 && from(b, "w"))
			++taken;
		check(taken == route::g_maxWaiting, "past the waiting limit the native box shows (never dropped silently)");
		route::forget(b);
		check(from(b, "after forget"), "an instance gone: its waiting notices go, the room is free again");
		route::forget(b);
		{
			route::Attachment moved = route::attach(a, [&](route::Notice _n) { toA.push_back(_n.title); });
			route::Attachment other = std::move(moved);
			check(!moved.attached() && other.attached() && from(a, "moved") && toA.back() == "moved", "a moved attachment keeps the sink");
		}
		const auto before = toA.size();
		check(from(a, "gone") && toA.size() == before, "the attachment destroyed: its sink is gone");
		route::forget(a);
	}
	if(g_failures)
	{
		std::fprintf(stderr, "mdPageBridgeTest: %d failure(s)\n", g_failures);
		return 1;
	}
	std::puts("mdPageBridgeTest: PASS");
	return 0;
}
