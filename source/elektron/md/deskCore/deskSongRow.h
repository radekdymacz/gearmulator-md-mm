#pragma once

namespace deskCore
{
	// 0.3.5, the song playhead: the song row that is heard. Both firmwares keep the row in a RAM byte that moves to
	// the next row about two steps before the pass ends: it queues it, as the pattern byte does (measured,
	// mdP4ProbeFirmwareTest songrow with SONGROW_TRACE). The row heard is that byte as it was when the pass began:
	// at PLAY and at each wrap of the playhead. While stopped it is the byte as it is (the page marks no row then).
	// A value, fed every telemetry tick.
	class SongRowHeard
	{
	public:
		// _raw: the RAM byte (-1 unknown), _step: the playhead (-1 unknown), _playing: the transport.
		int update(const int _raw, const int _step, const bool _playing)
		{
			if(!_playing || _raw < 0)
			{
				m_heard = -1;
				m_playing = _playing;
				m_lastStep = _step;
				return _raw;
			}
			const bool wrapped = m_lastStep >= 0 && _step >= 0 && _step < m_lastStep;
			if(!m_playing || m_heard < 0 || wrapped)
				m_heard = _raw;
			m_playing = true;
			m_lastStep = _step;
			return m_heard;
		}

	private:
		int m_heard = -1;
		int m_lastStep = -1;
		bool m_playing = false;
	};
}
