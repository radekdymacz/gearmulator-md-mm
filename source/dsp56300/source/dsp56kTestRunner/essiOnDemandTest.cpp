#include "dsp56kEmu/dsp.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
	using namespace dsp56k;
	void require(bool condition, const char* message)
	{
		if(!condition) throw std::runtime_error(message);
	}
	struct Fixture
	{
		DefaultMemoryValidator validator;
		Memory memory{validator, 0x10000};
		Peripherals56303 x;
		PeripheralsNop y;
		DSP dsp{memory, &x, &y};
	};

	void receive(unsigned port, bool strict, bool arrivesBeforeEnable, bool pendingDma)
	{
		Fixture f;
		auto& rx = port ? f.x.getEssi1() : f.x.getEssi0();
		auto& dma = f.x.getDMA();
		rx.setOnDemandRxWireSemantics(strict);
		rx.setPendingReceiveDmaOnEnable(pendingDma);
		rx.writeCRA(3u << Essi::CRA_WL0);
		rx.writeCRB((1u << Essi::CRB_RE) | (1u << Essi::CRB_MOD));
		TWord incoming = 0x123456;
		rx.setReadRxCallback([&](uint64_t& index, Audio::RxFrame& frame)
		{
			frame.resize(1);
			frame[0] = Audio::RxSlot{incoming};
			++index;
		});
		dma.setDSR(2, port ? Essi::ESSI1_RX : Essi::ESSI0_RX);
		dma.setDDR(2, 0x700);
		dma.setDCO(2, 1); // two distinguishable words, one stereo frame
		const auto source = port ? DmaChannel::RequestSource::Essi1ReceiveData
			: DmaChannel::RequestSource::Essi0ReceiveData;
		const auto control = (4u << DmaChannel::Dam0) | (5u << (DmaChannel::Dam0 + 3))
			| (static_cast<TWord>(source) << DmaChannel::Drs0)
			| (1u << DmaChannel::Dtm0) | (1u << DmaChannel::De);
		if(arrivesBeforeEnable) rx.execRX();
		dma.setDCR(2, control);
		if(arrivesBeforeEnable && !pendingDma)
		{
			require(dma.getDDR(2) == 0x700, "legacy DMA enable behavior changed");
			return;
		}
		if(!arrivesBeforeEnable) rx.execRX();
		require(f.memory.get(MemArea_X, 0x700) == 0x123456, "first pending word lost");
		require(dma.getDDR(2) == 0x701 && dma.getDCO(2) == 0, "first word count wrong");
		require(!rx.getSR().test(Essi::SSISR_RDF), "DMA failed to consume RDF");
		incoming = 0x654321;
		rx.execRX();
		require(f.memory.get(MemArea_X, 0x701) == 0x654321, "stereo word order changed");
		require(!(dma.getDCR(2) & (1u << DmaChannel::De)), "DMA did not finish");
		// Re-arming an empty receiver must not replay the last consumed word.
		dma.setDDR(2, 0x710);
		dma.setDCO(2, 0);
		dma.setDCR(2, control);
		require(dma.getDDR(2) == 0x710, "empty receiver manufactured a request");
	}

	void transmit(bool strict)
	{
		Fixture f;
		auto& tx = f.x.getEssi0();
		tx.setOnDemandTxWireSemantics(strict);
		tx.writeCRA(3u << Essi::CRA_WL0);
		tx.writeCRB((1u << Essi::CRB_TE0) | (1u << Essi::CRB_MOD));
		std::vector<TWord> words;
		unsigned idleTicks = 0;
		tx.setOnDemandTxIdleCallback([&] { ++idleTicks; });
		tx.setWriteTxCallback([&](uint64_t& index, const Audio::TxFrame& frame)
		{
			words.push_back(frame[0][0]);
			++index;
		});
		tx.writeTX(0, 0x123456);
		tx.execTX();
		for(unsigned i = 0; i < 64; ++i) tx.execTX();
		tx.writeTX(0, 0x654321);
		tx.execTX();
		require(words.size() == (strict ? 2 : 66), "on-demand idle wire behavior wrong");
		require(idleTicks == (strict ? 64 : 0), "idle clock rendezvous missing or fabricated");
		require(words.front() == 0x123456 && words.back() == 0x654321, "fresh TX values changed");
	}
}

int main() try
{
	for(unsigned port : {0u, 1u})
		for(bool strict : {false, true})
			for(bool early : {false, true})
				for(bool pending : {false, true}) receive(port, strict, early, pending);
	transmit(false);
	transmit(true);
	std::cout << "PASS: pending/empty DMA receives, word order and fresh-only transmission\n";
	return 0;
}
catch(const std::exception& e)
{
	std::cerr << "FAIL: " << e.what() << '\n';
	return 1;
}
