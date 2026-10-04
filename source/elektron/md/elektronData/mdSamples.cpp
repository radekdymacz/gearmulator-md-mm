#include "mdSamples.h"

#include "mdCommands.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace elektronData
{
	namespace
	{
		uint32_t be16(const uint8_t* _p) { return (uint32_t(_p[0]) << 8) | _p[1]; }
		// The record's 32-bit fields: two big-endian 16-bit words, the low one first.
		uint32_t field32(const uint8_t* _p) { return be16(_p) | (be16(_p + 2) << 16); }

		uint64_t mix(uint64_t _h, const uint64_t _v)
		{
			_h ^= _v + 0x9e3779b97f4a7c15ull + (_h << 6) + (_h >> 2);
			return _h;
		}

		int32_t signed24(const uint32_t _w) { return (_w & 0x800000) ? static_cast<int32_t>(_w | 0xff000000u) : static_cast<int32_t>(_w & 0xffffff); }

		// Data bytes of the record's sample _i: after the 22-byte head in its first sector, after the
		// 4-byte head in each next one.
		constexpr uint32_t g_firstWords = (g_mdSampleSectorSize - g_mdSampleHeaderSize) / 2;
		constexpr uint32_t g_nextWords = (g_mdSampleSectorSize - g_mdSampleNextSectorHead) / 2;
		uint32_t sectorsFor(const uint32_t _length)
		{
			return _length <= g_firstWords ? 1 : 1 + (_length - g_firstWords + g_nextWords - 1) / g_nextWords;
		}

		std::string nameAt(const MdSampleMemory& _m, const uint8_t _slot)
		{
			if(!_m.patch)
				return {};
			const auto at = g_mdSampleNamesAddress + 5u * _slot;
			std::string n;
			uint32_t sum = 0;
			for(uint32_t i = 0; i < 4; ++i)
			{
				const auto c = _m.patch(at + i);
				if(c < 0x20 || c > 0x7e)
					return {};
				n.push_back(static_cast<char>(c));
				sum += c;
			}
			if((sum & 0xff) != _m.patch(at + 4))
				return {};
			while(!n.empty() && n.back() == ' ')
				n.pop_back();
			return n;
		}

		// The period is whole nanoseconds (22676 ns is 44099.5 Hz): a rate within 0.05 % of a usual one is that one.
		uint32_t rateOfPeriod(const uint32_t _ns)
		{
			if(!_ns)
				return 0;
			const double r = 1e9 / _ns;
			for(const double usual : {8000.0, 11025.0, 16000.0, 22050.0, 32000.0, 44100.0, 48000.0})
				if(std::fabs(r - usual) < usual * 0.0005)
					return static_cast<uint32_t>(usual);
			return static_cast<uint32_t>(std::lround(r));
		}

		void writeRamFacts(const MdSampleMemory& _m, MdSampleIndex& _x)
		{
			if(!_m.dspX)
			{
				_x.ramReason = "The RAM buffers are in the emulated machine's DSP memory, which this engine cannot read.";
				return;
			}
			for(uint32_t r = 0; r < g_mdRamSlots; ++r)
				for(uint32_t w = 0; w < 4; ++w)
					_x.ram[r][w] = _m.dspX(g_mdDspSampleTable + 4 * (g_mdDspRamEntry + r) + w) & 0xffffff;
		}
	}

	std::optional<MdFlashSample> parseMdFlashSample(const uint8_t* _h, const uint32_t _offset)
	{
		if(_h[0] != 0x18 || _h[1] >= g_mdRomSlots || be16(_h + 2) != 16)
			return std::nullopt;
		MdFlashSample s;
		s.offset = _offset;
		s.slot = _h[1];
		s.periodNs = field32(_h + 4);
		s.length = field32(_h + 8);
		s.loopStart = field32(_h + 12);
		s.loopEnd = field32(_h + 16);
		s.loopType = _h[21];
		if(!s.length || s.periodNs < 10000 || s.periodNs > 2000000)
			return std::nullopt;
		if(_offset + uint64_t(sectorsFor(s.length)) * g_mdSampleSectorSize > g_mdSampleFlashEnd)
			return std::nullopt;
		return s;
	}

	uint64_t mdSampleSignature(const MdSampleMemory& _m)
	{
		uint64_t h = 1469598103934665603ull;
		if(_m.flash)
		{
			uint8_t head[g_mdSampleHeaderSize];
			for(uint32_t o = g_mdSampleFlashBegin; o < g_mdSampleFlashEnd; o += g_mdSampleSectorSize)
			{
				if(!_m.flash(o, sizeof(head), head))
					return 0;
				for(const auto b : head)
					h = mix(h, b);
			}
		}
		if(_m.patch)
			for(uint32_t i = 0; i < 5u * g_mdRomSlots; ++i)
				h = mix(h, _m.patch(g_mdSampleNamesAddress + i));
		if(_m.dspX)
			for(uint32_t i = 0; i < 4u * g_mdRamSlots; ++i)
				h = mix(h, _m.dspX(g_mdDspSampleTable + 4 * g_mdDspRamEntry + i));
		return h;
	}

	MdSampleIndex indexMdSamples(const MdSampleMemory& _m)
	{
		MdSampleIndex x;
		x.signature = mdSampleSignature(_m);
		if(_m.flash)
		{
			uint8_t head[g_mdSampleHeaderSize];
			for(uint32_t o = g_mdSampleFlashBegin; o < g_mdSampleFlashEnd; o += g_mdSampleSectorSize)
			{
				if(!_m.flash(o, sizeof(head), head))
					break;
				if(auto s = parseMdFlashSample(head, o); s && !x.rom[s->slot])
					x.rom[s->slot] = s;
			}
		}
		for(uint8_t s = 0; s < g_mdRomSlots; ++s)
			x.names[s] = nameAt(_m, s);
		writeRamFacts(_m, x);
		if(!_m.dspX)
			return x;
		// The expander: silence at 0x800, rising throughout. Anything else is not the firmware this knows.
		x.expander.resize(4096);
		bool rising = true;
		for(uint32_t i = 0; i < 4096; ++i)
		{
			x.expander[i] = signed24(_m.dspX(g_mdDspExpander + i));
			if(i && x.expander[i] <= x.expander[i - 1])
				rising = false;
		}
		bool entries = true;
		for(const auto& e : x.ram)
			entries &= e[0] >= g_mdDspSampleMemoryBegin && e[0] < g_mdDspSampleMemoryEnd && e[1] <= (g_mdDspSampleMemoryEnd - e[0]) * 2;
		if(!rising || x.expander[0x800] != 0 || !entries)
		{
			x.expander.clear();
			x.ramReason = "The RAM buffers' table in the DSP memory is not where MD OS 1.63 keeps it, so they are not shown.";
			return x;
		}
		x.ramReadable = true;
		return x;
	}

	std::vector<int8_t> mdPeaks(const uint32_t _length, const size_t _bins, const std::function<float(uint32_t)>& _at)
	{
		std::vector<int8_t> out;
		if(!_length || !_bins)
			return out;
		out.reserve(_bins * 2);
		const auto q = [](const float _v) { return static_cast<int8_t>(std::clamp<long>(std::lround(_v * 127.0f), -127, 127)); };
		for(size_t b = 0; b < _bins; ++b)
		{
			const auto from = static_cast<uint32_t>(uint64_t(_length) * b / _bins);
			auto to = static_cast<uint32_t>(uint64_t(_length) * (b + 1) / _bins);
			if(to <= from)
				to = std::min(_length, from + 1);
			float lo = 0, hi = 0;
			for(uint32_t i = from; i < to; ++i)
			{
				const auto v = _at(i);
				if(i == from)
					lo = hi = v;
				lo = std::min(lo, v);
				hi = std::max(hi, v);
			}
			out.push_back(q(lo));
			out.push_back(q(hi));
		}
		return out;
	}

	MdSampleSlot readMdRomSample(const MdSampleMemory& _m, const MdSampleIndex& _x, const uint8_t _slot, const size_t _bins)
	{
		MdSampleSlot s;
		s.slot = _slot;
		if(_slot >= g_mdRomSlots || !_x.rom[_slot] || !_m.flash)
			return s;
		const auto& r = *_x.rom[_slot];
		// The record's data, sector by sector (a sector whose head is not the record's ends it).
		std::vector<int16_t> data;
		data.reserve(r.length);
		std::vector<uint8_t> sector(g_mdSampleSectorSize);
		for(uint32_t k = 0, o = r.offset; data.size() < r.length && o < g_mdSampleFlashEnd; ++k, o += g_mdSampleSectorSize)
		{
			if(!_m.flash(o, sector.size(), sector.data()))
				break;
			if(k && (sector[0] != 0x1a || sector[1] != _slot))
				break;
			const size_t first = k ? g_mdSampleNextSectorHead : g_mdSampleHeaderSize;
			for(size_t b = first; b + 1 < sector.size() && data.size() < r.length; b += 2)
				data.push_back(static_cast<int16_t>(be16(&sector[b])));
		}
		s.empty = false;
		s.length = static_cast<uint32_t>(data.size());
		s.rate = rateOfPeriod(r.periodNs);
		if(r.loopType != 0x7f)
			s.loop = std::make_pair(r.loopStart, r.loopEnd);
		s.name = _x.names[_slot];
		s.peaks = mdPeaks(s.length, _bins, [&](const uint32_t _i) { return data[_i] / 32768.0f; });
		s.pcm = std::make_shared<const std::vector<int16_t>>(std::move(data));
		return s;
	}

	MdSampleSlot readMdRamSample(const MdSampleMemory& _m, const MdSampleIndex& _x, const uint8_t _slot, const size_t _bins)
	{
		MdSampleSlot s;
		s.ram = true;
		s.slot = _slot;
		if(_slot >= g_mdRamSlots || !_x.ramReadable || !_m.dspX)
			return s;
		const auto& e = _x.ram[_slot];
		if(!e[1])
			return s;
		s.empty = false;
		s.length = e[1];
		s.rate = static_cast<uint32_t>(std::lround(e[3] / double(0x40000) * 44100.0));
		const auto start = e[0];
		std::vector<int32_t> words((s.length + 1) / 2);
		for(uint32_t w = 0; w < words.size(); ++w)
			words[w] = static_cast<int32_t>(_m.dspX(start + w) & 0xffffff);
		// The codes through the expander (signed 24-bit), kept as their top 16 bits.
		std::vector<int16_t> pcm(s.length);
		for(uint32_t i = 0; i < s.length; ++i)
		{
			const auto w = words[i / 2];
			const auto code = (i & 1) ? (w & 0xfff) : ((w >> 12) & 0xfff);
			pcm[i] = static_cast<int16_t>(std::clamp(_x.expander[static_cast<size_t>(code)] >> 8, -32768, 32767));
		}
		s.peaks = mdPeaks(s.length, _bins, [&](const uint32_t _i) { return pcm[_i] / 32768.0f; });
		s.pcm = std::make_shared<const std::vector<int16_t>>(std::move(pcm));
		return s;
	}

	std::vector<int16_t> mdWavePeaks(const MdSampleSlot& _slot, const size_t _bins)
	{
		std::vector<int16_t> out;
		if(_slot.empty || !_slot.pcm || _slot.pcm->empty() || !_bins)
			return out;
		const auto& p = *_slot.pcm;
		const auto n = p.size();
		const auto bins = std::min(_bins, n);
		out.reserve(bins * 2);
		const auto q = [](const int _v) { return static_cast<int16_t>(std::max(-32767, _v)); };
		for(size_t b = 0; b < bins; ++b)
		{
			const auto from = n * b / bins, to = std::max(from + 1, n * (b + 1) / bins);
			const auto [lo, hi] = std::minmax_element(p.begin() + static_cast<std::ptrdiff_t>(from), p.begin() + static_cast<std::ptrdiff_t>(to));
			out.push_back(q(*lo));
			out.push_back(q(*hi));
		}
		return out;
	}

	json::Value mdSampleWaveMessage(const MdSampleSlot& _slot, const size_t _bins)
	{
		using json::Value;
		const auto peaks = mdWavePeaks(_slot, std::min(_bins, g_mdSampleWaveMaxBins));
		Value m = Value::object();
		m.set("type", "sampleWave");
		m.set("bank", _slot.ram ? "ram" : "rom");
		m.set("slot", static_cast<int>(_slot.slot));
		m.set("length", _slot.length);
		m.set("rate", _slot.rate);
		m.set("bins", static_cast<int>(peaks.size() / 2));
		m.set("scale", 32767);
		Value p = Value::array();
		for(const auto x : peaks)
			p.push(static_cast<int>(x));
		m.set("peaks", p);
		return m;
	}

	uint32_t MdSampleBank::used() const
	{
		uint32_t n = 0;
		for(const auto& s : rom)
			n += s.length;
		return n;
	}

	MdSampleBank readMdSampleBank(const MdSampleMemory& _m, const size_t _bins)
	{
		const auto x = indexMdSamples(_m);
		MdSampleBank b;
		b.signature = x.signature;
		b.ramReadable = x.ramReadable;
		b.ramReason = x.ramReason;
		for(uint8_t s = 0; s < g_mdRomSlots; ++s)
			b.rom.push_back(readMdRomSample(_m, x, s, _bins));
		for(uint8_t s = 0; s < g_mdRamSlots; ++s)
			b.ram.push_back(readMdRamSample(_m, x, s, _bins));
		return b;
	}

	json::Value mdSampleBankToJson(const MdSampleBank& _b)
	{
		using json::Value;
		const auto slot = [](const MdSampleSlot& _s)
		{
			Value v = Value::object();
			v.set("slot", static_cast<int>(_s.slot));
			v.set("empty", _s.empty);
			v.set("length", _s.length);
			v.set("rate", _s.rate);
			if(_s.loop)
			{
				Value l = Value::object();
				l.set("start", _s.loop->first);
				l.set("end", _s.loop->second);
				v.set("loop", l);
			}
			else
				v.set("loop", nullptr);
			v.set("name", _s.name.empty() ? Value(nullptr) : Value(_s.name));
			Value p = Value::array();
			for(const auto x : _s.peaks)
				p.push(static_cast<int>(x));
			v.set("peaks", p);
			return v;
		};
		Value d = Value::object();
		d.set("schema", "md-desk/samples");
		d.set("version", 1);
		d.set("bins", static_cast<int>(g_mdSampleBins));
		d.set("capacity", g_mdSampleCapacity);
		d.set("used", _b.used());
		Value rom = Value::array(), ram = Value::array();
		for(const auto& s : _b.rom)
			rom.push(slot(s));
		for(const auto& s : _b.ram)
			ram.push(slot(s));
		d.set("rom", rom);
		d.set("ram", ram);
		d.set("ramReadable", _b.ramReadable);
		d.set("ramReason", _b.ramReason);
		return d;
	}

	// ---- SDS ----

	std::vector<uint8_t> MdSdsDump::bytes() const
	{
		std::vector<uint8_t> b = header;
		b.insert(b.end(), name.begin(), name.end());
		for(const auto& p : packets)
			b.insert(b.end(), p.begin(), p.end());
		return b;
	}

	std::optional<MdSdsDump> mdSdsDump(const uint8_t _slot, const std::vector<int16_t>& _samples, const uint32_t _rate, const std::string& _name)
	{
		if(_slot >= g_mdRomSlots || _samples.empty() || _samples.size() > 0x1fffff || !_rate)
			return std::nullopt;
		MdSdsDump d;
		d.name = mdSetSampleName(_slot, _name);
		if(d.name.empty())
			return std::nullopt;
		const auto words = static_cast<uint32_t>(_samples.size());
		const auto period = static_cast<uint32_t>(std::lround(1e9 / _rate));
		const auto put21 = [&](const uint32_t _v)
		{
			d.header.push_back(_v & 0x7f);
			d.header.push_back((_v >> 7) & 0x7f);
			d.header.push_back((_v >> 14) & 0x7f);
		};
		d.header = {0xf0, 0x7e, 0x00, 0x01, _slot, 0x00, 16};
		put21(period);
		put21(words);
		put21(0);
		put21(words - 1);
		d.header.push_back(0x7f);	// no loop
		d.header.push_back(0xf7);
		constexpr size_t perPacket = 40;	// 120 bytes, 3 a 16-bit sample
		for(size_t first = 0, n = 0; first < _samples.size(); first += perPacket, ++n)
		{
			std::vector<uint8_t> p{0xf0, 0x7e, 0x00, 0x02, static_cast<uint8_t>(n & 0x7f)};
			for(size_t i = 0; i < perPacket; ++i)
			{
				// Unsigned, left-justified in 21 bits.
				const uint32_t u = first + i < _samples.size() ? static_cast<uint32_t>(static_cast<int32_t>(_samples[first + i]) + 0x8000) : 0;
				const uint32_t w = u << 5;
				p.push_back((w >> 14) & 0x7f);
				p.push_back((w >> 7) & 0x7f);
				p.push_back(w & 0x7f);
			}
			uint8_t sum = 0;
			for(size_t i = 1; i < p.size(); ++i)
				sum ^= p[i];
			p.push_back(sum & 0x7f);
			p.push_back(0xf7);
			d.packets.push_back(std::move(p));
		}
		return d;
	}

	std::optional<std::pair<SdsReply, uint8_t>> parseSdsReply(const std::vector<uint8_t>& _m)
	{
		if(_m.size() != 6 || _m[0] != 0xf0 || _m[1] != 0x7e || _m[5] != 0xf7)
			return std::nullopt;
		switch(_m[3])
		{
		case 0x7f: return std::make_pair(SdsReply::Ack, _m[4]);
		case 0x7e: return std::make_pair(SdsReply::Nak, _m[4]);
		case 0x7c: return std::make_pair(SdsReply::Wait, _m[4]);
		case 0x7d: return std::make_pair(SdsReply::Cancel, _m[4]);
		default: return std::nullopt;
		}
	}

	// ---- audio files ----

	namespace
	{
		uint32_t le16(const uint8_t* _p) { return uint32_t(_p[0]) | (uint32_t(_p[1]) << 8); }
		uint32_t le32(const uint8_t* _p) { return le16(_p) | (le16(_p + 2) << 16); }
		uint32_t bigEndian32(const uint8_t* _p) { return (be16(_p) << 16) | be16(_p + 2); }

		// One sample of _bits at _p as -1..1. _float: IEEE; _little: byte order; 8-bit WAV is unsigned.
		float sampleAt(const uint8_t* _p, const uint32_t _bits, const bool _float, const bool _little, const bool _unsigned8)
		{
			const auto bytes = _bits / 8;
			uint64_t v = 0;
			for(uint32_t i = 0; i < bytes; ++i)
				v |= uint64_t(_p[_little ? i : bytes - 1 - i]) << (8 * i);
			if(_float)
			{
				if(_bits == 32)
				{
					float f;
					const auto u = static_cast<uint32_t>(v);
					std::memcpy(&f, &u, 4);
					// finite by the exponent bits: -Ofast folds std::isfinite to true
					return (u & 0x7f800000u) != 0x7f800000u ? f : 0.0f;
				}
				double d;
				std::memcpy(&d, &v, 8);
				return (v & 0x7ff0000000000000ull) != 0x7ff0000000000000ull ? static_cast<float>(d) : 0.0f;
			}
			if(_bits == 8 && _unsigned8)
				return (static_cast<int>(v) - 128) / 128.0f;
			const auto shift = 64 - _bits;
			const auto s = static_cast<int64_t>(v << shift) >> shift;
			return static_cast<float>(static_cast<double>(s) / static_cast<double>(int64_t(1) << (_bits - 1)));
		}

		bool readFrames(AudioClip& _c, const uint8_t* _data, const size_t _size, const uint32_t _channels, const uint32_t _bits,
			const bool _float, const bool _little, const bool _unsigned8, std::string& _error)
		{
			if(!_channels || _channels > 16 || (_bits != 8 && _bits != 16 && _bits != 24 && _bits != 32 && _bits != 64)
				|| (_float && _bits != 32 && _bits != 64) || (!_float && _bits == 64))
			{
				_error = "This sample format is not supported (" + std::to_string(_bits) + "-bit" + (_float ? " float" : "") + ", "
					+ std::to_string(_channels) + " channels).";
				return false;
			}
			const size_t frame = _channels * (_bits / 8);
			const size_t frames = _size / frame;
			_c.channels.assign(_channels, std::vector<float>(frames));
			for(size_t f = 0; f < frames; ++f)
				for(uint32_t ch = 0; ch < _channels; ++ch)
					_c.channels[ch][f] = sampleAt(_data + f * frame + ch * (_bits / 8), _bits, _float, _little, _unsigned8);
			if(!frames)
			{
				_error = "The file holds no audio.";
				return false;
			}
			return true;
		}

		std::optional<AudioClip> decodeWav(const std::vector<uint8_t>& _b, std::string& _error)
		{
			uint32_t channels = 0, rate = 0, bits = 0, format = 0;
			const uint8_t* data = nullptr;
			size_t dataSize = 0;
			for(size_t o = 12; o + 8 <= _b.size();)
			{
				const auto id = std::string(reinterpret_cast<const char*>(&_b[o]), 4);
				const size_t size = le32(&_b[o + 4]);
				const size_t body = o + 8;
				const size_t avail = std::min(size, _b.size() - body);
				if(id == "fmt " && avail >= 16)
				{
					format = le16(&_b[body]);
					channels = le16(&_b[body + 2]);
					rate = le32(&_b[body + 4]);
					bits = le16(&_b[body + 14]);
					if(format == 0xfffe && avail >= 26)
						format = le16(&_b[body + 24]);
				}
				else if(id == "data")
				{
					data = &_b[body];
					dataSize = avail;
				}
				o = body + size + (size & 1);
			}
			if(!format || !data)
			{
				_error = "This WAV file has no audio format or no audio data.";
				return std::nullopt;
			}
			if(format != 1 && format != 3)
			{
				_error = "This WAV file is compressed (format " + std::to_string(format) + "); save it as PCM.";
				return std::nullopt;
			}
			AudioClip c;
			c.rate = rate;
			if(!readFrames(c, data, dataSize, channels, bits, format == 3, true, true, _error))
				return std::nullopt;
			return c;
		}

		// IEEE 754 80-bit extended (AIFF's sample rate).
		double extended80(const uint8_t* _p)
		{
			const int exponent = static_cast<int>(((_p[0] & 0x7f) << 8) | _p[1]) - 16383;
			uint64_t mantissa = 0;
			for(int i = 0; i < 8; ++i)
				mantissa = (mantissa << 8) | _p[2 + i];
			const double v = std::ldexp(static_cast<double>(mantissa), exponent - 63);
			return (_p[0] & 0x80) ? -v : v;
		}

		std::optional<AudioClip> decodeAiff(const std::vector<uint8_t>& _b, const bool _aifc, std::string& _error)
		{
			uint32_t channels = 0, bits = 0;
			double rate = 0;
			std::string compression = "NONE";
			const uint8_t* data = nullptr;
			size_t dataSize = 0;
			bool comm = false;
			for(size_t o = 12; o + 8 <= _b.size();)
			{
				const auto id = std::string(reinterpret_cast<const char*>(&_b[o]), 4);
				const size_t size = bigEndian32(&_b[o + 4]);
				const size_t body = o + 8;
				const size_t avail = std::min(size, _b.size() - body);
				if(id == "COMM" && avail >= 18)
				{
					comm = true;
					channels = be16(&_b[body]);
					bits = be16(&_b[body + 6]);
					rate = extended80(&_b[body + 8]);
					if(_aifc && avail >= 22)
						compression = std::string(reinterpret_cast<const char*>(&_b[body + 18]), 4);
				}
				else if(id == "SSND" && avail >= 8)
				{
					const size_t offset = bigEndian32(&_b[body]);
					if(8 + offset <= avail)
					{
						data = &_b[body + 8 + offset];
						dataSize = avail - 8 - offset;
					}
				}
				o = body + size + (size & 1);
			}
			if(!comm || !data)
			{
				_error = "This AIFF file has no audio format or no audio data.";
				return std::nullopt;
			}
			bool little = false, isFloat = false;
			if(compression == "sowt")
				little = true;
			else if(compression == "fl32" || compression == "FL32")
			{
				isFloat = true;
				bits = 32;
			}
			else if(compression == "fl64" || compression == "FL64")
			{
				isFloat = true;
				bits = 64;
			}
			else if(compression != "NONE" && compression != "twos")
			{
				_error = "This AIFF file is compressed (" + compression + "); save it as PCM.";
				return std::nullopt;
			}
			// AIFF pads samples to whole bytes, left-justified.
			bits = (bits + 7) / 8 * 8;
			AudioClip c;
			c.rate = rate > 0 && rate < 1e7 ? static_cast<uint32_t>(std::lround(rate)) : 0;
			if(!readFrames(c, data, dataSize, channels, bits, isFloat, little, false, _error))
				return std::nullopt;
			return c;
		}
	}

	std::optional<AudioClip> decodeAudioFile(const std::vector<uint8_t>& _b, std::string& _error)
	{
		const auto tag = [&](const size_t _o, const char* _t) { return _b.size() >= _o + 4 && std::memcmp(&_b[_o], _t, 4) == 0; };
		std::optional<AudioClip> c;
		if(tag(0, "RIFF") && tag(8, "WAVE"))
			c = decodeWav(_b, _error);
		else if(tag(0, "FORM") && (tag(8, "AIFF") || tag(8, "AIFC")))
			c = decodeAiff(_b, tag(8, "AIFC"), _error);
		else
		{
			_error = "This is not a WAV or AIFF file.";
			return std::nullopt;
		}
		if(c && (c->rate < 1000 || c->rate > 384000))
		{
			_error = "The file's sample rate (" + std::to_string(c->rate) + " Hz) is not one the editor can use.";
			return std::nullopt;
		}
		return c;
	}

	std::vector<uint8_t> encodeWav16(const AudioClip& _c)
	{
		const auto ch = static_cast<uint32_t>(_c.channels.size());
		const auto frames = static_cast<uint32_t>(_c.frames());
		std::vector<uint8_t> b;
		const auto u32 = [&](const uint32_t _v) { for(int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(_v >> (8 * i))); };
		const auto u16 = [&](const uint32_t _v) { b.push_back(static_cast<uint8_t>(_v)); b.push_back(static_cast<uint8_t>(_v >> 8)); };
		const auto text = [&](const char* _t) { b.insert(b.end(), _t, _t + 4); };
		const uint32_t data = frames * ch * 2;
		text("RIFF"); u32(36 + data); text("WAVE");
		text("fmt "); u32(16); u16(1); u16(ch); u32(_c.rate); u32(_c.rate * ch * 2); u16(ch * 2); u16(16);
		text("data"); u32(data);
		for(uint32_t f = 0; f < frames; ++f)
			for(uint32_t c = 0; c < ch; ++c)
			{
				const auto v = static_cast<int32_t>(std::lround(std::clamp(_c.channels[c][f], -1.0f, 1.0f) * 32767.0f));
				u16(static_cast<uint32_t>(v) & 0xffff);
			}
		return b;
	}

	std::string mdSampleNameFrom(const std::string& _fileName)
	{
		auto stem = _fileName;
		if(const auto slash = stem.find_last_of("/\\"); slash != std::string::npos)
			stem = stem.substr(slash + 1);
		if(const auto dot = stem.find_last_of('.'); dot != std::string::npos && dot > 0)
			stem = stem.substr(0, dot);
		std::string n;
		for(const char c : stem)
		{
			if(n.size() == 4)
				break;
			const auto u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			if((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || (u == '-' && !n.empty()))
				n.push_back(u);
		}
		return n.empty() ? "SMPL" : n;
	}

	std::optional<MdSampleUpload> prepareMdSample(const AudioClip& _c, const std::string& _fileName, const uint32_t _maxSamples, std::string& _error)
	{
		if(_c.channels.empty() || !_c.frames() || !_c.rate)
		{
			_error = "The file holds no audio.";
			return std::nullopt;
		}
		MdSampleUpload u;
		u.name = mdSampleNameFrom(_fileName);
		const size_t frames = _c.frames();
		std::vector<float> mono(frames, 0.0f);
		for(const auto& ch : _c.channels)
			for(size_t i = 0; i < frames; ++i)
				mono[i] += ch[i] / static_cast<float>(_c.channels.size());
		if(_c.channels.size() > 1)
			u.notes.push_back(std::to_string(_c.channels.size()) + " channels mixed to mono.");
		u.rate = _c.rate;
		if(_c.rate > g_mdSampleMaxRate)
		{
			// Down to the machine's rate: a short low-pass (a running average over the ratio), then linear
			// interpolation.
			const double ratio = static_cast<double>(_c.rate) / g_mdSampleMaxRate;
			const auto width = static_cast<size_t>(std::ceil(ratio));
			std::vector<float> smooth(frames);
			double acc = 0;
			for(size_t i = 0; i < frames; ++i)
			{
				acc += mono[i];
				if(i >= width)
					acc -= mono[i - width];
				smooth[i] = static_cast<float>(acc / static_cast<double>(std::min(i + 1, width)));
			}
			const auto out = static_cast<size_t>(std::floor((frames - 1) / ratio)) + 1;
			std::vector<float> r(out);
			const double delay = (static_cast<double>(width) - 1) / 2;
			for(size_t i = 0; i < out; ++i)
			{
				const double at = std::min(static_cast<double>(frames - 1), i * ratio + delay);
				const auto k = static_cast<size_t>(at);
				const double f = at - static_cast<double>(k);
				r[i] = static_cast<float>(smooth[k] * (1 - f) + (k + 1 < frames ? smooth[k + 1] : smooth[k]) * f);
			}
			mono = std::move(r);
			u.rate = g_mdSampleMaxRate;
			u.notes.push_back(std::to_string(_c.rate) + " Hz resampled to " + std::to_string(g_mdSampleMaxRate) + " Hz.");
		}
		if(!_maxSamples)
		{
			_error = "The machine's sample memory is full: clear a ROM slot or load a shorter sample.";
			return std::nullopt;
		}
		if(mono.size() > _maxSamples)
		{
			char t[96];
			std::snprintf(t, sizeof(t), "Cut to %.2f s of %.2f s: the sample memory left.", _maxSamples / double(u.rate), mono.size() / double(u.rate));
			u.notes.push_back(t);
			mono.resize(_maxSamples);
		}
		bool clipped = false;
		u.samples.reserve(mono.size());
		for(const auto v : mono)
		{
			clipped |= v > 1.0f || v < -1.0f;
			u.samples.push_back(static_cast<int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)));
		}
		if(clipped)
			u.notes.push_back("Peaks above full scale were clipped.");
		return u;
	}
}
