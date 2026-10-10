#pragma once

#include "baseLib/binarystream.h"

namespace pluginLib
{
	using PluginStream = baseLib::BinaryStream;

	enum ParameterLinkType : uint32_t
	{
		None = 0,
		Source = 1,
		Target = 2
	};

	using ParamValue = int32_t;

	enum class DeviceType
	{
		// note: these values are stored in the plugin state, do not change them!
		Local = 0,
		// 1 was Remote (upstream's DSP bridge, deleted in 0.5); an old project's REMO chunk is ignored
		Dummy = 2
	};
}
