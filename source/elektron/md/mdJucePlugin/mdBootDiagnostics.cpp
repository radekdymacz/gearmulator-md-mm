#include "mdBootDiagnostics.h"

#include "mdLib/mdtypes.h"

#include <cmath>
#include <cstdio>
#include <utility>

namespace mdJucePlugin
{
	BootRate BootRate::between(const BootSample& _before, const BootSample& _after)
	{
		BootRate r;
		const double seconds = (_after.wallMs - _before.wallMs) / 1000.0;
		if(seconds <= 0)
			return r;
		r.known = true;
		r.blocksPerSecond = _after.blocks >= _before.blocks ? static_cast<double>(_after.blocks - _before.blocks) / seconds : 0;
		// a new machine (state restore, ROM change) starts its cycles again: no rate for that second
		r.realtime = _after.cycles >= _before.cycles
			? static_cast<double>(_after.cycles - _before.cycles) / static_cast<double>(md::g_ucClockHz) / seconds : 0;
		return r;
	}

	std::string bootLine(const BootSample& _s, const BootRate& _r)
	{
		char buf[512];
		std::snprintf(buf, sizeof(buf),
			"boot t=%ds rate=%d block=%d blocks=%llu (%d/s) bypassed=%llu nonRealtime=%llu realtime=%.2fx dspBooted=%d/%d "
			"firmwareMidiReady=%d lifecycle=%s rom=%s cycles=%llu",
			static_cast<int>(std::lround(_s.wallMs / 1000.0)), static_cast<int>(std::lround(_s.sampleRate)), _s.blockSize,
			static_cast<unsigned long long>(_s.blocks), static_cast<int>(std::lround(_r.blocksPerSecond)),
			static_cast<unsigned long long>(_s.bypassed), static_cast<unsigned long long>(_s.nonRealtime), _r.realtime,
			_s.dspBooted, _s.dsps, _s.firmwareMidiReady ? 1 : 0, _s.lifecycle.empty() ? "-" : _s.lifecycle.c_str(),
			_s.rom.empty() ? "none" : _s.rom.c_str(), static_cast<unsigned long long>(_s.cycles));
		return buf;
	}

	void BootDiagnostics::record(const BootSample& _s)
	{
		m_rate = m_sampled ? BootRate::between(m_last, _s) : BootRate{};
		m_last = _s;
		m_sampled = true;
		if(_s.wallMs <= g_logSeconds * 1000.0 + 500.0)
			write(bootLine(_s, m_rate));
	}

	void BootDiagnostics::setLog(const juce::File& _file)
	{
		m_log = _file;
		for(const auto& line : std::exchange(m_pending, {}))
			if(m_log != juce::File())
				m_log.appendText(juce::String(line));
	}

	void BootDiagnostics::write(const std::string& _line)
	{
		const auto line = juce::Time::getCurrentTime().toString(true, true, true, true) + " " + juce::String(_line) + "\n";
		if(m_log != juce::File())
			m_log.appendText(line);
		else if(m_pending.size() < 64)
			m_pending.push_back(line.toStdString());
	}
}
