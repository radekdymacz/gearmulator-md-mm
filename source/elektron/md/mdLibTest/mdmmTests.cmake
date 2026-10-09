# The editors' firmware rigs and unit tests beside upstream's in mdLibTest/ (doc/modern-ux/UPSTREAM.md).
# Included from mdmmEditors.cmake when BUILD_TESTING is on. The executables land in mdLibTest's own
# build folder, as they did when they were declared in its CMakeLists.

function(mdmm_add_lib_tests _dir)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mdLibTest")

	# P0 modern-UX proof: live pattern edits by SysEx against MD 1.63 firmware.
	# Manual: needs a user-supplied ROM (no firmware is bundled), writes WAVs.
	add_executable(mdPatternLiveEditFirmwareTest ${_dir}/patternLiveEditFirmwareTest.cpp)
	target_link_libraries(mdPatternLiveEditFirmwareTest PRIVATE mdLib elektronData)
	set_property(TARGET mdPatternLiveEditFirmwareTest PROPERTY FOLDER "Elektron/test")

	# P1 data layer: capture MD 1.63 user-data dumps (all slots, or a scripted
	# sequence of SysEx edits) for the elektronData codec corpus. Manual: needs a
	# user-supplied ROM.
	add_executable(mdDataCaptureFirmwareTest ${_dir}/mdDataCaptureFirmwareTest.cpp)
	target_link_libraries(mdDataCaptureFirmwareTest PRIVATE mdLib)
	set_property(TARGET mdDataCaptureFirmwareTest PROPERTY FOLDER "Elektron/test")

	# P1 data layer: corpus round trip and behaviour probes (queue, kit link,
	# kit/song push timing and continuity) against MD 1.63 firmware. Manual:
	# needs a user-supplied ROM.
	add_executable(mdDataLayerFirmwareTest ${_dir}/mdDataLayerFirmwareTest.cpp ${_dir}/mdFirmwareSession.h)
	target_link_libraries(mdDataLayerFirmwareTest PRIVATE mdLib mdDataLink)
	set_property(TARGET mdDataLayerFirmwareTest PROPERTY FOLDER "Elektron/test")

	# P2 MD Desk smoke test: page commands through mdDesk::Desk into MD 1.63
	# firmware, read back by dump (patterns, songs, globals) and SAVE KIT (the
	# working kit). Manual: needs a user-supplied ROM.
	add_executable(mdDeskFirmwareTest ${_dir}/mdDeskFirmwareTest.cpp ${_dir}/mdFirmwareSession.h ${_dir}/contractCheck.h)
	target_link_libraries(mdDeskFirmwareTest PRIVATE mdLib mdDesk deskWire)
	target_compile_definitions(mdDeskFirmwareTest PRIVATE MDDESK_SCHEMA="${_dir}/../../../../doc/modern-ux/md-data-contract.schema.json")
	set_property(TARGET mdDeskFirmwareTest PROPERTY FOLDER "Elektron/test")

	# P3 Machinedrum Editor discovery probes (working kit in memory, live
	# recording, sample names, SDS). Manual: needs a user-supplied ROM.
	add_executable(mdEditorProbeFirmwareTest ${_dir}/mdEditorProbeFirmwareTest.cpp ${_dir}/mdFirmwareSession.h)
	target_link_libraries(mdEditorProbeFirmwareTest PRIVATE mdLib elektronData)
	set_property(TARGET mdEditorProbeFirmwareTest PROPERTY FOLDER "Elektron/test")

	# DESIGN-edit-flow.md: the firmware and the desk under a stream of page edits (manual: needs a ROM).
	add_executable(editFlowBenchTest ${_dir}/editFlowBenchTest.cpp ${_dir}/mdFirmwareSession.h)
	target_link_libraries(editFlowBenchTest PRIVATE mdLib mdDesk mmDesk deskWire)
	set_property(TARGET editFlowBenchTest PROPERTY FOLDER "Elektron/test")

	# P4: CPU per emulated MD instance, headless (manual: needs a user-supplied ROM).
	add_executable(mdCpuBenchTest ${_dir}/mdCpuBenchTest.cpp ${_dir}/mdFirmwareSession.h)
	target_link_libraries(mdCpuBenchTest PRIVATE mdLib elektronData)
	set_property(TARGET mdCpuBenchTest PROPERTY FOLDER "Elektron/test")

	# MM-P3: the same measurement for the Monomachine OS 1.32B. Manual: needs a ROM.
	add_executable(mmCpuBenchTest ${_dir}/mmCpuBenchTest.cpp ${_dir}/mdFirmwareSession.h)
	target_link_libraries(mmCpuBenchTest PRIVATE mdLib elektronData)
	set_property(TARGET mmCpuBenchTest PROPERTY FOLDER "Elektron/test")

	# The gate of the emulation CPU plan (doc/modern-ux/RESEARCH-emulation-cpu.md, section 5): bit-exact audio and
	# RAM hashes plus host instructions and cycles per frame, MD or MM, stopped and playing. Manual: needs a ROM.
	# The local release gate (doc/release/LOCAL-GATE.md) runs it against recorded goldens (--golden, goldens/).
	add_executable(mdmmPerfGateTest ${_dir}/mdmmPerfGateTest.cpp ${_dir}/mdFirmwareSession.h ${_dir}/mmSysexRecv.h)
	target_link_libraries(mdmmPerfGateTest PRIVATE mdLib mmDesk)
	set_property(TARGET mdmmPerfGateTest PROPERTY FOLDER "Elektron/test")

	# The local release gate's ROM check: each supported image boots and answers SysEx, a truncated and a garbage
	# image are refused. Manual: needs a user-supplied ROM.
	add_executable(mdmmRomLoadTest ${_dir}/mdmmRomLoadTest.cpp ${_dir}/mdFirmwareSession.h)
	target_link_libraries(mdmmRomLoadTest PRIVATE mdLib elektronData)
	set_property(TARGET mdmmRomLoadTest PROPERTY FOLDER "Elektron/test")

	# The local release gate's SysEx round trip: a machine's full dump imported into a fresh one as the editors
	# import a file, dumped again, byte for byte; real backups (fixtures, never in the repo) document by document.
	# Manual: needs a user-supplied ROM.
	add_executable(mdmmSysexRoundTripTest ${_dir}/mdmmSysexRoundTripTest.cpp ${_dir}/mdFirmwareSession.h ${_dir}/mmSysexRecv.h)
	target_link_libraries(mdmmSysexRoundTripTest PRIVATE mdLib mmDesk)
	set_property(TARGET mdmmSysexRoundTripTest PROPERTY FOLDER "Elektron/test")

	# P4 Machinedrum Editor discovery probes (start-up animation, chaining,
	# mutes, kit and pattern library). Manual: needs a user-supplied ROM.
	add_executable(mdP4ProbeFirmwareTest ${_dir}/mdP4ProbeFirmwareTest.cpp ${_dir}/mdFirmwareSession.h)
	target_link_libraries(mdP4ProbeFirmwareTest PRIVATE mdLib elektronData)
	set_property(TARGET mdP4ProbeFirmwareTest PROPERTY FOLDER "Elektron/test")

	add_executable(mdSequencerStateTest ${_dir}/mdSequencerStateTest.cpp)
	target_link_libraries(mdSequencerStateTest PRIVATE mdLib)
	add_test(NAME mdSequencerStateTest COMMAND mdSequencerStateTest)
	set_tests_properties(mdSequencerStateTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mdSequencerStateTest PROPERTY FOLDER "Elektron/test")

	# MM-P0 Monomachine Editor discovery probes (SYSEX RECV gate, dumps, playhead,
	# CC live edits) against MM OS 1.32B firmware. Manual: needs a user-supplied ROM.
	add_executable(mmEditorProbeFirmwareTest ${_dir}/mmEditorProbeFirmwareTest.cpp ${_dir}/mdFirmwareSession.h ${_dir}/sysexPanelDriver.h)
	target_link_libraries(mmEditorProbeFirmwareTest PRIVATE mdLib elektronData)
	set_property(TARGET mmEditorProbeFirmwareTest PROPERTY FOLDER "Elektron/test")

	# MM-P2 Monomachine Editor smoke test: mmDesk::Desk against MM OS 1.32B firmware
	# (SYSEX RECV session, pattern/kit/song delivery, queue). Manual: needs a ROM.
	add_executable(mmDeskFirmwareTest ${_dir}/mmDeskFirmwareTest.cpp ${_dir}/mdFirmwareSession.h)
	target_link_libraries(mmDeskFirmwareTest PRIVATE mdLib mmDesk deskWire)
	target_compile_definitions(mmDeskFirmwareTest PRIVATE MMDESK_SCHEMA="${_dir}/../../../../doc/modern-ux/mm-data-contract.schema.json")
	set_property(TARGET mmDeskFirmwareTest PROPERTY FOLDER "Elektron/test")
	# -DGEARMULATOR_MM_ROM=<OS 1.32B image> runs it in ctest; without it the test skips.
	add_test(NAME mmDeskFirmwareTest COMMAND mmDeskFirmwareTest ${GEARMULATOR_MM_ROM})
	set_tests_properties(mmDeskFirmwareTest PROPERTIES LABELS "Integration;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 400)

	# Unit test: mdDesk::wirePort and mmDesk::wirePort send the right bytes (kit param, mute, NRPN,
	# panel keys) on the base channel. No ROM, no emulator - links the two wirePort INTERFACE
	# targets only.
	add_executable(deskWirePortTest ${_dir}/deskWirePortTest.cpp)
	target_link_libraries(deskWirePortTest PRIVATE mdDeskWirePort mmDeskWirePort)
	add_test(NAME deskWirePortTest COMMAND deskWirePortTest)
	set_tests_properties(deskWirePortTest PROPERTIES LABELS "UnitTest;Midi")
	set_property(TARGET deskWirePortTest PROPERTY FOLDER "Elektron/test")

	# .syx import and export (P7): synthetic dumps; no firmware.
	add_executable(syxImportTest ${_dir}/syxImportTest.cpp)
	target_link_libraries(syxImportTest PRIVATE elektronData)
	add_test(NAME syxImportTest COMMAND syxImportTest)
	set_tests_properties(syxImportTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET syxImportTest PROPERTY FOLDER "Elektron/test")

	# The same on two local third-party backups (never in the repo); skips (77) when they are absent.
	add_executable(syxImportFileTest ${_dir}/syxImportFileTest.cpp)
	target_link_libraries(syxImportFileTest PRIVATE elektronData)
	add_test(NAME syxImportFileTest COMMAND syxImportFileTest)
	set_tests_properties(syxImportFileTest PROPERTIES LABELS "UnitTest" SKIP_RETURN_CODE 77)
	set_property(TARGET syxImportFileTest PROPERTY FOLDER "Elektron/test")
endfunction()

mdmm_add_lib_tests("${CMAKE_CURRENT_LIST_DIR}")
