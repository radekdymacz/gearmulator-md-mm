#pragma once

// The editors' release version (MDMM_EDITOR_VERSION, mdmmPlugins.cmake: "0.3.4"). It is compiled into one generated
// file (mdmmVersion.cpp.in), not given to every file as a definition: a version bump rebuilds that file and the
// plug-in wrappers that report the version to hosts, not the plug-ins (doc/modern-ux/FOUNDATION.md, Build and check).
namespace mdmm
{
	const char* editorVersion();
}
