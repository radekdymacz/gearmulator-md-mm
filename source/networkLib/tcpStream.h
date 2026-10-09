#pragma once

#include <cstdint>

#include "stream.h"

namespace ptypes
{
	class ipstream;
}

namespace networkLib
{
	class TcpStream : public Stream
	{
	public:
		explicit TcpStream(ptypes::ipstream* _stream);
		virtual ~TcpStream();

		void close() override;
		bool isValid() const override;
		bool flush() override;

		// Wakes a thread that is blocked reading or writing this stream: the read sees the end of the stream,
		// the write fails. Callable from another thread, unlike close(): the socket stays open, so its
		// descriptor cannot be reused meanwhile. Not concurrently with the owner's close() (HttpServer
		// serialises them).
		void interrupt();

		// A read that waits longer than this for data fails with ConnectionLost. 0 waits forever (the default).
		bool setReadTimeout(uint32_t _milliseconds);

		auto* getPtypesStream() const { return m_stream; }

	private:
		bool read(void* _buf, uint32_t _byteSize) override;
		bool write(const void* _buf, uint32_t _byteSize) override;

		ptypes::ipstream* m_stream;
	};
}
