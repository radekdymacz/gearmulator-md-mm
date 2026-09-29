#include "mdDeskHost.h"

#include "mdDeskSession.h"

#include "baseLib/binarystream.h"

namespace mdJucePlugin
{
	DeskHost::DeskHost(AudioPluginAudioProcessor& _processor) : m_processor(_processor)
	{
	}

	DeskHost::~DeskHost() = default;

	void DeskHost::startSession()
	{
		m_session = DeskSession::create(m_processor);
	}

	void DeskHost::saveChunks(baseLib::BinaryStream& _stream) const
	{
		if(const auto s = setup(); !s.empty())
		{
			baseLib::ChunkWriter chunk(_stream, "MDSK", 1);
			_stream.write(s);
		}
	}

	void DeskHost::addChunkReaders(baseLib::ChunkReader& _reader)
	{
		_reader.add("MDSK", 1, [this](baseLib::BinaryStream& _stream, uint32_t)
		{
			setSetup(_stream.readString());
		});
	}

	std::string DeskHost::setup() const
	{
		const std::lock_guard lock(m_setupMutex);
		return m_setup;
	}

	void DeskHost::setSetup(std::string _json)
	{
		const std::lock_guard lock(m_setupMutex);
		m_setup = std::move(_json);
		// The session picks it up on the message thread, whichever thread set it.
		m_setupVersion.fetch_add(1, std::memory_order_release);
	}
}
