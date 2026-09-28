#include "pluginEditorWindow.h"

#include "pluginEditor.h"
#include "pluginEditorState.h"
#include "windowFit.h"

#include "dsp56kBase/logging.h"

#include "juceRmlPlugin/rmlParameterBinding.h"

#include "juceRmlUi/juceRmlComponent.h"

#include "RmlUi/Core/Elements/ElementFormControlInput.h"

namespace jucePluginEditorLib
{

//==============================================================================
EditorWindow::EditorWindow(juce::AudioProcessor& _p, PluginEditorState& _s, juce::PropertiesFile& _config)
	: AudioProcessorEditor(&_p), m_state(_s), m_config(_config)
	, m_skinLoadedListener(m_state.evSkinLoaded, [this](juce::Component* _component)
		{
			setUiRoot(_component);
		})
	, m_guiScaleListener(m_state.evSetGuiScale, [this](const int _scale)
		{
			if(getNumChildComponents() > 0)
				setGuiScale(static_cast<float>(_scale));
		})
{
	addMouseListener(this, true);

	setUiRoot(m_state.getUiRoot());
}

EditorWindow::~EditorWindow()
{
	setUiRoot(nullptr);
}

void EditorWindow::resized()
{
	AudioProcessorEditor::resized();

	if(!m_state.getWidth() || !m_state.getHeight())
		return;

	const auto w = getWidth();
	const auto h = getHeight();

	const auto scaleX = static_cast<float>(w) / static_cast<float>(m_state.getWidth());
	const auto scaleY = static_cast<float>(h) / static_cast<float>(m_state.getHeight());

	const auto scale = std::min(scaleX, scaleY);

	if (!m_state.resizeEditor(w,h))
		return;

	if(m_scaleRestore.shouldPersistResize() && !m_fitting)
	{
		const auto percent = 100.f * scale / m_state.getRootScale();
		m_config.setValue("scale", percent);
		if(fluid())
		{
			m_config.setValue("windowWidth", w);
			m_config.setValue("windowHeight", h);
		}
		m_config.saveIfNeeded();
	}

	// Prettymuch unbelievable Juce VST3 bug, but our root component is a child of the VST3 editor component
	// and that one is not resized! The host window is, the first child (our editor component) is, but the
	// root component is not! This is no drama as long as you do not have a juce OpenGL context, because
	// that one uses the "top level component" to set the clipping rectangle! W T F
	startTimer(m_scaleRestore.delayedRestorePending() ? 50 : 1);
}

int EditorWindow::getControlParameterIndex(Component& _component)
{
	// This code relies on the fact that getComponentAt() is called with a XY position
	// first and then the parameter is queried for that returned component afterwards.
	// As we do not have Juce components, we remember the last Rml element that was
	// under the mouse and query the parameter binding for that element here.
	// It would be better if there was a function like "getParameterForPosition" but unfortunately
	// Juce does not provide that.
	if (const auto* editor = m_state.getEditor())
	{
		if (const auto* comp = editor->getRmlComponent())
		{
			if (const auto* binding = editor->getRmlParameterBinding())
			{
				if (const auto* elem = comp->getLastElementByGetComponentAt())
				{
					if (const auto* param = binding->getParameterForElement(elem))
						return param->getParameterIndex();

					if (const auto* parent = elem->GetParentNode())
					{
						if (dynamic_cast<const Rml::ElementFormControlInput*>(parent))
						{
							if (const auto* param = binding->getParameterForElement(parent))
								return param->getParameterIndex();
						}
					}
				}
			}
		}
	}

	return AudioProcessorEditor::getControlParameterIndex(_component);
}

void EditorWindow::setEmbedded(const bool _embedded)
{
	m_scaleRestore.setEmbedded(_embedded);
	if(m_scaleRestore.isEmbedded())
	{
		stopTimer();
		setResizable(false, false);
		setConstrainer(nullptr);
	}
}

void EditorWindow::setGuiScale(const float _percent)
{
	if(!m_state.getWidth() || !m_state.getHeight())
		return;

	const auto s = _percent / 100.0f * m_state.getRootScale();

	const auto w = static_cast<int>(static_cast<float>(m_state.getWidth()) * s);
	const auto h = static_cast<int>(static_cast<float>(m_state.getHeight()) * s);

	setSize(w, h);

	m_config.setValue("scale", _percent);
	m_config.saveIfNeeded();
}

bool EditorWindow::fluid() const
{
	const auto* e = m_state.getEditor();
	return e && !e->keepsAspectRatio();
}

void EditorWindow::restoreSize(const float _percent)
{
	const int w = m_config.getIntValue("windowWidth", 0), h = m_config.getIntValue("windowHeight", 0);
	if(fluid() && w > 0 && h > 0)
		setSize(w, h);
	else
		setGuiScale(_percent);
}

void EditorWindow::setUiRoot(juce::Component* _component)
{
	removeAllChildren();
	setConstrainer(nullptr);

	if(!_component)
		return;

	if(!m_state.getWidth() || !m_state.getHeight())
		return;

	// A free editor (its page lays itself out) resizes in both directions, down to 60 % of its
	// design size; a skin drawn to a fixed size keeps its aspect ratio.
	if(fluid())
	{
		m_sizeConstrainer.setMinimumSize(m_state.getWidth() * 6 / 10, m_state.getHeight() * 6 / 10);
		m_sizeConstrainer.setFixedAspectRatio(0.0);
	}
	else
	{
		m_sizeConstrainer.setMinimumSize(m_state.getWidth() / 10, m_state.getHeight() / 10);
		m_sizeConstrainer.setFixedAspectRatio(static_cast<double>(m_state.getWidth()) / static_cast<double>(m_state.getHeight()));
	}
	m_sizeConstrainer.setMaximumSize(m_state.getWidth() * 4, m_state.getHeight() * 4);

	const auto configuredScale = static_cast<float>(m_config.getDoubleValue("scale", 100));
	const auto attachAction = m_scaleRestore.attachRoot(
		juce::JUCEApplicationBase::isStandaloneApp(), configuredScale);
	if(attachAction.applyConfiguredScale)
		restoreSize(configuredScale);

	_component->setSize(getWidth(), getHeight());

	addAndMakeVisible(_component);

	if(m_scaleRestore.isEmbedded())
	{
		setResizable(false, false);
		setConstrainer(nullptr);
		stopTimer();
	}
	else
	{
		setResizable(true, true);
		setConstrainer(&m_sizeConstrainer);
		startTimer(m_scaleRestore.delayedRestorePending() ? 50 : 1);
	}
}

void EditorWindow::timerCallback()
{
	if(m_scaleRestore.isEmbedded())
	{
		stopTimer();
		return;
	}

	float restoreScale = 100.0f;
	if(m_scaleRestore.consumeDelayedRestore(restoreScale))
	{
		// A standalone host can impose its small placeholder size after the editor
		// has loaded. Reapply the configured size once that native parent exists,
		// and do not persist the placeholder resizes as the user's GUI scale.
		restoreSize(restoreScale);
	}

	fixParentWindowSize();
	fitToScreen();
	stopTimer();
}

void EditorWindow::fitToScreen()
{
	if(!juce::JUCEApplicationBase::isStandaloneApp() || m_scaleRestore.isEmbedded())
		return;
	auto* top = getTopLevelComponent();
	if(!top || top == this || !top->isOnDesktop())
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
	const int extraW = bounds.getWidth() - getWidth(), extraH = bounds.getHeight() - getHeight();
	const auto u = display->userArea;
	const windowFit::Rect content{bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight()};
	const auto r = windowFit::fit(content, frame, {u.getX(), u.getY(), u.getWidth(), u.getHeight()},
		m_sizeConstrainer.getMinimumWidth() + extraW, m_sizeConstrainer.getMinimumHeight() + extraH, !fluid());
	if(r == content)
		return;
	LOG("Window " << content.w << "x" << content.h << " at " << content.x << "," << content.y << " fitted to the screen: "
		<< r.w << "x" << r.h << " at " << r.x << "," << r.y);
	if(r.w != content.w || r.h != content.h)
	{
		// Not the user's size: the next screen may have room for it again.
		m_fitting = true;
		setSize(r.w - extraW, r.h - extraH);
		m_fitting = false;
	}
	top->setTopLeftPosition(r.x, r.y);
}

void EditorWindow::fixParentWindowSize() const
{
	const auto w = getWidth();
	const auto h = getHeight();

	auto* parent = getParentComponent();

	while (parent)
	{
		if (parent->getWidth() < w || parent->getHeight() < h)
		{
			LOG("Parent " << parent->getName() << " has wrong size: " << parent->getName() <<
				", expected: " << w << "x" << h <<
				", actual: " << parent->getWidth() << "x" << parent->getHeight());
			parent->setSize(w, h);
		}

		parent = parent->getParentComponent();
	}
}
}
