#include "mdUpdater.h"

#include "mdPluginProcessor.h"

#include "juce_events/juce_events.h"

#include <algorithm>

#if JUCE_WINDOWS
#include <process.h>
#else
#include <unistd.h>
#endif

#ifndef MDMM_EDITOR_VERSION
#define MDMM_EDITOR_VERSION "0.0.0"
#endif

namespace mdJucePlugin::updates
{
	namespace mu = mdmmUpdate;

	namespace
	{
		constexpr int g_manifestSeconds = 30;
		constexpr int g_downloadSeconds = 30 * 60;

		juce::File curlProgram()
		{
#if JUCE_WINDOWS
			return juce::File::getSpecialLocation(juce::File::windowsSystemDirectory).getChildFile("curl.exe");
#elif JUCE_MAC
			return juce::File("/usr/bin/curl");
#else
			for(const char* p : {"/usr/bin/curl", "/bin/curl", "/usr/local/bin/curl"})
				if(juce::File(p).existsAsFile())
					return juce::File(p);
			return {};
#endif
		}

		// curl without the user's ~/.curlrc (-q, first), HTTPS only (also after redirects), no cookies (none
		// given), a fixed user agent, size and time limits: a plain GET of _url into _out.
		juce::StringArray curlArgs(const juce::File& _curl, const std::string& _url, const juce::File& _out, const uint64_t _maxBytes, const int _seconds)
		{
			return {_curl.getFullPathName(), "-q", "--fail", "--silent", "--show-error", "--location",
				"--proto", "=https", "--proto-redir", "=https", "--max-redirs", "5",
				"--connect-timeout", "15", "--max-time", juce::String(_seconds),
				"--max-filesize", juce::String(static_cast<juce::int64>(_maxBytes)),
				"--user-agent", "mdmm-editor", "--output", _out.getFullPathName(), juce::String(_url)};
		}

		// Runs curl and waits, polling _cancel (and calling _tick) every 100 ms. Empty: done; else why not.
		std::string runCurl(const std::string& _url, const juce::File& _out, const uint64_t _maxBytes, const int _seconds,
			const std::atomic<bool>& _cancel, const std::function<void()>& _tick)
		{
			const auto curl = curlProgram();
			if(curl == juce::File() || !curl.existsAsFile())
				return "no curl on this system";
			juce::ChildProcess p;
			if(!p.start(curlArgs(curl, _url, _out, _maxBytes, _seconds), 0))
				return "curl did not start";
			while(!p.waitForProcessToFinish(100))
			{
				if(_cancel.load())
				{
					p.kill();
					return "cancelled";
				}
				if(_tick)
					_tick();
			}
			const auto code = p.getExitCode();
			if(code == 0)
				return {};
			if(code == 22)
				return "the server answered with an error";
			if(code == 63)
				return "the file is larger than expected";
			if(code == 6 || code == 7 || code == 28)
				return "could not reach the server";
			return "curl exit code " + std::to_string(code);
		}

		juce::File workFolder()
		{
			const juce::String product = Updater::thisMachine() == mu::Machine::Md ? MDMM_PRODUCT_NAME_MD : MDMM_PRODUCT_NAME_MM;
			return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(product + " update");
		}

#if !JUCE_MAC
		// The app's folder takes a new file (a probe, removed again): then the helper can swap the app there.
		bool canWrite(const juce::File& _dir)
		{
			const auto probe = _dir.getNonexistentChildFile(".mdmm-write-probe", ".tmp", false);
			if(!probe.replaceWithText("x"))
				return false;
			probe.deleteFile();
			return true;
		}

		// Windows: the zip's <top>/<name>.exe and the text files beside it. Linux: the tarball's <top>/Standalone/*.
		// Both: <top>/[VST3/]<name>.vst3 is the plug-in, left for the user.
		std::string stageArchive(const juce::File& _archive, const juce::File& _stage, Updater::Staged& _out)
		{
			_stage.deleteRecursively();
			if(!_stage.createDirectory())
				return "could not make " + _stage.getFullPathName().toStdString();
#if JUCE_WINDOWS
			juce::ZipFile zip(_archive);
			if(zip.getNumEntries() == 0)
				return "the zip is empty or not a zip";
			for(int i = 0; i < zip.getNumEntries(); ++i)
			{
				const auto name = zip.getEntry(i)->filename.replace("\\", "/");
				if(name.startsWith("/") || name.contains("..") || name.contains(":"))
					return "the zip has an unsafe path";
			}
			if(const auto r = zip.uncompressTo(_stage); r.failed())
				return "could not unpack: " + r.getErrorMessage().toStdString();
#else
			juce::ChildProcess tar;
			if(!tar.start(juce::StringArray{"tar", "-xzf", _archive.getFullPathName(), "-C", _stage.getFullPathName()}, 0))
				return "tar did not start";
			if(!tar.waitForProcessToFinish(120000) || tar.getExitCode() != 0)
				return "could not unpack the archive";
#endif
			const auto tops = _stage.findChildFiles(juce::File::findDirectories, false);
			if(tops.size() != 1)
				return "the archive is not one folder";
			const auto top = tops[0];
			_out.folder = top;
			const auto installed = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
			const auto dir = installed.getParentDirectory();
			_out.app = installed;
#if JUCE_WINDOWS
			const auto exes = top.findChildFiles(juce::File::findFiles, false, "*.exe");
			if(exes.size() != 1)
				return "the zip does not hold one app";
			_out.copies.push_back({exes[0], installed});
			for(const char* text : {"README.txt", "LICENSE.md", "WebView2-LICENSE.txt"})
				if(top.getChildFile(text).existsAsFile())
					_out.copies.push_back({top.getChildFile(text), dir.getChildFile(text)});
			const auto vst3 = top.findChildFiles(juce::File::findDirectories, false, "*.vst3");
#else
			const auto standaloneDir = top.getChildFile("Standalone");
			juce::Array<juce::File> exes;
			for(const auto& f : standaloneDir.findChildFiles(juce::File::findFiles, false))
			{
				if(f.getFileExtension() == ".so")
					_out.copies.push_back({f, dir.getChildFile(f.getFileName())});
				else
					exes.add(f);
			}
			if(exes.size() != 1)
				return "the archive does not hold one app";
			_out.copies.push_back({exes[0], installed});
			const auto vst3 = top.getChildFile("VST3").findChildFiles(juce::File::findDirectories, false, "*.vst3");
#endif
			if(!vst3.isEmpty())
				_out.plugin = vst3[0];
			return {};
		}
#endif
	}

	Updater::Updater() : m_pool(std::make_unique<juce::ThreadPool>(1))
	{
	}

	Updater::~Updater()
	{
		// a job in flight sees cancel within 100 ms (runCurl kills curl); its results are dropped (m_alive)
		m_alive.reset();
		*m_cancel = true;
		m_pool->removeAllJobs(true, 10000);
		m_pool.reset();
	}

	mu::Version Updater::currentVersion()
	{
		return mu::parseVersion(MDMM_EDITOR_VERSION).value_or(mu::Version{});
	}

	mu::Os Updater::thisOs()
	{
#if JUCE_WINDOWS
		return mu::Os::Win;
#elif JUCE_MAC
		return mu::Os::Mac;
#else
		return mu::Os::Linux;
#endif
	}

	mu::Machine Updater::thisMachine()
	{
		return AudioPluginAudioProcessor::getCompiledProductModel() == md::MachineModel::Monomachine ? mu::Machine::Mm : mu::Machine::Md;
	}

	bool Updater::standalone()
	{
		return juce::JUCEApplicationBase::isStandaloneApp();
	}

	int Updater::subscribe(Listener _listener)
	{
		const int token = m_nextToken++;
		m_listeners.emplace_back(token, std::move(_listener));
		return token;
	}

	void Updater::unsubscribe(const int _token)
	{
		m_listeners.erase(std::remove_if(m_listeners.begin(), m_listeners.end(), [_token](const auto& _l) { return _l.first == _token; }),
			m_listeners.end());
	}

	void Updater::changed()
	{
		const auto listeners = m_listeners;
		for(const auto& [token, listener] : listeners)
			if(listener)
				listener();
	}

	void Updater::setPhase(const Phase _p)
	{
		m_phase = _p;
		m_hidden = false;
		changed();
	}

	bool Updater::enabled(juce::PropertiesFile& _config)
	{
		return _config.getBoolValue(g_checkKey, true);
	}

	void Updater::setEnabled(juce::PropertiesFile& _config, const bool _on)
	{
		_config.setValue(g_checkKey, _on);
		_config.saveIfNeeded();
	}

	void Updater::poll(juce::PropertiesFile& _config)
	{
		if(m_phase == Phase::Checking || m_phase == Phase::Downloading || m_phase == Phase::Ready)
			return;
		const auto now = juce::Time::currentTimeMillis() / 1000;
		const auto last = static_cast<int64_t>(_config.getValue(g_lastCheckKey, "0").getLargeIntValue());
		if(mu::checkDue(now, last, enabled(_config)))
			startCheck(_config, false);
	}

	void Updater::checkNow(juce::PropertiesFile& _config)
	{
		if(m_phase == Phase::Checking || m_phase == Phase::Downloading || m_phase == Phase::Ready)
		{
			m_hidden = false;
			changed();
			return;
		}
		startCheck(_config, true);
	}

	void Updater::startCheck(juce::PropertiesFile& _config, const bool _userAsked)
	{
		_config.setValue(g_lastCheckKey, juce::String(juce::Time::currentTimeMillis() / 1000));
		_config.saveIfNeeded();
		m_userAsked = _userAsked;
		setPhase(Phase::Checking);
		*m_cancel = false;
		m_pool->addJob([cancel = m_cancel, alive = std::weak_ptr<int>(m_alive), this]
		{
			const juce::TemporaryFile temp(".json");
			auto error = runCurl(mu::g_manifestUrl, temp.getFile(), mu::g_maxManifestBytes, g_manifestSeconds, *cancel, {});
			std::optional<std::string> body;
			if(error.empty())
				body = temp.getFile().loadFileAsString().toStdString();
			juce::MessageManager::callAsync([alive, this, body = std::move(body), error = std::move(error)]() mutable
			{
				if(!alive.expired())
					onChecked(std::move(body), std::move(error));
			});
		});
	}

	void Updater::onChecked(std::optional<std::string> _body, std::string _error)
	{
		std::optional<mu::Manifest> manifest;
		std::string why = "Could not reach mdmm.dev: " + _error + ".";
		if(_body)
		{
			auto parsed = mu::parseManifest(*_body);
			manifest = std::move(parsed.manifest);
			if(!manifest)
				why = "mdmm.dev answered with something unexpected (" + parsed.error + ").";
		}
		if(!manifest)
		{
			// A daily check that fails says nothing; one the user asked for says why.
			m_manifest.reset();
			m_offer = mu::Offer::None;
			m_error = why;
			setPhase(m_userAsked ? Phase::Failed : Phase::Idle);
			return;
		}
		m_manifest = std::move(manifest);
		m_offer = mu::offerFor(*m_manifest, currentVersion(), thisOs(), thisMachine(), standalone(), mu::buildKey().has_value());
		if(m_offer == mu::Offer::None)
		{
			setPhase(m_userAsked ? Phase::UpToDate : Phase::Idle);
			return;
		}
		setPhase(Phase::Available);
	}

	void Updater::startDownload()
	{
		if(!m_manifest || m_offer != mu::Offer::Install || m_phase == Phase::Downloading)
			return;
		const auto* asset = m_manifest->find(thisOs(), thisMachine());
		if(!asset)
			return;
		m_percent = 0;
		setPhase(Phase::Downloading);
		*m_cancel = false;
		m_pool->addJob([cancel = m_cancel, alive = std::weak_ptr<int>(m_alive), this, asset = *asset, version = m_manifest->version]
		{
			const auto post = [alive, this](Ready _ready, Staged _staged, std::string _error)
			{
				juce::MessageManager::callAsync([alive, this, _ready, staged = std::move(_staged), error = std::move(_error)]() mutable
				{
					if(!alive.expired())
						onDownloaded(_ready, std::move(staged), std::move(error));
				});
			};
			const auto base = workFolder();
			const auto downloads = base.getChildFile("download");
			downloads.deleteRecursively();
			if(!downloads.createDirectory())
				return post(Ready::None, {}, "could not make " + downloads.getFullPathName().toStdString());
			const auto file = downloads.getChildFile(asset.name);

			int lastPercent = -1;
			const auto tick = [&]
			{
				const int percent = static_cast<int>(std::min<juce::int64>(100, file.getSize() * 100 / static_cast<juce::int64>(asset.size)));
				if(percent == lastPercent)
					return;
				lastPercent = percent;
				juce::MessageManager::callAsync([alive, this, percent]
				{
					if(alive.expired() || m_phase != Phase::Downloading)
						return;
					m_percent = percent;
					changed();
				});
			};
			auto error = runCurl(asset.url, file, asset.size, g_downloadSeconds, *cancel, tick);
			if(!error.empty())
			{
				file.deleteFile();
				return post(Ready::None, {}, error == "cancelled" ? error : "The download failed: " + error + ".");
			}

			// The file against latest.json: size, SHA-256 and the signature with this build's key. Nothing is
			// unpacked or opened before that holds.
			mu::Sha256 sha;
			{
				juce::FileInputStream in(file);
				if(!in.openedOk())
					return post(Ready::None, {}, "Could not read the download.");
				juce::HeapBlock<char> buffer(1 << 20);
				for(;;)
				{
					const auto n = in.read(buffer.get(), 1 << 20);
					if(n <= 0)
						break;
					sha.update(buffer.get(), static_cast<size_t>(n));
					if(cancel->load())
						return post(Ready::None, {}, "cancelled");
				}
			}
			const auto verdict = mu::verifyDownload(asset, version, static_cast<uint64_t>(file.getSize()), sha.finish(), mu::buildKey());
			if(verdict != mu::Verdict::Ok)
			{
				file.deleteFile();
				return post(Ready::None, {}, std::string("The download did not verify (") + mu::verdictText(verdict) + "): nothing was installed.");
			}

			Staged staged;
#if JUCE_MAC
			staged.package = file;
			return post(Ready::InstallerOpened, std::move(staged), {});
#else
			if(auto why = stageArchive(file, base.getChildFile("staged"), staged); !why.empty())
				return post(Ready::None, {}, "The update could not be unpacked (" + why + "): nothing was installed.");
			const bool writable = canWrite(staged.app.getParentDirectory());
			return post(writable ? Ready::SwapOnQuit : Ready::CopyByHand, std::move(staged), {});
#endif
		});
	}

	void Updater::onDownloaded(const Ready _ready, Staged _staged, std::string _error)
	{
		if(_error == "cancelled")
		{
			setPhase(Phase::Available);
			m_hidden = true;
			changed();
			return;
		}
		if(!_error.empty())
		{
			m_error = std::move(_error);
			setPhase(Phase::Failed);
			return;
		}
		m_ready = _ready;
		m_staged = std::move(_staged);
		if(m_ready == Ready::InstallerOpened)
			m_staged.package.startAsProcess();	// the system Installer: the user clicks Install
		else if(m_ready == Ready::CopyByHand)
			m_staged.folder.startAsProcess();	// the file manager on the unpacked folder
		setPhase(Phase::Ready);
	}

	Banner Updater::banner() const
	{
		Banner b;
		if(m_hidden)
			return b;
		const auto mine = mu::toString(currentVersion());
		const std::string next = m_manifest ? mu::toString(m_manifest->version) : std::string();
		switch(m_phase)
		{
		case Phase::Idle:
		case Phase::Checking:
			break;
		case Phase::UpToDate:
			b.title = "No update";
			b.text = "You have the latest version (" + mine + ").";
			b.buttons = {{"OK", Action::Ok}};
			break;
		case Phase::Available:
			b.title = "Update available: " + next;
			if(m_offer == mu::Offer::Install)
			{
				b.text = "You have " + mine + ". The new version is checked against our signature before it installs.";
				b.buttons = {{"Update", Action::Update}, {"Later", Action::Later}, {"Don't check", Action::DontCheck}};
			}
			else
			{
				b.text = "You have " + mine + ". " + (standalone()
					? (mu::buildKey() ? std::string("Get it from mdmm.dev.") : std::string("This build cannot install updates itself: get it from mdmm.dev."))
					: std::string("Get it from mdmm.dev, or update from the standalone app."));
				b.buttons = {{"Download", Action::Download}, {"Later", Action::Later}, {"Don't check", Action::DontCheck}};
			}
			break;
		case Phase::Downloading:
			b.title = "Downloading " + next + "\xE2\x80\xA6 " + std::to_string(m_percent) + " %";
			b.text = "It is checked against our signature before anything is installed.";
			b.buttons = {{"Cancel", Action::Cancel}};
			break;
		case Phase::Failed:
			b.title = m_manifest ? "The update did not install" : "Could not check for updates";
			b.text = m_error;
			if(m_manifest)
				b.buttons = {{"Download", Action::Download}, {"OK", Action::Ok}};
			else
				b.buttons = {{"OK", Action::Ok}};
			break;
		case Phase::Ready:
		{
			const std::string plugin = m_staged.plugin == juce::File() ? std::string()
				: " The plug-in is not replaced while a DAW may have it loaded: close your DAW, then copy " + m_staged.plugin.getFileName().toStdString()
					+ " from " + m_staged.plugin.getParentDirectory().getFullPathName().toStdString() + " to your VST3 folder.";
			if(m_ready == Ready::InstallerOpened)
			{
				b.title = "The installer is open";
				b.text = "Quit the editor, then click Install in the installer. It updates the app and the plug-ins: close your DAW first if it uses them.";
				b.buttons = {{"Quit", Action::Quit}, {"OK", Action::Ok}};
			}
			else if(m_ready == Ready::SwapOnQuit)
			{
				b.title = "Update ready: " + next;
				b.text = "It replaces the app when you quit." + plugin;
				b.buttons = {{"Restart now", Action::RestartNow}, {"When I quit", Action::WhenIQuit}};
			}
			else
			{
				b.title = "Update ready: " + next;
				b.text = "The editor cannot write to its own folder (" + m_staged.app.getParentDirectory().getFullPathName().toStdString()
					+ "). Quit it, then copy the files from the folder that just opened (" + m_staged.folder.getFullPathName().toStdString()
					+ ") over it." + plugin;
				b.buttons = {{"OK", Action::Ok}};
			}
			break;
		}
		}
		return b;
	}

	void Updater::act(const Action _action, juce::PropertiesFile& _config)
	{
		switch(_action)
		{
		case Action::Update:
			startDownload();
			return;
		case Action::Download:
			juce::URL(mu::g_downloadPage).launchInDefaultBrowser();
			break;
		case Action::DontCheck:
			setEnabled(_config, false);
			break;
		case Action::Cancel:
			*m_cancel = true;	// the job ends within 100 ms and says "cancelled" (onDownloaded)
			break;
		case Action::Quit:
			if(auto* app = juce::JUCEApplicationBase::getInstance())
				app->systemRequestedQuit();
			break;
		case Action::RestartNow:
			launchPendingSwap(true);
			if(auto* app = juce::JUCEApplicationBase::getInstance())
				app->systemRequestedQuit();
			break;
		case Action::Later:
		case Action::WhenIQuit:
		case Action::Ok:
			break;
		}
		if(m_phase == Phase::UpToDate || (m_phase == Phase::Failed && !m_manifest))
			m_phase = Phase::Idle;
		m_hidden = true;
		changed();
	}

	void Updater::launchPendingSwap(const bool _restart)
	{
		if(m_phase != Phase::Ready || m_ready != Ready::SwapOnQuit || m_swapLaunched || m_staged.copies.empty())
			return;
		m_swapLaunched = true;
		mu::SwapPlan plan;
#if JUCE_WINDOWS
		plan.pid = static_cast<int64_t>(_getpid());
#else
		plan.pid = static_cast<int64_t>(getpid());
#endif
		for(const auto& [from, to] : m_staged.copies)
			plan.copies.emplace_back(from.getFullPathName().toStdString(), to.getFullPathName().toStdString());
		plan.app = m_staged.app.getFullPathName().toStdString();
		plan.restart = _restart;
		const auto base = m_staged.folder.getParentDirectory().getParentDirectory();
		plan.log = base.getChildFile("swap.log").getFullPathName().toStdString();
#if JUCE_WINDOWS
		const auto script = base.getChildFile("swap.ps1");
		// UTF-16 with a byte-order mark: Windows PowerShell 5 reads any path in it as written.
		if(!script.replaceWithText(juce::String::fromUTF8(mu::swapScriptPowerShell(plan).c_str()), true, true, nullptr))
			return;
		const auto powershell = juce::File::getSpecialLocation(juce::File::windowsSystemDirectory)
			.getChildFile("WindowsPowerShell").getChildFile("v1.0").getChildFile("powershell.exe");
		juce::ChildProcess p;
		p.start(juce::StringArray{powershell.getFullPathName(), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
			"-WindowStyle", "Hidden", "-File", script.getFullPathName()}, 0);
#else
		const auto script = base.getChildFile("swap.sh");
		if(!script.replaceWithText(juce::String::fromUTF8(mu::swapScriptSh(plan).c_str()), false, false, "\n"))
			return;
		// detached: the helper outlives the app (nohup, its own background job)
		juce::ChildProcess p;
		if(p.start(juce::StringArray{"/bin/sh", "-c", "nohup /bin/sh \"$0\" >/dev/null 2>&1 &", script.getFullPathName()}, 0))
			p.waitForProcessToFinish(5000);
#endif
	}
}
