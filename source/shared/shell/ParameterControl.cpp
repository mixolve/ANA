#include "ParameterControl.h"
#include "Theme.h"

#include <algorithm>
#include <utility>

ParameterControl::ParameterControl(juce::AudioProcessorValueTreeState& state,
                                   const juce::String& parameterId,
                                   juce::String title,
                                   Formatter formatter)
    : titleText(std::move(title)), valueFormatter(std::move(formatter)), compact(titleText.isEmpty())
{
    pressGesture.onArmed = [this]
    {
        if (onResetRequested)
            onResetRequested(*this);

        // R? is the armed state; cancel the gesture so mouse-up cannot perform a second action.
        pressGesture.cancel();
        repaint();
    };
    parameter = state.getParameter(parameterId);
    choiceParameter = dynamic_cast<juce::AudioParameterChoice*>(parameter);
    boolParameter = dynamic_cast<juce::AudioParameterBool*>(parameter);
    slider.onValueChange = [this]
    {
        repaint();

        if (onValueChanged)
            onValueChanged();
    };
    valueEditor.setFont(ana::ui::makeFont());
    valueEditor.setJustification(juce::Justification::centred);
    valueEditor.setPopupMenuEnabled(false);
    valueEditor.setSelectAllWhenFocused(true);
    valueEditor.setColour(juce::TextEditor::textColourId, ana::ui::white);
    valueEditor.setColour(juce::TextEditor::backgroundColourId, ana::ui::dark);
    valueEditor.setColour(juce::TextEditor::outlineColourId, ana::ui::light);
    valueEditor.setColour(juce::TextEditor::focusedOutlineColourId, ana::ui::light);
    valueEditor.setColour(juce::TextEditor::highlightColourId, ana::ui::black);
    valueEditor.setColour(juce::TextEditor::highlightedTextColourId, ana::ui::white);
    valueEditor.onReturnKey = [this] { hideValueEditor(false); };
    valueEditor.onEscapeKey = [this] { hideValueEditor(true); };
    valueEditor.onFocusLost = [this]
    {
        if (valueEditorActive)
            hideValueEditor(false);
    };
    addChildComponent(valueEditor);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, parameterId, slider);
    // SliderAttachment installs the parameter's default text formatter, so the UI-specific
    // formatter must be applied afterwards.
    slider.textFromValueFunction = [this] (const double value)
    {
        return valueFormatter != nullptr ? valueFormatter(value) : juce::String(value);
    };
}

ParameterControl::~ParameterControl()
{
    pressGesture.cancel();
}

void ParameterControl::setInteractionEnabled(const bool shouldEnable,
                                             const bool showValueWhenDisabled)
{
    if (interactionEnabled == shouldEnable
        && displayValueWhenDisabled == showValueWhenDisabled)
        return;

    interactionEnabled = shouldEnable;
    displayValueWhenDisabled = showValueWhenDisabled;

    if (! interactionEnabled)
    {
        selected = false;
        hideValueEditor(true);
    }

    repaint();
}

juce::StringArray ParameterControl::getChoiceNames() const
{
    return choiceParameter != nullptr ? choiceParameter->choices : juce::StringArray();
}

int ParameterControl::getSelectedChoiceIndex() const noexcept
{
    return choiceParameter != nullptr ? juce::roundToInt(slider.getValue()) : -1;
}

void ParameterControl::setSelectedChoiceIndex(const int choiceIndex)
{
    if (choiceParameter == nullptr || ! juce::isPositiveAndBelow(choiceIndex, choiceParameter->choices.size()))
        return;

    slider.setValue(choiceIndex, juce::sendNotificationSync);
}

void ParameterControl::setSelected(const bool shouldSelect)
{
    const auto nextSelected = shouldSelect && interactionEnabled;

    if (selected == nextSelected)
        return;

    selected = nextSelected;
    repaint();
}

void ParameterControl::setCompact(const bool shouldUseCompactLayout)
{
    if (compact == shouldUseCompactLayout)
        return;

    compact = shouldUseCompactLayout;
    resized();
    repaint();
}

void ParameterControl::paint(juce::Graphics& graphics)
{
    const auto titleHovered = interactionEnabled && hoverRegion == PressRegion::title;
    const auto valueHovered = interactionEnabled && hoverRegion == PressRegion::value;
    graphics.setColour(titleHovered ? ana::ui::light : ana::ui::dark);
    graphics.fillRect(titleBounds);
    graphics.setColour(valueHovered ? ana::ui::light : ana::ui::dark);
    graphics.fillRect(valueBounds);
    graphics.setColour(selected ? ana::ui::white : ana::ui::light);
    graphics.drawRect(titleBounds, selected ? ana::ui::activeBorderWidth : 1);
    graphics.setColour(ana::ui::light);
    graphics.drawRect(valueBounds, 1);
    graphics.setFont(ana::ui::makeFont());
    graphics.setColour(! interactionEnabled ? ana::ui::light
                       : titleHovered ? juce::Colours::black : ana::ui::white);
    graphics.drawText(titleText,
                      titleBounds.reduced(ana::ui::textPadding, 1),
                      juce::Justification::centredLeft, true);
    graphics.setColour(! interactionEnabled ? ana::ui::light
                       : valueHovered ? juce::Colours::black : ana::ui::white);
    graphics.drawText(interactionEnabled || displayValueWhenDisabled
                          ? slider.getTextFromValue(slider.getValue()) : "OFF",
                      ana::ui::readoutTextBounds(valueBounds.reduced(ana::ui::gap.pixels(), 1)),
                      juce::Justification::centred, true);
}

void ParameterControl::resized()
{
    auto row = getLocalBounds();

    if (! compact)
    {
        const auto availableWidth = std::max(0, row.getWidth() - ana::ui::gap.pixels());
        titleBounds = row.removeFromLeft(availableWidth / 2);
        ana::ui::gap.removeFromLeft(row);
        valueBounds = row;
    }
    else
    {
        titleBounds = {};
        valueBounds = row;
    }
    valueEditor.setBounds(valueBounds);
}

void ParameterControl::mouseDown(const juce::MouseEvent& event)
{
    if (! interactionEnabled || ! event.mods.isLeftButtonDown())
        return;

    pressHighlighted = true;
    pressRegion = titleBounds.contains(event.getPosition()) ? PressRegion::title
                  : valueBounds.contains(event.getPosition()) ? PressRegion::value
                                                               : PressRegion::none;
    pressGesture.begin(pressRegion == PressRegion::title && parameter != nullptr);

    repaint();
}

void ParameterControl::mouseMove(const juce::MouseEvent& event)
{
    const auto nextHoverRegion = ! interactionEnabled ? PressRegion::none
        : titleBounds.contains(event.getPosition()) ? PressRegion::title
        : valueBounds.contains(event.getPosition()) ? PressRegion::value
                                                    : PressRegion::none;
    if (hoverRegion != nextHoverRegion)
    {
        hoverRegion = nextHoverRegion;
        repaint();
    }
}

void ParameterControl::mouseDrag(const juce::MouseEvent& event)
{
    if (! pressGesture.isActive())
        return;

    if (event.mouseWasDraggedSinceMouseDown())
    {
        pressGesture.markDragged();
        pressHighlighted = false;
        repaint();
    }
}

void ParameterControl::mouseUp(const juce::MouseEvent& event)
{
    if (! pressGesture.isActive())
        return;

    const auto releasedRegion = pressRegion;
    const auto releaseResult = pressGesture.release();
    const auto shouldActivate = releaseResult == LongPressGesture::ReleaseResult::shortPress
        && ((releasedRegion == PressRegion::title && titleBounds.contains(event.getPosition()))
            || (releasedRegion == PressRegion::value && valueBounds.contains(event.getPosition())));
    pressHighlighted = false;
    pressRegion = PressRegion::none;
    repaint();

    if (! shouldActivate)
        return;

    if (releasedRegion == PressRegion::title)
    {
        if (onFocusRequested)
            onFocusRequested(*this);

        return;
    }

    if (choiceParameter != nullptr)
    {
        if (onChoiceRequested)
            onChoiceRequested(*this);
    }
    else if (boolParameter != nullptr)
    {
        slider.setValue(slider.getValue() < 0.5 ? 1.0 : 0.0, juce::sendNotificationSync);
    }
    else
    {
        // Single-click values stay inert; labels focus the shared knob, double-click edits text.
    }
}

void ParameterControl::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (! interactionEnabled || ! event.mods.isLeftButtonDown()
        || ! valueBounds.contains(event.getPosition())
        || parameter == nullptr || choiceParameter != nullptr || boolParameter != nullptr)
        return;

    // Cancel pending gesture handling before opening the inline editor.
    pressGesture.cancel();
    pressHighlighted = false;
    pressRegion = PressRegion::none;

    showValueEditor();
    repaint();
}

void ParameterControl::commitPendingEditor()
{
    hideValueEditor(false);
}

void ParameterControl::mouseExit(const juce::MouseEvent&)
{
    hoverRegion = PressRegion::none;

    if (! pressGesture.isActive())
    {
        repaint();
        return;
    }

    pressHighlighted = false;
    pressGesture.cancelArming();
    repaint();
}

void ParameterControl::showValueEditor()
{
    if (! interactionEnabled || parameter == nullptr || choiceParameter != nullptr || valueEditorActive)
        return;

    valueEditorActive = true;
    if (onTextEditingChanged)
        onTextEditingChanged(true);

    valueEditor.setText(slider.getTextFromValue(slider.getValue()), false);
    valueEditor.setVisible(true);
    valueEditor.toFront(false);
    valueEditor.grabKeyboardFocus();
    valueEditor.selectAll();
}

void ParameterControl::hideValueEditor(const bool discardChanges)
{
    if (! valueEditorActive)
        return;

    const auto enteredText = valueEditor.getText();
    valueEditorActive = false;
    valueEditor.setVisible(false);

    if (! discardChanges)
    {
        const auto enteredValue = slider.getValueFromText(enteredText);
        slider.setValue(juce::jlimit(slider.getMinimum(), slider.getMaximum(), enteredValue),
                        juce::sendNotificationSync);
    }

    if (onTextEditingChanged)
        onTextEditingChanged(false);

    repaint();
}

void ParameterControl::resetToDefault()
{
    if (parameter == nullptr || ! interactionEnabled)
        return;

    slider.setValue(parameter->convertFrom0to1(parameter->getDefaultValue()),
                    juce::sendNotificationSync);
}
