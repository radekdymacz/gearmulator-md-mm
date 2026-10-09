#include "mdLib/mdsim.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
	void require(const bool _condition, const char* _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// Every case below runs once per stepping mode (md::Sim::setDeferStepping): both must give the same result.
	bool g_deferStepping = true;

	struct Sim : md::Sim
	{
		Sim() { setDeferStepping(g_deferStepping); }
	};

	uint32_t configureUart(md::Sim& _sim, const unsigned _uart)
	{
		const auto base = _uart == md::Sim::g_uartPanel ? md::Sim::g_uart2Base : md::Sim::g_uart1Base;
		// Select receive-ready mode and enable RX/TX. This does not validate the
		// incomplete command, mode-pointer, FIFO-full interrupt or serial timing model.
		_sim.write8(base + md::Sim::g_uartMr, 0x13);
		_sim.write8(base + md::Sim::g_uartCr, 0x05);
		const auto icr = _uart == md::Sim::g_uartPanel ? md::Sim::g_icrUart2 : md::Sim::g_icrUart1;
		_sim.write8(icr, 3 << 2);
		_sim.write8(base + md::Sim::g_uartIvr, 0x60);
		_sim.write16(md::Sim::g_imr, 0);
		return base;
	}

	void sourceStatus(const unsigned _uart)
	{
		Sim sim;
		const auto base = configureUart(sim, _uart);
		// MCF5206EUM 12.4.1.10/.11: UIMR gates delivery, not UISR source status.
		sim.write8(base + md::Sim::g_uartIsr, 0);
		require(sim.read8(base + md::Sim::g_uartIsr) == md::Sim::g_uimrTxRdy,
			"masked transmitter readiness disappeared from UISR");
		sim.queueRx(_uart, 0x42);
		const auto ready = sim.read8(base + md::Sim::g_uartIsr);
		require(ready == (md::Sim::g_uimrTxRdy | md::Sim::g_uimrRxRdy), "incorrect ready sources in UISR");
		require(!sim.isReceiveInterruptEnabled(_uart), "status read changed UIMR");
		require(sim.queuedRxBytes(_uart) == 1, "status read consumed receive data");
		const auto other = _uart == md::Sim::g_uartMidi ? md::Sim::g_uartPanel : md::Sim::g_uartMidi;
		const auto otherBase = other == md::Sim::g_uartPanel ? md::Sim::g_uart2Base : md::Sim::g_uart1Base;
		for(unsigned mask = 0; mask < 256; ++mask)
		{
			sim.write8(base + md::Sim::g_uartIsr, static_cast<uint8_t>(mask));
			require(sim.read8(base + md::Sim::g_uartIsr) == ready, "UIMR write changed source status");
			require(sim.isReceiveInterruptEnabled(_uart) == ((mask & md::Sim::g_uimrRxRdy) != 0),
				"status read changed UIMR");
			require(!sim.isReceiveInterruptEnabled(other), "mask write affected the other UART");
			require(sim.read8(otherBase + md::Sim::g_uartIsr) == md::Sim::g_uimrTxRdy,
				"receive status leaked to the other UART");
		}
		require(sim.queuedRxBytes(_uart) == 1, "repeated status reads consumed receive data");
		require(sim.isReceiveInterruptEnabled(_uart), "UIMR write did not enable receive interrupts");
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x42, "receive data changed");
		require(sim.read8(base + md::Sim::g_uartIsr) == md::Sim::g_uimrTxRdy,
			"drained receiver still reported ready");
		require(sim.isReceiveInterruptEnabled(_uart), "draining receive data changed UIMR");
	}

	void receiveUnmask(const unsigned _uart)
	{
		Sim sim;
		const auto base = configureUart(sim, _uart);
		sim.write8(base + md::Sim::g_uartIsr, 0);
		sim.queueRx(_uart, 0x57);
		uint8_t level = 0, vector = 0;
		require(!sim.takeNextInterrupt(level, vector), "masked UART requested service");
		require(!sim.needsInterruptCheck(), "idle interrupt scan did not clear the check gate");
		// No new byte or TX-ready interrupt may be needed to service pending RX.
		sim.write8(base + md::Sim::g_uartIsr, md::Sim::g_uimrRxRdy);
		require(sim.needsInterruptCheck(), "unmasking did not schedule an interrupt check");
		require(sim.takeNextInterrupt(level, vector), "unmasking stranded an already-received byte");
		require(level == 3 && vector == 0x60, "receive request used the wrong level/vector");
		require(sim.queuedRxBytes(_uart) == 1, "interrupt offer consumed receive data");
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x57, "unmasking changed pending receive data");
		require(!sim.takeNextInterrupt(level, vector), "drained receiver requested extra service");
	}

	void pendingMaskChanges(const unsigned _uart)
	{
		Sim sim;
		const auto base = configureUart(sim, _uart);
		const auto source = _uart == md::Sim::g_uartPanel ? md::Sim::g_irqSrcUart2 : md::Sim::g_irqSrcUart1;
		sim.write16(md::Sim::g_imr, static_cast<uint16_t>(1u << source));
		sim.queueRx(_uart, 0x42);
		sim.write8(base + md::Sim::g_uartIsr, md::Sim::g_uimrRxRdy);
		uint8_t level = 0, vector = 0;
		require(!sim.takeNextInterrupt(level, vector), "global mask did not block RX");
		require(!sim.needsInterruptCheck(), "globally masked scan did not settle");
		sim.write16(md::Sim::g_imr, 0);
		require(sim.needsInterruptCheck(), "global unmask did not reactivate the check gate");
		require(sim.takeNextInterrupt(level, vector), "global unmask lost pending RX");
		require(level == 3 && vector == 0x60, "global unmask changed interrupt routing");

		// None of these writes creates new RX data or consumes the offered byte.
		// Bit 7 changes another mask bit without enabling the model's TX source.
		for(const auto mask : {0x02, 0x82, 0x80, 0x82, 0x00, 0x02, 0x02})
		{
			sim.write8(base + md::Sim::g_uartIsr, static_cast<uint8_t>(mask));
			require(!sim.takeNextInterrupt(level, vector), "mask write duplicated an offered RX request");
			require(!sim.needsInterruptCheck(), "mask-only scan did not settle");
		}
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x42, "mask changes lost the offered byte");
		sim.queueRx(_uart, 0x43);
		require(sim.takeNextInterrupt(level, vector), "fresh receive data did not rearm RX");
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x43, "fresh receive data changed");
		require(!sim.takeNextInterrupt(level, vector), "freshly drained RX requested extra service");
	}

	void maskedDrain(const unsigned _uart)
	{
		Sim sim;
		const auto base = configureUart(sim, _uart);
		sim.queueRx(_uart, 0x41);
		sim.queueRx(_uart, 0x42);
		sim.queueRx(_uart, 0x43);
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x41, "masked polling changed the first byte");
		sim.write8(base + md::Sim::g_uartIsr, md::Sim::g_uimrRxRdy);
		uint8_t level = 0, vector = 0;
		require(sim.takeNextInterrupt(level, vector), "partially drained RX was stranded on unmask");
		require(!sim.takeNextInterrupt(level, vector), "queued bytes duplicated the current RX offer");
		sim.write8(base + md::Sim::g_uartIsr, 0);
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x42, "masked drain changed the second byte");
		require(!sim.takeNextInterrupt(level, vector), "draining bypassed the UART mask");
		sim.write8(base + md::Sim::g_uartIsr, md::Sim::g_uimrRxRdy);
		require(sim.takeNextInterrupt(level, vector), "masked drain lost the next byte's RX request");
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x43, "masked drain changed the last byte");
		require(!sim.takeNextInterrupt(level, vector), "fully drained RX requested extra service");

		// Draining before unmask must not resurrect a stale receive request.
		sim.write8(base + md::Sim::g_uartIsr, 0);
		sim.queueRx(_uart, 0x44);
		require(sim.read8(base + md::Sim::g_uartRxTx) == 0x44, "masked polling changed fresh data");
		sim.write8(base + md::Sim::g_uartIsr, md::Sim::g_uimrRxRdy);
		require(!sim.takeNextInterrupt(level, vector), "unmask resurrected drained RX");
		require(!sim.needsInterruptCheck(), "drained RX left the interrupt-check gate hot");

		sim.queueRx(_uart, 0x45);
		sim.reset();
		configureUart(sim, _uart);
		sim.write8(base + md::Sim::g_uartIsr, md::Sim::g_uimrRxRdy);
		require(sim.queuedRxBytes(_uart) == 0, "reset retained receive data");
		require(!sim.takeNextInterrupt(level, vector), "reset retained a receive request");
	}

	void panelTransmitPacing()
	{
		Sim sim;
		constexpr auto uart = md::Sim::g_uartPanel;
		constexpr auto base = md::Sim::g_uart2Base;
		std::array<uint8_t, 3> received{};
		size_t count = 0;
		sim.setTransmitCallback(uart, [&](const uint8_t byte)
		{
			if(count < received.size())
				received[count++] = byte;
		});

		// MD OS 1.63 panel configuration: 8N1, internal baud generator, UBG=8.
		sim.write8(base + md::Sim::g_uartMr, 0xb3);
		sim.write8(base + md::Sim::g_uartMr, 0x07);
		sim.write8(base + md::Sim::g_uartUsr, 0xdd);
		sim.write8(base + md::Sim::g_uartBg1, 0x00);
		sim.write8(base + md::Sim::g_uartBg2, 0x08);
		sim.write8(base + md::Sim::g_uartCr, 0x04);
		require(sim.read8(base + md::Sim::g_uartUsr)
			== (md::Sim::g_usrTxEmp | md::Sim::g_usrTxRdy),
			"enabled empty panel transmitter did not report ready");

		sim.write8(base + md::Sim::g_uartRxTx, 0x11);
		require(count == 0 && sim.cyclesUntilNextUartTransmit() == 2560,
			"first panel byte bypassed its configured wire time");
		require(sim.read8(base + md::Sim::g_uartUsr) == md::Sim::g_usrTxRdy,
			"shift-register load did not free the holding register");
		sim.write8(base + md::Sim::g_uartRxTx, 0x22);
		sim.write8(base + md::Sim::g_uartRxTx, 0x33);
		require(sim.read8(base + md::Sim::g_uartUsr) == 0,
			"full panel holding register still reported ready");

		sim.exec(2559);
		require(count == 0 && sim.cyclesUntilNextUartTransmit() == 1,
			"panel byte completed before its stop bit");
		sim.exec(1);
		require(count == 1 && received[0] == 0x11
			&& sim.cyclesUntilNextUartTransmit() == 2560,
			"first completion did not advance the queued holding byte");
		require(sim.read8(base + md::Sim::g_uartUsr) == md::Sim::g_usrTxRdy,
			"holding-to-shift transfer did not reassert ready");
		sim.exec(2560);
		require(count == 2 && received[1] == 0x22,
			"second panel byte did not complete at its wire deadline");
		require(sim.read8(base + md::Sim::g_uartUsr)
			== (md::Sim::g_usrTxEmp | md::Sim::g_usrTxRdy),
			"drained panel transmitter did not report empty");
		require(sim.cyclesUntilNextUartTransmit() == md::Sim::g_noTimerInterruptDeadline,
			"drained panel transmitter retained a deadline");
	}

	void panelTransmitInterruptTiming()
	{
		Sim sim;
		constexpr auto uart = md::Sim::g_uartPanel;
		constexpr auto base = md::Sim::g_uart2Base;
		unsigned received = 0;
		sim.setTransmitCallback(uart, [&](uint8_t) { ++received; });
		sim.write8(base + md::Sim::g_uartMr, 0xb3);
		sim.write8(base + md::Sim::g_uartMr, 0x07);
		sim.write8(base + md::Sim::g_uartUsr, 0xdd);
		sim.write8(base + md::Sim::g_uartBg1, 0x00);
		sim.write8(base + md::Sim::g_uartBg2, 0x08);
		sim.write8(base + md::Sim::g_uartCr, 0x04);
		sim.write8(md::Sim::g_icrUart2, 3 << 2);
		sim.write8(base + md::Sim::g_uartIvr, 0x60);
		sim.write16(md::Sim::g_imr, 0);
		sim.write8(base + md::Sim::g_uartIsr, md::Sim::g_uimrTxRdy);

		uint8_t level = 0, vector = 0;
		require(sim.takeNextInterrupt(level, vector) && level == 3 && vector == 0x60,
			"enabling an empty panel transmitter did not request its first byte");
		sim.write8(base + md::Sim::g_uartRxTx, 0x11);
		require(sim.takeNextInterrupt(level, vector),
			"loading the shift register did not request a second holding byte");
		sim.write8(base + md::Sim::g_uartRxTx, 0x22);
		require(!sim.takeNextInterrupt(level, vector),
			"full holding register requested another transmit byte");
		sim.exec(2559);
		require(received == 0 && !sim.takeNextInterrupt(level, vector),
			"transmit interrupt arrived before the first stop bit completed");
		sim.exec(1);
		require(received == 1 && sim.takeNextInterrupt(level, vector),
			"character completion did not reassert TxRDY interrupt");
		require(!sim.takeNextInterrupt(level, vector),
			"one holding-register transition offered duplicate interrupts");
	}

	// A seeded xorshift: the mirrors below draw the same program from the same seed.
	class Rng
	{
	public:
		explicit Rng(const uint64_t _seed) : m_state(_seed * 0x9e3779b97f4a7c15ull + 1) {}
		uint32_t next()
		{
			m_state ^= m_state << 13;
			m_state ^= m_state >> 7;
			m_state ^= m_state << 17;
			return static_cast<uint32_t>(m_state >> 11);
		}

	private:
		uint64_t m_state;
	};

	// One Sim driven by a random program of what the emulator does to it (instructions of 1 to 24 cycles, now and
	// then a long one; timer and UART register reads and writes; transmit bytes; interrupt delivery as processUC
	// does it; deadline queries; resets) and a trace of everything the program can see. The program depends only on
	// the seed, so mirrors of the same seed must leave the same trace, whatever their stepping mode.
	class Mirror
	{
	public:
		Mirror(const uint64_t _seed, const bool _defer, const bool _toggle)
			: m_rng(_seed), m_toggleRng(_seed + 1000), m_toggle(_toggle)
		{
			m_sim.setDeferStepping(_defer);
			m_sim.setTransmitCallback(md::Sim::g_uartPanel, [this](const uint8_t _byte) { note(1, _byte); });
			m_sim.setTransmitCallback(md::Sim::g_uartMidi, [this](const uint8_t _byte) { note(2, _byte); });
			configure();
		}

		const std::vector<uint64_t>& trace() const { return m_trace; }

		void step()
		{
			++m_action;
			// The mode changes at any point of the run, as the menu entry does.
			if(m_toggle && (m_toggleRng.next() & 15) == 0)
				m_sim.setDeferStepping((m_toggleRng.next() & 1) != 0);

			const auto op = m_rng.next() % 100;
			const auto timer = (m_rng.next() & 1) ? md::Sim::g_timer1Base : md::Sim::g_timer2Base;
			const auto base = (m_rng.next() & 1) ? md::Sim::g_uart1Base : md::Sim::g_uart2Base;
			const auto value = m_rng.next();
			if(op < 50)
			{
				// processUC: the instruction's cycles, then the interrupt scan when the SIM asks for it.
				m_sim.exec((value % 64) == 0 ? 1 + (value >> 6) % 6000 : 1 + (value >> 6) % 24);
				note(5, m_sim.needsInterruptCheck());
				if(m_sim.needsInterruptCheck())
				{
					uint8_t level = 0, vector = 0;
					while(m_sim.takeNextInterrupt(level, vector))
						note(3, static_cast<uint32_t>(level) << 8 | vector);
				}
			}
			else if(op < 56)
				note(4, m_sim.read16(timer + md::Sim::g_timerTcn));
			else if(op < 60)
				note(4, m_sim.read8(timer + md::Sim::g_timerTer));
			else if(op < 66)
				note(4, m_sim.read8(base + md::Sim::g_uartUsr));
			else if(op < 68)
				note(4, m_sim.read8(base + md::Sim::g_uartIsr));
			else if(op < 74)
				m_sim.write8(base + md::Sim::g_uartRxTx, static_cast<uint8_t>(value));
			else if(op < 78)
			{
				// RST (mostly on), ICLK master or master/16 or stopped, FRR, ORI, a small prescaler.
				const uint16_t tmr = static_cast<uint16_t>(((value & 7) != 0 ? 1 : 0) | ((value >> 3) % 3) << 1
					| ((value >> 5) & 1) << 3 | ((value >> 6) & 1) << 4 | ((value >> 7) % 5) << 8);
				m_sim.write16(timer + md::Sim::g_timerTmr, tmr);
			}
			else if(op < 82)
				m_sim.write16(timer + md::Sim::g_timerTrr, static_cast<uint16_t>((value & 3) == 0 ? value % 8 : value % 3000));
			else if(op < 84)
				m_sim.write8(timer + md::Sim::g_timerTer, static_cast<uint8_t>(value));
			else if(op < 86)
				m_sim.write16(timer + md::Sim::g_timerTcn, static_cast<uint16_t>(value));
			else if(op < 88)
				m_sim.write8(base + md::Sim::g_uartIsr, static_cast<uint8_t>(value & 3));
			else if(op < 89)
				m_sim.write16(md::Sim::g_imr, (value & 1) ? 0 : static_cast<uint16_t>(value & 0x3ffe));
			else if(op < 92)
			{
				note(6, m_sim.cyclesUntilNextTimerInterrupt());
				note(7, m_sim.cyclesUntilNextUartTransmit());
			}
			else if(op < 94)
				m_sim.setMidiTransmitCharacterCycles((value & 3) == 0 ? 0 : 80 << (value >> 2) % 6);
			else if(op < 96)
				m_sim.queueRx((value & 1) ? md::Sim::g_uartPanel : md::Sim::g_uartMidi, static_cast<uint8_t>(value >> 1));
			else if(op < 97)
				observeAll();
			else if(op == 97 && (value & 7) == 0)
			{
				m_sim.reset();
				configure();
			}
			else
				m_sim.exec(1 + value % 16);
		}

	private:
		void note(const uint64_t _kind, const uint64_t _value)
		{
			m_trace.push_back(_kind << 56 | (m_action & 0xffffff) << 32 | (_value & 0xffffffff));
		}

		void observeAll()
		{
			for(const auto timer : {md::Sim::g_timer1Base, md::Sim::g_timer2Base})
			{
				note(8, m_sim.read16(timer + md::Sim::g_timerTcn));
				note(8, m_sim.read8(timer + md::Sim::g_timerTer));
			}
			for(const auto base : {md::Sim::g_uart1Base, md::Sim::g_uart2Base})
			{
				note(9, m_sim.read8(base + md::Sim::g_uartUsr));
				note(9, m_sim.read8(base + md::Sim::g_uartIsr));
			}
			note(10, m_sim.read16(md::Sim::g_ipr));
		}

		// The panel UART as the MD firmware programs it, the MIDI UART the same, Timer 1 as its tick.
		void configure()
		{
			for(const auto base : {md::Sim::g_uart1Base, md::Sim::g_uart2Base})
			{
				m_sim.write8(base + md::Sim::g_uartMr, 0xb3);
				m_sim.write8(base + md::Sim::g_uartMr, 0x07);
				m_sim.write8(base + md::Sim::g_uartUsr, 0xdd);
				m_sim.write8(base + md::Sim::g_uartBg1, 0x00);
				m_sim.write8(base + md::Sim::g_uartBg2, 0x08);
				m_sim.write8(base + md::Sim::g_uartCr, 0x04);
				m_sim.write8(base + md::Sim::g_uartIvr, 0x60);
				m_sim.write8(base + md::Sim::g_uartIsr, md::Sim::g_uimrTxRdy);
			}
			m_sim.write8(md::Sim::g_icrUart1, 3 << 2);
			m_sim.write8(md::Sim::g_icrUart2, 3 << 2);
			m_sim.write8(md::Sim::g_icrTimer1, md::Sim::g_icrAutovector | 1 << 2);
			m_sim.write8(md::Sim::g_icrTimer2, md::Sim::g_icrAutovector | 1 << 2);
			m_sim.write16(md::Sim::g_imr, 0);
			m_sim.setMidiTransmitCharacterCycles(2560);
			m_sim.write16(md::Sim::g_timer1Base + md::Sim::g_timerTrr, 700);
			m_sim.write16(md::Sim::g_timer1Base + md::Sim::g_timerTmr, 0x0101 | md::Sim::g_tmrOri);
		}

		Sim m_sim;
		Rng m_rng;
		Rng m_toggleRng;
		const bool m_toggle;
		uint64_t m_action = 0;
		std::vector<uint64_t> m_trace;
	};

	// The deferred stepping, the per-instruction stepping and a Sim that changes between the two at random points
	// must be indistinguishable: the same register reads, the same interrupts after the same instructions, the same
	// transmitted bytes at the same instruction, the same deadlines.
	void steppingModesAgree()
	{
		for(uint64_t seed = 1; seed <= 6; ++seed)
		{
			Mirror deferred(seed, true, false);
			Mirror perInstruction(seed, false, false);
			Mirror toggling(seed, true, true);
			size_t checked = 0;
			for(unsigned action = 0; action < 60000; ++action)
			{
				deferred.step();
				perInstruction.step();
				toggling.step();
				const auto& reference = perInstruction.trace();
				require(deferred.trace().size() == reference.size() && toggling.trace().size() == reference.size(),
					"a stepping mode saw a different number of events");
				for(; checked < reference.size(); ++checked)
				{
					require(deferred.trace()[checked] == reference[checked],
						"deferred stepping differs from per-instruction stepping");
					require(toggling.trace()[checked] == reference[checked],
						"switching the stepping mode changed what the SIM did");
				}
			}
			// Guard against a program that never gets anywhere: interrupts, bytes and reads must all occur.
			size_t interrupts = 0, bytes = 0;
			for(const auto entry : perInstruction.trace())
			{
				interrupts += entry >> 56 == 3;
				bytes += entry >> 56 == 1 || entry >> 56 == 2;
			}
			require(interrupts > 50 && bytes > 50, "the random program did not exercise timers and transmitters");
		}
	}
}

int main()
{
	unsigned failures = 0;
	for(const bool defer : {true, false})
	{
		g_deferStepping = defer;
		const char* const mode = defer ? "deferred" : "per-instruction";
		for(unsigned uart = 0; uart < md::Sim::g_uartCount; ++uart)
		{
			// Run independently so a status failure cannot hide the unmasking regression.
			for(const auto test : {sourceStatus, receiveUnmask, pendingMaskChanges, maskedDrain})
			{
				try
				{
					test(uart);
				}
				catch(const std::exception& error)
				{
					std::cerr << mode << " stepping, UART " << uart << ": " << error.what() << '\n';
					++failures;
				}
			}
		}
		try
		{
			panelTransmitPacing();
			panelTransmitInterruptTiming();
		}
		catch(const std::exception& error)
		{
			std::cerr << mode << " stepping, panel transmit pacing: " << error.what() << '\n';
			++failures;
		}
	}
	try
	{
		steppingModesAgree();
	}
	catch(const std::exception& error)
	{
		std::cerr << "stepping modes: " << error.what() << '\n';
		++failures;
	}
	if(failures)
		return 1;
	std::cout << "UART register, receive-unmask, and panel transmit timing tests passed, in both stepping modes\n";
	return 0;
}
