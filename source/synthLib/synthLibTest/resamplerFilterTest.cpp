// The legacy (libresample) resampler's low-pass filter is one immutable copy per process (synthLib::Resampler), not one
// per channel: a handle that reads a shared filter resamples bit-identically to one with its own, and a resampler
// with eight channels does not allocate eight copies of the 544 KiB tables (doc/modern-ux/RESEARCH-emulation-cpu.md,
// "Around the core", item 4).

#include "synthLib/resampler.h"

#include "libresample/include/libresample.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <malloc/malloc.h>
#endif

namespace
{
	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	std::vector<float> noise(const size_t _count)
	{
		std::vector<float> v(_count);
		uint32_t x = 0x12345678u;
		for(auto& s : v)
		{
			x = x * 1664525u + 1013904223u;
			s = static_cast<float>(static_cast<int32_t>(x)) / 2147483648.0f;
		}
		return v;
	}

	// The same input through a handle with its own filter and one reading the shared tables, in uneven chunks.
	void sharedFilterIsBitIdentical(const double _factor)
	{
		const auto size = static_cast<size_t>(resample_filter_size(1));
		std::vector<float> imp(size), impD(size);
		resample_build_filter(1, imp.data(), impD.data());

		void* own = resample_open(1, _factor, _factor);
		void* shared = resample_open_with_filter(1, _factor, _factor, imp.data(), impD.data());
		void* dup = resample_dup(shared);
		require(own && shared && dup, "could not open the resamplers");

		const auto input = noise(48000);
		// room for the largest factor below (x 2.18) and one more call's output
		std::vector<float> a(120000), b(120000), c(120000);
		size_t in = 0, outA = 0, outB = 0, outC = 0;
		const size_t chunks[] = {1, 7, 64, 127, 512, 33, 1024};
		for(size_t k = 0; in < input.size() && outA + 4096 <= a.size(); ++k)
		{
			const auto n = std::min(chunks[k % std::size(chunks)], input.size() - in);
			int usedA = 0, usedB = 0, usedC = 0;
			auto* src = const_cast<float*>(input.data() + in);
			outA += static_cast<size_t>(resample_process(own, _factor, src, static_cast<int>(n), 0, &usedA, a.data() + outA, 4096));
			outB += static_cast<size_t>(resample_process(shared, _factor, src, static_cast<int>(n), 0, &usedB, b.data() + outB, 4096));
			outC += static_cast<size_t>(resample_process(dup, _factor, src, static_cast<int>(n), 0, &usedC, c.data() + outC, 4096));
			require(usedA == usedB && usedA == usedC, "the handles consumed different input");
			in += static_cast<size_t>(usedA);
		}
		require(outA == outB && outA == outC && outA > 39000, "the handles produced different lengths");
		require(std::memcmp(a.data(), b.data(), outA * sizeof(float)) == 0, "a shared filter changed the output");
		require(std::memcmp(a.data(), c.data(), outA * sizeof(float)) == 0, "a duplicated shared handle changed the output");
		resample_close(own);
		resample_close(dup);	// must not free the caller's tables: the shared handle still reads them
		resample_close(shared);
	}

	// A resampler with eight channels (the editors' 6 outputs + 2 inputs) holds the filter once, not eight times.
	void resamplerAllocatesNoFilterPerChannel()
	{
#ifdef __APPLE__
		synthLib::Resampler warm(44100.0f, 48000.0f);	// builds the process-wide filter
		synthLib::TAudioOutputs outputs{};
		std::vector<std::vector<float>> buffers(8, std::vector<float>(256));
		for(size_t i = 0; i < 8; ++i)
			outputs[i] = buffers[i].data();
		const auto pull = [](synthLib::TAudioOutputs& _out, const uint32_t _count)
		{
			for(size_t i = 0; i < 8; ++i)
				std::fill_n(_out[i], _count, 0.0f);
		};
		warm.process(outputs, 8, 256, false, pull);

		malloc_statistics_t before{}, after{};
		malloc_zone_statistics(nullptr, &before);
		synthLib::Resampler resampler(44100.0f, 48000.0f);
		resampler.process(outputs, 8, 256, false, pull);
		malloc_zone_statistics(nullptr, &after);
		const auto grown = after.size_in_use > before.size_in_use ? after.size_in_use - before.size_in_use : 0;
		std::cout << "eight-channel resampler: " << grown << " bytes\n";
		// eight private filters were 8 x 2 x 69,632 floats (4.25 MiB); a channel's own buffers are about 40 KiB
		require(grown < 1024 * 1024, "the resampler allocated a filter per channel: " + std::to_string(grown) + " bytes");
#endif
	}
}

int main()
{
	try
	{
		sharedFilterIsBitIdentical(48000.0 / 44100.0);
		sharedFilterIsBitIdentical(44100.0 / 48000.0);
		sharedFilterIsBitIdentical(96000.0 / 44100.0);
		resamplerAllocatesNoFilterPerChannel();
	}
	catch(const std::exception& e)
	{
		std::cerr << "synthLibResamplerFilterTest: " << e.what() << '\n';
		return 1;
	}
	std::cout << "synthLibResamplerFilterTest: ok\n";
	return 0;
}
