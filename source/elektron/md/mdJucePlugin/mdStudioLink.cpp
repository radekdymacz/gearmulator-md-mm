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
	}

	StudioLink::StudioLink(AudioPluginAudioProcessor& _processor, Controller& _controller)
		: m_processor(_processor)
		, m_session([this](const std::vector<uint8_t>& _m) { sendSysex(_m); })
		, m_sysexListener(_controller.evDeviceSysex, [this](const synthLib::SysexBuffer& _m) { onDeviceSysex(_m); })
		, m_alive(std::make_shared<StudioLink*>(this))
	{
		m_session.onPattern = [this](const elektronData::MdPattern& _p)
		{
			if(onPattern)
				onPattern(_p);
		};
		// A newly reported current pattern is fetched, as the P0 editor did.
		m_session.onState = [this, last = std::optional<uint8_t>()](const mdDataLink::Session::State& _s) mutable
		{
			if(_s.pattern && _s.pattern != last)
				m_session.requestPattern(*_s.pattern);
			last = _s.pattern;
		};
	}

	StudioLink::~StudioLink()
	{
		*m_alive = nullptr;
	}

	void StudioLink::requestCurrentPattern()
	{
		// Always re-fetch, even when the pattern number did not change.
		sendSysex(elektronData::mdStatusRequest(elektronData::MdStatus::Pattern));
		if(const auto current = m_session.state().pattern)
			m_session.requestPattern(*current);
	}

	void StudioLink::sendPattern(const elektronData::MdPattern& _pattern)
	{
		m_session.pushPattern(_pattern);
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
		// Drain thread -> message thread; the Session is single-threaded.
		if(_message.size() < 9 || _message[0] != 0xf0 || _message[4] != 0x02)
			return;
		juce::MessageManager::callAsync([alive = std::weak_ptr<StudioLink*>(m_alive),
			m = std::vector<uint8_t>(_message.begin(), _message.end())]
		{
			const auto self = alive.lock();
			if(self && *self)
				(*self)->m_session.onSysex(m);
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
