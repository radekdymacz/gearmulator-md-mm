// The page bridge's transport on the plug-in's side (mdPageBridge.h, review finding 13): the page's long batches
// joined from their pieces, and the outbox split into gm.recv calls at message boundaries. Pure.
#include "mdPageBridge.h"

#include <cstdio>
#include <string>

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
		const auto all = bridge::recvScripts(outbox);
		check(all.size() == 1 && all[0] == "javascript:window.gm&&gm.recv(" + json::write(json::Value(json::Value::Array(outbox.begin(), outbox.end()))) + ")",
			"an outbox that fits: one call, the same text as before");
		const auto split = bridge::recvScripts(outbox, 300);
		bool order = split.size() == 3;
		std::string rejoined;
		for(const auto& s : split)
		{
			const auto open = std::string("javascript:window.gm&&gm.recv([");
			order = order && s.rfind(open, 0) == 0 && s.size() <= open.size() + 300 + 2;
			rejoined += (rejoined.empty() ? "" : ",") + s.substr(open.size(), s.size() - open.size() - 2);
		}
		check(order && "[" + rejoined + "]" == json::write(json::Value(json::Value::Array(outbox.begin(), outbox.end()))),
			"a long outbox: calls of at most the limit, in order, every message once");
		const auto big = bridge::recvScripts(outbox, 10);
		check(big.size() == 5, "a message longer than the limit goes alone");
		check(bridge::recvScripts({}).empty(), "nothing to send: no call");
	}
	if(g_failures)
	{
		std::fprintf(stderr, "mdPageBridgeTest: %d failure(s)\n", g_failures);
		return 1;
	}
	std::puts("mdPageBridgeTest: PASS");
	return 0;
}
