#include "commandReader.h"

#include "command.h"
#include "types.h"
#include "dsp56kBase/logging.h"
#include "networkLib/stream.h"

#include <stdexcept>
#include <string>

namespace bridgeLib
{
	namespace
	{
		// Checked before the payload buffer is resized: the size comes from the peer.
		void checkCommandSize(const uint32_t _size)
		{
			if(_size <= g_maxCommandSize)
				return;
			throw std::length_error("command size " + std::to_string(_size) + " exceeds the limit of " + std::to_string(g_maxCommandSize) + " bytes");
		}
	}

	CommandReader::CommandReader(CommandCallback&& _callback) : m_stream(512 * 1024), m_commandCallback(std::move(_callback))
	{
	}

	void CommandReader::read(networkLib::Stream& _stream)
	{
		// read command (4 bytes)
		char temp[5]{0,0,0,0,0};

		_stream.read(temp, 4);
		const auto command = static_cast<Command>(cmd(temp));

		// read size (4 bytes)
		uint32_t size;
		_stream.read(&size, sizeof(size));
		checkCommandSize(size);

		// read data (n bytes)
		m_stream.getVector().resize(size);
		_stream.read(m_stream.getVector().data(), size);

//		LOG("Recv cmd " << commandToString(command) << ", len " << size);
		m_stream.setReadPos(0);
		handleCommand(command, m_stream);
	}

	void CommandReader::read(baseLib::BinaryStream& _in)
	{
		// read command (4 bytes)
		char temp[5]{0,0,0,0,0};

		_in.read(temp[0]);
		_in.read(temp[1]);
		_in.read(temp[2]);
		_in.read(temp[3]);

		const auto command = cmd(temp);

		// read size (4 bytes)
		const uint32_t size = _in.read<uint32_t>();
		checkCommandSize(size);

		// read data (n bytes)
		m_stream.getVector().resize(size);
		for(size_t i=0; i<size; ++i)
			m_stream.getVector()[i] = _in.read<uint8_t>();

		m_stream.setReadPos(0);
		handleCommand(static_cast<Command>(command), m_stream);
	}
}
