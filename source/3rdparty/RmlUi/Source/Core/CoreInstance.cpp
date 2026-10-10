#include "../../Include/RmlUi/Core/CoreInstance.h"

namespace Rml
{
	CoreInstance::CoreInstance() : factory(new Factory(*this))
	{
	}

	CoreInstance::~CoreInstance()
	{
		delete factory;
	}
}
