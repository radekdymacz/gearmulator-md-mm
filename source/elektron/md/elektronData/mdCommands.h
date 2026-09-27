#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace elektronData
{
	// Short Machinedrum control messages (manual Appendix C). Pure byte builders
	// and parsers; sending them is the transport's job.
	enum class MdStatus : uint8_t
	{
		GlobalSlot = 0x01,
		Kit = 0x02,
		Pattern = 0x04,
		Song = 0x08,
		SequencerMode = 0x10,	// 0 pattern mode, 1 song mode
		LockMode = 0x20,		// 0 classic, 1 extended
		Track = 0x22
	};

	struct MdStatusValue
	{
		MdStatus param;
		uint8_t value;
	};

	std::vector<uint8_t> mdStatusRequest(MdStatus _param);
	std::vector<uint8_t> mdSetStatus(MdStatus _param, uint8_t _value);
	std::optional<MdStatusValue> parseMdStatusResponse(const std::vector<uint8_t>& _sysex);

	std::vector<uint8_t> mdLoadPattern(uint8_t _slot);

	// Working-kit edits (manual Appendix C). Like a knob move they change the kit
	// that plays, not its stored slot: that needs SAVE KIT. Values are masked to
	// the ranges the manual gives.
	enum class MdMachineInit : uint8_t
	{
		Synthesis = 0,				// the firmware re-initialises the 8 synthesis parameters
		SynthesisEffects = 1,
		SynthesisEffectsRouting = 2
	};

	// _model as stored in a kit (UW machines carry g_mdUwFlag, see mdMachines.h).
	std::vector<uint8_t> mdAssignMachine(uint8_t _track, uint32_t _model, MdMachineInit _init);
	// _fx in MdKit::MasterFx order: 0 gate box, 1 rhythm echo, 2 EQ, 3 dynamix.
	std::vector<uint8_t> mdSetMasterFx(size_t _fx, uint8_t _param, uint8_t _value);
	// _param: 0 track, 1 parameter, 2 shape 1, 3 shape 2, 4 update mode (the kit's
	// LFO block order). Speed, depth and mix are the track's routing 5-7 (CC).
	std::vector<uint8_t> mdSetLfo(uint8_t _lfo, uint8_t _param, uint8_t _value);
	std::vector<uint8_t> mdSetTrigGroup(uint8_t _track, uint8_t _target);
	std::vector<uint8_t> mdSetMuteGroup(uint8_t _track, uint8_t _target);
	// Up to 16 characters, 7-bit; shorter names are NUL padded.
	std::vector<uint8_t> mdSetKitName(const std::string& _name);
	// Global settings, current global slot: output 0-5 = A-F, 6 = MAIN.
	std::vector<uint8_t> mdSetTrackRouting(uint8_t _track, uint8_t _output);
	// Tempo in BPM x 24 (30-300 BPM).
	std::vector<uint8_t> mdSetTempo(uint16_t _tempo);

	// UW sample name (manual Appendix C, 0x73): slot 0-47 (0-31 on MKI), up to 4
	// 7-bit ASCII characters, space padded. Affects the current sample bank. The
	// firmware has no request for names: this only sets one. Empty if invalid.
	std::vector<uint8_t> mdSetSampleName(uint8_t _slot, const std::string& _name);

	// Which dump a complete MD SysEx message is, by command byte; 0 if none.
	uint8_t mdDumpCommand(const std::vector<uint8_t>& _sysex);

	constexpr uint8_t g_mdGlobalDump = 0x50;
	constexpr uint8_t g_mdKitDump = 0x52;
	constexpr uint8_t g_mdPatternDump = 0x67;
	constexpr uint8_t g_mdSongDump = 0x69;
}
