// P1 data layer: capture Machinedrum user-data dumps from the running MD OS 1.63
// firmware, headless, for the elektronData codec tests.
//
//   mdDataCaptureFirmwareTest dump <MD-1.63-ROM> <out-dir> [device-state.mdst]
//       Boot (optionally from a saved plug-in device state), then request every
//       global (8), kit (64), pattern (128) and song (32) and write each reply
//       to <out-dir>/<type>_<nn>.syx.
//   mdDataCaptureFirmwareTest script <MD-1.63-ROM> <out-dir> <script.txt> [device-state.mdst]
//       Boot, then run a line script: "send <hex bytes>", "sendfile <path>",
//       "cc <ch> <cc> <value>", "wait <ms>", "play", "stop",
//       "req <type> <n> <name>" (type global|kit|pattern|song, reply saved as
//       <out-dir>/<name>.syx), "status <param>".
//
// Requires user-supplied firmware; exits 77 (skip) without it.

#include "mdLib/mdhardware.h"
#include "mdLib/mdpanel.h"
#include "mdLib/mdstate.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using Bytes = std::vector<uint8_t>;

	constexpr uint32_t g_block = 64;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	Bytes load(const std::string& _path)
	{
		std::ifstream s(_path, std::ios::binary);
		require(s.good(), "cannot read " + _path);
		return {std::istreambuf_iterator<char>(s), std::istreambuf_iterator<char>()};
	}

	void save(const std::string& _path, const Bytes& _data)
	{
		std::ofstream s(_path, std::ios::binary);
		s.write(reinterpret_cast<const char*>(_data.data()), static_cast<std::streamsize>(_data.size()));
	}

	class Machine
	{
	public:
		Machine(const Bytes& _rom, const std::string& _romName, const Bytes& _patchRam)
			: m_hw(_rom, _romName, md::MachineModel::Machinedrum, _patchRam)
		{
			require(m_hw.isValid(), "firmware did not construct a valid machine");
			uint32_t frames = 0;
			while(!m_hw.isFirmwareMidiReady())
			{
				m_hw.advance(g_block);
				frames += g_block;
				require(frames < 44100 * 60, "MD firmware boot timed out");
			}
			// The splash animation keeps running for ~20 s after MIDI is ready.
			advanceMs(25000);
		}

		void advanceMs(const uint32_t _ms)
		{
			const auto blocks = static_cast<uint64_t>(_ms) * 44100 / 1000 / g_block;
			for(uint64_t i = 0; i < blocks; ++i)
			{
				m_hw.advance(g_block);
				drain();
			}
		}

		void send(const Bytes& _bytes)
		{
			synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
			if(!_bytes.empty() && _bytes[0] == 0xf0)
				e.sysex.assign(_bytes.begin(), _bytes.end());
			else
			{
				e.a = _bytes.size() > 0 ? _bytes[0] : 0;
				e.b = _bytes.size() > 1 ? _bytes[1] : 0;
				e.c = _bytes.size() > 2 ? _bytes[2] : 0;
			}
			m_hw.sendMidi(e);
			waitIdle();
		}

		void waitIdle()
		{
			for(uint32_t f = 0; f < 44100 * 10; f += g_block)
			{
				if(m_hw.isMidiIngressIdle())
					return;
				m_hw.advance(g_block);
				drain();
			}
			throw std::runtime_error("MIDI ingress did not drain");
		}

		Bytes request(const Bytes& _request, const uint8_t _reply)
		{
			m_rx.clear();
			send(_request);
			for(uint32_t f = 0; f < 44100 * 5; f += g_block)
			{
				for(const auto& m : m_rx)
					if(m.size() > 7 && m[6] == _reply)
						return m;
				m_hw.advance(g_block);
				drain();
			}
			return {};
		}

		uint8_t read8(const uint32_t _address) { return m_hw.getUC().read8(_address); }

		void panel(const md::PanelControl _control)
		{
			const auto packet = md::panelPacket(md::MachineModel::Machinedrum, _control);
			require(packet.has_value(), "no panel packet");
			m_hw.trySendPanelEvent(packet->row, packet->mask);
			advanceMs(50);
			m_hw.trySendPanelEvent(packet->row, 0);
			advanceMs(50);
		}

	private:
		void drain()
		{
			m_events.clear();
			m_hw.readMidiOut(m_events);
			for(const auto& e : m_events)
				if(!e.sysex.empty())
					m_rx.emplace_back(e.sysex.begin(), e.sysex.end());
		}

		md::Hardware m_hw;
		std::vector<synthLib::SMidiEvent> m_events;
		std::vector<Bytes> m_rx;
	};

	Bytes requestMessage(const uint8_t _command, const uint8_t _value)
	{
		return {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, _command, static_cast<uint8_t>(_value & 0x7f), 0xf7};
	}

	struct DumpType
	{
		const char* name;
		uint8_t request;
		uint8_t reply;
		uint32_t count;
	};

	constexpr DumpType g_types[] = {
		{"global", 0x51, 0x50, 8},
		{"kit", 0x53, 0x52, 64},
		{"pattern", 0x68, 0x67, 128},
		{"song", 0x6a, 0x69, 32},
	};

	const DumpType& typeByName(const std::string& _name)
	{
		for(const auto& t : g_types)
			if(_name == t.name)
				return t;
		throw std::runtime_error("unknown dump type " + _name);
	}

	std::string slotName(const DumpType& _type, const uint32_t _slot)
	{
		char text[64];
		std::snprintf(text, sizeof(text), "%s_%03u.syx", _type.name, _slot);
		return text;
	}

	Bytes patchRamFromState(const std::string& _path, const Bytes& _rom)
	{
		const auto state = load(_path);
		md::DecodedState decoded;
		for(const auto type : {synthLib::StateTypeGlobal, synthLib::StateTypeCurrentProgram})
			if(md::decodeState(decoded, state, _rom, md::MachineModel::Machinedrum, type))
				return decoded.patchRam;
		throw std::runtime_error("device state did not decode: " + _path);
	}

	int dumpAll(Machine& _md, const std::string& _outDir)
	{
		int missing = 0;
		for(const auto& type : g_types)
		{
			uint32_t got = 0;
			for(uint32_t slot = 0; slot < type.count; ++slot)
			{
				const auto reply = _md.request(requestMessage(type.request, static_cast<uint8_t>(slot)), type.reply);
				if(reply.empty())
				{
					++missing;
					continue;
				}
				save(_outDir + "/" + slotName(type, slot), reply);
				++got;
			}
			std::printf("%s: %u of %u dumped\n", type.name, got, type.count);
		}
		return missing ? 1 : 0;
	}

	Bytes parseHex(std::istringstream& _in)
	{
		Bytes bytes;
		std::string token;
		while(_in >> token)
			bytes.push_back(static_cast<uint8_t>(std::stoul(token, nullptr, 16)));
		return bytes;
	}

	int runScript(Machine& _md, const std::string& _outDir, const std::string& _script)
	{
		std::ifstream in(_script);
		require(in.good(), "cannot read script " + _script);
		std::string line;
		while(std::getline(in, line))
		{
			std::istringstream words(line);
			std::string op;
			if(!(words >> op) || op[0] == '#')
				continue;
			if(op == "send")
				_md.send(parseHex(words));
			else if(op == "sendfile")
			{
				std::string path;
				words >> path;
				_md.send(load(path));
			}
			else if(op == "cc")
			{
				unsigned ch = 0, cc = 0, v = 0;
				words >> ch >> cc >> v;
				_md.send({static_cast<uint8_t>(0xb0 | (ch & 15)), static_cast<uint8_t>(cc), static_cast<uint8_t>(v)});
			}
			else if(op == "wait")
			{
				uint32_t ms = 0;
				words >> ms;
				_md.advanceMs(ms);
			}
			else if(op == "play")
				_md.panel(md::PanelControl::Play);
			else if(op == "stop")
				_md.panel(md::PanelControl::Stop);
			else if(op == "req")
			{
				std::string type, name;
				unsigned slot = 0;
				words >> type >> slot >> name;
				const auto& t = typeByName(type);
				const auto reply = _md.request(requestMessage(t.request, static_cast<uint8_t>(slot)), t.reply);
				std::printf("req %s %u -> %zu bytes (%s)\n", type.c_str(), slot, reply.size(), name.c_str());
				if(!reply.empty())
					save(_outDir + "/" + name + ".syx", reply);
			}
			else if(op == "trace")
			{
				// Poll the current pattern (status 0x04) and the playhead byte.
				uint32_t ms = 0, every = 0;
				words >> ms >> every;
				int lastPattern = -2, lastStep = -2;
				for(uint32_t t = 0; t < ms; t += every)
				{
					const auto reply = _md.request(requestMessage(0x70, 0x04), 0x72);
					const int pattern = reply.size() == 10 ? reply[8] : -1;
					const int step = _md.read8(0x261aa7);
					if(pattern != lastPattern || step != lastStep)
						std::printf("  t=%5u pattern=%3d step=%2d\n", t, pattern, step);
					lastPattern = pattern;
					lastStep = step;
					_md.advanceMs(every);
				}
			}
			else if(op == "echo")
			{
				std::string rest;
				std::getline(words, rest);
				std::printf("#%s\n", rest.c_str());
			}
			else if(op == "status")
			{
				unsigned p = 0;
				words >> std::hex >> p;
				const auto reply = _md.request(requestMessage(0x70, static_cast<uint8_t>(p)), 0x72);
				std::printf("status 0x%02x -> %d\n", p, reply.size() == 10 ? reply[8] : -1);
			}
			else
				throw std::runtime_error("unknown script op " + op);
		}
		return 0;
	}
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 4)
	{
		std::puts("usage: mdDataCaptureFirmwareTest dump|script <MD-1.63-ROM> <out-dir> [script] [state.mdst]");
		return 77;
	}
	try
	{
		const std::string mode = _argv[1];
		const auto rom = load(_argv[2]);
		require(rom.size() == md::g_romSize, "ROM must be the 8 MiB MD 1.63 image");
		const std::string outDir = _argv[3];
		const int stateArg = mode == "script" ? 5 : 4;
		const auto patchRam = _argc > stateArg ? patchRamFromState(_argv[stateArg], rom) : Bytes{};
		Machine machine(rom, _argv[2], patchRam);
		if(mode == "dump")
			return dumpAll(machine, outDir);
		if(mode == "script" && _argc > 4)
			return runScript(machine, outDir, _argv[4]);
		std::puts("unknown mode");
		return 2;
	}
	catch(const std::exception& _e)
	{
		std::fprintf(stderr, "mdDataCaptureFirmwareTest FAIL: %s\n", _e.what());
		return 1;
	}
}
