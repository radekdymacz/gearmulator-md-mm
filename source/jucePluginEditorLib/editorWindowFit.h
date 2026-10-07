#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "editorTraits.h"
#include "pluginEditor.h"
#include "pluginEditorState.h"
#include "windowFit.h"

#include "dsp56kBase/logging.h"

namespace jucePluginEditorLib
{
	// This fork's sizing for EditorWindow (doc/modern-ux/UPSTREAM.md). A FreeSizeEditor's window
	// resizes freely, down to 60 % of its design size, and remembers its width and height; the
	// standalone's window is kept no larger than its screen's visible area, and inside it (P7).
	// EditorWindow holds one and calls it from its own sizing code; with any other editor it changes
	// nothing but the screen fit.
	class EditorWindowFit
	{
	public:
		static bool freeSize(const PluginEditorState& _state)
		{
			return dynamic_cast<const FreeSizeEditor*>(_state.getEditor()) != nullptr;
		}

		// After the constrainer was set for a skin drawn to a fixed size.
		static void constrain(const PluginEditorState& _state, juce::ComponentBoundsConstrainer& _constrainer)
		{
			if(!freeSize(_state))
				return;
			_constrainer.setMinimumSize(_state.getWidth() * 6 / 10, _state.getHeight() * 6 / 10);
			_constrainer.setFixedAspectRatio(0.0);
		}

		// A free editor's own width and height, when it has them; false: the caller applies the GUI scale.
		static bool restoreSize(juce::Component& _window, const PluginEditorState& _state, const juce::PropertiesFile& _config)
		{
			const int w = _config.getIntValue("windowWidth", 0);
			const int h = _config.getIntValue("windowHeight", 0);
			if(!freeSize(_state) || w <= 0 || h <= 0)
				return false;
			_window.setSize(w, h);
			return true;
		}

		// A size the user gave the window (not one fitToScreen made).
		static void persistSize(const PluginEditorState& _state, juce::PropertiesFile& _config, const int _width, const int _height)
		{
			if(!freeSize(_state))
				return;
			_config.setValue("windowWidth", _width);
			_config.setValue("windowHeight", _height);
		}

		// True while fitToScreen resizes the window: that size is not the user's.
		bool isFitting() const { return m_fitting; }

		// The standalone's window no larger than its screen's visible area (menu bar and Dock
		// excluded), and inside it. A plug-in's editor (a free one) no larger than its screen either
		// (fitPluginSize): the host places the window, the editor picks a size that fits.
		void fitToScreen(juce::Component& _window, const PluginEditorState& _state,
			const juce::ComponentBoundsConstrainer& _constrainer, const bool _embedded)
		{
			if(_embedded)
				return;
			if(!juce::JUCEApplicationBase::isStandaloneApp())
			{
				m_plugin = {&_window, &_state, &_constrainer, 0};
				fitPlugin();
				return;
			}
			auto* top = _window.getTopLevelComponent();
			if(!top || top == &_window || !top->isOnDesktop())
				return;
			const auto bounds = top->getScreenBounds();
			const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(bounds);
			if(!display)
				return;
			windowFit::Frame frame;
			if(auto* peer = top->getPeer())
			{
				const auto b = peer->getFrameSize();
				frame = {b.getTop(), b.getLeft(), b.getBottom(), b.getRight()};
			}
			// What the window holds around the editor (its own bars), kept as it is.
			const int extraW = bounds.getWidth() - _window.getWidth(), extraH = bounds.getHeight() - _window.getHeight();
			const auto u = display->userArea;
			const windowFit::Rect content{bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight()};
			const auto r = windowFit::fit(content, frame, {u.getX(), u.getY(), u.getWidth(), u.getHeight()},
				_constrainer.getMinimumWidth() + extraW, _constrainer.getMinimumHeight() + extraH, !freeSize(_state));
			if(r == content)
				return;
			LOG("Window " << content.w << "x" << content.h << " at " << content.x << "," << content.y << " fitted to the screen: "
				<< r.w << "x" << r.h << " at " << r.x << "," << r.y);
			if(r.w != content.w || r.h != content.h)
			{
				// Not the user's size: the next screen may have room for it again.
				m_fitting = true;
				_window.setSize(r.w - extraW, r.h - extraH);
				m_fitting = false;
			}
			top->setTopLeftPosition(r.x, r.y);
		}

	private:
		// The host shows the editor some time after it was made (an AU's view is put in its window when the
		// host gets to it): until the editor is on a screen, it looks again a few times a second, for a while.
		struct Retry final : juce::Timer
		{
			std::function<void()> tick;
			void timerCallback() override { tick(); }
		};

		void fitPlugin()
		{
			auto* window = m_plugin.window;
			if(!window || !freeSize(*m_plugin.state))
				return;
			if(!window->isShowing() || !window->getPeer())
			{
				if(++m_plugin.tries > 50)
				{
					m_retry.stopTimer();
					return;
				}
				m_retry.tick = [this] { fitPlugin(); };
				if(!m_retry.isTimerRunning())
					m_retry.startTimer(200);
				return;
			}
			m_retry.stopTimer();
			const auto bounds = window->getScreenBounds();
			const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(bounds);
			if(!display)
				return;
			const auto u = display->userArea;
			const windowFit::Rect content{bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight()};
			const auto r = windowFit::fitPluginSize(content, {u.getX(), u.getY(), u.getWidth(), u.getHeight()},
				m_plugin.constrainer->getMinimumWidth(), m_plugin.constrainer->getMinimumHeight(), false);
			if(r.w == content.w && r.h == content.h)
				return;
			LOG("Plug-in editor " << content.w << "x" << content.h << " fitted to its screen (" << u.getWidth() << "x" << u.getHeight()
				<< " visible): " << r.w << "x" << r.h);
			// Not the user's size: a larger screen may have room for it again.
			m_fitting = true;
			window->setSize(r.w, r.h);
			m_fitting = false;
		}

		struct Plugin
		{
			juce::Component* window = nullptr;
			const PluginEditorState* state = nullptr;
			const juce::ComponentBoundsConstrainer* constrainer = nullptr;
			int tries = 0;
		};

		bool m_fitting = false;
		Plugin m_plugin;
		Retry m_retry;
	};
}
