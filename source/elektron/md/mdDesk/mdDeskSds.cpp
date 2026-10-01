#include "mdDeskSds.h"

namespace mdDesk
{
	namespace ed = elektronData;

	void SdsSender::start(const uint8_t _slot, ed::MdSdsDump _dump, const double _nowMs, const Send& _send)
	{
		m_dump = std::move(_dump);
		m_progress = {};
		m_progress.state = State::Sending;
		m_progress.slot = _slot;
		m_progress.total = m_dump.packets.size();
		m_waiting = Waiting::Header;
		m_packet = 0;
		m_packetRetries = 0;
		m_held = false;
		sendCurrent(_nowMs, _send);
	}

	void SdsSender::sendCurrent(const double _nowMs, const Send& _send)
	{
		m_sentMs = _nowMs;
		if(m_waiting == Waiting::Header)
			_send(m_dump.header);
		else
			_send(m_dump.packets[m_packet]);
	}

	// The current message is through: the next one, or done.
	void SdsSender::next(const double _nowMs, const Send& _send)
	{
		m_packetRetries = 0;
		m_held = false;
		if(m_waiting == Waiting::Header)
		{
			m_waiting = Waiting::Packet;
			m_packet = 0;
			_send(m_dump.name);
			sendCurrent(_nowMs, _send);
			return;
		}
		m_progress.sent = m_packet + 1;
		if(++m_packet >= m_dump.packets.size())
		{
			finish(State::Done, "");
			return;
		}
		sendCurrent(_nowMs, _send);
	}

	void SdsSender::finish(const State _state, std::string _text)
	{
		m_progress.state = _state;
		m_progress.text = std::move(_text);
		m_dump = {};
	}

	bool SdsSender::onReply(const Bytes& _message, const double _nowMs, const Send& _send)
	{
		if(!active())
			return false;
		const auto reply = ed::parseSdsReply(_message);
		if(!reply)
			return false;
		const auto [kind, number] = *reply;
		const bool current = number == (m_waiting == Waiting::Header ? 0 : packetNumber());
		// A lost ACK: the packet sent again is answered with a NAK for the next one.
		const bool wantsNext = kind == ed::SdsReply::Nak && m_waiting == Waiting::Packet && m_packetRetries
			&& number == static_cast<uint8_t>((m_packet + 1) & 0x7f) && m_packet + 1 < m_dump.packets.size();
		// Open loop goes on at its own pace; only a CANCEL stops it.
		if(!m_progress.handshake && kind != ed::SdsReply::Cancel)
			return true;
		switch(kind)
		{
		case ed::SdsReply::Cancel:
			finish(State::Failed, "The Machinedrum cancelled the transfer.");
			return true;
		case ed::SdsReply::Wait:
			m_held = true;
			m_sentMs = _nowMs;
			return true;
		case ed::SdsReply::Ack:
			if(current)
				next(_nowMs, _send);
			return true;
		case ed::SdsReply::Nak:
			if(wantsNext)
				next(_nowMs, _send);
			else if(current)
			{
				if(m_packetRetries >= g_retries)
					finish(State::Failed, "The Machinedrum refused a packet " + std::to_string(g_retries + 1) + " times (NAK).");
				else
				{
					++m_packetRetries;
					++m_progress.retries;
					m_held = false;
					sendCurrent(_nowMs, _send);
				}
			}
			return true;
		}
		return true;
	}

	void SdsSender::pump(const double _nowMs, const Send& _send)
	{
		if(!active())
			return;
		const double since = _nowMs - m_sentMs;
		if(m_held)
		{
			if(since >= g_waitMs)
				finish(State::Failed, "The Machinedrum asked to wait and did not go on within 30 s.");
			return;
		}
		if(!m_progress.handshake)
		{
			// Open loop: one packet every g_openLoopMs.
			while(active() && _nowMs - m_sentMs >= g_openLoopMs)
				next(m_sentMs + g_openLoopMs, _send);
			return;
		}
		if(since < g_replyMs)
			return;
		if(m_waiting == Waiting::Header)
		{
			// No answer to the header: the SDS rule says go on without a handshake.
			m_progress.handshake = false;
			next(_nowMs, _send);
			return;
		}
		if(m_packetRetries >= g_retries)
		{
			finish(State::Failed, "The Machinedrum stopped answering after " + std::to_string(m_progress.sent) + " of "
				+ std::to_string(m_progress.total) + " packets.");
			return;
		}
		++m_packetRetries;
		++m_progress.retries;
		sendCurrent(_nowMs, _send);
	}

	void SdsSender::cancel(const Send& _send)
	{
		if(!active())
			return;
		_send({0xf0, 0x7e, 0x00, 0x7d, m_waiting == Waiting::Header ? uint8_t(0) : packetNumber(), 0xf7});
		finish(State::Cancelled, "Stopped. The slot may hold part of the sample until another is loaded.");
	}
}
