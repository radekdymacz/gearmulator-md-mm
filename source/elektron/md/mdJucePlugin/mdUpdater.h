#pragma once

// The editors' update check and install (doc/modern-ux/DESIGN-updates.md): the JUCE side of mdmmUpdate's pure parts.
//
// One Updater a process (juce::SharedResourcePointer: each open window and the standalone app hold it). Its state
// lives on the message thread; the network and the files are a background thread's (never the audio thread), and
// what that thread finds comes back through MessageManager::callAsync. The network is the system's curl (macOS,
// Linux, Windows 10+): a plain GET of https://mdmm.dev/latest.json with no cookies and no query, and the download
// the user asked for from the repository's releases. A window shows what the Updater says as a non-modal banner
// (banner(), a `notice` with modal false) and hands the user's answer back (act()).

#include "updateCore.h"

#include "juce_core/juce_core.h"
#include "juce_events/juce_events.h"
#include "juce_data_structures/juce_data_structures.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mdJucePlugin::updates
{
	// The editor's config keys (DESIGN-updates.md 3.3).
	constexpr const char* g_checkKey = "updateCheck";
	constexpr const char* g_lastCheckKey = "updateLastCheck";

	enum class Phase : uint8_t
	{
		Idle,
		Checking,
		Available,		// a newer version (manifest, offer)
		UpToDate,		// a check the user asked for found nothing newer
		Downloading,
		Ready,			// downloaded and verified: what happens next is `ready`
		Failed			// `error` says why
	};

	enum class Ready : uint8_t
	{
		None,
		InstallerOpened,	// macOS: the system Installer has the .pkg
		SwapOnQuit,			// Windows / Linux: a helper replaces the app's files after it exits
		CopyByHand			// the app's folder is not writable: the unpacked folder is open
	};

	// What the user can press on a banner.
	enum class Action : uint8_t
	{
		Update, Download, Later, DontCheck, Cancel, Quit, RestartNow, WhenIQuit, Ok
	};

	struct Banner
	{
		std::string title;					// empty: no banner
		std::string text;
		std::vector<std::pair<std::string, Action>> buttons;
	};

	class Updater
	{
	public:
		Updater();
		~Updater();
		Updater(const Updater&) = delete;
		Updater& operator=(const Updater&) = delete;

		// A window's hold on the Updater's news: called on the message thread whenever the state changes.
		using Listener = std::function<void()>;
		int subscribe(Listener _listener);
		void unsubscribe(int _token);

		// The once-a-day check, when the config allows it and it is due (windows call it every minute).
		void poll(juce::PropertiesFile& _config);
		// "Check for Updates Now": now, whatever the day; says "up to date" or "could not check" too.
		void checkNow(juce::PropertiesFile& _config);
		static bool enabled(juce::PropertiesFile& _config);
		static void setEnabled(juce::PropertiesFile& _config, bool _on);

		// The banner for the current state; empty title when there is nothing to show (or it was put away).
		Banner banner() const;
		void act(Action _action, juce::PropertiesFile& _config);

		// The standalone, as it quits: start the helper that swaps the app's files (Windows, Linux), once.
		void launchPendingSwap(bool _restart);

		Phase phase() const { return m_phase; }
		static mdmmUpdate::Version currentVersion();
		static mdmmUpdate::Os thisOs();
		static mdmmUpdate::Machine thisMachine();
		static bool standalone();

	private:
	public:
		// What a verified download left behind, ready to install.
		struct Staged
		{
			juce::File package;										// macOS: the verified .pkg
			juce::File folder;										// Windows, Linux: the unpacked archive's top folder
			std::vector<std::pair<juce::File, juce::File>> copies;	// staged file -> the installed file it replaces
			juce::File app;											// the installed app, started again after "Restart now"
			juce::File plugin;										// the new VST3 (copied by hand once the DAW is closed)
		};

	private:

		void startCheck(juce::PropertiesFile& _config, bool _userAsked);
		void onChecked(std::optional<std::string> _body, std::string _error);
		void startDownload();
		void onDownloaded(Ready _ready, Staged _staged, std::string _error);
		void setPhase(Phase _p);
		juce::ThreadPool& pool();
		void changed();

		std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);	// the job in flight stops
		std::unique_ptr<juce::ThreadPool> m_pool;	// one background thread: the network and the files
		std::shared_ptr<int> m_alive = std::make_shared<int>(0);
		std::vector<std::pair<int, Listener>> m_listeners;
		int m_nextToken = 1;

		Phase m_phase = Phase::Idle;
		bool m_userAsked = false;
		bool m_hidden = false;			// "Later": no banner until the next check
		std::optional<mdmmUpdate::Manifest> m_manifest;
		mdmmUpdate::Offer m_offer = mdmmUpdate::Offer::None;
		int m_percent = 0;
		std::string m_error;
		Ready m_ready = Ready::None;
		Staged m_staged;
		bool m_swapLaunched = false;
	};
}
