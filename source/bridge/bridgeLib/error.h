#pragma once

#include <cstdint>

namespace bridgeLib
{
	enum class ErrorCode : uint32_t
	{
		Ok = 0,
		NoError = Ok,
		Unknown,

		WrongProtocolVersion,
		WrongPluginVersion,
		InvalidPluginDesc,

		UnexpectedCommand,

		FailedToCreateDevice,	// the last one: Error::read rejects higher values (commands.cpp)
	};
}
