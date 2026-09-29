#include "mdDeskHost.h"

#include "mdDeskSession.h"
#if MDMM_EDITFLOW_DRIVER
#include "mdEditFlowDriver.h"
#endif

#include "baseLib/binarystream.h"

namespace mdJucePlugin
{
	DeskHost::DeskHost(AudioPluginAudioProcessor& _processor) : m_processor(_processor)
	{
	}

	DeskHost::~DeskHost()
	{
		m_editFlowDriver.reset();
	}

	void DeskHost::startSession()
	{
		m_session = DeskSession::create(m_processor);
#if MDMM_EDITFLOW_DRIVER
		m_editFlowDriver = startEditFlowDriver(m_processor, *m_session);
#endif
	}

	void DeskHost::saveChunks(baseLib::BinaryStream& _stream) const
	{
		if(const auto s = setup(); !s.empty())
		{
			baseLib::ChunkWriter chunk(_stream, "MDSK", 1);
			_stream.write(s);
		}
		if(const auto c = controller(); !c.empty())
		{
			baseLib::ChunkWriter chunk(_stream, "MDCT", 1);
			_stream.write(c);
		}
	}

	void DeskHost::addChunkReaders(baseLib::ChunkReader& _reader)
	{
		_reader.add("MDSK", 1, [this](baseLib::BinaryStream& _stream, uint32_t)
		{
			setSetup(_stream.readString());
		});
		_reader.add("MDCT", 1, [this](baseLib::BinaryStream& _stream, uint32_t)
		{
			setController(_stream.readString());
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

	std::string DeskHost::controller() const
	{
		const std::lock_guard lock(m_setupMutex);
		return m_controller;
	}

	void DeskHost::setController(std::string _json)
	{
		const std::lock_guard lock(m_setupMutex);
		m_controller = std::move(_json);
		m_controllerVersion.fetch_add(1, std::memory_order_release);
	}
}
