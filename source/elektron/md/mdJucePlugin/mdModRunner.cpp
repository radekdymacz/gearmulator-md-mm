#include "mdModRunner.h"

#include "mdPluginProcessor.h"
#include "mdStudioLink.h"

#include "mdDesk/mdDeskSetup.h"

#include "jucePluginLib/controller.h"
#include "jucePluginLib/parameter.h"
#include "synthLib/plugin.h"

namespace mdJucePlugin
{
	namespace
	{
		double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
	}

	ModRunner::ModRunner(AudioPluginAudioProcessor& _processor) : m_processor(_processor)
	{
		// Faster than the fastest step (300 BPM at 2X: 25 ms).
		startTimer(8);
	}

	ModRunner::~ModRunner()
	{
		stopTimer();
	}

	void ModRunner::setSetupJson(const std::string& _json)
	{
		if(_json.empty())
		{
			m_engine.setSetup({});
			return;
		}
		const auto doc = elektronData::json::parse(_json);
		std::vector<std::string> errors;
		if(const auto setup = doc ? mdDesk::deskSetupFromJson(*doc, errors) : std::nullopt)
			m_engine.setSetup(setup->modulators);
	}

	mdDesk::ModReport ModRunner::report()
	{
		return {m_engine.values(), m_engine.ccPerSecond(nowMs())};
	}

	void ModRunner::poll()
	{
		if(const auto v = m_processor.getDeskSetupVersion(); v != m_setupVersion)
		{
			m_setupVersion = v;
			setSetupJson(m_processor.getDeskSetup());
		}
		if(m_engine.setup().links.empty() || m_processor.isExternalMidi())
			return;
		const auto now = nowMs();
		// The device can be replaced (ROM change, state restore): look again now and then.
		if(!m_telemetry || now - m_telemetryCheckedMs > 1000)
		{
			m_telemetryCheckedMs = now;
			m_telemetry = m_processor.getPlugin().withDeviceLocked([](synthLib::Device* _base) -> std::shared_ptr<const md::Device::SequencerTelemetry>
			{
				auto* device = dynamic_cast<md::Device*>(_base);
				return device ? device->getSequencerTelemetry() : nullptr;
			});
		}
		if(!m_telemetry)
			return;
		const int step = m_telemetry->step.load(std::memory_order_relaxed);
		const int playing = m_telemetry->playing.load(std::memory_order_relaxed);
		if(step < 0 || playing < 0)
			return;
		for(const auto& o : m_engine.onPlayhead(step, playing == 1, now))
			if(auto* p = m_processor.getController().getParameter(StudioLink::parameterName(o.param), o.track))
				p->setUnnormalizedValueNotifyingHost(o.value, pluginLib::Parameter::Origin::Ui);
	}
}
