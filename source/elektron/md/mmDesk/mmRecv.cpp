#include "mmRecv.h"

namespace mmDesk
{
	std::vector<Key> RecvSession::enterMacro()
	{
		std::vector<Key> k{Key::Global, Key::Enter};
		for(int i = 0; i < 4; ++i) k.push_back(Key::Left);
		for(int i = 0; i < 8; ++i) k.push_back(Key::Up);
		k.insert(k.end(), {Key::Down, Key::Down, Key::Right});
		for(int i = 0; i < 8; ++i) k.push_back(Key::Up);
		k.insert(k.end(), {Key::Down, Key::Enter, Key::Right, Key::Enter});
		return k;
	}

	std::vector<Key> RecvSession::exitKeys()
	{
		return std::vector<Key>(6, Key::Exit);
	}

	const char* RecvSession::stateName() const
	{
		switch(m_state)
		{
		case State::Idle: return "idle";
		case State::ToMain: return "toMain";
		case State::Entering: return "entering";
		case State::Parked: return "parked";
		case State::Leaving: return "leaving";
		case State::Failed: return "failed";
		}
		return "?";
	}

	RecvSession::Out RecvSession::tick(const double _now, const Telemetry& _t)
	{
		Out out;
		if(!_t.valid)
			return out;
		const bool onRecv = onSysexRecv(_t);
		const bool keysDone = _now >= m_keysDone;
		const bool onMain = _t.screen == g_screenMain;
		switch(m_state)
		{
		case State::Failed:
			if(_now - m_since < 5000)
				break;
			m_attempts = 0;
			go(State::Idle, _now);
			[[fallthrough]];
		case State::Idle:
			if(m_queue.empty())
				break;
			if(onRecv)
			{
				go(State::Parked, _now);
				m_lastActivity = _now;
				break;
			}
			if(onMain)
			{
				out.keys = enterMacro();
				go(State::Entering, _now);
			}
			else
			{
				out.keys = exitKeys();
				go(State::ToMain, _now);
			}
			break;
		case State::ToMain:
			if(!keysDone)
				break;
			if(onMain)
			{
				out.keys = enterMacro();
				go(State::Entering, _now);
			}
			else if(_now - m_since > timeoutMs)
			{
				if(++m_attempts > 2)
					go(State::Failed, _now);
				else
				{
					out.keys = exitKeys();
					go(State::ToMain, _now);
				}
			}
			break;
		case State::Entering:
			if(!keysDone)
				break;
			if(onRecv)
			{
				m_attempts = 0;
				m_lastActivity = _now;
				go(State::Parked, _now);
			}
			else if(_now - m_since > timeoutMs)
			{
				if(++m_attempts > 2)
					go(State::Failed, _now);
				else
				{
					out.keys = exitKeys();
					go(State::ToMain, _now);
				}
			}
			break;
		case State::Parked:
			if(!onRecv)
			{
				// Someone left the screen (the panel): enter again when needed.
				go(State::Idle, _now);
				break;
			}
			while(!m_queue.empty())
			{
				out.sends.push_back(std::move(m_queue.front()));
				m_queue.pop_front();
				m_lastActivity = _now;
			}
			if(_now - m_lastActivity > idleMs)
			{
				out.keys = exitKeys();
				go(State::Leaving, _now);
			}
			break;
		case State::Leaving:
			if(keysDone && (onMain || _now - m_since > timeoutMs))
				go(State::Idle, _now);
			break;
		}
		// Each key is held 10 ms and released for 10 ms (the edge's timing), plus a margin.
		if(!out.keys.empty())
			m_keysDone = _now + 20.0 * static_cast<double>(out.keys.size() + 1) + 60;
		return out;
	}
}
