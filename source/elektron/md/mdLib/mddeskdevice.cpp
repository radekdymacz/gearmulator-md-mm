#include "mddeskdevice.h"

#include <algorithm>

namespace md
{
	bool DeskDevice::sendPanelSequence(const std::vector<PanelPacket>& _states, const uint32_t _holdFrames)
	{
		// Drop what was sent, then append.
		std::copy(m_panelSequence.begin() + static_cast<std::ptrdiff_t>(m_panelSequenceNext),
			m_panelSequence.begin() + static_cast<std::ptrdiff_t>(m_panelSequenceSize), m_panelSequence.begin());
		std::copy(m_panelSequenceHolds.begin() + static_cast<std::ptrdiff_t>(m_panelSequenceNext),
			m_panelSequenceHolds.begin() + static_cast<std::ptrdiff_t>(m_panelSequenceSize), m_panelSequenceHolds.begin());
		m_panelSequenceSize -= m_panelSequenceNext;
		m_panelSequenceNext = 0;
		if(_states.empty() || m_panelSequenceSize + _states.size() > m_panelSequence.size())
			return false;
		std::copy(_states.begin(), _states.end(), m_panelSequence.begin() + static_cast<std::ptrdiff_t>(m_panelSequenceSize));
		// Each packet keeps its own hold (a knob turn queued after a key does not shorten the key's).
		std::fill(m_panelSequenceHolds.begin() + static_cast<std::ptrdiff_t>(m_panelSequenceSize),
			m_panelSequenceHolds.begin() + static_cast<std::ptrdiff_t>(m_panelSequenceSize + _states.size()), _holdFrames);
		m_panelSequenceSize += _states.size();
		return true;
	}

	void DeskDevice::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
	{
		Device::processAudio(_inputs, _outputs, _samples);
		// P9: the audition on the main output (Main A/B), from its rate to the machine's.
		m_audition.mix(_outputs[0], _outputs[1], _samples, getSamplerate());
		if(m_silentOutput)
			for(auto* out : _outputs)
				if(out)
					std::fill_n(out, _samples, 0.0f);
		publishSequencerTelemetry(_samples);
		scanSamples();
		m_frames += _samples;
		sendPanelPackets();
	}

	std::shared_ptr<const elektronData::MdSampleBank> DeskDevice::readSampleBank(uint32_t& _sequence) const
	{
		m_sampleWanted.store(true, std::memory_order_relaxed);
		std::lock_guard lock(m_sampleMutex);
		_sequence = m_sampleSequence;
		return m_sampleBank;
	}

	// P9: the UW sample memory as elektronData reads it, on the thread that owns the hardware.
	elektronData::MdSampleMemory sampleMemoryOf(Hardware& _hardware)
	{
		elektronData::MdSampleMemory m;
		auto& uc = _hardware.getUC();
		m.flash = [&uc](const uint32_t _offset, const size_t _size, uint8_t* _out) { return uc.copyFlashDataRangeRealtime(_out, _offset, _size); };
		m.patch = [&uc](const uint32_t _offset) { return uc.read8(0x700000 + _offset); };
		auto& memory = _hardware.getDspProducer().dsp().memory();
		m.dspX = [&memory](const uint32_t _address) { return static_cast<uint32_t>(memory.get(dsp56k::MemArea_X, _address)); };
		return m;
	}

	void DeskDevice::scanSamples()
	{
		auto& hardware = getHardware();
		auto& s = m_samples;
		if(s.of != &hardware)
			s = SampleScan{&hardware};
		if(!m_sampleWanted.load(std::memory_order_relaxed) || getModel() != MachineModel::Machinedrum
			|| hardware.firmwareFingerprint() != g_mdOs163Fingerprint || !hardware.isFirmwareMidiReady())
			return;
		const auto memory = sampleMemoryOf(hardware);
		if(s.scanning)
		{
			// One slot a block: ROM 1-48, then RAM 1-4.
			if(s.next < elektronData::g_mdRomSlots)
				s.bank.rom.push_back(elektronData::readMdRomSample(memory, s.index, static_cast<uint8_t>(s.next)));
			else
				s.bank.ram.push_back(elektronData::readMdRamSample(memory, s.index, static_cast<uint8_t>(s.next - elektronData::g_mdRomSlots)));
			if(++s.next < size_t(elektronData::g_mdRomSlots) + elektronData::g_mdRamSlots)
				return;
			std::unique_lock lock(m_sampleMutex, std::try_to_lock);
			if(!lock.owns_lock())
			{
				--s.next;	// the last slot again next block, then publish
				s.bank.ram.pop_back();
				return;
			}
			m_sampleBank = std::make_shared<const elektronData::MdSampleBank>(std::move(s.bank));
			++m_sampleSequence;
			s.published = s.index.signature;
			s.scanning = false;
			return;
		}
		// Look at the memory about 10 times a second (64-frame blocks); read it once it is the same twice
		// and flash has been quiet for 0.3 s, or when asked.
		if(s.blocks++ % 64)
			return;
		const auto signature = elektronData::mdSampleSignature(memory);
		s.stable = signature == s.seen ? s.stable + 1 : 0;
		s.seen = signature;
		const auto refresh = m_sampleRefresh.load(std::memory_order_relaxed);
		const bool asked = refresh != s.refresh;
		auto& uc = hardware.getUC();
		const bool quiet = !uc.flashDirty() || uc.flashIdleCycles() > 12'000'000;
		if(!asked && (s.stable < 1 || !quiet || (m_sampleSequence && signature == s.published)))
			return;
		s.refresh = refresh;
		s.index = elektronData::indexMdSamples(memory);
		s.bank = {};
		s.bank.signature = s.index.signature;
		s.bank.ramReadable = s.index.ramReadable;
		s.bank.ramReason = s.index.ramReason;
		s.next = 0;
		s.scanning = true;
	}

	void DeskDevice::sendPanelPackets()
	{
		if(m_panelSequenceNext < m_panelSequenceSize && m_frames >= m_panelSequenceAt)
		{
			const auto hold = m_panelSequenceHolds[m_panelSequenceNext];
			const auto& p = m_panelSequence[m_panelSequenceNext++];
			(void)getHardware().trySendPanelEvent(p.row, p.mask);
			m_panelSequenceAt = m_frames + hold;
		}
		// Pending until the last packet was sent and held.
		m_sequencerTelemetry->panelPending.store(m_panelSequenceNext < m_panelSequenceSize || m_frames < m_panelSequenceAt
			? static_cast<int>(m_panelSequenceSize - m_panelSequenceNext) + 1 : 0, std::memory_order_relaxed);
	}

	void DeskDevice::publishSequencerTelemetry(const size_t _frames)
	{
		// Plain RAM reads on the thread that owns the hardware, once per block.
		constexpr uint32_t patternAddress = 0x28d205;
		// Playing and recording: md::SequencerState (the stopped byte alone reads
		// "playing" after STOP pressed twice).
		auto& t = *m_sequencerTelemetry;
		auto& hardware = getHardware();
		if(getModel() == MachineModel::Monomachine)
		{
			if(hardware.firmwareFingerprint() == g_mmOs132bFingerprint)
			{
				auto& uc = hardware.getUC();
				m_mmTelemetry->publish([&uc](const uint32_t _a) { return uc.read8(_a); });
			}
			else
				m_mmTelemetry->clear();
		}
		if(getModel() != MachineModel::Machinedrum || hardware.firmwareFingerprint() != g_mdOs163Fingerprint)
		{
			t.step.store(-1, std::memory_order_relaxed);
			t.pattern.store(-1, std::memory_order_relaxed);
			t.playing.store(-1, std::memory_order_relaxed);
			t.recording.store(-1, std::memory_order_relaxed);
			t.gridEdit.store(-1, std::memory_order_relaxed);
			t.knobPage.store(-1, std::memory_order_relaxed);
			t.bootAnimation.store(-1, std::memory_order_relaxed);
			t.mutes.store(-1, std::memory_order_relaxed);
			t.chainActive.store(-1, std::memory_order_relaxed);
			return;
		}
		auto& uc = hardware.getUC();
		// A new machine (state restore, ROM change) boots again.
		if(m_bootAnimationOf != &hardware)
		{
			m_bootAnimationOf = &hardware;
			m_bootAnimation.reset();
		}
		if(hardware.isFirmwareMidiReady())
			m_bootAnimation.update(uc.read8(BootAnimation::g_mainScreenAddress), _frames);
		t.bootAnimation.store(m_bootAnimation.state(), std::memory_order_relaxed);
		const auto step = uc.read8(SequencerState::g_stepAddress);
		t.step.store(step, std::memory_order_relaxed);
		t.pattern.store(uc.read8(patternAddress), std::memory_order_relaxed);
		m_sequencer.update(step, uc.read8(SequencerState::g_stoppedAddress), uc.read8(SequencerState::g_recordLedAddress),
			_frames);
		t.playing.store(m_sequencer.playing() ? 1 : 0, std::memory_order_relaxed);
		t.recording.store(m_sequencer.recording() ? 1 : 0, std::memory_order_relaxed);
		t.gridEdit.store(m_sequencer.gridEdit() ? 1 : 0, std::memory_order_relaxed);
		const auto page = uc.read8(SequencerState::g_knobPageAddress);
		t.knobPage.store(page <= 2 ? page : -1, std::memory_order_relaxed);
		const auto blocks = t.blocks.fetch_add(1, std::memory_order_release);

		// The working kit, mutes and chain about 90 times a second.
		if(blocks % 8)
			return;
		t.mutes.store((uc.read8(ChainAndMutes::g_muteAddress) << 8) | uc.read8(ChainAndMutes::g_muteAddress + 1),
			std::memory_order_relaxed);
		{
			const auto long32 = [&](const uint32_t _a)
			{
				return (uint32_t(uc.read8(_a)) << 24) | (uint32_t(uc.read8(_a + 1)) << 16) | (uint32_t(uc.read8(_a + 2)) << 8)
					| uc.read8(_a + 3);
			};
			const auto active = long32(ChainAndMutes::g_chainAddress);
			const auto next = long32(ChainAndMutes::g_chainAddress + 4);
			const auto length = long32(ChainAndMutes::g_chainAddress + 8);
			const bool sane = active <= 1 && length <= ChainAndMutes::g_maxChain && next <= ChainAndMutes::g_maxChain;
			t.chainActive.store(sane ? static_cast<int>(active) : -1, std::memory_order_relaxed);
			t.chainNext.store(sane ? static_cast<int>(next) : -1, std::memory_order_relaxed);
			t.chainLength.store(sane ? static_cast<int>(length) : 0, std::memory_order_relaxed);
			for(uint32_t i = 0; sane && i < length; ++i)
				t.chain[i].store(static_cast<uint8_t>(long32(ChainAndMutes::g_chainAddress + 12 + 4 * i) & 0x7f),
					std::memory_order_relaxed);
		}
		constexpr auto size = SequencerTelemetry::g_workingKitSize;
		bool changed = t.workingKitSequence.load(std::memory_order_relaxed) == 0;
		for(size_t i = 0; i < size && !changed; ++i)
			changed = uc.read8(SequencerTelemetry::g_workingKitAddress + static_cast<uint32_t>(i))
				!= t.workingKit[i].load(std::memory_order_relaxed);
		if(!changed)
			return;
		const auto sequence = t.workingKitSequence.load(std::memory_order_relaxed);
		t.workingKitSequence.store(sequence + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		for(size_t i = 0; i < size; ++i)
			t.workingKit[i].store(uc.read8(SequencerTelemetry::g_workingKitAddress + static_cast<uint32_t>(i)),
				std::memory_order_relaxed);
		t.workingKitSequence.store(sequence + 2, std::memory_order_release);
	}
}
