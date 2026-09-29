#include "mmStudioLink.h"
#include "mdEditFlowCounters.h"

#include "mdController.h"
#include "mdPluginProcessor.h"

#include "deskWire/mmWire.h"

#include "mdLib/mdhardware.h"
#include "mdLib/mdpanelsequence.h"

#include "jucePluginLib/parameter.h"
#include "synthLib/plugin.h"

#include "juce_events/juce_events.h"

namespace mdJucePlugin
{
	namespace
	{
		// parameterDescriptions_mm.json: pages 0-6 (8 each), 7 level, 8 mute.
		constexpr const char* g_names[7][8] = {
			{"SynthesisA", "SynthesisB", "SynthesisC", "SynthesisD", "SynthesisE", "SynthesisF", "SynthesisG", "SynthesisH"},
			{"AmpAttack", "AmpHold", "AmpDecay", "AmpRelease", "AmpDistortion", "AmpVolume", "AmpPan", "AmpPortamento"},
			{"FilterBase", "FilterWidth", "FilterHighpassQ", "FilterLowpassQ", "FilterAttack", "FilterDecay", "FilterBaseOffset", "FilterWidthOffset"},
			{"EffectsEqFrequency", "EffectsEqGain", "EffectsSampleRate", "EffectsDelayTime", "EffectsDelaySend", "EffectsDelayFeedback", "EffectsDelayBase", "EffectsDelayWidth"},
			{"Lfo1Page", "Lfo1Destination", "Lfo1Trigger", "Lfo1Waveform", "Lfo1Multiplier", "Lfo1Speed", "Lfo1Interlace", "Lfo1Depth"},
			{"Lfo2Page", "Lfo2Destination", "Lfo2Trigger", "Lfo2Waveform", "Lfo2Multiplier", "Lfo2Speed", "Lfo2Interlace", "Lfo2Depth"},
			{"Lfo3Page", "Lfo3Destination", "Lfo3Trigger", "Lfo3Waveform", "Lfo3Multiplier", "Lfo3Speed", "Lfo3Interlace", "Lfo3Depth"}};

		double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

		std::optional<md::PanelControl> control(const mmDesk::Key _k)
		{
			using K = mmDesk::Key;
			using C = md::PanelControl;
			switch(_k)
			{
			case K::Exit: return C::Exit;
			case K::Enter: return C::Enter;
			case K::Up: return C::Up;
			case K::Down: return C::Down;
			case K::Left: return C::Left;
			case K::Right: return C::Right;
			case K::Play: return C::Play;
			case K::Stop: return C::Stop;
			case K::Global: return C::Kit;
			case K::Record: return C::Record;
			case K::LiveRecord: return C::Play;	// with RECORD held
			case K::MuteWindow: return C::BankGroup;	// with FUNCTION
			case K::Trig9: case K::Trig10: case K::Trig11: case K::Trig12: case K::Trig13: case K::Trig14:
				return static_cast<C>(static_cast<int>(C::Trigger1) + 8 + (static_cast<int>(_k) - static_cast<int>(K::Trig9)));
			}
			return std::nullopt;
		}
	}

	MmStudioLink::MmStudioLink(AudioPluginAudioProcessor& _processor, Controller& _controller)
		: m_processor(_processor)
		, m_controller(_controller)
		, m_sysexListener(_controller.evDeviceSysex, [this](const synthLib::SysexBuffer& _m) { onDeviceSysex(_m); })
		, m_alive(std::make_shared<MmStudioLink*>(this))
	{
		for(uint8_t t = 0; t < 6; ++t)
		{
			for(uint8_t pg = 0; pg <= 8; ++pg)
			{
				for(uint8_t i = 0; i < (pg < 7 ? 8 : 1); ++i)
				{
					auto* p = parameter(t, pg, i);
					if(!p)
						continue;
					const uint64_t bit = uint64_t{1} << (pg < 7 ? pg * 8 + i : 56 + (pg - 7));
					m_paramListeners.emplace_back(p->onValueChanged, [this, t, bit](pluginLib::Parameter*)
					{
						m_dirtyParams[t].fetch_or(bit, std::memory_order_relaxed);
					});
				}
			}
		}
	}

	MmStudioLink::~MmStudioLink()
	{
		*m_alive = nullptr;
	}

	const char* MmStudioLink::parameterName(const uint8_t _page, const uint8_t _index)
	{
		if(_page < 7)
			return _index < 8 ? g_names[_page][_index] : "";
		return _page == 7 ? "Level" : _page == 8 ? "Mute" : "";
	}

	pluginLib::Parameter* MmStudioLink::parameter(const uint8_t _track, const uint8_t _page, const uint8_t _index) const
	{
		if(_track > 5 || _page > 8)
			return nullptr;
		const auto* name = parameterName(_page, _index);
		return *name ? m_controller.getParameter(name, _track) : nullptr;
	}

	void MmStudioLink::sendSysex(const Bytes& _message) const
	{
		editFlow::sysexOut(_message);
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		event.sysex.assign(_message.begin(), _message.end());
		m_processor.addMidiEvent(event);
	}

	bool MmStudioLink::setParam(const uint8_t _track, const uint8_t _page, const uint8_t _index, const uint8_t _value) const
	{
		auto* p = parameter(_track, _page, _index);
		if(!p)
			return false;
		p->setUnnormalizedValueNotifyingHost(static_cast<int>(_value), pluginLib::Parameter::Origin::Ui);
		return true;
	}

	void MmStudioLink::sendNrpn(const uint8_t _track, const uint8_t _param, const uint8_t _value) const
	{
		// NRPN on the base channel, the wire's own encoding (deskWire::mm::nrpn).
		for(const auto& m : deskWire::mm::nrpn(static_cast<uint8_t>(m_controller.getAutomationBaseChannel() & 0x0f), _track, _param, _value))
			m_processor.addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Editor, m[0], m[1], m[2]));
	}

	bool MmStudioLink::sendMidi(const uint8_t _status, const uint8_t _data1, const uint8_t _data2) const
	{
		if(_status < 0x80 || _status >= 0xf0 || _data1 > 0x7f || _data2 > 0x7f)
			return false;
		m_processor.addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Editor, _status, _data1, _data2));
		return true;
	}

	bool MmStudioLink::pressKeys(const std::vector<mmDesk::Key>& _keys) const
	{
		std::vector<md::PanelPacket> states;
		const auto fn = md::panelPacket(md::MachineModel::Monomachine, md::PanelControl::Function);
		const auto rec = md::panelPacket(md::MachineModel::Monomachine, md::PanelControl::Record);
		for(const auto k : _keys)
		{
			const auto c = control(k);
			const auto pk = c ? md::panelPacket(md::MachineModel::Monomachine, *c) : std::nullopt;
			if(!pk || !fn || !rec)
				return false;
			if(mmDesk::isChord(k))
			{
				// FUNCTION (or RECORD for LIVE RECORDING) held while the key goes down and up
				const auto held = mmDesk::heldIsRecord(k) ? *rec : *fn;
				md::PanelRowState rows;
				states.push_back(rows.press(held));
				states.push_back(rows.press(*pk));
				states.push_back(rows.release(*pk));
				states.push_back(rows.release(held));
				continue;
			}
			states.push_back(*pk);
			states.push_back({pk->row, 0});
		}
		editFlow::panelOut(states.size());
		// 10 ms each, in machine time (the audio thread sends them).
		return m_processor.getPlugin().withDeviceLocked([&](synthLib::Device* _base)
		{
			auto* device = dynamic_cast<md::DeskDevice*>(_base);
			return device && device->sendPanelSequence(states, md::g_samplerate * 10 / 1000);
		});
	}

	mmDesk::Telemetry MmStudioLink::readTelemetry()
	{
		const auto now = nowMs();
		if(!m_telemetry || now - m_telemetryCheckedMs > 1000)
		{
			m_telemetryCheckedMs = now;
			m_telemetry = m_processor.getPlugin().withDeviceLocked(
				[](synthLib::Device* _base) -> std::shared_ptr<const md::MmTelemetry>
			{
				auto* device = dynamic_cast<md::DeskDevice*>(_base);
				return device ? device->getMmTelemetry() : nullptr;
			});
		}
		mmDesk::Telemetry t;
		if(!m_telemetry)
			return t;
		t.step = m_telemetry->step.load(std::memory_order_relaxed);
		const int running = m_telemetry->running.load(std::memory_order_relaxed);
		t.valid = t.step >= 0 && running >= 0;
		t.running = running == 1;
		t.screen = md::MmTelemetry::screenOf(m_telemetry->screen.load(std::memory_order_relaxed));
		t.recvCount = m_telemetry->recvCount.load(std::memory_order_relaxed);
		t.recvErrors = m_telemetry->recvErrors.load(std::memory_order_relaxed);
		t.recvActive = m_telemetry->recvActive.load(std::memory_order_relaxed) == 1;
		t.tempo = m_telemetry->tempo.load(std::memory_order_relaxed);
		t.mutes = m_telemetry->mutes.load(std::memory_order_relaxed);
		t.recording = m_telemetry->recording.load(std::memory_order_relaxed);
		return t;
	}

	bool MmStudioLink::readWorkingKit(Bytes& _region)
	{
		if(!m_telemetry)
			return false;
		uint32_t sequence = 0;
		if(!m_telemetry->readWorkingKit(_region, sequence))
			return false;
		if(sequence == m_workingKitSequence && m_workingKitSource == m_telemetry.get())
			return false;
		m_workingKitSequence = sequence;
		m_workingKitSource = m_telemetry.get();
		return true;
	}

	deskCore::LifeFacts::Probe MmStudioLink::probe() const
	{
		using P = deskCore::LifeFacts::Probe;
		return m_processor.getPlugin().withDeviceLocked([](synthLib::Device* _base)
		{
			auto* device = dynamic_cast<md::DeskDevice*>(_base);
			// No device yet: not a missing ROM (P5). Only an invalid device is a definite NO ROM.
			if(!device)
				return P::Loading;
			if(!device->isValid())
				return P::Missing;
			const auto& hw = device->getHardware();
			if(hw.getModel() != md::MachineModel::Monomachine || hw.firmwareFingerprint() != md::g_mmOs132bFingerprint)
				return P::Unsupported;
			if(device->isProjectStateRestorePending())
				return P::Loading;
			return hw.isFirmwareMidiReady() ? P::Running : P::Booting;
		});
	}

	bool MmStudioLink::readLcd(std::vector<uint8_t>& _bits) const
	{
		return m_processor.getPlugin().withDeviceLocked([&](synthLib::Device* _base)
		{
			auto* device = dynamic_cast<md::DeskDevice*>(_base);
			if(!device || !device->isValid())
				return false;
			const auto panel = device->getFrontPanelSnapshot();
			_bits.assign(1024, 0);
			for(uint32_t y = 0; y < 64; ++y)
				for(uint32_t x = 0; x < 128; ++x)
					if(panel.getLcdPixel(x, y))
						_bits[y * 16 + x / 8] |= static_cast<uint8_t>(0x80 >> (x & 7));
			return true;
		});
	}

	void MmStudioLink::drainParameterChanges(const std::function<void(uint8_t, uint8_t, uint8_t, uint8_t)>& _visit)
	{
		for(uint8_t t = 0; t < 6; ++t)
		{
			auto bits = m_dirtyParams[t].exchange(0, std::memory_order_relaxed);
			for(uint8_t b = 0; bits; ++b, bits >>= 1)
			{
				if(!(bits & 1))
					continue;
				const uint8_t page = b < 56 ? b / 8 : static_cast<uint8_t>(7 + (b - 56));
				const uint8_t index = b < 56 ? b % 8 : 0;
				if(const auto* p = parameter(t, page, index))
					_visit(t, page, index, static_cast<uint8_t>(p->getUnnormalizedValue()));
			}
		}
	}

	void MmStudioLink::onDeviceSysex(const synthLib::SysexBuffer& _message)
	{
		if(_message.size() < 9 || _message[0] != 0xf0 || _message[4] != 0x03)
			return;
		juce::MessageManager::callAsync([alive = std::weak_ptr<MmStudioLink*>(m_alive), m = Bytes(_message.begin(), _message.end())]
		{
			const auto self = alive.lock();
			if(self && *self && (*self)->onSysex)
				(*self)->onSysex(m);
		});
	}
}
