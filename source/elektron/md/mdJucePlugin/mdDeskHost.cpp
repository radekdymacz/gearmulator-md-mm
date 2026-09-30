#include "mdDeskHost.h"

#include "mdDeskSession.h"
#include "mdPluginProcessor.h"
#if MDMM_EDITFLOW_DRIVER
#include "mdEditFlowDriver.h"
#endif

#include "baseLib/binarystream.h"
#include "jucePluginLib/dummydevice.h"

namespace mdJucePlugin
{
	synthLib::Device* makeNoRomDevice()
	{
		return new pluginLib::DummyDevice({});
	}

	bool isNoRomDevice(const synthLib::Device* const _device)
	{
		return dynamic_cast<const pluginLib::DummyDevice*>(_device) != nullptr;
	}

	DeskHost::DeskHost(AudioPluginAudioProcessor& _processor) : m_processor(_processor)
	{
	}

	DeskHost::~DeskHost()
	{
		m_editFlowDriver.reset();
	}

	bool DeskHost::holdState(const void* const _data, const int _size)
	{
		if(!m_processor.getPlugin().withDeviceLocked([](synthLib::Device* const _d) { return isNoRomDevice(_d); }))
			return false;
		const std::lock_guard lock(m_heldMutex);
		m_held.assign(static_cast<const uint8_t*>(_data), static_cast<const uint8_t*>(_data) + std::max(_size, 0));
		return true;
	}

	bool DeskHost::heldState(juce::MemoryBlock& _out) const
	{
		if(!m_processor.getPlugin().withDeviceLocked([](synthLib::Device* const _d) { return isNoRomDevice(_d); }))
			return false;
		const std::lock_guard lock(m_heldMutex);
		_out.append(m_held.data(), m_held.size());
		return true;
	}

	bool DeskHost::takeHeldState(std::vector<uint8_t>& _out)
	{
		const std::lock_guard lock(m_heldMutex);
		_out = std::move(m_held);
		m_held.clear();
		return !_out.empty();
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
