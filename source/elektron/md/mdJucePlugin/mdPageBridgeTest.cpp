// The page bridge's transport on the plug-in's side (mdPageBridge.h, review finding 13): the page's long batches
// joined from their pieces, and the outbox split into numbered gm.recv calls at message boundaries. Pure. Also
// the route of the plug-in's notices to the page (juceUiLib/messageRoute.h): one sink per open window.
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
