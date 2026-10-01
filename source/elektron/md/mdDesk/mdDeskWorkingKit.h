#pragma once

#include "elektronData/mdKit.h"

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace mdDesk
{
	// Every field that differs between _before and _after has _after's value in _image (and, on a track
	// with a new machine, every synthesis value: the machine was assigned with its defaults).
	bool reflects(const elektronData::MdKit& _image, const elektronData::MdKit& _before, const elektronData::MdKit& _after);
}
