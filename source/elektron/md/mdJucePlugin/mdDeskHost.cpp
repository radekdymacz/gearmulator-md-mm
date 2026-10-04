#include "mdDeskHost.h"

#include "mdController.h"
#include "mdDeskSession.h"
#include "mdPluginProcessor.h"
#if MDMM_EDITFLOW_DRIVER
#include "mdEditFlowDriver.h"
#endif

#include "baseLib/binarystream.h"
#include "juceUiLib/messageRoute.h"
#include "jucePluginLib/dummydevice.h"

#include <cstring>
#include <optional>

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
		genericUI::messageRoute::enable();	// no native alerts: the page shows what the plug-in has to say
	}

	DeskHost::~DeskHost()
	{
		genericUI::messageRoute::forget(static_cast<const void*>(&m_processor));	// its notices that wait for a window
		m_editFlowDriver.reset();
	}

	namespace
	{
		// A project as pluginLib::Processor::getStateInformation writes it: a header (the magic string, the
		// version and, in version 1, the device state) and then the processor's chunks as one length-prefixed
		// block, each chunk a 4CC, a version, a length and its data (baseLib::ChunkWriter). Bytes in, bytes out.
		struct ProjectBytes
		{
			std::vector<uint8_t> header;	// everything before the chunk block
			std::vector<uint8_t> chunks;	// the block's content
		};

		constexpr size_t g_chunkHeadSize = 12;	// 4CC, version, length

		std::optional<ProjectBytes> splitProject(const std::vector<uint8_t>& _project)
		{
			try
			{
				baseLib::BinaryStream s(_project);
				if(!s.checkString("DSP56300"))	// pluginLib::Processor's g_saveMagic
					return {};
				(void)s.readString();
				const auto version = s.read<uint32_t>();
				if(version != 1 && version != 2)
					return {};
				if(version == 1)
				{
					std::vector<uint8_t> deviceState;
					s.read(deviceState);
				}
				ProjectBytes p;
				const auto headerEnd = s.getReadPos();
				p.header.assign(_project.begin(), _project.begin() + headerEnd);
				s.read(p.chunks);
				if(s.getReadPos() != _project.size())
					return {};
				return p;
			}
			catch(const std::exception&)
			{
				return {};
			}
		}

		// [begin, end) of the chunk _fourCC (its head included) in a chunk block; nullopt if absent or malformed.
		std::optional<std::pair<size_t, size_t>> findChunk(const std::vector<uint8_t>& _chunks, const char* _fourCC)
		{
			size_t at = 0;
			while(at + g_chunkHeadSize <= _chunks.size())
			{
				uint32_t length;
				std::memcpy(&length, &_chunks[at + 8], sizeof(length));	// BinaryStream's own byte order
				const auto end = at + g_chunkHeadSize + static_cast<size_t>(length);
				if(end > _chunks.size())
					return {};
				if(std::memcmp(&_chunks[at], _fourCC, 4) == 0)
					return std::make_pair(at, end);
				at = end;
			}
			return {};
		}

		// The data of a project's chunk _fourCC; nullopt when the bytes are no such project, an empty vector
		// when the project has no such chunk.
		std::optional<std::vector<uint8_t>> chunkIn(const std::vector<uint8_t>& _project, const char* _fourCC)
		{
			const auto p = splitProject(_project);
			if(!p)
				return {};
			const auto c = findChunk(p->chunks, _fourCC);
			if(!c)
				return std::vector<uint8_t>{};
			return std::vector<uint8_t>(p->chunks.begin() + static_cast<ptrdiff_t>(c->first + g_chunkHeadSize),
				p->chunks.begin() + static_cast<ptrdiff_t>(c->second));
		}

		// The project with its chunk replaced by _chunk (a whole chunk, head included: in place, at the end if
		// the project had none, removed if _chunk is empty); nullopt when the bytes are no such project.
		std::optional<std::vector<uint8_t>> withChunk(const std::vector<uint8_t>& _project, const char* _fourCC, const std::vector<uint8_t>& _chunk)
		{
			auto p = splitProject(_project);
			if(!p)
				return {};
			auto at = p->chunks.end();
			if(const auto c = findChunk(p->chunks, _fourCC))
				at = p->chunks.erase(p->chunks.begin() + static_cast<ptrdiff_t>(c->first), p->chunks.begin() + static_cast<ptrdiff_t>(c->second));
			p->chunks.insert(at, _chunk.begin(), _chunk.end());
			baseLib::BinaryStream out;
			out.write(p->header.data(), p->header.size());
			out.write(p->chunks);
			std::vector<uint8_t> result;
			out.toVector(result);
			return result;
		}

		// The automation snapshot in a project's "AUTO" chunk (AudioPluginAudioProcessor::saveChunkData);
		// empty when there is none.
		std::vector<uint8_t> automationIn(const std::vector<uint8_t>& _project)
		{
			const auto data = chunkIn(_project, "AUTO");
			if(!data || data->empty())
				return {};
			try
			{
				baseLib::BinaryStream s(*data);
				std::vector<uint8_t> snapshot;
				s.read(snapshot);
				return snapshot;
			}
			catch(const std::exception&)
			{
				return {};
			}
		}

		// The editor's setup in a project's "MDSK" chunk (DeskHost::saveChunks): empty when the project has
		// none (the default setup), nullopt when the bytes are no such project.
		std::optional<std::string> setupIn(const std::vector<uint8_t>& _project)
		{
			const auto data = chunkIn(_project, "MDSK");
			if(!data)
				return {};
			if(data->empty())
				return std::string();
			try
			{
				baseLib::BinaryStream s(*data);
				return s.readString();
			}
			catch(const std::exception&)
			{
				return std::string();
			}
		}

		std::vector<uint8_t> automationChunk(const std::vector<uint8_t>& _snapshot)
		{
			std::vector<uint8_t> chunk;
			if(_snapshot.empty())
				return chunk;
			baseLib::BinaryStream s;
			{
				baseLib::ChunkWriter cw(s, "AUTO", 1);
				s.write(_snapshot);
			}
			s.toVector(chunk);
			return chunk;
		}

		std::vector<uint8_t> setupChunk(const std::string& _setup)
		{
			std::vector<uint8_t> chunk;
			if(_setup.empty())
				return chunk;
			baseLib::BinaryStream s;
			{
				baseLib::ChunkWriter cw(s, "MDSK", 1);
				s.write(_setup);
			}
			s.toVector(chunk);
			return chunk;
		}
	}

	std::vector<uint8_t> DeskHost::automationSnapshot() const
	{
		if(!m_processor.hasController())
			return {};
		return dynamic_cast<const Controller&>(m_processor.getController()).createAutomationSnapshot();
	}

	bool DeskHost::holdState(const void* const _data, const int _size)
	{
		if(!m_processor.getPlugin().withDeviceLocked([](synthLib::Device* const _d) { return isNoRomDevice(_d); }))
			return false;
		std::vector<uint8_t> project(static_cast<const uint8_t*>(_data), static_cast<const uint8_t*>(_data) + std::max(_size, 0));
		// What lives without a machine follows the project at once: the DAW's parameters and the editor's setup.
		if(const auto given = automationIn(project); !given.empty())
			(void)dynamic_cast<Controller&>(m_processor.getController()).restoreAutomationSnapshot(given);
		if(auto given = setupIn(project))
			setSetup(std::move(*given));
		auto automation = automationSnapshot();
		auto editorSetup = setup();
		const std::lock_guard lock(m_heldMutex);
		m_held = std::move(project);
		m_heldAutomation = std::move(automation);
		m_heldSetup = std::move(editorSetup);
		return true;
	}

	bool DeskHost::heldState(juce::MemoryBlock& _out) const
	{
		if(!m_processor.getPlugin().withDeviceLocked([](synthLib::Device* const _d) { return isNoRomDevice(_d); }))
			return false;
		std::vector<uint8_t> held;
		std::vector<uint8_t> heldAutomation;
		std::string heldSetup;
		{
			const std::lock_guard lock(m_heldMutex);
			held = m_held;
			heldAutomation = m_heldAutomation;
			heldSetup = m_heldSetup;
		}
		const auto automation = automationSnapshot();
		const auto editorSetup = setup();
		if(automation != heldAutomation || editorSetup != heldSetup)	// moved since the project was given
		{
			if(held.empty())
				return false;	// no project to keep: the plug-in's own state saves them
			if(automation != heldAutomation)
				if(auto project = withChunk(held, "AUTO", automationChunk(automation)))
					held = std::move(*project);
			if(editorSetup != heldSetup)
				if(auto project = withChunk(held, "MDSK", setupChunk(editorSetup)))
					held = std::move(*project);
		}
		_out.append(held.data(), held.size());
		return true;
	}

	bool DeskHost::takeHeldState(std::vector<uint8_t>& _out)
	{
		const std::lock_guard lock(m_heldMutex);
		_out = std::move(m_held);
		m_held.clear();
		m_heldAutomation.clear();
		m_heldSetup.clear();
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
