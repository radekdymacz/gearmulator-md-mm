#pragma once

#include "mdGlobal.h"
#include "mmGlobal.h"

#include <cstdint>

namespace elektronData
{
	// B-051, F3: the globals each machine ships with, as its firmware writes them on a fresh start (the MD: eight the same; the MM: its own per slot): GLOBAL ›
	// Reset to defaults writes it. Measured, not typed: mdDeskFirmwareTest factoryglobal and mmDeskFirmwareTest
	// factoryglobal read a fresh machine's eight slots and fail when they differ from these (FACTORY_PRINT=1 prints
	// the tables below again). _slot becomes the document's position.
	MdGlobal mdFactoryGlobal(uint8_t _slot);
	MmGlobal mmFactoryGlobal(uint8_t _slot);
}
