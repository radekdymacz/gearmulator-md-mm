#pragma once

#include <string>
#include <vector>

namespace mdJucePlugin
{
	// A session's page identity and self-test hookup (P6): the bundled HTML, its diagnostics log,
	// and the self-test variable/values that make the page test itself. Data only, no JUCE: the
	// session (mdDeskSession.h) declares its pageSpec() with this alone; mdWebPageHost.h aliases it
	// as WebPageHost::Spec for the page host, which does need JUCE.
	struct PageSpec
	{
		std::string page;					// the bundled page, e.g. "mdStudio.html"
		std::string log;					// the diagnostics log in the temp folder, e.g. "gearmulator-mdStudio.log"
		std::string selfTestVariable;		// GEARMULATOR_MDSTUDIO_SELFTEST
		std::vector<std::string> selfTests;	// the values (prefixes) that make the page test itself
		int designWidth = 1440;				// the page's design size: the window zooms it to fit (mdPageZoom.h)
		int designHeight = 924;
		int minHeight = 720;				// below it the page is zoomed out as a whole
	};
}
