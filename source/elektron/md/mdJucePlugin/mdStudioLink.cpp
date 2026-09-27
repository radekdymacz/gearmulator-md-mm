#include "mdStudioLink.h"

#include "mdController.h"
#include "mdPluginProcessor.h"

#include "mdLib/mddevice.h"
#include "mdLib/mdhardware.h"

#include "synthLib/plugin.h"

#include "juce_events/juce_events.h"

namespace mdJucePlugin
{
	namespace
	{
		// Found by mdPatternLiveEditFirmwareTest: current step, 0..length-1.
		constexpr uint32_t g_mdPlayheadAddress = 0x261aa7;

		constexpr uint8_t g_statusResponse = 0x72;
		constexpr uint8_t g_statusPattern = 0x04;
		constexpr uint8_t g_patternDump = 0x67;

		bool isMdMessage(const synthLib::SysexBuffer& _m, const uint8_t _command)
		{
			return _m.size() > 8 && _m[0] == 0xf0 && _m[1] == 0x00 && _m[2] == 0x20 && _m[3] == 0x3c
				&& _m[4] == 0x02 && _m[6] == _command;
		}
	}

	StudioLink::StudioLink(AudioPluginAudioProcessor& _processor, Controller& _controller)
		: m_processor(_processor)
		, m_sysexListener(_controller.evDeviceSysex, [this](const synthLib::SysexBuffer& _m) { onDeviceSysex(_m); })
		, m_alive(std::make_shared<StudioLink*>(this))
	{
	}

	StudioLink::~StudioLink()
	{
		*m_alive = nullptr;
	}

	void StudioLink::requestCurrentPattern()
	{
		sendSysex({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x70, g_statusPattern, 0xf7});
	}

	void StudioLink::sendPattern(const elektronData::MdPattern& _pattern)
	{
		sendSysex(elektronData::encodeMdPattern(_pattern));
		sendSysex(elektronData::mdPatternRequest(_pattern.position));
	}

	std::optional<uint8_t> StudioLink::readPlayhead() const
	{
		return m_processor.getPlugin().withDeviceLocked([](synthLib::Device* _base) -> std::optional<uint8_t>
		{
			auto* device = dynamic_cast<md::Device*>(_base);
			if(!device || !device->isValid())
				return {};
			auto& hw = device->getHardware();
			if(hw.getModel() != md::MachineModel::Machinedrum || hw.firmwareFingerprint() != md::g_mdOs163Fingerprint)
				return {};
			return hw.getUC().read8(g_mdPlayheadAddress);
		});
	}

	void StudioLink::onDeviceSysex(const synthLib::SysexBuffer& _message)
	{
		if(isMdMessage(_message, g_statusResponse) && _message[7] == g_statusPattern)
		{
			sendSysex(elektronData::mdPatternRequest(_message[8]));
			return;
		}
		if(!isMdMessage(_message, g_patternDump))
			return;
		auto pattern = elektronData::decodeMdPattern({_message.begin(), _message.end()});
		if(!pattern)
			return;
		juce::MessageManager::callAsync([alive = std::weak_ptr<StudioLink*>(m_alive), p = std::move(*pattern)]
		{
			const auto self = alive.lock();
			if(self && *self && (*self)->onPattern)
				(*self)->onPattern(p);
		});
	}

	void StudioLink::sendSysex(const std::vector<uint8_t>& _message) const
	{
		// Same route as Controller::sendSysEx: editor-sourced MIDI into the device.
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		event.sysex.assign(_message.begin(), _message.end());
		m_processor.addMidiEvent(event);
	}
}
