# The committed MD/MM profile (lever L6, doc/modern-ux/RESEARCH-emulation-cpu.md): mdmm-macos.proftext is LLVM's
# text profile format (function names, CFG hashes and execution counts, nothing from the firmware), trained on
# this Mac's firmware runs by scripts/macos/train_mdmm_pgo.sh; mdmm-macos.json says what it was trained on.
# The text format is the one every llvm-profdata reads, so the build converts it with its own compiler's tool
# and a profile written by a newer Xcode still works on the CI runner's older one.
#
# mdmm_committed_pgo_profile(<out-profile> <out-reason>): <out-profile> is the converted .profdata in the build
# tree, or empty with <out-reason> saying why (the caller then builds without PGO and warns). Sets the INTERNAL
# cache values GEARMULATOR_MDMM_APPLE_PGO_COMMITTED_SHA256 (of the text) and GEARMULATOR_MDMM_APPLE_PGO_STALE
# (the profiled source folders changed since training, "unknown" when git cannot say, empty when current).

function(mdmm_committed_pgo_profile _outProfile _outReason)
	set(${_outProfile} "" PARENT_SCOPE)
	set(_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
	set(_text "${_dir}/mdmm-macos.proftext")
	set(_meta "${_dir}/mdmm-macos.json")
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_text}" "${_meta}")
	if(NOT EXISTS "${_text}" OR NOT EXISTS "${_meta}")
		set(${_outReason} "no committed profile (${_text}, ${_meta})" PARENT_SCOPE)
		return()
	endif()

	# This compiler's own llvm-profdata (Xcode keeps it beside clang), else the selected Xcode's.
	get_filename_component(_compilerDir "${CMAKE_CXX_COMPILER}" DIRECTORY)
	find_program(_profdata NAMES llvm-profdata HINTS "${_compilerDir}" NO_DEFAULT_PATH NO_CACHE)
	if(NOT _profdata)
		execute_process(COMMAND xcrun --find llvm-profdata OUTPUT_VARIABLE _profdata
			OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _rc)
		if(NOT _rc EQUAL 0)
			set(_profdata "")
		endif()
	endif()
	if(NOT _profdata)
		set(${_outReason} "llvm-profdata not found beside ${CMAKE_CXX_COMPILER} or through xcrun" PARENT_SCOPE)
		return()
	endif()

	set(_out "${CMAKE_BINARY_DIR}/mdmm-pgo/mdmm-macos.profdata")
	file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/mdmm-pgo")

	# libc++ tags its inline functions with its version (e.g. ...B8ne200100...). When this SDK's libc++ is another
	# version than the training Mac's, give the profile this one's tag, so the many small std:: functions still
	# find their counts (clang compares each function's hash, so one whose body changed is still ignored).
	set(_input "${_text}")
	file(READ "${_text}" _profile)
	string(REGEX MATCH "B8([a-z][a-z])([0-9][0-9][0-9][0-9][0-9][0-9])" _tag "${_profile}")
	set(_tagLetters "${CMAKE_MATCH_1}")
	set(_tagVersion "${CMAKE_MATCH_2}")
	set(_sysroot "")
	if(CMAKE_OSX_SYSROOT)
		set(_sysroot -isysroot "${CMAKE_OSX_SYSROOT}")
	endif()
	execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -x c++ -std=c++17 ${_sysroot} -E -dM -include version /dev/null
		OUTPUT_VARIABLE _macros ERROR_QUIET RESULT_VARIABLE _rc)
	string(REGEX MATCH "#define _LIBCPP_VERSION ([0-9]+)" _found "${_macros}")
	set(_buildVersion "${CMAKE_MATCH_1}")
	if(_tag AND _buildVersion AND NOT _buildVersion STREQUAL _tagVersion)
		string(REPLACE "B8${_tagLetters}${_tagVersion}" "B8${_tagLetters}${_buildVersion}" _profile "${_profile}")
		set(_input "${CMAKE_BINARY_DIR}/mdmm-pgo/mdmm-macos.proftext")
		file(WRITE "${_input}" "${_profile}")
		message(STATUS "MD/MM PGO: libc++ tag ${_tagLetters}${_tagVersion} of the profile renamed to this SDK's ${_tagLetters}${_buildVersion}")
	endif()
	unset(_profile)

	execute_process(COMMAND "${_profdata}" merge --instr -o "${_out}" "${_input}"
		RESULT_VARIABLE _rc OUTPUT_VARIABLE _log ERROR_VARIABLE _log)
	if(NOT _rc EQUAL 0 OR NOT EXISTS "${_out}")
		set(${_outReason} "${_profdata} could not convert ${_text}: ${_log}" PARENT_SCOPE)
		return()
	endif()

	file(SHA256 "${_text}" _textSha)
	set(GEARMULATOR_MDMM_APPLE_PGO_COMMITTED_SHA256 "${_textSha}" CACHE INTERNAL
		"SHA-256 of the committed MD/MM text profile" FORCE)

	# Staleness: the git tree of each profiled folder now, against the one recorded at training. A changed folder
	# only means some functions lost their profile data (clang ignores those, with its own warning per file).
	file(READ "${_meta}" _json)
	string(JSON _recordedSha ERROR_VARIABLE _err GET "${_json}" profile sha256)
	if(NOT _err AND NOT _recordedSha STREQUAL _textSha)
		message(WARNING "MD/MM PGO: ${_text} does not match the SHA-256 in ${_meta}: retrain (scripts/macos/train_mdmm_pgo.sh)")
	endif()
	string(JSON _count ERROR_VARIABLE _err LENGTH "${_json}" sources)
	set(_stale "")
	if(_err OR _count EQUAL 0)
		set(_stale "unknown")
	else()
		find_package(Git QUIET)
		math(EXPR _last "${_count} - 1")
		foreach(_i RANGE ${_last})
			string(JSON _folder MEMBER "${_json}" sources ${_i})
			string(JSON _trained GET "${_json}" sources "${_folder}")
			set(_now "")
			if(GIT_EXECUTABLE)
				execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" rev-parse "HEAD:${_folder}"
					OUTPUT_VARIABLE _now OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _rc)
				if(NOT _rc EQUAL 0)
					set(_now "")
				endif()
			endif()
			if(NOT _now)
				set(_stale "unknown")
				break()
			elseif(NOT _now STREQUAL _trained)
				list(APPEND _stale "${_folder}")
			endif()
		endforeach()
	endif()
	set(GEARMULATOR_MDMM_APPLE_PGO_STALE "${_stale}" CACHE INTERNAL
		"Profiled source folders changed since the committed MD/MM profile was trained" FORCE)
	if(_stale)
		string(JSON _commit ERROR_VARIABLE _err GET "${_json}" trained commit)
		message(WARNING "MD/MM PGO: the committed profile (trained at ${_commit}) is stale for: ${_stale}. "
			"It is still used; changed functions get no profile data. Retrain before a release: "
			"scripts/macos/train_mdmm_pgo.sh (doc/release/LOCAL-GATE.md).")
	endif()
	message(STATUS "MD/MM PGO: committed profile ${_textSha} converted by ${_profdata}")
	set(${_outProfile} "${_out}" PARENT_SCOPE)
	set(${_outReason} "" PARENT_SCOPE)
endfunction()
