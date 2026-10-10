// The local release gate's ROM check (doc/release/LOCAL-GATE.md): the release build boots the images the editors
// support, and refuses what is not one of them, cleanly.
//   mdmmRomLoadTest <ROM> [<ROM> ...]
// For each image: its FNV-1a 64 fingerprint, the model it is for, decided as the editors' ROM install decides
// (md::checkRom against the pinned fingerprints of mdtypes.h, g_mdOs163Fingerprint and g_mmOs132bFingerprint), and
// the firmware that fingerprint names (md::firmwareName: the image itself carries no version string the emulator
// reads, and the LCD is pixels). A supported image is booted headless with md::Hardware for its model and must
// take MIDI (isFirmwareMidiReady) within 60 s of machine time; once its start-up animation is over it must answer
// SysEx: a status request (the current pattern) and a global dump, whose format bytes are printed as the firmware
// reports them. An image that is not supported fails the run.
// Then the negative cases, in memory, for each model: a truncated image (the first 1 MiB of the first image given)
// and a garbage image (8 MiB of seeded pseudo-random bytes). Each must be refused by the editors' check with its
// message, by the loader (RomLoader::isRomForModel) for both models, and must not boot: md::Hardware built from it is
// not valid, or (when the loader's fall-back search finds an image beside the executable or in the working folder)
// it runs that image, not the bytes given. Nothing may crash.
// One line per image and per negative case, then PASS or FAIL as the last line. Exits 77 without arguments, 1 on any
// failure. Manual: needs a user-supplied ROM (no firmware is bundled).

#include "mdFirmwareSession.h"

#include "mdLib/mdromcheck.h"
#include "mdLib/mdromloader.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdGlobal.h"
#include "elektronData/mmCommands.h"
#include "elektronData/mmGlobal.h"

#include <chrono>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

using namespace mdFirmwareSession;
namespace ed = elektronData;

namespace
{
	constexpr double g_bootLimitSeconds = 60.0;
	constexpr size_t g_truncatedSize = 1024 * 1024;

	int g_failures = 0;

	void fail(const std::string& _what)
	{
		std::printf("  FAIL %s\n", _what.c_str());
		++g_failures;
	}

	const char* modelKey(const md::MachineModel _model)
	{
		return _model == md::MachineModel::Monomachine ? "mm" : "md";
	}

	// The model the image is for, as the editors' ROM install decides it; none when neither accepts it.
	std::optional<md::MachineModel> modelOf(const Bytes& _image)
	{
		for(const auto model : {md::MachineModel::Machinedrum, md::MachineModel::Monomachine})
			if(md::checkRom(_image, model).ok)
				return model;
		return std::nullopt;
	}

	// Boot to MIDI-ready (Machine's own 60 s limit), report the machine time it took, wait out the start-up
	// animation, then ask for the current pattern and the first global's dump.
	void boot(const Bytes& _image, const std::string& _path, const md::MachineModel _model)
	{
		const auto wall = std::chrono::steady_clock::now();
		std::unique_ptr<Machine> m;
		try
		{
			m = std::make_unique<Machine>(_image, _path, Bytes{}, false, _model);
		}
		catch(const std::exception& e)
		{
			std::printf("mdmmRomLoadTest: rom=%s model=%s ready=no error=\"%s\"\n", _path.c_str(), modelKey(_model),
				e.what());
			fail(_path + ": " + e.what());
			return;
		}
		const double bootSeconds =
			static_cast<double>(m->hardware().hostCurrentCycle()) / static_cast<double>(md::g_ucClockHz);
		const bool booted = m->hardware().isFirmwareMidiReady() && bootSeconds <= g_bootLimitSeconds;
		const bool loaded = m->hardware().firmwareFingerprint() == md::romFingerprint(_image);

		// The start-up animation (MD about 20 s, MM 9.2 s after power-on: MM-P0-RESULT §6) before the SysEx probes.
		m->run((_model == md::MachineModel::Monomachine ? 9.0 : 25.0) * 1000.0);
		const bool mm = _model == md::MachineModel::Monomachine;
		int pattern = -1;
		if(mm)
		{
			if(const auto s = ed::parseMmStatusResponse(m->request(ed::mmStatusRequest(ed::MmStatus::Pattern), 0x72)))
				pattern = s->value;
		}
		else if(const auto s = ed::parseMdStatusResponse(m->request(ed::mdStatusRequest(ed::MdStatus::Pattern), 0x72)))
			pattern = s->value;
		const auto global = m->request(mm ? ed::mmGlobalRequest(0) : ed::mdGlobalRequest(0), 0x50);
		const bool globalOk = mm ? ed::decodeMmGlobal(global).has_value() : ed::decodeMdGlobal(global).has_value();
		char format[16] = "none";
		if(global.size() > 9)
			std::snprintf(format, sizeof(format), "%u.%u", global[7], global[8]);

		std::printf("mdmmRomLoadTest: rom=%s fingerprint=%016llx model=%s os=\"%s\" boot_s=%.2f ready=%s "
			"status_pattern=%d global_format=%s wall_s=%.1f\n", _path.c_str(),
			static_cast<unsigned long long>(md::romFingerprint(_image)), modelKey(_model), md::firmwareName(_model),
			bootSeconds, booted ? "yes" : "no", pattern, format,
			std::chrono::duration<double>(std::chrono::steady_clock::now() - wall).count());
		if(!loaded)
			fail(_path + ": the machine runs another image than the one given");
		if(!booted)
			fail(_path + ": not MIDI-ready within 60 s of machine time");
		if(pattern < 0)
			fail(_path + ": no reply to the pattern status request");
		if(!globalOk)
			fail(_path + ": no readable global dump");
	}

	// A bad image must be refused by the check and the loader, and must never run.
	void refused(const char* _case, const Bytes& _image, const md::MachineModel _model)
	{
		const auto check = md::checkRom(_image, _model);
		const bool loaderRefuses = !md::RomLoader::isRomForModel(_image, md::MachineModel::Machinedrum)
			&& !md::RomLoader::isRomForModel(_image, md::MachineModel::Monomachine);
		std::string boot;
		bool runsIt = false;
		try
		{
			// large: on the heap
			const auto hw = std::make_unique<md::Hardware>(_image, _case, _model);
			if(!hw->isValid())
				boot = "invalid";
			else
			{
				// the loader's fall-back search (the executable's folder, the working folder) found an image
				runsIt = hw->firmwareFingerprint() == md::romFingerprint(_image);
				boot = runsIt ? "runs-these-bytes" : "fell-back-to-a-found-image";
			}
		}
		catch(const std::exception& e)
		{
			boot = std::string("threw: ") + e.what();
		}
		const bool ok = !check.ok && loaderRefuses && !runsIt;
		std::printf("mdmmRomLoadTest: negative=%s model=%s bytes=%zu check=refused:%s loader=%s hardware=%s "
			"message=\"%s\" result=%s\n", _case, modelKey(_model), _image.size(), check.ok ? "no" : "yes",
			loaderRefuses ? "refused" : "ACCEPTED", boot.c_str(), check.text.c_str(), ok ? "refused" : "NOT-REFUSED");
		if(!ok)
			fail(std::string(_case) + " image for " + modelKey(_model) + " was not refused");
	}
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 2)
	{
		std::puts("usage: mdmmRomLoadTest <ROM> [<ROM> ...]");
		return 77;
	}
	Bytes first;
	for(int i = 1; i < _argc; ++i)
	{
		const std::string path = _argv[i];
		Bytes image;
		try
		{
			image = load(path);
		}
		catch(const std::exception& e)
		{
			std::printf("mdmmRomLoadTest: rom=%s error=\"%s\"\n", path.c_str(), e.what());
			fail(path + ": cannot read");
			continue;
		}
		if(first.empty())
			first = image;
		const auto model = modelOf(image);
		if(!model)
		{
			std::printf("mdmmRomLoadTest: rom=%s fingerprint=%016llx model=none bytes=%zu check=\"%s\"\n", path.c_str(),
				static_cast<unsigned long long>(md::romFingerprint(image)), image.size(),
				md::checkRom(image, md::MachineModel::Machinedrum).text.c_str());
			fail(path + ": not a supported image (Machinedrum OS 1.63 or Monomachine OS 1.32B)");
			continue;
		}
		boot(image, path, *model);
	}

	// Negative cases: the first 1 MiB of the first image, and seeded noise of the full size.
	const auto truncatedSize = static_cast<std::ptrdiff_t>(std::min(first.size(), g_truncatedSize));
	Bytes truncated(first.begin(), first.begin() + truncatedSize);
	Bytes garbage(md::g_romSize);
	std::mt19937_64 random(0x6d646d6d);	// "mdmm": the same bytes every run
	for(size_t i = 0; i < garbage.size(); i += 8)
	{
		const auto word = random();
		for(size_t b = 0; b < 8 && i + b < garbage.size(); ++b)
			garbage[i + b] = static_cast<uint8_t>(word >> (8 * b));
	}
	for(const auto model : {md::MachineModel::Machinedrum, md::MachineModel::Monomachine})
	{
		if(!truncated.empty())
			refused("truncated", truncated, model);
		refused("garbage", garbage, model);
	}

	if(g_failures)
	{
		std::printf("mdmmRomLoadTest: FAIL (%d failure(s))\n", g_failures);
		return 1;
	}
	std::puts("mdmmRomLoadTest: PASS");
	return 0;
}
