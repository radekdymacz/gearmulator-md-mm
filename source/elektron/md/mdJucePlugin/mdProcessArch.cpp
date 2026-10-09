#include "mdProcessArch.h"

#include <cstdlib>
#include <iterator>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mdJucePlugin::processArch
{
	namespace
	{
		bool isArm(const std::string& _arch) { return _arch == "arm64"; }
		bool isIntel(const std::string& _arch) { return _arch == "x86_64" || _arch == "x86"; }

#if defined(__APPLE__)
		int readInt(const char* const _name)
		{
			int value = 0;
			size_t size = sizeof(value);
			if(sysctlbyname(_name, &value, &size, nullptr, 0) != 0)
				return -1;		// absent (macOS before 11, or not Rosetta capable) or unreadable: unknown, not "native"
			return value;
		}

		std::string readString(const char* const _name)
		{
			char buffer[256] = {};
			size_t size = sizeof(buffer) - 1;
			if(sysctlbyname(_name, buffer, &size, nullptr, 0) != 0)
				return {};
			return buffer;
		}
#elif defined(_WIN32)
		// The machine types of winnt.h, repeated so an older SDK still builds.
		constexpr USHORT g_machineI386 = 0x014c;
		constexpr USHORT g_machineAmd64 = 0x8664;
		constexpr USHORT g_machineArm64 = 0xAA64;

		std::string windowsMachine()
		{
			// IsWow64Process2 is Windows 10 1709 and later: looked up, so the editor still starts on anything older.
			using Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
			const auto kernel = GetModuleHandleW(L"kernel32.dll");
			const auto proc = kernel ? GetProcAddress(kernel, "IsWow64Process2") : nullptr;
			if(!proc)
				return {};
			USHORT process = 0, native = 0;
			if(!reinterpret_cast<Fn>(reinterpret_cast<void*>(proc))(GetCurrentProcess(), &process, &native))
				return {};
			switch(native)
			{
			case g_machineArm64: return "arm64";
			case g_machineAmd64: return "x86_64";
			case g_machineI386: return "x86";
			default: return {};
			}
		}
#endif
	}

	const char* compiledArch()
	{
		// ARM64EC defines _M_X64 as well: it is an Arm build.
#if defined(__aarch64__) || defined(_M_ARM64) || defined(_M_ARM64EC)
		return "arm64";
#elif defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
		return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
		return "x86";
#else
		return "unknown";
#endif
	}

	Os thisOs()
	{
#if defined(__APPLE__)
		return Os::Mac;
#elif defined(_WIN32)
		return Os::Windows;
#else
		return Os::Other;
#endif
	}

	ProcessArch classify(const Facts& _facts)
	{
		ProcessArch arch;
		arch.processArch = _facts.compiledArch.empty() ? "unknown" : _facts.compiledArch;
		// The sysctl is the system's own answer; the CPU Rosetta presents ("VirtualApple") only stands in for it
		// when it cannot be read.
		const bool rosetta = _facts.procTranslated == 1
			|| (_facts.procTranslated < 0 && _facts.cpuBrand.find("VirtualApple") != std::string::npos);
		if(!_facts.nativeMachine.empty())
			arch.machineArch = _facts.nativeMachine;
		else if(rosetta)
			arch.machineArch = "arm64";		// only Apple silicon translates
		else
			arch.machineArch = arch.processArch;
		arch.translated = rosetta || (isArm(arch.machineArch) && isIntel(arch.processArch));
		return arch;
	}

	Facts readFacts()
	{
		Facts facts;
		facts.compiledArch = compiledArch();
#if defined(MDMM_DIAGNOSTICS) && MDMM_DIAGNOSTICS
		// A diagnostics build can pretend to run under Rosetta, to see the notice and the report's fields on a Mac
		// without it (GEARMULATOR_MDMM_FAKE_ROSETTA=1: an Intel build the system says is translated). Not in a release.
		if(const char* const fake = std::getenv("GEARMULATOR_MDMM_FAKE_ROSETTA"); fake && fake[0] == '1')
		{
			facts.compiledArch = "x86_64";
			facts.procTranslated = 1;
			facts.cpuBrand = "VirtualApple @ 2.50GHz";
			return facts;
		}
#endif
#if defined(__APPLE__)
		facts.procTranslated = readInt("sysctl.proc_translated");
		facts.cpuBrand = readString("machdep.cpu.brand_string");
#elif defined(_WIN32)
		facts.nativeMachine = windowsMachine();
#endif
		return facts;
	}

	ProcessArch detect()
	{
		return classify(readFacts());
	}

	const ProcessArch& current()
	{
		static const ProcessArch arch = detect();
		return arch;
	}

	std::string describe(const ProcessArch& _arch)
	{
		return "process " + _arch.processArch + " on " + _arch.machineArch
			+ (_arch.translated ? ", translated" : ", native");
	}

	Fields sessionFields(const ProcessArch& _arch)
	{
		return {
			{"process_arch", _arch.processArch},
			{"machine_arch", _arch.machineArch},
			{"translated", _arch.translated ? "true" : "false"}
		};
	}

	void addSessionFields(Fields& _fields)
	{
		auto fields = sessionFields(current());
		_fields.insert(_fields.end(), std::make_move_iterator(fields.begin()), std::make_move_iterator(fields.end()));
	}

	std::optional<Notice> translatedNotice(const ProcessArch& _arch, const Os _os, const bool _standalone)
	{
		if(!_arch.translated)
			return std::nullopt;
		Notice notice;
		notice.buttons = {"Don't show again", "OK"};
		notice.dontShowAgain = 0;
		switch(_os)
		{
		case Os::Mac:
			notice.title = "Running under Rosetta";
			notice.text = "This is the Intel version of the editor running translated on an Apple silicon Mac. "
				"It uses about twice the CPU. ";
			notice.text += _standalone
				? "Open this app as an Apple silicon app (uncheck 'Open using Rosetta' in its Get Info window)."
				: "Open your DAW as an Apple silicon app (uncheck 'Open using Rosetta'), or use the Apple silicon "
					"standalone app.";
			return notice;
		case Os::Windows:
			notice.title = "Running in emulation";
			notice.text = "This is the x64 version of the editor running in emulation on a Windows on Arm PC. "
				"It uses much more CPU than on a native machine. If you hear crackles, raise the audio buffer size in ";
			notice.text += _standalone ? "the audio settings." : "your DAW.";
			return notice;
		case Os::Other:
			break;
		}
		return std::nullopt;
	}
}
