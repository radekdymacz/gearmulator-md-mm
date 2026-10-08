#pragma once

// 0.3.4: which editor this is, for the editor menu's first line and the standalone's About box. The names and the
// maker come from scripts/mdmm-product.env, the version from mdmm::editorVersion() (mdmmVersion.h); the page shows
// the same version (skins/shared/deskAbout.js, opened with ?version= by mdWebPageHost.cpp).

#include "mdmmVersion.h"

#include <string>

#ifndef MDMM_PRODUCT_NAME_MD
#define MDMM_PRODUCT_NAME_MD "Machinedrum Editor"
#endif
#ifndef MDMM_PRODUCT_NAME_MM
#define MDMM_PRODUCT_NAME_MM "Monomachine Editor"
#endif
#ifndef MDMM_VENDOR
#define MDMM_VENDOR "Future Native Audio"
#endif
#ifndef MDMM_WEBSITE
#define MDMM_WEBSITE "https://mdmm.dev"
#endif

namespace mdJucePlugin::about
{
	inline const char* product(const bool _monomachine) { return _monomachine ? MDMM_PRODUCT_NAME_MM : MDMM_PRODUCT_NAME_MD; }

	// "Machinedrum Editor 0.3.4"
	inline std::string title(const bool _monomachine) { return std::string(product(_monomachine)) + " " + mdmm::editorVersion(); }

	// The About box's text, under its title.
	inline std::string text()
	{
		return std::string("Version ") + mdmm::editorVersion() + "\n" + MDMM_VENDOR + " \xc2\xb7 " + MDMM_WEBSITE + "\n\n"
			"Free software under the GNU General Public License, version 3.\n\n"
			"Built on gearmulator-md-mm by joelanders (the Machinedrum and Monomachine emulation) and Gearmulator by "
			"The Usual Suspects (dsp56300) and its contributors. Thanks to Elektron for the instruments.";
	}
}
