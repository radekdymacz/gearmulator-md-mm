#include "mdStudioLink.h"

#include "mdController.h"
#include "mdPluginProcessor.h"

#include "mdLib/mdhardware.h"
#include "mdLib/mdpanel.h"

#include "elektronData/mdWorkingKit.h"

#include "jucePluginLib/parameter.h"
#include "synthLib/plugin.h"

#include "juce_events/juce_events.h"

#include <algorithm>
#include <cstdlib>

namespace mdJucePlugin
{
	namespace
	{
		// parameterDescriptions_md.json: pages 0-2 (synthesis, effects, routing),
		// 3 level, 4 mute; one part per track.
		constexpr const char* g_paramNames[26] = {
			"MachineParameter1", "MachineParameter2", "MachineParameter3", "MachineParameter4",
			"MachineParameter5", "MachineParameter6", "MachineParameter7", "MachineParameter8",
			"AMDepth", "AMRate", "EQFrequency", "EQGain", "FilterBase", "FilterWidth", "FilterQ",
			"SampleRateReduction",
			"Distortion", "Volume", "Pan", "DelaySend", "ReverbSend", "LFOSpeed", "LFOAmount", "LFOShape",
			"Level", "Mute"};
		constexpr uint8_t g_muteIndex = 25;

		double nowMs()
		{
			return juce::Time::getMillisecondCounterHiRes();
		}
	}

	StudioLink::StudioLink(AudioPluginAudioProcessor& _processor, Controller& _controller)
		: m_processor(_processor)
		, m_controller(_controller)
		, m_sysexListener(_controller.evDeviceSysex, [this](const synthLib::SysexBuffer& _m) { onDeviceSysex(_m); })
		, m_alive(std::make_shared<StudioLink*>(this))
	{
		for(uint8_t t = 0; t < 16; ++t)
		{
			for(uint8_t i = 0; i <= g_muteIndex; ++i)
			{
				auto* p = parameter(t, i);
				if(!p)
					continue;
				// Any thread; the message thread reads the values in drainParameterChanges.
				m_paramListeners.emplace_back(p->onValueChanged, [this, t, i](pluginLib::Parameter*)
				{
					m_dirtyParams[t].fetch_or(1u << i, std::memory_order_relaxed);
				});
			}
		}
	}

	StudioLink::~StudioLink()
	{
		*m_alive = nullptr;
	}

	const char* StudioLink::parameterName(const uint8_t _index)
	{
		return _index <= g_muteIndex ? g_paramNames[_index] : "";
	}

	pluginLib::Parameter* StudioLink::parameter(const uint8_t _track, const uint8_t _index) const
	{
		if(_track > 15 || _index > g_muteIndex)
			return nullptr;
		return m_controller.getParameter(g_paramNames[_index], _track);
	}

	void StudioLink::sendSysex(const Bytes& _message) const
	{
		// Same route as Controller::sendSysEx: editor-sourced MIDI into the device.
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		event.sysex.assign(_message.begin(), _message.end());
		m_processor.addMidiEvent(event);
	}

	bool StudioLink::setKitParam(const uint8_t _track, const uint8_t _index, const uint8_t _value) const
	{
		auto* p = parameter(_track, _index);
		if(!p || _index > 24)
			return false;
		p->setUnnormalizedValueNotifyingHost(static_cast<int>(_value), pluginLib::Parameter::Origin::Ui);
		return true;
	}

	bool StudioLink::setMute(const uint8_t _track, const bool _muted) const
	{
		auto* p = parameter(_track, g_muteIndex);
		if(!p)
			return false;
		p->setUnnormalizedValueNotifyingHost(_muted ? 1 : 0, pluginLib::Parameter::Origin::Ui);
		return true;
	}

	void StudioLink::drainParameterChanges(const std::function<void(uint8_t, uint8_t, uint8_t)>& _visit)
	{
		for(uint8_t t = 0; t < 16; ++t)
		{
			auto bits = m_dirtyParams[t].exchange(0, std::memory_order_relaxed);
			for(uint8_t i = 0; bits; ++i, bits >>= 1)
			{
				if(!(bits & 1))
					continue;
				if(const auto* p = parameter(t, i))
					_visit(t, i, static_cast<uint8_t>(p->getUnnormalizedValue()));
			}
		}
	}

	bool StudioLink::sendPanel(const uint8_t _row, const uint8_t _mask) const
	{
		return m_processor.getPlugin().withDeviceLocked([&](synthLib::Device* _base)
		{
			auto* device = dynamic_cast<md::Device*>(_base);
			return device && device->sendPanelEvent(_row, _mask);
		});
	}

	bool StudioLink::pressKey(const std::string& _key)
	{
		const auto states = md::panelKeySequence(md::MachineModel::Machinedrum, _key);
		if(states.empty())
			return false;
		// Held 40 ms each in machine time (the audio thread sends them).
		return m_processor.getPlugin().withDeviceLocked([&](synthLib::Device* _base)
		{
			auto* device = dynamic_cast<md::Device*>(_base);
			return device && device->sendPanelSequence(states, md::g_samplerate * 40 / 1000);
		});
	}

	bool StudioLink::turnKnob(const uint8_t _encoder, const int _steps) const
	{
		const auto command = md::panelEncoderCommand(md::MachineModel::Machinedrum,
			static_cast<md::PanelEncoder>(_encoder));
		if(_encoder > 7 || !command || !_steps)
			return false;
		bool ok = true;
		for(int i = 0; i < std::abs(_steps) && ok; ++i)
			ok = sendPanel(*command, _steps > 0 ? 0x01 : 0xff);
		return ok;
	}

	mdDesk::Telemetry StudioLink::readTelemetry()
	{
		// The device can be replaced (ROM change, state restore): look again now and then.
		const auto now = nowMs();
		if(!m_telemetry || now - m_telemetryCheckedMs > 1000)
		{
			m_telemetryCheckedMs = now;
			m_telemetry = m_processor.getPlugin().withDeviceLocked(
				[](synthLib::Device* _base) -> std::shared_ptr<const md::Device::SequencerTelemetry>
			{
				auto* device = dynamic_cast<md::Device*>(_base);
				return device ? device->getSequencerTelemetry() : nullptr;
			});
			m_panel = m_processor.getPlugin().withDeviceLocked([](synthLib::Device* _base) -> std::shared_ptr<md::FrontPanelPublisher>
			{
				auto* device = dynamic_cast<md::Device*>(_base);
				return device ? device->getFrontPanelPublisher() : nullptr;
			});
		}
		mdDesk::Telemetry t;
		if(!m_telemetry)
			return t;
		const int step = m_telemetry->step.load(std::memory_order_relaxed);
		const int playing = m_telemetry->playing.load(std::memory_order_relaxed);
		t.valid = step >= 0 && playing >= 0;
		t.step = step;
		t.pattern = m_telemetry->pattern.load(std::memory_order_relaxed);
		t.playing = playing == 1;
		t.recording = m_telemetry->recording.load(std::memory_order_relaxed) == 1;
		t.gridEdit = m_telemetry->gridEdit.load(std::memory_order_relaxed) == 1;
		t.knobPage = m_telemetry->knobPage.load(std::memory_order_relaxed);
		t.mutes = m_telemetry->mutes.load(std::memory_order_relaxed);
		const int active = m_telemetry->chainActive.load(std::memory_order_relaxed);
		t.chainKnown = active >= 0;
		if(t.chainKnown)
		{
			t.chain.active = active == 1;
			t.chain.next = m_telemetry->chainNext.load(std::memory_order_relaxed);
			const int length = std::min(16, m_telemetry->chainLength.load(std::memory_order_relaxed));
			for(int i = 0; i < length; ++i)
				t.chain.patterns.push_back(m_telemetry->chain[static_cast<size_t>(i)].load(std::memory_order_relaxed));
		}
		if(m_panel)
		{
			const auto panel = m_panel->read();
			using L = md::FrontPanel::ModeLed;
			if(panel.wasLedBankWritten(md::FrontPanel::LedBank::Mode))
				t.bankGroup = panel.getModeLed(L::BankGroupEH) ? 1 : panel.getModeLed(L::BankGroupAD) ? 0 : -1;
		}
		return t;
	}

	bool StudioLink::readWorkingKit(Bytes& _region)
	{
		using T = md::Device::SequencerTelemetry;
		static_assert(T::g_workingKitAddress == elektronData::g_mdWorkingKitRegionAddress
			&& T::g_workingKitSize == elektronData::g_mdWorkingKitRegionSize, "working-kit region: mdLib and elektronData disagree");
		if(!m_telemetry)
			return false;
		uint32_t sequence = 0;
		if(!m_telemetry->readWorkingKit(_region, sequence))
			return false;
		// A new device (state restore, ROM change) starts its own sequence.
		if(sequence == m_workingKitSequence && m_workingKitSource == m_telemetry.get())
			return false;
		m_workingKitSequence = sequence;
		m_workingKitSource = m_telemetry.get();
		return true;
	}

	mdDesk::Desk::Firmware StudioLink::firmware() const
	{
		return m_processor.getPlugin().withDeviceLocked([](synthLib::Device* _base)
		{
			auto* device = dynamic_cast<md::Device*>(_base);
			if(!device || !device->isValid())
				return mdDesk::Desk::Firmware::Missing;
			const auto& hw = device->getHardware();
			if(hw.getModel() != md::MachineModel::Machinedrum || hw.firmwareFingerprint() != md::g_mdOs163Fingerprint)
				return mdDesk::Desk::Firmware::Unsupported;
			if(device->isProjectStateRestorePending())
				return mdDesk::Desk::Firmware::Loading;
			if(!hw.isFirmwareMidiReady())
				return mdDesk::Desk::Firmware::Booting;
			return mdDesk::Desk::Firmware::Present;
		});
	}

	void StudioLink::onDeviceSysex(const synthLib::SysexBuffer& _message)
	{
		// Drain thread -> message thread; the Desk is single-threaded.
		if(_message.size() < 9 || _message[0] != 0xf0 || _message[4] != 0x02)
			return;
		juce::MessageManager::callAsync([alive = std::weak_ptr<StudioLink*>(m_alive),
			m = Bytes(_message.begin(), _message.end())]
		{
			const auto self = alive.lock();
			if(self && *self && (*self)->onSysex)
				(*self)->onSysex(m);
		});
	}
}
