#include "tcpStream.h"

#include <cstdint>

#include "exception.h"
#include "ptypes/pinet.h"

#ifndef _WIN32
#	include <sys/socket.h>
#	include <sys/time.h>
#endif

namespace networkLib
{
	TcpStream::TcpStream(ptypes::ipstream* _stream)	: m_stream(_stream)
	{
	}

	TcpStream::~TcpStream()
	{
		TcpStream::close();
		delete m_stream;
		m_stream = nullptr;
	}

	void TcpStream::close()
	{
		if(!m_stream)
			return;
		m_stream->close();
	}

	void TcpStream::interrupt()
	{
		if(!m_stream || m_stream->get_handle() == ptypes::invhandle)
			return;
#ifdef _WIN32
		const auto handle = static_cast<SOCKET>(m_stream->get_handle());
		::shutdown(handle, SD_BOTH);
		// On Windows, shutdown() is not documented to wake a recv() that another thread is blocked in. Cancelling
		// the socket's pending I/O does (a socket is an overlapped handle), and the shutdown fails any later call.
		::CancelIoEx(reinterpret_cast<HANDLE>(handle), nullptr);
#else
		::shutdown(m_stream->get_handle(), SHUT_RDWR);
#endif
	}

	bool TcpStream::setReadTimeout(const uint32_t _milliseconds)
	{
		if(!m_stream || m_stream->get_handle() == ptypes::invhandle)
			return false;
#ifdef _WIN32
		const DWORD timeout = _milliseconds;
		return ::setsockopt(static_cast<SOCKET>(m_stream->get_handle()), SOL_SOCKET, SO_RCVTIMEO,
			reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0;
#else
		timeval timeout{};
		timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(_milliseconds / 1000);
		timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>((_milliseconds % 1000) * 1000);
		return ::setsockopt(m_stream->get_handle(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0;
#endif
	}

	bool TcpStream::isValid() const
	{
		return m_stream && m_stream->get_active();
	}

	bool TcpStream::flush()
	{
		if (!isValid())
			return false;

		try
		{
			m_stream->flush();
		}
		catch(ptypes::exception* e)  // NOLINT(misc-throw-by-value-catch-by-reference)
		{
			const std::string msg(e->get_message());
			delete e;
			throw NetException(ConnectionLost, msg);
		}
		return true;
	}

	bool TcpStream::read(void* _buf, const uint32_t _byteSize)
	{
		if (!isValid())
			throw NetException(ConnectionClosed, "Couldn't read");

		try
		{
			const auto numRead = static_cast<uint32_t>(m_stream->read(_buf, static_cast<int>(_byteSize)));
			if(numRead == _byteSize)
				return true;
			throw NetException(ConnectionClosed, "Couldn't read");
		}
		catch(ptypes::exception* e)  // NOLINT(misc-throw-by-value-catch-by-reference)
		{
			const std::string msg(e->get_message());
			delete e;
			throw NetException(ConnectionLost, msg);
		}
	}

	bool TcpStream::write(const void* _buf, const uint32_t _byteSize)
	{
		if(!isValid())
			throw NetException(ConnectionClosed, "Couldn't write");

		try
		{
			const auto numWritten = static_cast<uint32_t>(m_stream->write(_buf, static_cast<int>(_byteSize)));
			if(_byteSize == numWritten)
				return true;
			throw NetException(ConnectionClosed, "Couldn't write");
		}
		catch(ptypes::exception* e)  // NOLINT(misc-throw-by-value-catch-by-reference)
		{
			const std::string msg(e->get_message());
			delete e;
			throw NetException(ConnectionLost, msg);
		}
	}
}
