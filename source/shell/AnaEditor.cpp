#include "AnaEditor.h"
#include "AnaProcessor.h"
#include "AnaTheme.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr float minimumCrossoverGapHz = 1.0f;
constexpr int bandButtonWidth = 44;
constexpr int bandZoomValueWidth = 74;
constexpr int bandRangeSliderHeight = 14;
constexpr int bandZoomSliderWidth = bandRangeSliderHeight;
constexpr int minimumBandHeight = ana::ui::gap.pixels() * 3 + ana::ui::controlHeight + bandRangeSliderHeight;
constexpr int minimumEditorWidth = 1300;
constexpr int minimumEditorHeight = 300;
constexpr int maximumEditorSize = 32768;
constexpr int defaultEditorWidth = 1024;
constexpr int defaultEditorHeight = 720;
constexpr int waveformRightInset = ana::ui::gap.pixels() + bandZoomSliderWidth;
constexpr std::array<const char*, 6> scopeModeButtonNames { "LR", "L", "R", "MS", "M", "S" };
constexpr std::array<ana::ScopeChannelMode, 6> scopeModeButtonModes {
    ana::ScopeChannelMode::lr,
    ana::ScopeChannelMode::left,
    ana::ScopeChannelMode::right,
    ana::ScopeChannelMode::ms,
    ana::ScopeChannelMode::mid,
    ana::ScopeChannelMode::side
};

struct DisplayedModes
{
    std::array<size_t, 2> indices {};
    size_t count = 1;
};

DisplayedModes getDisplayedModes(const ana::ScopeChannelMode mode) noexcept
{
    switch (mode)
    {
        case ana::ScopeChannelMode::lr: return { { 0, 1 }, 2 };
        case ana::ScopeChannelMode::ms: return { { 2, 3 }, 2 };
        case ana::ScopeChannelMode::left: return { { 0, 0 }, 1 };
        case ana::ScopeChannelMode::right: return { { 1, 1 }, 1 };
        case ana::ScopeChannelMode::mid: return { { 2, 2 }, 1 };
        case ana::ScopeChannelMode::side: return { { 3, 3 }, 1 };
    }

    return { { 2, 2 }, 1 };
}

juce::String formatFrequency(const double frequency)
{
    return juce::String(frequency, frequency >= 100.0 ? 0 : 1);
}

double parseFrequency(const juce::String& text)
{
    const auto trimmed = text.trim().toLowerCase();
    const auto multiplier = trimmed.containsChar('k') ? 1000.0 : 1.0;
    return trimmed.getDoubleValue() * multiplier;
}

juce::String formatZoomValue(const float decibels)
{
    const auto displayValue = std::abs(decibels) < 0.05f ? 0.0f : decibels;
    return (displayValue > 0.0f ? "+" : "") + juce::String(displayValue, 2);
}

void drawWaveformEnvelope(juce::Graphics& graphics,
                          const std::vector<float>& minimums,
                          const std::vector<float>& maximums,
                          const juce::Rectangle<float> bounds,
                          const float rangeStart,
                          const float rangeEnd,
                          const float verticalZoomDecibels,
                          const bool filledStyle)
{
    if (minimums.empty() || maximums.size() != minimums.size() || bounds.isEmpty())
        return;

    const auto sourceColumns = minimums.size();
    const auto visibleStart = juce::jlimit(0.0f, 0.999f, rangeStart);
    const auto visibleEnd = juce::jlimit(visibleStart + 0.001f, 1.0f, rangeEnd);
    const auto firstSourcePosition = visibleStart * static_cast<float>(sourceColumns - 1);
    const auto lastSourcePosition = visibleEnd * static_cast<float>(sourceColumns - 1);
    const auto visibleSourceLength = std::max(0.001f,
        lastSourcePosition - firstSourcePosition);
    const auto firstSourceIndex = std::min(
        sourceColumns - 1,
        static_cast<size_t>(std::floor(firstSourcePosition)));
    const auto lastSourceIndex = std::min(
        sourceColumns - 1,
        static_cast<size_t>(std::ceil(lastSourcePosition)));
    const auto centreY = bounds.getCentreY();
    const auto amplitude = std::max(0.0f, bounds.getHeight() * 0.5f);
    const auto zoomGain = juce::Decibels::decibelsToGain(verticalZoomDecibels);
    juce::Graphics::ScopedSaveState clipState(graphics);
    graphics.reduceClipRegion(bounds.toNearestInt());
    juce::Path path;
    std::vector<juce::Point<float>> lowerEdge;
    auto pointCount = 0;
    auto singleTop = 0.0f;

    const auto flushPath = [&]
    {
        if (pointCount == 1)
        {
            graphics.drawLine(lowerEdge.front().x, singleTop,
                              lowerEdge.front().x, lowerEdge.front().y, 1.0f);
        }
        else if (pointCount > 1)
        {
            for (auto iterator = lowerEdge.rbegin(); iterator != lowerEdge.rend(); ++iterator)
                path.lineTo(*iterator);

            path.closeSubPath();
            if (filledStyle)
                graphics.fillPath(path);
            else
                graphics.strokePath(path, juce::PathStrokeType(1.25f));
        }

        path.clear();
        lowerEdge.clear();
        pointCount = 0;
    };

    for (auto sourceIndex = firstSourceIndex;
         sourceIndex <= lastSourceIndex;
         ++sourceIndex)
    {
        auto minimum = minimums[sourceIndex];
        auto maximum = maximums[sourceIndex];

        if (minimum > maximum)
        {
            flushPath();
            continue;
        }

        minimum *= zoomGain;
        maximum *= zoomGain;
        const auto normalizedX =
            (static_cast<float>(sourceIndex) - firstSourcePosition) / visibleSourceLength;
        const auto x = bounds.getX() + normalizedX * bounds.getWidth();
        const auto top = centreY - maximum * amplitude;
        const auto bottom = centreY - minimum * amplitude;

        if (pointCount == 0)
        {
            path.startNewSubPath(x, top);
            singleTop = top;
        }
        else
        {
            path.lineTo(x, top);
        }

        lowerEdge.emplace_back(x, bottom);
        ++pointCount;
    }

    flushPath();
}

} // namespace

AnaScopeButton::AnaScopeButton(juce::String text)
    : juce::Button(std::move(text))
{
    setWantsKeyboardFocus(false);
}

void AnaScopeButton::paintButton(juce::Graphics& graphics,
                                 const bool,
                                 const bool shouldDrawButtonAsDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    auto fill = shouldDrawButtonAsDown ? ana::ui::grey700 : ana::ui::field;

    graphics.setColour(fill);
    graphics.fillRect(bounds);
    const auto active = isEnabled() && getToggleState();
    graphics.setColour(active ? ana::ui::accent : ana::ui::grey500);
    graphics.drawRect(bounds, active ? 1.5f : 1.0f);
    graphics.setColour(isEnabled() ? ana::ui::white : ana::ui::grey500);
    graphics.setFont(ana::ui::makeFont());
    graphics.drawFittedText(getButtonText(), getLocalBounds().reduced(ana::ui::gap.pixels(), 1),
                            juce::Justification::centred, 1);
}

juce::Slider::SliderLayout AnaSliderLookAndFeel::getSliderLayout(juce::Slider& slider)
{
    if (slider.getSliderStyle() == juce::Slider::LinearBarVertical)
        return { slider.getLocalBounds(), {} };

    return juce::LookAndFeel_V4::getSliderLayout(slider);
}

void AnaSliderLookAndFeel::drawLinearSlider(juce::Graphics& graphics,
                                            const int x, const int y, const int width, const int height,
                                            const float sliderPosition,
                                            const float minimumSliderPosition,
                                            const float maximumSliderPosition,
                                            const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearBarVertical)
    {
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                    static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
        const auto markerY = juce::jlimit(bounds.getY(), bounds.getBottom(), sliderPosition);
        const auto neutralProportion = static_cast<float>(slider.valueToProportionOfLength(0.0));
        const auto neutralY = juce::jmap(neutralProportion, bounds.getBottom(), bounds.getY());
        const auto fillTop = std::min(markerY, neutralY);
        const auto fillBottom = std::max(markerY, neutralY);
        graphics.setColour(ana::ui::field);
        graphics.fillRect(bounds);
        graphics.setColour(ana::ui::accent.withAlpha(slider.isEnabled() ? 0.22f : 0.06f));
        graphics.fillRect(bounds.withTop(fillTop).withBottom(fillBottom));
        graphics.setColour(ana::ui::grey500);
        graphics.fillRect(bounds.getX(), neutralY - 0.5f, bounds.getWidth(), 1.0f);
        graphics.setColour(slider.isEnabled() ? ana::ui::accent : ana::ui::grey500);
        graphics.fillRect(bounds.getX(), markerY - 1.0f, bounds.getWidth(), 2.0f);
        graphics.setColour(ana::ui::grey500);
        graphics.drawRect(bounds, 1.0f);
        return;
    }

    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
    {
        juce::LookAndFeel_V4::drawLinearSlider(graphics, x, y, width, height,
                                               sliderPosition, minimumSliderPosition, maximumSliderPosition,
                                               style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    const auto markerX = juce::jlimit(bounds.getX(), bounds.getRight(), sliderPosition);

    graphics.setColour(ana::ui::field);
    graphics.fillRect(bounds);
    graphics.setColour(ana::ui::accent.withAlpha(slider.isEnabled() ? 0.18f : 0.06f));
    graphics.fillRect(bounds.withRight(markerX));
    graphics.setColour(slider.isEnabled() ? ana::ui::accent : ana::ui::grey500);
    graphics.fillRect(markerX - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(bounds, 1.0f);

    if (style == juce::Slider::LinearBar)
    {
        graphics.setColour(slider.isEnabled() ? ana::ui::white : ana::ui::grey500);
        graphics.setFont(ana::ui::makeFont());
        graphics.drawFittedText(slider.getTextFromValue(slider.getValue()),
                                bounds.toNearestInt().reduced(ana::ui::gap.pixels(), 1),
                                juce::Justification::centred, 1);
    }
}

void AnaSliderLookAndFeel::drawScrollbar(juce::Graphics& graphics,
                                         juce::ScrollBar&,
                                         const int x, const int y, const int width, const int height,
                                         const bool isScrollbarVertical,
                                         const int thumbStartPosition,
                                         const int thumbSize,
                                         const bool,
                                         const bool isMouseDown)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height);
    graphics.setColour(ana::ui::field);
    graphics.fillRect(bounds);
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(bounds, 1);

    if (thumbSize <= 0)
        return;

    auto thumb = isScrollbarVertical
        ? juce::Rectangle<int>(x + 2, thumbStartPosition, std::max(1, width - 4), thumbSize)
        : juce::Rectangle<int>(thumbStartPosition, y + 2, thumbSize, std::max(1, height - 4));
    graphics.setColour(ana::ui::accent.withAlpha(isMouseDown ? 1.0f : 0.65f));
    graphics.fillRect(thumb);
}

juce::Label* AnaSliderLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(ana::ui::makeFont());
    label->setJustificationType(juce::Justification::centred);
    label->setColour(juce::Label::backgroundColourId, ana::ui::field);
    label->setColour(juce::Label::outlineColourId, ana::ui::grey500);
    label->setColour(juce::Label::textColourId, ana::ui::white);
    label->setColour(juce::Label::textWhenEditingColourId, ana::ui::white);
    return label;
}

AnaParameterControl::AnaParameterControl(juce::AudioProcessorValueTreeState& state,
                                         const juce::String& parameterId,
                                         juce::String title,
                                         Formatter formatter)
    : titleText(std::move(title)), valueFormatter(std::move(formatter)), compact(titleText.isEmpty())
{
    parameter = state.getParameter(parameterId);
    choiceParameter = dynamic_cast<juce::AudioParameterChoice*>(parameter);
    boolParameter = dynamic_cast<juce::AudioParameterBool*>(parameter);
    setLookAndFeel(&sliderLookAndFeel);
    slider.setLookAndFeel(&sliderLookAndFeel);
    slider.setSliderStyle(compact ? juce::Slider::LinearBar : juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(compact ? juce::Slider::NoTextBox : juce::Slider::TextBoxRight,
                           false, compact ? 0 : 116, 34);
    slider.setScrollWheelEnabled(false);
    slider.setWantsKeyboardFocus(false);
    slider.setMouseClickGrabsKeyboardFocus(false);
    slider.setInterceptsMouseClicks(false, false);
    slider.textFromValueFunction = [this] (const double value)
    {
        return valueFormatter != nullptr ? valueFormatter(value) : juce::String(value);
    };
    slider.setColour(juce::Slider::textBoxTextColourId, ana::ui::white);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, ana::ui::field);
    slider.setColour(juce::Slider::textBoxOutlineColourId, ana::ui::grey500);
    slider.onValueChange = [this]
    {
        repaint();

        if (onValueChanged)
            onValueChanged();
    };
    addChildComponent(slider);
    valueEditor.setFont(ana::ui::makeFont());
    valueEditor.setJustification(juce::Justification::centred);
    valueEditor.setPopupMenuEnabled(false);
    valueEditor.setSelectAllWhenFocused(true);
    valueEditor.setColour(juce::TextEditor::textColourId, ana::ui::white);
    valueEditor.setColour(juce::TextEditor::backgroundColourId, ana::ui::field);
    valueEditor.setColour(juce::TextEditor::outlineColourId, ana::ui::grey500);
    valueEditor.setColour(juce::TextEditor::focusedOutlineColourId, ana::ui::grey500);
    valueEditor.setColour(juce::TextEditor::highlightColourId, ana::ui::accent.withAlpha(0.45f));
    valueEditor.onReturnKey = [this] { hideValueEditor(false); };
    valueEditor.onEscapeKey = [this] { hideValueEditor(true); };
    valueEditor.onFocusLost = [this]
    {
        if (valueEditorActive)
            hideValueEditor(false);
    };
    addChildComponent(valueEditor);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, parameterId, slider);
}

AnaParameterControl::~AnaParameterControl()
{
    stopTimer();
    slider.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void AnaParameterControl::setInteractionEnabled(const bool shouldEnable)
{
    if (interactionEnabled == shouldEnable)
        return;

    interactionEnabled = shouldEnable;

    if (! interactionEnabled)
    {
        selected = false;
        hideValueEditor(true);
    }

    repaint();
}

juce::StringArray AnaParameterControl::getChoiceNames() const
{
    return choiceParameter != nullptr ? choiceParameter->choices : juce::StringArray();
}

int AnaParameterControl::getSelectedChoiceIndex() const noexcept
{
    return choiceParameter != nullptr ? juce::roundToInt(slider.getValue()) : -1;
}

void AnaParameterControl::setSelectedChoiceIndex(const int choiceIndex)
{
    if (choiceParameter == nullptr || ! juce::isPositiveAndBelow(choiceIndex, choiceParameter->choices.size()))
        return;

    slider.setValue(choiceIndex, juce::sendNotificationSync);
}

void AnaParameterControl::setSelected(const bool shouldSelect)
{
    const auto nextSelected = shouldSelect && interactionEnabled;

    if (selected == nextSelected)
        return;

    selected = nextSelected;
    repaint();
}

void AnaParameterControl::setCompact(const bool shouldUseCompactLayout)
{
    if (compact == shouldUseCompactLayout)
        return;

    compact = shouldUseCompactLayout;
    resized();
    repaint();
}

void AnaParameterControl::paint(juce::Graphics& graphics)
{
    graphics.setColour(pointerDown && pressHighlighted && pressRegion == PressRegion::title
                           ? ana::ui::grey700 : ana::ui::field);
    graphics.fillRect(titleBounds);
    graphics.setColour(pointerDown && pressHighlighted && pressRegion == PressRegion::value
                           ? ana::ui::grey700 : ana::ui::field);
    graphics.fillRect(valueBounds);
    graphics.setColour(selected ? ana::ui::accent : ana::ui::grey500);
    graphics.drawRect(titleBounds, selected ? 2 : 1);
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(valueBounds, 1);
    graphics.setColour(interactionEnabled ? ana::ui::white : ana::ui::grey500);
    graphics.setFont(ana::ui::makeFont());
    graphics.drawFittedText(longPressArmed ? "RESET?" : titleText, titleBounds.reduced(12, 1),
                            juce::Justification::centredLeft, 1);
    graphics.drawFittedText(interactionEnabled ? slider.getTextFromValue(slider.getValue()) : "OFF",
                            valueBounds.reduced(ana::ui::gap.pixels(), 1),
                            juce::Justification::centred, 1);
}

void AnaParameterControl::resized()
{
    auto row = getLocalBounds();

    if (! compact)
    {
        const auto availableWidth = std::max(0, row.getWidth() - ana::ui::gap.pixels());
        titleBounds = row.removeFromLeft(availableWidth / 2);
        ana::ui::gap.removeFromLeft(row);
    }
    else
    {
        titleBounds = {};
    }

    valueBounds = row;
    slider.setBounds(valueBounds);
    valueEditor.setBounds(valueBounds);
}

void AnaParameterControl::mouseDown(const juce::MouseEvent& event)
{
    if (! interactionEnabled || ! event.mods.isLeftButtonDown())
        return;

    pointerDown = true;
    dragDetected = false;
    longPressArmed = false;
    pressHighlighted = true;
    pressRegion = titleBounds.contains(event.getPosition()) ? PressRegion::title
                  : valueBounds.contains(event.getPosition()) ? PressRegion::value
                                                               : PressRegion::none;

    if (pressRegion == PressRegion::title && parameter != nullptr)
        startTimer(500);

    repaint();
}

void AnaParameterControl::mouseDrag(const juce::MouseEvent& event)
{
    if (! pointerDown)
        return;

    if (event.mouseWasDraggedSinceMouseDown())
    {
        dragDetected = true;
        longPressArmed = false;
        pressHighlighted = false;
        stopTimer();
        repaint();
    }
}

void AnaParameterControl::mouseUp(const juce::MouseEvent& event)
{
    if (! pointerDown)
        return;

    stopTimer();
    const auto releasedRegion = pressRegion;
    const auto wasLongPress = longPressArmed;
    const auto shouldActivate = ! dragDetected
        && ((releasedRegion == PressRegion::title && titleBounds.contains(event.getPosition()))
            || (releasedRegion == PressRegion::value && valueBounds.contains(event.getPosition())));
    pointerDown = false;
    dragDetected = false;
    pressHighlighted = false;
    longPressArmed = false;
    pressRegion = PressRegion::none;
    repaint();

    if (! shouldActivate)
        return;

    if (releasedRegion == PressRegion::title)
    {
        if (wasLongPress)
            resetToDefault();
        else if (onFocusRequested)
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
        if (onFocusRequested)
            onFocusRequested(*this);

        showValueEditor();
    }
}

void AnaParameterControl::commitPendingEditor()
{
    hideValueEditor(false);
}

void AnaParameterControl::mouseExit(const juce::MouseEvent&)
{
    if (! pointerDown)
        return;

    pressHighlighted = false;
    longPressArmed = false;
    stopTimer();
    repaint();
}

void AnaParameterControl::timerCallback()
{
    stopTimer();

    if (! pointerDown || dragDetected || pressRegion != PressRegion::title || ! pressHighlighted)
        return;

    longPressArmed = true;
    repaint();
}

void AnaParameterControl::showValueEditor()
{
    if (! interactionEnabled || choiceParameter != nullptr || valueEditorActive)
        return;

    valueEditorActive = true;
    valueEditor.setText(slider.getTextFromValue(slider.getValue()), false);
    valueEditor.setVisible(true);
    valueEditor.toFront(false);
    valueEditor.grabKeyboardFocus();
    valueEditor.selectAll();
}

void AnaParameterControl::hideValueEditor(const bool discardChanges)
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

    repaint();
}

void AnaParameterControl::resetToDefault()
{
    if (parameter == nullptr || ! interactionEnabled)
        return;

    slider.setValue(parameter->convertFrom0to1(parameter->getDefaultValue()),
                    juce::sendNotificationSync);
}

AnaChoicePrompt::AnaChoicePrompt(juce::Rectangle<int> anchorBoundsIn,
                                 juce::StringArray choicesIn,
                                 const int selectedIndex,
                                 std::function<void(int)> selectCallback,
                                 std::function<void()> closeCallback)
    : anchorBounds(anchorBoundsIn),
      choices(std::move(choicesIn)),
      onSelect(std::move(selectCallback)),
      onClose(std::move(closeCallback))
{
    setOpaque(false);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setInterceptsMouseClicks(true, true);
    choiceButtons.reserve(static_cast<size_t>(choices.size()));

    for (int index = 0; index < choices.size(); ++index)
    {
        auto button = std::make_unique<AnaScopeButton>(choices[index]);
        button->setToggleState(index == selectedIndex, juce::dontSendNotification);
        button->onClick = [this, index] { choose(index); };
        addAndMakeVisible(*button);
        choiceButtons.push_back(std::move(button));
    }
}

void AnaChoicePrompt::paint(juce::Graphics& graphics)
{
    graphics.setColour(juce::Colours::black.withAlpha(0.55f));
    graphics.fillAll();
    graphics.setColour(ana::ui::grey700);
    graphics.fillRect(panelBounds);
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(panelBounds, 1);
}

void AnaChoicePrompt::resized()
{
    const auto itemCount = static_cast<int>(choiceButtons.size());
    const auto contentHeight = itemCount * ana::ui::controlHeight
        + std::max(0, itemCount - 1) * ana::ui::gap.pixels();
    auto contentWidth = 0.0f;
    for (const auto& choice : choices)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(ana::ui::makeFont(), choice, 0.0f, 0.0f);
        contentWidth = std::max(contentWidth,
                                glyphs.getBoundingBox(0, glyphs.getNumGlyphs(), true).getWidth());
    }
    const auto desiredPanelWidth = juce::roundToInt(std::ceil(contentWidth))
        + ana::ui::gap.pixels() * 4;
    const auto panelWidth = std::max(1, std::min(
        std::max(anchorBounds.getWidth(), desiredPanelWidth),
        getWidth() - ana::ui::gap.pixels() * 2));
    const auto panelHeight = std::min(getHeight() - ana::ui::gap.pixels() * 2,
                                      contentHeight + ana::ui::gap.pixels() * 2);
    panelBounds = juce::Rectangle<int>(panelWidth, std::max(1, panelHeight));
    panelBounds.setPosition(anchorBounds.getPosition());
    panelBounds = panelBounds.constrainedWithin(getLocalBounds().reduced(ana::ui::gap.pixels()));

    auto area = panelBounds.reduced(ana::ui::gap.pixels());
    for (size_t index = 0; index < choiceButtons.size(); ++index)
    {
        choiceButtons[index]->setBounds(area.removeFromTop(ana::ui::controlHeight));

        if (index + 1 < choiceButtons.size())
            area.removeFromTop(ana::ui::gap.pixels());
    }
}

void AnaChoicePrompt::mouseDown(const juce::MouseEvent& event)
{
    if (! panelBounds.contains(event.getPosition()))
        close();
}

void AnaChoicePrompt::choose(const int index)
{
    if (closing)
        return;

    closing = true;
    auto selectCallback = std::move(onSelect);
    auto closeCallback = std::move(onClose);
    juce::MessageManager::callAsync([index,
                                     select = std::move(selectCallback),
                                     closePrompt = std::move(closeCallback)]() mutable
    {
        if (select)
            select(index);

        if (closePrompt)
            closePrompt();
    });
}

void AnaChoicePrompt::close()
{
    if (closing)
        return;

    closing = true;
    auto closeCallback = std::move(onClose);
    juce::MessageManager::callAsync([closePrompt = std::move(closeCallback)]() mutable
    {
        if (closePrompt)
            closePrompt();
    });
}

AnaLocalChoiceControl::AnaLocalChoiceControl(juce::String title)
    : titleText(std::move(title))
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    choices.add("ALL");
}

void AnaLocalChoiceControl::setChoices(juce::StringArray newChoices,
                                       const int newSelectedIndex)
{
    if (newChoices.isEmpty())
        newChoices.add("ALL");

    choices = std::move(newChoices);
    selectedIndex = juce::jlimit(0, choices.size() - 1, newSelectedIndex);
    repaint();
}

void AnaLocalChoiceControl::setSelectedChoiceIndex(const int newSelectedIndex,
                                                   const bool sendChange)
{
    const auto nextIndex = juce::jlimit(0, std::max(0, choices.size() - 1), newSelectedIndex);

    if (selectedIndex == nextIndex)
        return;

    selectedIndex = nextIndex;
    repaint();

    if (sendChange && onSelectionChanged)
        onSelectionChanged(selectedIndex);
}

void AnaLocalChoiceControl::paint(juce::Graphics& graphics)
{
    graphics.setColour(ana::ui::field);
    graphics.fillRect(titleBounds);
    graphics.setColour(pointerDown ? ana::ui::grey700 : ana::ui::field);
    graphics.fillRect(valueBounds);
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(titleBounds, 1);
    graphics.drawRect(valueBounds, 1);
    graphics.setColour(isEnabled() ? ana::ui::white : ana::ui::grey500);
    graphics.setFont(ana::ui::makeFont());
    graphics.drawFittedText(titleText, titleBounds.reduced(ana::ui::gap.pixels(), 1),
                            juce::Justification::centredLeft, 1);
    graphics.drawFittedText(choices[selectedIndex], valueBounds.reduced(ana::ui::gap.pixels(), 1),
                            juce::Justification::centred, 1);
}

void AnaLocalChoiceControl::resized()
{
    auto area = getLocalBounds();
    titleBounds = area.removeFromLeft(area.getWidth() / 2);
    valueBounds = area;
}

void AnaLocalChoiceControl::mouseDown(const juce::MouseEvent& event)
{
    pointerDown = isEnabled() && valueBounds.contains(event.getPosition());
    repaint();
}

void AnaLocalChoiceControl::mouseUp(const juce::MouseEvent& event)
{
    const auto shouldOpen = pointerDown && valueBounds.contains(event.getPosition());
    pointerDown = false;
    repaint();

    if (shouldOpen && onChoiceRequested)
        onChoiceRequested();
}

void AnaLocalChoiceControl::mouseExit(const juce::MouseEvent&)
{
    if (! isMouseButtonDown())
    {
        pointerDown = false;
        repaint();
    }
}

void AnaRangeSlider::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);

    graphics.setColour(ana::ui::field);
    graphics.fillRect(bounds);
    graphics.setColour(ana::ui::accent.withAlpha(0.2f));

    if (orientation == Orientation::horizontal)
    {
        const auto startX = juce::jmap(rangeStart, bounds.getX(), bounds.getRight());
        const auto endX = juce::jmap(rangeEnd, bounds.getX(), bounds.getRight());
        graphics.fillRect(bounds.withLeft(startX).withRight(endX));
        graphics.setColour(ana::ui::accent);
        graphics.fillRect(startX - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
        graphics.fillRect(endX - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
    }
    else
    {
        const auto startY = juce::jmap(rangeStart, bounds.getY(), bounds.getBottom());
        const auto endY = juce::jmap(rangeEnd, bounds.getY(), bounds.getBottom());
        graphics.fillRect(bounds.withTop(startY).withBottom(endY));
        graphics.setColour(ana::ui::accent);
        graphics.fillRect(bounds.getX(), startY - 1.0f, bounds.getWidth(), 2.0f);
        graphics.fillRect(bounds.getX(), endY - 1.0f, bounds.getWidth(), 2.0f);
    }

    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(bounds, 1.0f);
}

void AnaRangeSlider::mouseDown(const juce::MouseEvent& event)
{
    constexpr float handleHitRadius = 8.0f;
    const auto length = static_cast<float>(std::max(1, orientation == Orientation::horizontal
        ? getWidth() - 1 : getHeight() - 1));
    const auto start = rangeStart * length;
    const auto end = rangeEnd * length;
    const auto mouse = static_cast<float>(orientation == Orientation::horizontal ? event.x : event.y);

    dragStartRangeStart = rangeStart;
    dragStartRangeEnd = rangeEnd;

    if (std::abs(mouse - start) <= handleHitRadius)
        dragMode = DragMode::start;
    else if (std::abs(mouse - end) <= handleHitRadius)
        dragMode = DragMode::end;
    else if (mouse > start && mouse < end)
        dragMode = DragMode::range;
    else
        dragMode = DragMode::none;
}

void AnaRangeSlider::mouseDrag(const juce::MouseEvent& event)
{
    constexpr float minimumRange = 0.01f;
    const auto delta = static_cast<float>(orientation == Orientation::horizontal
        ? event.getDistanceFromDragStartX() : event.getDistanceFromDragStartY())
        / static_cast<float>(std::max(1, orientation == Orientation::horizontal
            ? getWidth() - 1 : getHeight() - 1));

    switch (dragMode)
    {
        case DragMode::start:
            updateRange(juce::jlimit(0.0f, dragStartRangeEnd - minimumRange,
                                     dragStartRangeStart + delta),
                        dragStartRangeEnd);
            break;
        case DragMode::end:
            updateRange(dragStartRangeStart,
                        juce::jlimit(dragStartRangeStart + minimumRange, 1.0f,
                                     dragStartRangeEnd + delta));
            break;
        case DragMode::range:
        {
            const auto rangeWidth = dragStartRangeEnd - dragStartRangeStart;
            const auto newStart = juce::jlimit(0.0f, 1.0f - rangeWidth,
                                               dragStartRangeStart + delta);
            updateRange(newStart, newStart + rangeWidth);
            break;
        }
        case DragMode::none:
            break;
    }
}

void AnaRangeSlider::mouseUp(const juce::MouseEvent&)
{
    dragMode = DragMode::none;
}

void AnaRangeSlider::setRange(const float newStart, const float newEnd)
{
    updateRange(juce::jlimit(0.0f, 1.0f, newStart),
                juce::jlimit(0.0f, 1.0f, newEnd));
}

void AnaRangeSlider::updateRange(const float newStart, const float newEnd)
{
    if (std::abs(rangeStart - newStart) <= 1.0e-6f
        && std::abs(rangeEnd - newEnd) <= 1.0e-6f)
        return;

    rangeStart = newStart;
    rangeEnd = newEnd;
    repaint();

    if (onRangeChanged)
        onRangeChanged();
}

AnaFrequencyDisplayComponent::AnaFrequencyDisplayComponent(AnaAudioProcessor& processorRef)
    : processor(processorRef),
      frequencyLowControl(processorRef.getParameters(), AnaAudioProcessor::frequencyLowParameterId,
                          "FREQ-LOW", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      frequencyHighControl(processorRef.getParameters(), AnaAudioProcessor::frequencyHighParameterId,
                           "FREQ-HIGH", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      rangeLowControl(processorRef.getParameters(), AnaAudioProcessor::frequencyRangeLowParameterId,
                      "RANGE-LOW", [] (const double value) { return juce::String(value, 1); }),
      rangeHighControl(processorRef.getParameters(), AnaAudioProcessor::frequencyRangeHighParameterId,
                       "RANGE-HIGH", [] (const double value) { return juce::String(value, 1); })
{
    for (auto* component : std::array<juce::Component*, 6> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl,
             &frequencyRangeSlider, &magnitudeRangeSlider })
        addAndMakeVisible(*component);

    cursorReadoutLabel.setFont(ana::ui::makeFont());
    cursorReadoutLabel.setJustificationType(juce::Justification::centred);
    cursorReadoutLabel.setText("---", juce::dontSendNotification);
    cursorReadoutLabel.setColour(juce::Label::textColourId, ana::ui::white);
    cursorReadoutLabel.setColour(juce::Label::backgroundColourId, ana::ui::field);
    cursorReadoutLabel.setColour(juce::Label::outlineColourId, ana::ui::grey500);
    cursorReadoutLabel.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    cursorReadoutLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(cursorReadoutLabel);

    cursorNoteReadoutLabel.setFont(ana::ui::makeFont());
    cursorNoteReadoutLabel.setJustificationType(juce::Justification::centred);
    cursorNoteReadoutLabel.setText("---", juce::dontSendNotification);
    cursorNoteReadoutLabel.setColour(juce::Label::textColourId, ana::ui::white);
    cursorNoteReadoutLabel.setColour(juce::Label::backgroundColourId, ana::ui::field);
    cursorNoteReadoutLabel.setColour(juce::Label::outlineColourId, ana::ui::grey500);
    cursorNoteReadoutLabel.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    cursorNoteReadoutLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(cursorNoteReadoutLabel);

    cursorVerticalReadoutLabel.setFont(ana::ui::makeFont());
    cursorVerticalReadoutLabel.setJustificationType(juce::Justification::centred);
    cursorVerticalReadoutLabel.setText("---", juce::dontSendNotification);
    cursorVerticalReadoutLabel.setColour(juce::Label::textColourId, ana::ui::white);
    cursorVerticalReadoutLabel.setColour(juce::Label::backgroundColourId, ana::ui::field);
    cursorVerticalReadoutLabel.setColour(juce::Label::outlineColourId, ana::ui::grey500);
    cursorVerticalReadoutLabel.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    cursorVerticalReadoutLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(cursorVerticalReadoutLabel);

    static constexpr std::array<const char*, 7> monitorNames { "ST", "LR", "L", "R", "MS", "M", "S" };
    for (size_t index = 0; index < monitorButtons.size(); ++index)
    {
        auto button = std::make_unique<AnaScopeButton>(monitorNames[index]);
        button->onClick = [this, index]
        {
            if (auto* parameter = processor.getParameters().getParameter(
                    AnaAudioProcessor::frequencyChannelModeParameterId))
                parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(index)));
        };
        addAndMakeVisible(*button);
        monitorButtons[index] = std::move(button);
    }
    splitButton.setClickingTogglesState(true);
    splitButton.onClick = [this]
    {
        if (auto* parameter = processor.getParameters().getParameter(AnaAudioProcessor::frequencySplitViewParameterId))
            parameter->setValueNotifyingHost(splitButton.getToggleState() ? 1.0f : 0.0f);
    };
    addAndMakeVisible(splitButton);

    for (auto* control : std::array<AnaParameterControl*, 4> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl })
    {
        control->setCompact(true);
        control->onValueChanged = [this] { repaint(); };
    }

    frequencyRangeSlider.onRangeChanged = [this]
    {
        if (! synchronisingRanges)
            updateFrequencyRangeFromSlider();
    };
    magnitudeRangeSlider.onRangeChanged = [this]
    {
        if (! synchronisingRanges)
            updateMagnitudeRangeFromSlider();
    };
    startTimerHz(30);
}

void AnaFrequencyDisplayComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);
    const auto plotBounds = getPlotBounds();
    if (plotBounds.isEmpty())
        return;

    int fftSize = 0;
    auto sampleRate = processor.getFrequencySpectrum().getSampleRate();
    const auto spectrumType = [this] (const char* parameterId)
    {
        const auto* value = processor.getParameters().getRawParameterValue(parameterId);
        return value != nullptr && value->load(std::memory_order_relaxed) >= 0.5f
            ? ana::freq::FrequencySpectrumProcessor::DisplayType::maximum
            : ana::freq::FrequencySpectrumProcessor::DisplayType::realtimeAverage;
    };

    const auto* monitorMode = processor.getParameters().getRawParameterValue(
        AnaAudioProcessor::frequencyChannelModeParameterId);
    const auto mode = juce::jlimit(0, 6, monitorMode != nullptr
                                        ? juce::roundToInt(monitorMode->load(std::memory_order_relaxed))
                                        : 0);
    const auto firstChannel = mode == 0 ? ana::freq::FrequencySpectrumProcessor::Channel::stereo
                                        : mode == 3 ? ana::freq::FrequencySpectrumProcessor::Channel::right
                                        : mode == 5 || mode == 4 ? ana::freq::FrequencySpectrumProcessor::Channel::mid
                                        : mode == 6 ? ana::freq::FrequencySpectrumProcessor::Channel::side
                                                    : ana::freq::FrequencySpectrumProcessor::Channel::left;
    const auto secondChannel = mode == 1 ? ana::freq::FrequencySpectrumProcessor::Channel::right
                                         : mode == 4 ? ana::freq::FrequencySpectrumProcessor::Channel::side
                                                     : firstChannel;
    const auto* split = processor.getParameters().getRawParameterValue(
        AnaAudioProcessor::frequencySplitViewParameterId);
    const auto supportsSplitView = mode == 1 || mode == 4;
    const auto useSplitView = supportsSplitView && split != nullptr
        && split->load(std::memory_order_relaxed) >= 0.5f;
    const auto* secondSpectrumEnabled = processor.getParameters().getRawParameterValue(
        AnaAudioProcessor::frequencySecondSpectrumParameterId);
    const auto drawSecondSpectrum = useSplitView || secondSpectrumEnabled == nullptr
        || secondSpectrumEnabled->load(std::memory_order_relaxed) >= 0.5f;
    const auto copySpectra = [&] (const ana::freq::FrequencySpectrumProcessor& spectrum)
    {
        const auto firstType = spectrumType(AnaAudioProcessor::frequencyFirstSpectrumTypeParameterId);
        spectrum.copySpectrum(firstChannel, firstType,
                              primarySpectrum, fftSize);
        if (drawSecondSpectrum)
            spectrum.copySpectrum(secondChannel,
                                  spectrumType(AnaAudioProcessor::frequencySecondSpectrumTypeParameterId),
                                  secondarySpectrum, fftSize);
    };
    if (processor.isOfflineMode())
    {
        if (const auto snapshot = processor.getOfflineScopeSnapshot();
            snapshot != nullptr && snapshot->frequencySpectrum != nullptr)
        {
            sampleRate = snapshot->frequencySampleRate;
            copySpectra(*snapshot->frequencySpectrum);
        }
    }
    else
    {
        copySpectra(processor.getFrequencySpectrum());
    }

    if (useSplitView)
    {
        auto upperBounds = plotBounds;
        constexpr float dividerHeight = 1.0f;
        upperBounds.setHeight((plotBounds.getHeight() - dividerHeight) * 0.5f);
        auto lowerBounds = upperBounds.withY(upperBounds.getBottom() + dividerHeight);
        graphics.setColour(ana::ui::accent);
        graphics.fillRect(plotBounds.getX(), upperBounds.getBottom(), plotBounds.getWidth(), dividerHeight);
        drawSpectrum(graphics, primarySpectrum, fftSize, sampleRate, upperBounds, ana::ui::white);
        drawSpectrum(graphics, secondarySpectrum, fftSize, sampleRate, lowerBounds, ana::ui::accent);
    }
    else
    {
        drawSpectrum(graphics, primarySpectrum, fftSize, sampleRate, plotBounds, ana::ui::white);
        if (drawSecondSpectrum)
            drawSpectrum(graphics, secondarySpectrum, fftSize, sampleRate, plotBounds, ana::ui::accent);
    }

    const auto* cursorReadout = processor.getParameters().getRawParameterValue(
        AnaAudioProcessor::frequencyCursorReadoutParameterId);
    if (cursorInside && (cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f))
    {
        graphics.setColour(ana::ui::white);
        graphics.drawLine(cursorPosition.x, plotBounds.getY(), cursorPosition.x, plotBounds.getBottom(), 0.5f);
        graphics.drawLine(plotBounds.getX(), cursorPosition.y, plotBounds.getRight(), cursorPosition.y, 0.5f);
    }
}

void AnaFrequencyDisplayComponent::resized()
{
    const auto plotBounds = getPlotBounds().toNearestInt();
    const auto readoutWidth = std::min(110, std::max(64, plotBounds.getWidth() / 5));
    const auto readoutY = plotBounds.getBottom() - ana::ui::gap.pixels() - ana::ui::controlHeight;
    const auto graphRight = plotBounds.getRight();
    auto topArea = juce::Rectangle<int>(plotBounds.getX(), plotBounds.getY(),
                                        plotBounds.getWidth(), ana::ui::controlHeight);
    auto topReadouts = topArea.removeFromRight(readoutWidth * 2 + ana::ui::gap.pixels());
    ana::ui::gap.removeFromRight(topArea);
    ana::ui::FixedGapRow topControls(topArea);
    cursorReadoutLabel.setBounds(topControls.takeLeft(readoutWidth));
    cursorNoteReadoutLabel.setBounds(topControls.takeLeft(72));
    ana::ui::FixedGapRow monitorControls(topControls.remaining());
    constexpr int monitorButtonWidth = 44;
    for (auto& button : monitorButtons)
        button->setBounds(monitorControls.takeLeft(monitorButtonWidth));
    splitButton.setBounds(monitorControls.takeLeft(58));
    ana::ui::FixedGapRow topReadoutControls(topReadouts);
    cursorVerticalReadoutLabel.setBounds(topReadoutControls.takeLeft(readoutWidth));
    rangeHighControl.setBounds(topReadoutControls.takeLeft(readoutWidth));
    frequencyLowControl.setBounds(plotBounds.getX(), readoutY, readoutWidth, ana::ui::controlHeight);
    frequencyHighControl.setBounds(graphRight - readoutWidth * 2 - ana::ui::gap.pixels(), readoutY,
                                   readoutWidth, ana::ui::controlHeight);
    rangeLowControl.setBounds(graphRight - readoutWidth, readoutY,
                              readoutWidth, ana::ui::controlHeight);
    frequencyRangeSlider.setBounds(0, getHeight() - 14, getWidth(), 14);
    magnitudeRangeSlider.setBounds(getWidth() - 14, 0, 14, getHeight() - 14 - ana::ui::gap.pixels());
    syncRangeSliders();
    refreshMonitorControls();
}

void AnaFrequencyDisplayComponent::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent == this)
        processor.clearFrequencySpectrum();
}

void AnaFrequencyDisplayComponent::mouseMove(const juce::MouseEvent& event)
{
    const auto nextCursorInside = getPlotBounds().contains(event.position);
    if (cursorInside == nextCursorInside && cursorPosition == event.position)
        return;

    cursorInside = nextCursorInside;
    cursorPosition = event.position;
    if (cursorInside)
    {
        const auto readParameter = [this] (const char* parameterId, const float fallback)
        {
            if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
                return value->load(std::memory_order_relaxed);
            return fallback;
        };
        const auto lowFrequency = std::max(20.0f, readParameter(AnaAudioProcessor::frequencyLowParameterId, 20.0f));
        const auto highFrequency = std::max(lowFrequency + 1.0f,
                                            readParameter(AnaAudioProcessor::frequencyHighParameterId, 20000.0f));
        const auto normalisedX = juce::jlimit(0.0f, 1.0f,
                                              (cursorPosition.x - getPlotBounds().getX())
                                                  / getPlotBounds().getWidth());
        lastCursorFrequency = lowFrequency * std::pow(highFrequency / lowFrequency, normalisedX);
        cursorReadoutLabel.setText(juce::String(lastCursorFrequency, 1), juce::dontSendNotification);
        static constexpr std::array<const char*, 12> noteNames {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        const auto midiNote = juce::roundToInt(69.0 + 12.0 * std::log2(lastCursorFrequency / 440.0f));
        const auto noteIndex = (midiNote % 12 + 12) % 12;
        cursorNoteReadoutLabel.setText(juce::String(noteNames[static_cast<size_t>(noteIndex)])
                                           + juce::String(midiNote / 12 - 1),
                                       juce::dontSendNotification);
        const auto lowRange = readParameter(AnaAudioProcessor::frequencyRangeLowParameterId, -96.0f);
        const auto highRange = std::max(lowRange + 1.0f,
            readParameter(AnaAudioProcessor::frequencyRangeHighParameterId, 0.0f));
        const auto normalisedY = juce::jlimit(0.0f, 1.0f,
            (cursorPosition.y - getPlotBounds().getY()) / getPlotBounds().getHeight());
        cursorVerticalReadoutLabel.setText(juce::String(highRange - normalisedY * (highRange - lowRange), 2),
                                           juce::dontSendNotification);
    }
    repaint();
}

void AnaFrequencyDisplayComponent::mouseExit(const juce::MouseEvent&)
{
    if (! cursorInside)
        return;

    cursorInside = false;
    repaint();
}

juce::Rectangle<float> AnaFrequencyDisplayComponent::getPlotBounds() const noexcept
{
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromBottom(static_cast<float>(14 + ana::ui::gap.pixels()));
    bounds.removeFromRight(static_cast<float>(14 + ana::ui::gap.pixels()));
    return bounds;
}

void AnaFrequencyDisplayComponent::timerCallback()
{
    syncRangeSliders();
    refreshMonitorControls();
    if (processor.isOfflineMode())
    {
        if (const auto snapshot = processor.getOfflineScopeSnapshot();
            snapshot != nullptr && displayedOfflineRevision != snapshot->revision)
        {
            displayedOfflineRevision = snapshot->revision;
            repaint();
        }
        return;
    }

    const auto currentRevision = processor.getFrequencySpectrum().getRevision();
    if (displayedRevision != currentRevision)
    {
        displayedRevision = currentRevision;
        repaint();
    }
}

void AnaFrequencyDisplayComponent::refreshMonitorControls()
{
    const auto readValue = [this] (const char* parameterId, const float fallback)
    {
        if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto showMonitor = readValue(AnaAudioProcessor::frequencyMonitorControlsParameterId, 1.0f) >= 0.5f;
    const auto showZoom = readValue(AnaAudioProcessor::frequencyZoomControlsParameterId, 1.0f) >= 0.5f;
    const auto mode = juce::jlimit(0, static_cast<int>(monitorButtons.size()) - 1,
                                   juce::roundToInt(readValue(AnaAudioProcessor::frequencyChannelModeParameterId, 0.0f)));
    const auto splitAvailable = mode == 1 || mode == 4;
    for (size_t index = 0; index < monitorButtons.size(); ++index)
    {
        monitorButtons[index]->setVisible(showMonitor);
        monitorButtons[index]->setToggleState(static_cast<int>(index) == mode, juce::dontSendNotification);
    }
    splitButton.setVisible(showMonitor);
    splitButton.setEnabled(splitAvailable);
    splitButton.setToggleState(splitAvailable
                                   && readValue(AnaAudioProcessor::frequencySplitViewParameterId, 0.0f) >= 0.5f,
                               juce::dontSendNotification);
    frequencyRangeSlider.setVisible(showZoom);
    magnitudeRangeSlider.setVisible(showZoom);
}

void AnaFrequencyDisplayComponent::syncRangeSliders()
{
    const auto readParameter = [this] (const char* parameterId, const float fallback)
    {
        if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto lowFrequency = readParameter(AnaAudioProcessor::frequencyLowParameterId, 20.0f);
    const auto highFrequency = readParameter(AnaAudioProcessor::frequencyHighParameterId, 20000.0f);
    const auto lowRange = readParameter(AnaAudioProcessor::frequencyRangeLowParameterId, -96.0f);
    const auto highRange = readParameter(AnaAudioProcessor::frequencyRangeHighParameterId, 0.0f);
    const auto* rangesVisible = processor.getParameters().getRawParameterValue(
        AnaAudioProcessor::frequencyRangesVisibleParameterId);
    const auto* cursorReadout = processor.getParameters().getRawParameterValue(
        AnaAudioProcessor::frequencyCursorReadoutParameterId);
    const auto shouldShowRanges = rangesVisible == nullptr || rangesVisible->load(std::memory_order_relaxed) >= 0.5f;

    for (auto* control : std::array<AnaParameterControl*, 4> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl })
        control->setVisible(shouldShowRanges);
    cursorReadoutLabel.setVisible(cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f);
    cursorNoteReadoutLabel.setVisible(cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f);
    cursorVerticalReadoutLabel.setVisible(cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f);

    const juce::ScopedValueSetter<bool> guard(synchronisingRanges, true);
    frequencyRangeSlider.setRange(frequencyToNormalised(std::min(lowFrequency, highFrequency)),
                                  frequencyToNormalised(std::max(lowFrequency, highFrequency)));
    magnitudeRangeSlider.setRange((24.0f - std::max(lowRange, highRange)) / 144.0f,
                                  (24.0f - std::min(lowRange, highRange)) / 144.0f);
}

void AnaFrequencyDisplayComponent::updateFrequencyRangeFromSlider()
{
    const auto lowFrequency = normalisedToFrequency(frequencyRangeSlider.getRangeStart());
    const auto highFrequency = normalisedToFrequency(frequencyRangeSlider.getRangeEnd());
    frequencyLowControl.getSlider().setValue(lowFrequency, juce::sendNotificationSync);
    frequencyHighControl.getSlider().setValue(highFrequency, juce::sendNotificationSync);
    repaint();
}

void AnaFrequencyDisplayComponent::updateMagnitudeRangeFromSlider()
{
    const auto highRange = 24.0f - magnitudeRangeSlider.getRangeStart() * 144.0f;
    const auto lowRange = 24.0f - magnitudeRangeSlider.getRangeEnd() * 144.0f;
    rangeLowControl.getSlider().setValue(lowRange, juce::sendNotificationSync);
    rangeHighControl.getSlider().setValue(highRange, juce::sendNotificationSync);
    repaint();
}

void AnaFrequencyDisplayComponent::drawSpectrum(juce::Graphics& graphics,
                                                 const std::vector<float>& spectrum,
                                                 const int fftSize,
                                                 const double sampleRate,
                                                 const juce::Rectangle<float> plotBounds,
                                                 const juce::Colour colour) const
{
    if (fftSize <= 0 || spectrum.empty())
        return;

    const auto readParameter = [this] (const char* parameterId, const float fallback)
    {
        if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto lowFrequency = std::max(20.0f, readParameter(AnaAudioProcessor::frequencyLowParameterId, 20.0f));
    const auto highFrequency = std::max(lowFrequency + 1.0f,
                                        readParameter(AnaAudioProcessor::frequencyHighParameterId, 20000.0f));
    const auto lowRange = readParameter(AnaAudioProcessor::frequencyRangeLowParameterId, -96.0f);
    const auto highRange = std::max(lowRange + 1.0f,
                                    readParameter(AnaAudioProcessor::frequencyRangeHighParameterId, 0.0f));
    const auto slope = readParameter(AnaAudioProcessor::frequencySlopeParameterId, 0.0f);
    const auto binFrequency = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    const auto lowLog = std::log(lowFrequency);
    const auto highLog = std::log(highFrequency);
    const auto logSpan = std::max(0.0001f, highLog - lowLog);

    juce::Path path;
    juce::Point<float> firstPoint;
    juce::Point<float> lastPoint;
    bool hasPoint = false;
    auto activeColumn = -1;
    auto accumulatedPower = 0.0f;
    auto columnSampleCount = 0;
    const auto columnCount = std::max(1, static_cast<int>(std::ceil(plotBounds.getWidth())));
    const auto flushColumn = [&]
    {
        if (columnSampleCount == 0)
            return;

        const auto averagedDecibels = juce::Decibels::gainToDecibels(
            std::sqrt(accumulatedPower / static_cast<float>(columnSampleCount)), lowRange);
        const auto normalisedLevel = juce::jlimit(0.0f, 1.0f,
            (averagedDecibels - lowRange) / (highRange - lowRange));
        const auto point = juce::Point<float>(plotBounds.getX() + static_cast<float>(activeColumn) + 0.5f,
                                              plotBounds.getBottom() - normalisedLevel * plotBounds.getHeight());
        if (! hasPoint)
        {
            firstPoint = point.withX(plotBounds.getX());
            path.startNewSubPath(firstPoint);
            hasPoint = true;
        }
        else
        {
            path.lineTo(point);
        }
        lastPoint = point;
        accumulatedPower = 0.0f;
        columnSampleCount = 0;
    };
    for (size_t bin = 1; bin < spectrum.size(); ++bin)
    {
        const auto frequency = static_cast<float>(bin) * binFrequency;
        if (frequency < lowFrequency || frequency > highFrequency)
            continue;

        const auto normalisedFrequency = (std::log(frequency) - lowLog) / logSpan;
        const auto slopedValue = spectrum[bin] + slope * normalisedFrequency;
        const auto column = juce::jlimit(0, columnCount - 1,
            static_cast<int>(std::floor(normalisedFrequency * plotBounds.getWidth())));
        if (activeColumn >= 0 && column != activeColumn)
            flushColumn();
        activeColumn = column;
        const auto gain = juce::Decibels::decibelsToGain(slopedValue);
        accumulatedPower += gain * gain;
        ++columnSampleCount;
    }

    flushColumn();

    if (! hasPoint)
        return;

    const auto* filled = processor.getParameters().getRawParameterValue(
        AnaAudioProcessor::frequencyFilledDisplayParameterId);
    if (filled != nullptr && filled->load(std::memory_order_relaxed) >= 0.5f)
    {
        auto fillPath = path;
        fillPath.lineTo(lastPoint.x, plotBounds.getBottom());
        fillPath.lineTo(firstPoint.x, plotBounds.getBottom());
        fillPath.closeSubPath();
        graphics.setColour(colour.withAlpha(0.28f));
        graphics.fillPath(fillPath);
    }

    graphics.setColour(colour);
    const auto* antiAlias = processor.getParameters().getRawParameterValue(
        AnaAudioProcessor::frequencyAntiAliasParameterId);
    if (antiAlias == nullptr || antiAlias->load(std::memory_order_relaxed) >= 0.5f)
    {
        graphics.strokePath(path, juce::PathStrokeType(1.0f));
    }
    else
    {
        juce::PathFlatteningIterator iterator(path, juce::AffineTransform(), 0.25f);
        while (iterator.next())
            graphics.drawLine(static_cast<float>(juce::roundToInt(iterator.x1)),
                              static_cast<float>(juce::roundToInt(iterator.y1)),
                              static_cast<float>(juce::roundToInt(iterator.x2)),
                              static_cast<float>(juce::roundToInt(iterator.y2)), 1.0f);
    }
}

float AnaFrequencyDisplayComponent::frequencyToNormalised(const float frequency) noexcept
{
    return juce::jlimit(0.0f, 1.0f, std::log(std::max(20.0f, frequency) / 20.0f) / std::log(1000.0f));
}

float AnaFrequencyDisplayComponent::normalisedToFrequency(const float normalised) noexcept
{
    return 20.0f * std::pow(1000.0f, juce::jlimit(0.0f, 1.0f, normalised));
}

AnaCorrelationDisplayComponent::AnaCorrelationDisplayComponent(AnaAudioProcessor& processorRef)
    : processor(processorRef),
      frequencyLowControl(processorRef.getParameters(), AnaAudioProcessor::correlationLowParameterId,
                          "LOW", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      frequencyHighControl(processorRef.getParameters(), AnaAudioProcessor::correlationHighParameterId,
                           "HIGH", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      rangeLowControl(processorRef.getParameters(), AnaAudioProcessor::correlationRangeLowParameterId,
                      "LOW", [] (const double value) { return juce::String(value, 2); }),
      rangeHighControl(processorRef.getParameters(), AnaAudioProcessor::correlationRangeHighParameterId,
                       "HIGH", [] (const double value) { return juce::String(value, 2); })
{
    for (auto* component : std::array<juce::Component*, 9> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl,
             &cursorReadoutLabel, &cursorVerticalReadoutLabel, &phaseModeButton, &amplitudeModeButton,
             &frequencyRangeSlider })
        addAndMakeVisible(*component);
    addAndMakeVisible(correlationRangeSlider);

    for (auto* control : std::array<AnaParameterControl*, 4> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl })
    {
        control->setCompact(true);
        control->onValueChanged = [this] { repaint(); };
    }
    cursorReadoutLabel.setFont(ana::ui::makeFont());
    cursorReadoutLabel.setJustificationType(juce::Justification::centred);
    cursorReadoutLabel.setText("---", juce::dontSendNotification);
    cursorReadoutLabel.setColour(juce::Label::textColourId, ana::ui::white);
    cursorReadoutLabel.setColour(juce::Label::backgroundColourId, ana::ui::field);
    cursorReadoutLabel.setColour(juce::Label::outlineColourId, ana::ui::grey500);
    cursorReadoutLabel.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    cursorReadoutLabel.setInterceptsMouseClicks(false, false);

    cursorVerticalReadoutLabel.setFont(ana::ui::makeFont());
    cursorVerticalReadoutLabel.setJustificationType(juce::Justification::centred);
    cursorVerticalReadoutLabel.setText("---", juce::dontSendNotification);
    cursorVerticalReadoutLabel.setColour(juce::Label::textColourId, ana::ui::white);
    cursorVerticalReadoutLabel.setColour(juce::Label::backgroundColourId, ana::ui::field);
    cursorVerticalReadoutLabel.setColour(juce::Label::outlineColourId, ana::ui::grey500);
    cursorVerticalReadoutLabel.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    cursorVerticalReadoutLabel.setInterceptsMouseClicks(false, false);

    phaseModeButton.onClick = [this] { setCorrelationMode(0); };
    amplitudeModeButton.onClick = [this] { setCorrelationMode(1); };
    frequencyRangeSlider.onRangeChanged = [this]
    {
        if (! synchronisingRanges)
            updateFrequencyRangeFromSlider();
    };
    correlationRangeSlider.onRangeChanged = [this]
    {
        if (! synchronisingRanges)
            updateCorrelationRangeFromSlider();
    };
    startTimerHz(30);
}

void AnaCorrelationDisplayComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);
    const auto plotBounds = getPlotBounds();
    if (plotBounds.isEmpty())
        return;

    int fftSize = 0;
    const auto* modeParameter = processor.getParameters().getRawParameterValue(
        AnaAudioProcessor::correlationModeParameterId);
    const auto mode = modeParameter != nullptr && modeParameter->load(std::memory_order_relaxed) >= 0.5f
        ? ana::corr::StereoCorrelationProcessor::Mode::amplitude
        : ana::corr::StereoCorrelationProcessor::Mode::phase;
    const auto displayType = [this] (const char* parameterId)
    {
        const auto* value = processor.getParameters().getRawParameterValue(parameterId);
        return value != nullptr && value->load(std::memory_order_relaxed) >= 0.5f
            ? ana::corr::StereoCorrelationProcessor::DisplayType::maximum
            : ana::corr::StereoCorrelationProcessor::DisplayType::realtimeAverage;
    };
    const auto* displayedSpectrum = &processor.getCorrelationSpectrum();
    auto sampleRate = displayedSpectrum->getSampleRate();
    if (processor.isOfflineMode())
    {
        if (const auto snapshot = processor.getOfflineScopeSnapshot();
            snapshot != nullptr && snapshot->correlationSpectrum != nullptr)
        {
            displayedSpectrum = snapshot->correlationSpectrum.get();
            sampleRate = snapshot->correlationSampleRate;
        }
    }

    displayedSpectrum->copyCorrelation(
        mode, displayType(AnaAudioProcessor::correlationFirstSpectrumTypeParameterId),
        primaryCorrelation, fftSize);
    if (fftSize <= 0 || primaryCorrelation.empty())
        return;

    const auto readParameter = [this] (const char* parameterId, const float fallback)
    {
        if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto lowFrequency = std::max(20.0f, readParameter(AnaAudioProcessor::correlationLowParameterId, 20.0f));
    const auto highFrequency = std::max(lowFrequency + 1.0f,
                                        readParameter(AnaAudioProcessor::correlationHighParameterId, 20000.0f));
    const auto lowRange = readParameter(AnaAudioProcessor::correlationRangeLowParameterId, -1.0f);
    const auto highRange = std::max(lowRange + 0.01f,
                                    readParameter(AnaAudioProcessor::correlationRangeHighParameterId, 1.0f));
    const auto zeroY = plotBounds.getBottom() - juce::jlimit(0.0f, 1.0f,
        (0.0f - lowRange) / (highRange - lowRange)) * plotBounds.getHeight();
    drawCorrelation(graphics, primaryCorrelation, plotBounds, lowFrequency, highFrequency,
                    lowRange, highRange, sampleRate, fftSize, ana::ui::white);

    const auto* secondSpectrum = processor.getParameters().getRawParameterValue(
        AnaAudioProcessor::correlationSecondSpectrumParameterId);
    if (secondSpectrum != nullptr && secondSpectrum->load(std::memory_order_relaxed) >= 0.5f)
    {
        int secondaryFftSize = 0;
        displayedSpectrum->copyCorrelation(
            mode, displayType(AnaAudioProcessor::correlationSecondSpectrumTypeParameterId),
            secondaryCorrelation, secondaryFftSize);
        if (secondaryFftSize == fftSize)
            drawCorrelation(graphics, secondaryCorrelation, plotBounds, lowFrequency, highFrequency,
                            lowRange, highRange, sampleRate, fftSize, ana::ui::accent);
    }

    graphics.setColour(ana::ui::grey500);
    graphics.fillRect(plotBounds.getX(), zeroY, plotBounds.getWidth(), 1.0f);

    const auto* cursorReadout = processor.getParameters().getRawParameterValue(
        AnaAudioProcessor::correlationCursorReadoutParameterId);
    if (cursorInside && (cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f))
    {
        graphics.setColour(ana::ui::white);
        graphics.drawLine(cursorPosition.x, plotBounds.getY(), cursorPosition.x, plotBounds.getBottom(), 0.5f);
        graphics.drawLine(plotBounds.getX(), cursorPosition.y, plotBounds.getRight(), cursorPosition.y, 0.5f);
    }
}

void AnaCorrelationDisplayComponent::drawCorrelation(juce::Graphics& graphics,
                                                      const std::vector<float>& values,
                                                      const juce::Rectangle<float> plotBounds,
                                                      const float lowFrequency,
                                                      const float highFrequency,
                                                      const float lowRange,
                                                      const float highRange,
                                                      const double sampleRate,
                                                      const int fftSize,
                                                      const juce::Colour colour)
{
    if (fftSize <= 0 || values.empty())
        return;

    const auto binFrequency = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    const auto lowLog = std::log(lowFrequency);
    const auto logSpan = std::max(0.0001f, std::log(highFrequency) - lowLog);
    const auto columnCount = std::max(1, static_cast<int>(std::ceil(plotBounds.getWidth())));
    correlationColumns.assign(static_cast<size_t>(columnCount), 0.0f);
    correlationColumnCounts.assign(static_cast<size_t>(columnCount), 0);
    for (size_t bin = 1; bin < values.size(); ++bin)
    {
        const auto frequency = static_cast<float>(bin) * binFrequency;
        if (frequency < lowFrequency || frequency > highFrequency)
            continue;
        const auto normalisedFrequency = (std::log(frequency) - lowLog) / logSpan;
        const auto column = juce::jlimit(0, columnCount - 1,
            static_cast<int>(std::floor(normalisedFrequency * plotBounds.getWidth())));
        correlationColumns[static_cast<size_t>(column)] += values[bin];
        ++correlationColumnCounts[static_cast<size_t>(column)];
    }

    const auto* smoothingParameter = processor.getParameters().getRawParameterValue(
        AnaAudioProcessor::correlationSmoothingParameterId);
    const auto smoothing = smoothingParameter != nullptr
        ? smoothingParameter->load(std::memory_order_relaxed) : 30.0f;
    const auto smoothingRadius = juce::jlimit(0, 48, juce::roundToInt(smoothing * 0.48f));
    juce::Path path;
    juce::Point<float> firstPoint;
    juce::Point<float> lastPoint;
    auto hasPoint = false;
    for (int column = 0; column < columnCount; ++column)
    {
        auto summedCorrelation = 0.0f;
        auto summedWeight = 0.0f;
        const auto firstColumn = std::max(0, column - smoothingRadius);
        const auto lastColumn = std::min(columnCount - 1, column + smoothingRadius);
        for (int neighbour = firstColumn; neighbour <= lastColumn; ++neighbour)
        {
            const auto count = correlationColumnCounts[static_cast<size_t>(neighbour)];
            if (count == 0)
                continue;

            const auto distance = static_cast<float>(std::abs(neighbour - column));
            const auto sigma = std::max(0.5f, static_cast<float>(smoothingRadius) * 0.5f);
            const auto weight = std::exp(-0.5f * distance * distance / (sigma * sigma));
            summedCorrelation += correlationColumns[static_cast<size_t>(neighbour)] * weight;
            summedWeight += static_cast<float>(count) * weight;
        }
        if (summedWeight <= 0.0f)
            continue;

        const auto value = summedCorrelation / summedWeight;
        const auto y = plotBounds.getBottom() - juce::jlimit(0.0f, 1.0f,
            (value - lowRange) / (highRange - lowRange)) * plotBounds.getHeight();
        const auto point = juce::Point<float>(plotBounds.getX() + static_cast<float>(column) + 0.5f, y);
        if (! hasPoint)
        {
            firstPoint = point.withX(plotBounds.getX());
            path.startNewSubPath(firstPoint);
            hasPoint = true;
        }
        else
        {
            path.lineTo(point);
        }
        lastPoint = point;
    }

    if (! hasPoint)
        return;

    const auto* filled = processor.getParameters().getRawParameterValue(
        AnaAudioProcessor::correlationFilledDisplayParameterId);
    if (filled == nullptr || filled->load(std::memory_order_relaxed) >= 0.5f)
    {
        auto fillPath = path;
        fillPath.lineTo(lastPoint.x, plotBounds.getBottom());
        fillPath.lineTo(firstPoint.x, plotBounds.getBottom());
        fillPath.closeSubPath();
        graphics.setColour(colour.withAlpha(0.28f));
        graphics.fillPath(fillPath);
    }

    graphics.setColour(colour);
    graphics.strokePath(path, juce::PathStrokeType(1.0f));
}

void AnaCorrelationDisplayComponent::resized()
{
    const auto plotBounds = getPlotBounds().toNearestInt();
    const auto readoutWidth = std::min(110, std::max(64, plotBounds.getWidth() / 5));
    const auto readoutY = plotBounds.getBottom() - ana::ui::controlHeight;
    const auto graphRight = plotBounds.getRight();
    auto topArea = juce::Rectangle<int>(plotBounds.getX(), plotBounds.getY(),
                                        plotBounds.getWidth(), ana::ui::controlHeight);
    auto topReadouts = topArea.removeFromRight(readoutWidth * 2 + ana::ui::gap.pixels());
    ana::ui::gap.removeFromRight(topArea);
    ana::ui::FixedGapRow topControls(topArea);
    cursorReadoutLabel.setBounds(topControls.takeLeft(readoutWidth));
    phaseModeButton.setBounds(topControls.takeLeft(70));
    amplitudeModeButton.setBounds(topControls.takeLeft(108));
    ana::ui::FixedGapRow topReadoutControls(topReadouts);
    cursorVerticalReadoutLabel.setBounds(topReadoutControls.takeLeft(readoutWidth));
    rangeHighControl.setBounds(topReadoutControls.takeLeft(readoutWidth));
    frequencyLowControl.setBounds(plotBounds.getX(), readoutY, readoutWidth, ana::ui::controlHeight);
    frequencyHighControl.setBounds(graphRight - readoutWidth * 2 - ana::ui::gap.pixels(), readoutY,
                                   readoutWidth, ana::ui::controlHeight);
    rangeLowControl.setBounds(graphRight - readoutWidth, readoutY, readoutWidth, ana::ui::controlHeight);
    frequencyRangeSlider.setBounds(0, getHeight() - bandRangeSliderHeight, getWidth(), bandRangeSliderHeight);
    correlationRangeSlider.setBounds(getWidth() - bandRangeSliderHeight, 0, bandRangeSliderHeight,
                                     getHeight() - bandRangeSliderHeight - ana::ui::gap.pixels());
    syncRangeSliders();
    refreshControls();
}

void AnaCorrelationDisplayComponent::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent == this)
        processor.clearCorrelationSpectrum();
}

void AnaCorrelationDisplayComponent::mouseMove(const juce::MouseEvent& event)
{
    const auto plotBounds = getPlotBounds();
    const auto nextCursorInside = plotBounds.contains(event.position);
    if (cursorInside == nextCursorInside && cursorPosition == event.position)
        return;
    cursorInside = nextCursorInside;
    cursorPosition = event.position;
    if (cursorInside)
    {
        const auto* low = processor.getParameters().getRawParameterValue(AnaAudioProcessor::correlationLowParameterId);
        const auto* high = processor.getParameters().getRawParameterValue(AnaAudioProcessor::correlationHighParameterId);
        const auto lowFrequency = std::max(20.0f, low != nullptr ? low->load(std::memory_order_relaxed) : 20.0f);
        const auto highFrequency = std::max(lowFrequency + 1.0f,
            high != nullptr ? high->load(std::memory_order_relaxed) : 20000.0f);
        const auto normalisedX = juce::jlimit(0.0f, 1.0f,
            (cursorPosition.x - plotBounds.getX()) / plotBounds.getWidth());
        lastCursorFrequency = lowFrequency * std::pow(highFrequency / lowFrequency, normalisedX);
        cursorReadoutLabel.setText(juce::String(lastCursorFrequency, 1), juce::dontSendNotification);
        const auto* lowRangeParameter = processor.getParameters().getRawParameterValue(
            AnaAudioProcessor::correlationRangeLowParameterId);
        const auto* highRangeParameter = processor.getParameters().getRawParameterValue(
            AnaAudioProcessor::correlationRangeHighParameterId);
        const auto lowRange = lowRangeParameter != nullptr
            ? lowRangeParameter->load(std::memory_order_relaxed) : -1.0f;
        const auto highRange = std::max(lowRange + 0.01f,
            highRangeParameter != nullptr ? highRangeParameter->load(std::memory_order_relaxed) : 1.0f);
        const auto normalisedY = juce::jlimit(0.0f, 1.0f,
            (cursorPosition.y - plotBounds.getY()) / plotBounds.getHeight());
        cursorVerticalReadoutLabel.setText(juce::String(highRange - normalisedY * (highRange - lowRange), 2),
                                           juce::dontSendNotification);
    }
    repaint();
}

void AnaCorrelationDisplayComponent::mouseExit(const juce::MouseEvent&)
{
    cursorInside = false;
    repaint();
}

void AnaCorrelationDisplayComponent::timerCallback()
{
    syncRangeSliders();
    refreshControls();
    if (processor.isOfflineMode())
    {
        if (offlineRenderPending)
        {
            offlineRenderPending = false;
            repaint();
            if (onOfflineUpdateStatus)
                onOfflineUpdateStatus("UPDATED " + juce::Time::getCurrentTime().formatted("%d.%m.%Y %H:%M:%S"));
        }
        if (const auto snapshot = processor.getOfflineScopeSnapshot();
            snapshot != nullptr && displayedOfflineRevision != snapshot->revision)
        {
            displayedOfflineRevision = snapshot->revision;
            repaint();
        }
        return;
    }

    const auto revision = processor.getCorrelationSpectrum().getRevision();
    if (displayedRevision != revision)
    {
        displayedRevision = revision;
        repaint();
    }
}

void AnaCorrelationDisplayComponent::setCorrelationMode(const int mode)
{
    if (auto* parameter = processor.getParameters().getParameter(AnaAudioProcessor::correlationModeParameterId))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(mode)));

    if (! processor.isOfflineMode())
        return;

    offlineRenderPending = true;
    if (onOfflineUpdateStatus)
        onOfflineUpdateStatus("UPDATING...");
}

void AnaCorrelationDisplayComponent::syncRangeSliders()
{
    const auto read = [this] (const char* id, const float fallback)
    {
        if (const auto* value = processor.getParameters().getRawParameterValue(id))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto lowFrequency = read(AnaAudioProcessor::correlationLowParameterId, 20.0f);
    const auto highFrequency = read(AnaAudioProcessor::correlationHighParameterId, 20000.0f);
    const auto lowRange = read(AnaAudioProcessor::correlationRangeLowParameterId, -1.0f);
    const auto highRange = read(AnaAudioProcessor::correlationRangeHighParameterId, 1.0f);
    const auto* ranges = processor.getParameters().getRawParameterValue(AnaAudioProcessor::correlationRangesVisibleParameterId);
    const auto* cursor = processor.getParameters().getRawParameterValue(AnaAudioProcessor::correlationCursorReadoutParameterId);
    const auto showRanges = ranges == nullptr || ranges->load(std::memory_order_relaxed) >= 0.5f;
    for (auto* control : std::array<AnaParameterControl*, 4> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl })
        control->setVisible(showRanges);
    cursorReadoutLabel.setVisible(cursor == nullptr || cursor->load(std::memory_order_relaxed) >= 0.5f);
    cursorVerticalReadoutLabel.setVisible(cursor == nullptr || cursor->load(std::memory_order_relaxed) >= 0.5f);
    const juce::ScopedValueSetter<bool> guard(synchronisingRanges, true);
    frequencyRangeSlider.setRange(frequencyToNormalised(std::min(lowFrequency, highFrequency)),
                                  frequencyToNormalised(std::max(lowFrequency, highFrequency)));
    correlationRangeSlider.setRange((1.0f - std::max(lowRange, highRange)) * 0.5f,
                                    (1.0f - std::min(lowRange, highRange)) * 0.5f);
}

void AnaCorrelationDisplayComponent::refreshControls()
{
    const auto* zoom = processor.getParameters().getRawParameterValue(
        AnaAudioProcessor::correlationZoomControlsParameterId);
    const auto* mode = processor.getParameters().getRawParameterValue(AnaAudioProcessor::correlationModeParameterId);
    const auto amplitude = mode != nullptr && mode->load(std::memory_order_relaxed) >= 0.5f;
    phaseModeButton.setVisible(true);
    amplitudeModeButton.setVisible(true);
    phaseModeButton.setToggleState(! amplitude, juce::dontSendNotification);
    amplitudeModeButton.setToggleState(amplitude, juce::dontSendNotification);
    const auto showZoom = zoom == nullptr || zoom->load(std::memory_order_relaxed) >= 0.5f;
    frequencyRangeSlider.setVisible(showZoom);
    correlationRangeSlider.setVisible(showZoom);
}

void AnaCorrelationDisplayComponent::updateFrequencyRangeFromSlider()
{
    frequencyLowControl.getSlider().setValue(normalisedToFrequency(frequencyRangeSlider.getRangeStart()),
                                              juce::sendNotificationSync);
    frequencyHighControl.getSlider().setValue(normalisedToFrequency(frequencyRangeSlider.getRangeEnd()),
                                               juce::sendNotificationSync);
}

void AnaCorrelationDisplayComponent::updateCorrelationRangeFromSlider()
{
    rangeHighControl.getSlider().setValue(1.0f - correlationRangeSlider.getRangeStart() * 2.0f,
                                          juce::sendNotificationSync);
    rangeLowControl.getSlider().setValue(1.0f - correlationRangeSlider.getRangeEnd() * 2.0f,
                                         juce::sendNotificationSync);
}

juce::Rectangle<float> AnaCorrelationDisplayComponent::getPlotBounds() const noexcept
{
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromBottom(static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels()));
    bounds.removeFromRight(static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels()));
    return bounds;
}

float AnaCorrelationDisplayComponent::frequencyToNormalised(const float frequency) noexcept
{
    return juce::jlimit(0.0f, 1.0f, std::log(std::max(20.0f, frequency) / 20.0f) / std::log(1000.0f));
}

float AnaCorrelationDisplayComponent::normalisedToFrequency(const float normalised) noexcept
{
    return 20.0f * std::pow(1000.0f, juce::jlimit(0.0f, 1.0f, normalised));
}

AnaMultibandScopeComponent::AnaMultibandScopeComponent(AnaAudioProcessor& processorRef)
    : processor(processorRef)
{
    setOpaque(false);
    bandHeightWeights.fill(1.0f);
    singleViewBand = processor.getScopeSingleViewBand();
    fullSourceView = processor.isScopeFullSourceView();

    for (size_t bandIndex = 0; bandIndex < bandModeButtons.size(); ++bandIndex)
    {
        for (size_t modeIndex = 0; modeIndex < bandModeButtons[bandIndex].size(); ++modeIndex)
        {
            auto button = std::make_unique<AnaScopeButton>(scopeModeButtonNames[modeIndex]);
            button->onClick = [this, bandIndex, modeIndex]
            {
                const auto mode = scopeModeButtonModes[modeIndex];

                if (processor.getScopeChannelMode(bandIndex) != mode)
                {
                    processor.setScopeChannelMode(bandIndex, mode);
                    processor.setScopeBandNormalized(bandIndex, false);
                }

                refreshBandModeButtons();
            };
            addAndMakeVisible(*button);
            bandModeButtons[bandIndex][modeIndex] = std::move(button);
        }

        auto clearButton = std::make_unique<AnaScopeButton>("CLEAR");
        clearButton->onClick = [this, bandIndex] { clearBandHistory(bandIndex); };
        addAndMakeVisible(*clearButton);
        bandClearButtons[bandIndex] = std::move(clearButton);

        auto singleViewButton = std::make_unique<AnaScopeButton>("SVIEW");
        singleViewButton->onClick = [this, bandIndex]
        {
            if (fullSourceView)
            {
                fullSourceView = false;
                processor.setScopeFullSourceView(false);
            }
            singleViewBand = singleViewBand == static_cast<int>(bandIndex)
                ? -1
                : static_cast<int>(bandIndex);
            processor.setScopeSingleViewBand(singleViewBand);
            draggedBandSeparator = -1;
            refreshBandModeButtons();
            repaint();
        };
        addAndMakeVisible(*singleViewButton);
        bandSingleViewButtons[bandIndex] = std::move(singleViewButton);

        auto zoomSlider = std::make_unique<juce::Slider>();
        zoomSlider->setLookAndFeel(&bandZoomLookAndFeel);
        zoomSlider->setSliderStyle(juce::Slider::LinearBarVertical);
        zoomSlider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        zoomSlider->setRange(-48.0, 96.0, 0.1);
        zoomSlider->setValue(processor.getScopeVerticalZoomDecibels(bandIndex), juce::dontSendNotification);
        zoomSlider->setSliderSnapsToMousePosition(false);
        zoomSlider->setScrollWheelEnabled(true);
        zoomSlider->setWantsKeyboardFocus(false);
        zoomSlider->setDoubleClickReturnValue(true, 0.0);
        zoomSlider->onValueChange = [this, bandIndex, slider = zoomSlider.get()]
        {
            if (! updatingBandControls)
            {
                processor.setScopeBandNormalized(bandIndex, false);
                processor.setScopeVerticalZoomDecibels(bandIndex, static_cast<float>(slider->getValue()));
                refreshBandModeButtons();
                repaint();
            }
        };
        addAndMakeVisible(*zoomSlider);
        bandZoomSliders[bandIndex] = std::move(zoomSlider);

        auto zoomValueLabel = std::make_unique<juce::Label>();
        zoomValueLabel->setFont(ana::ui::makeFont());
        zoomValueLabel->setJustificationType(juce::Justification::centred);
        zoomValueLabel->setColour(juce::Label::textColourId, ana::ui::white);
        zoomValueLabel->setColour(juce::Label::backgroundColourId, ana::ui::field);
        zoomValueLabel->setColour(juce::Label::outlineColourId, ana::ui::grey500);
        zoomValueLabel->setBorderSize(juce::BorderSize<int>(1));
        zoomValueLabel->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*zoomValueLabel);
        bandZoomValueLabels[bandIndex] = std::move(zoomValueLabel);

        auto normalizeButton = std::make_unique<AnaScopeButton>("N");
        normalizeButton->onClick = [this, bandIndex] { normalizeBandWithZoom(bandIndex); };
        addAndMakeVisible(*normalizeButton);
        bandNormalizeButtons[bandIndex] = std::move(normalizeButton);

        auto rangeSlider = std::make_unique<AnaRangeSlider>();
        rangeSlider->onRangeChanged = [this] { repaint(); };
        addAndMakeVisible(*rangeSlider);
        bandRangeSliders[bandIndex] = std::move(rangeSlider);

    }

    resetHistory();
    refreshBandModeButtons();
    startTimerHz(60);
}

void AnaMultibandScopeComponent::setFrozen(const bool shouldFreeze)
{
    if (frozen == shouldFreeze)
        return;

    frozen = shouldFreeze;

    if (! frozen)
    {
        readCursor = processor.getMultibandScope().getWriteCursor();
        columnSampleProgress = 0.0;
        resetColumnAccumulator();
    }
}

void AnaMultibandScopeComponent::clearHistory()
{
    if (showingOfflineSnapshot)
    {
        displayedOfflineSnapshot.reset();
        clearedBands.fill(true);
        widebandCleared = true;

        for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
            processor.setScopeBandNormalized(bandIndex, false);

        refreshBandModeButtons();
        repaint();
        return;
    }

    resetHistory();
}

void AnaMultibandScopeComponent::refreshWaveform()
{
    if (processor.isOfflineMode())
    {
        offlineAnalysisColumnCount = getWaveformColumnCount();
        showingOfflineSnapshot = true;
        offlineSnapshotRevision = 0;
        displayedOfflineSnapshot.reset();
        clearedBands.fill(false);

        for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
            processor.setScopeBandNormalized(bandIndex, false);

        if (onOfflineUpdateStatus)
            onOfflineUpdateStatus("UPDATING...");
        processor.requestOfflineAnalysis(offlineAnalysisColumnCount, true);
    }

    repaint();
}

void AnaMultibandScopeComponent::equalizeBandHeights()
{
    bandHeightWeights.fill(1.0f);
    draggedBandSeparator = -1;
    refreshBandModeButtons();
    repaint();
}

void AnaMultibandScopeComponent::refreshDisplaySettings()
{
    if (! showingOfflineSnapshot && ! historyContainsRecordedData)
        resizeHistory(getWaveformColumnCount());

    refreshBandModeButtons();
    repaint();
}

void AnaMultibandScopeComponent::setFullSourceView(const bool shouldShowFullSource)
{
    if (fullSourceView == shouldShowFullSource)
        return;

    fullSourceView = shouldShowFullSource;
    if (fullSourceView)
    {
        singleViewBand = -1;
        processor.setScopeSingleViewBand(-1);
    }

    draggedBandSeparator = -1;
    refreshBandModeButtons();
    repaint();
}

void AnaMultibandScopeComponent::timerCallback()
{
    const auto timeMilliseconds = processor.getScopeTimeMilliseconds();
    const auto waveformColumnCount = getWaveformColumnCount();
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    std::array<ana::ScopeChannelMode, ana::MultibandScope::numBands> currentModes;
    auto channelModeChanged = false;

    if (fullSourceView != processor.isScopeFullSourceView())
        setFullSourceView(processor.isScopeFullSourceView());

    for (size_t bandIndex = 0; bandIndex < currentModes.size(); ++bandIndex)
    {
        currentModes[bandIndex] = processor.getScopeChannelMode(bandIndex);

        if (historyChannelModes[bandIndex] != currentModes[bandIndex])
        {
            historyChannelModes[bandIndex] = currentModes[bandIndex];
            processor.setScopeBandNormalized(bandIndex, false);
            channelModeChanged = true;
        }
    }

    refreshBandModeButtons();

    const auto shouldShowOffline = processor.isOfflineMode();

    if (shouldShowOffline)
    {
        if (! showingOfflineSnapshot)
        {
            offlineAnalysisColumnCount = waveformColumnCount;
            showingOfflineSnapshot = true;
            offlineSnapshotRevision = 0;
            displayedOfflineSnapshot.reset();
            clearedBands.fill(false);
            widebandCleared = false;

            for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
                processor.setScopeBandNormalized(bandIndex, false);

            if (onOfflineUpdateStatus)
                onOfflineUpdateStatus("UPDATING...");
            processor.requestOfflineAnalysis(offlineAnalysisColumnCount, true);
            repaint();
        }

        if (offlineAnalysisColumnCount == 0)
            offlineAnalysisColumnCount = waveformColumnCount;

        processor.requestOfflineAnalysis(offlineAnalysisColumnCount);

        if (const auto snapshot = processor.getOfflineScopeSnapshot())
        {
            if (! showingOfflineSnapshot
                || offlineSnapshotRevision != snapshot->revision
                || channelModeChanged
                || historyBandCount != activeBandCount)
                renderOfflineSnapshot(snapshot);
        }

        return;
    }

    if (showingOfflineSnapshot)
    {
        showingOfflineSnapshot = false;
        offlineAnalysisColumnCount = 0;
        offlineSnapshotRevision = 0;
        displayedOfflineSnapshot.reset();
        widebandCleared = false;

        for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
            processor.setScopeBandNormalized(bandIndex, false);

        if (onOfflineUpdateStatus)
            onOfflineUpdateStatus({});

        readCursor = processor.getMultibandScope().getWriteCursor();
        columnSampleProgress = 0.0;
        resetColumnAccumulator();
        repaint();
        return;
    }

    if (frozen)
        return;

    if (juce::Time::getMillisecondCounterHiRes() < realtimeResumeTimeMilliseconds)
    {
        readCursor = processor.getMultibandScope().getWriteCursor();
        columnSampleProgress = 0.0;
        resetColumnAccumulator();
        return;
    }

    if (std::abs(historyTimeMilliseconds - timeMilliseconds) > 0.001
        || historyBandCount != activeBandCount)
    {
        resetHistory();
        return;
    }

    if (getWidth() <= 0 || historyEnvelopes.front().front().minimums.empty())
        return;

    auto& scope = processor.getMultibandScope();
    scope.copySince(incomingSamples, readCursor);

    const auto sampleCount = incomingSamples.front().front().size();
    if (sampleCount == 0)
        return;

    const auto visibleHistoryColumns = std::max<size_t>(
        1, historyEnvelopes.front().front().minimums.size());
    const auto samplesPerColumn = std::max(
        0.001,
        scope.getSampleRate() * timeMilliseconds * 0.001
            / static_cast<double>(visibleHistoryColumns));
    auto appendedColumn = false;

    for (size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        const auto widebandModes = getRealtimeWidebandModeSamples(sampleIndex);
        for (size_t modeIndex = 0; modeIndex < widebandModes.size(); ++modeIndex)
        {
            widebandColumnMinimums[modeIndex] = std::min(
                widebandColumnMinimums[modeIndex], widebandModes[modeIndex]);
            widebandColumnMaximums[modeIndex] = std::max(
                widebandColumnMaximums[modeIndex], widebandModes[modeIndex]);
        }

        for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
        {
            const auto modes = getRealtimeModeSamples(bandIndex, sampleIndex);

            for (size_t modeIndex = 0; modeIndex < modes.size(); ++modeIndex)
            {
                columnMinimums[bandIndex][modeIndex] = std::min(
                    columnMinimums[bandIndex][modeIndex], modes[modeIndex]);
                columnMaximums[bandIndex][modeIndex] = std::max(
                    columnMaximums[bandIndex][modeIndex], modes[modeIndex]);
            }
        }

        columnSampleProgress += 1.0;
        const auto columnsToAppend = static_cast<size_t>(columnSampleProgress / samplesPerColumn);

        if (columnsToAppend == 0)
            continue;

        for (size_t columnIndex = 0; columnIndex < columnsToAppend; ++columnIndex)
            appendHistoryColumn(activeBandCount);

        columnSampleProgress -= static_cast<double>(columnsToAppend) * samplesPerColumn;
        resetColumnAccumulator();
        appendedColumn = true;
    }

    if (appendedColumn)
        repaint();
}

void AnaMultibandScopeComponent::paint(juce::Graphics& graphics)
{
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    const auto filledStyle = processor.isScopeFilledStyle();
    graphics.setColour(ana::ui::white.withAlpha(processor.getScopeOpacity()));

    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
    {
        if ((fullSourceView && widebandCleared)
            || (! fullSourceView && clearedBands[bandIndex]))
            continue;

        const auto laneBounds = getBandBounds(bandIndex, activeBandCount);

        if (laneBounds.isEmpty())
            continue;

        auto waveformBounds = laneBounds;
        const auto zoomSlidersVisible = shouldShowZoomSliders(bandIndex, activeBandCount);
        if (zoomSlidersVisible)
        {
            waveformBounds.setBottom(std::max(waveformBounds.getY() + 1.0f,
                                              waveformBounds.getBottom()
                                                  - static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels())));
            waveformBounds.setRight(std::max(
                waveformBounds.getX() + 1.0f,
                static_cast<float>(getWidth() - waveformRightInset)));
        }

        const auto rangeStart = bandRangeSliders[bandIndex]->getRangeStart();
        const auto rangeEnd = bandRangeSliders[bandIndex]->getRangeEnd();
        const auto displayedModes = getDisplayedModes(processor.getScopeChannelMode(bandIndex));

        for (size_t displayIndex = 0; displayIndex < displayedModes.count; ++displayIndex)
        {
            auto displayBounds = waveformBounds;

            if (displayedModes.count == 2)
            {
                const auto halfHeight = waveformBounds.getHeight() * 0.5f;
                displayBounds.setY(waveformBounds.getY() + halfHeight * static_cast<float>(displayIndex));
                displayBounds.setHeight(displayIndex == 0
                    ? halfHeight
                    : waveformBounds.getBottom() - displayBounds.getY());
            }

            const auto modeIndex = displayedModes.indices[displayIndex];
            const auto* envelope = showingOfflineSnapshot && displayedOfflineSnapshot != nullptr
                ? (fullSourceView
                    ? &displayedOfflineSnapshot->wideband[modeIndex]
                    : &displayedOfflineSnapshot->bands[bandIndex][modeIndex])
                : ! showingOfflineSnapshot
                    ? (fullSourceView
                        ? &widebandHistoryEnvelopes[modeIndex]
                        : &historyEnvelopes[bandIndex][modeIndex])
                    : nullptr;

            if (envelope != nullptr)
                drawWaveformEnvelope(graphics, envelope->minimums, envelope->maximums, displayBounds,
                                     rangeStart, rangeEnd,
                                     processor.getScopeVerticalZoomDecibels(bandIndex),
                                     filledStyle);
        }
    }

    graphics.setColour(ana::ui::white.withAlpha(0.42f));

    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
    {
        auto waveformBounds = getBandBounds(bandIndex, activeBandCount);

        if (waveformBounds.isEmpty())
            continue;

        const auto zoomSlidersVisible = shouldShowZoomSliders(bandIndex, activeBandCount);

        if (zoomSlidersVisible)
            waveformBounds.setBottom(std::max(waveformBounds.getY() + 1.0f,
                                              waveformBounds.getBottom()
                                                  - static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels())));

        const auto lineWidth = zoomSlidersVisible
            ? std::max(0, getWidth() - waveformRightInset)
            : getWidth();
        const auto displayedModes = getDisplayedModes(processor.getScopeChannelMode(bandIndex));

        for (size_t displayIndex = 0; displayIndex < displayedModes.count; ++displayIndex)
        {
            const auto centre = displayedModes.count == 1
                ? waveformBounds.getCentreY()
                : waveformBounds.getY()
                    + waveformBounds.getHeight() * (static_cast<float>(displayIndex) + 0.5f) * 0.5f;
            graphics.fillRect(0, juce::roundToInt(centre), lineWidth, 1);
        }

        if (displayedModes.count == 2)
        {
            graphics.setColour(ana::ui::accent);
            graphics.fillRect(0, juce::roundToInt(waveformBounds.getCentreY()), lineWidth, 1);
            graphics.setColour(ana::ui::white.withAlpha(0.42f));
        }
    }

    graphics.setColour(ana::ui::accent);

    for (size_t bandIndex = 1;
         ! fullSourceView && singleViewBand < 0 && bandIndex < activeBandCount;
         ++bandIndex)
    {
        const auto y = juce::roundToInt(getBandBounds(bandIndex, activeBandCount).getY());
        graphics.fillRect(0, y, getWidth(), 1);
    }
}

void AnaMultibandScopeComponent::resized()
{
    const auto sizeChanged = lastComponentWidth != getWidth()
        || lastComponentHeight != getHeight();
    lastComponentWidth = getWidth();
    lastComponentHeight = getHeight();

    if (sizeChanged && historyContainsRecordedData && ! showingOfflineSnapshot)
    {
        realtimeResumeTimeMilliseconds = juce::Time::getMillisecondCounterHiRes() + 120.0;
        readCursor = processor.getMultibandScope().getWriteCursor();
        columnSampleProgress = 0.0;
        resetColumnAccumulator();
    }

    if (! showingOfflineSnapshot && ! historyContainsRecordedData)
        resizeHistory(getWaveformColumnCount());

    refreshBandModeButtons();
    repaint();
}

void AnaMultibandScopeComponent::resetHistory()
{
    showingOfflineSnapshot = false;
    offlineAnalysisColumnCount = 0;
    offlineSnapshotRevision = 0;
    displayedOfflineSnapshot.reset();
    historyTimeMilliseconds = processor.getScopeTimeMilliseconds();
    historyBandCount = processor.getActiveSplitCount() + 1;
    historyContainsRecordedData = false;
    clearedBands.fill(false);
    widebandCleared = false;

    for (size_t bandIndex = 0; bandIndex < historyChannelModes.size(); ++bandIndex)
        historyChannelModes[bandIndex] = processor.getScopeChannelMode(bandIndex);

    readCursor = processor.getMultibandScope().getWriteCursor();
    columnSampleProgress = 0.0;
    resetColumnAccumulator();

    const auto historyColumnCount = getWaveformColumnCount();

    for (auto& band : historyEnvelopes)
        for (auto& envelope : band)
        {
            envelope.minimums.assign(historyColumnCount, std::numeric_limits<float>::max());
            envelope.maximums.assign(historyColumnCount, std::numeric_limits<float>::lowest());
        }

    for (auto& envelope : widebandHistoryEnvelopes)
    {
        envelope.minimums.assign(historyColumnCount, std::numeric_limits<float>::max());
        envelope.maximums.assign(historyColumnCount, std::numeric_limits<float>::lowest());
    }

    repaint();
}

void AnaMultibandScopeComponent::resizeHistory(const size_t newColumnCount)
{
    const auto targetColumnCount = std::max<size_t>(1, newColumnCount);
    const auto resizeEnvelopes = [targetColumnCount] (auto& envelopes)
    {
        for (auto& envelope : envelopes)
        {
            const auto oldMinimums = std::move(envelope.minimums);
            const auto oldMaximums = std::move(envelope.maximums);
            envelope.minimums.assign(targetColumnCount, std::numeric_limits<float>::max());
            envelope.maximums.assign(targetColumnCount, std::numeric_limits<float>::lowest());

            if (oldMinimums.empty() || oldMaximums.size() != oldMinimums.size())
                continue;

            const auto sourceColumnCount = oldMinimums.size();

            for (size_t targetColumn = 0; targetColumn < targetColumnCount; ++targetColumn)
            {
                const auto firstSourceColumn = std::min(sourceColumnCount - 1,
                    targetColumn * sourceColumnCount / targetColumnCount);
                const auto endSourceColumn = std::max(firstSourceColumn + 1,
                    std::min(sourceColumnCount,
                        static_cast<size_t>(std::ceil(
                            static_cast<double>(targetColumn + 1)
                            * static_cast<double>(sourceColumnCount)
                            / static_cast<double>(targetColumnCount)))));

                for (auto sourceColumn = firstSourceColumn;
                     sourceColumn < endSourceColumn;
                     ++sourceColumn)
                {
                    if (oldMinimums[sourceColumn] <= oldMaximums[sourceColumn])
                    {
                        envelope.minimums[targetColumn] = std::min(
                            envelope.minimums[targetColumn], oldMinimums[sourceColumn]);
                        envelope.maximums[targetColumn] = std::max(
                            envelope.maximums[targetColumn], oldMaximums[sourceColumn]);
                    }
                }
            }
        }
    };

    for (auto& band : historyEnvelopes)
        resizeEnvelopes(band);
    resizeEnvelopes(widebandHistoryEnvelopes);

    repaint();
}

void AnaMultibandScopeComponent::clearBandHistory(const size_t bandIndex)
{
    if (bandIndex >= historyBandCount)
        return;

    const auto bandBounds = getBandBounds(bandIndex, historyBandCount).toNearestInt();
    if (fullSourceView)
    {
        for (auto& envelope : widebandHistoryEnvelopes)
        {
            envelope.minimums.assign(envelope.minimums.size(), std::numeric_limits<float>::max());
            envelope.maximums.assign(envelope.maximums.size(), std::numeric_limits<float>::lowest());
        }
        widebandCleared = showingOfflineSnapshot;
        widebandColumnMinimums.fill(std::numeric_limits<float>::max());
        widebandColumnMaximums.fill(std::numeric_limits<float>::lowest());
        processor.setScopeBandNormalized(0, false);
        refreshBandModeButtons();
        repaint(bandBounds);
        return;
    }

    for (auto& envelope : historyEnvelopes[bandIndex])
    {
        envelope.minimums.assign(envelope.minimums.size(), std::numeric_limits<float>::max());
        envelope.maximums.assign(envelope.maximums.size(), std::numeric_limits<float>::lowest());
    }
    clearedBands[bandIndex] = showingOfflineSnapshot;
    columnMinimums[bandIndex].fill(std::numeric_limits<float>::max());
    columnMaximums[bandIndex].fill(std::numeric_limits<float>::lowest());
    processor.setScopeBandNormalized(bandIndex, false);
    refreshBandModeButtons();
    repaint(bandBounds);
}

void AnaMultibandScopeComponent::resetColumnAccumulator()
{
    for (auto& band : columnMinimums)
        band.fill(std::numeric_limits<float>::max());
    for (auto& band : columnMaximums)
        band.fill(std::numeric_limits<float>::lowest());
    widebandColumnMinimums.fill(std::numeric_limits<float>::max());
    widebandColumnMaximums.fill(std::numeric_limits<float>::lowest());
}

void AnaMultibandScopeComponent::normalizeBandWithZoom(const size_t bandIndex)
{
    if (! processor.isOfflineMode()
        || ! showingOfflineSnapshot
        || displayedOfflineSnapshot == nullptr
        || bandIndex >= historyBandCount
        || (fullSourceView ? widebandCleared : clearedBands[bandIndex]))
        return;

    if (processor.isScopeBandNormalized(bandIndex))
    {
        processor.setScopeBandNormalized(bandIndex, false);
        processor.setScopeVerticalZoomDecibels(bandIndex, 0.0f);
        refreshBandModeButtons();
        repaint();
        return;
    }

    auto peak = 0.0f;
    size_t validColumnCount = 0;
    const auto displayedModes = getDisplayedModes(processor.getScopeChannelMode(bandIndex));

    for (size_t displayIndex = 0; displayIndex < displayedModes.count; ++displayIndex)
    {
        const auto& envelope = fullSourceView
            ? displayedOfflineSnapshot->wideband[displayedModes.indices[displayIndex]]
            : displayedOfflineSnapshot->bands[bandIndex][displayedModes.indices[displayIndex]];

        if (envelope.minimums.empty()
            || envelope.maximums.size() != envelope.minimums.size())
            continue;

        for (size_t index = 0; index < envelope.minimums.size(); ++index)
        {
            if (envelope.minimums[index] <= envelope.maximums[index])
            {
                peak = std::max(peak,
                    std::max(std::abs(envelope.minimums[index]),
                             std::abs(envelope.maximums[index])));
                ++validColumnCount;
            }
        }
    }

    if (validColumnCount == 0 || peak <= 1.0e-6f)
        return;

    const auto normalizationZoom = juce::jlimit(
        -48.0f, 96.0f, juce::Decibels::gainToDecibels(1.0f / peak));
    processor.setScopeVerticalZoomDecibels(bandIndex, normalizationZoom);
    processor.setScopeBandNormalized(bandIndex, true);
    refreshBandModeButtons();
    repaint();
}

void AnaMultibandScopeComponent::refreshBandModeButtons()
{
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    const auto persistedSingleViewBand = fullSourceView ? -1 : processor.getScopeSingleViewBand();

    if (persistedSingleViewBand != singleViewBand)
        singleViewBand = persistedSingleViewBand;

    if (singleViewBand >= static_cast<int>(activeBandCount))
    {
        singleViewBand = -1;
        processor.setScopeSingleViewBand(-1);
    }

    constexpr int buttonWidth = bandButtonWidth;
    constexpr int otherButtonWidth = 64;
    constexpr int buttonHeight = ana::ui::controlHeight;
    constexpr int zoomSliderWidth = bandZoomSliderWidth;
    constexpr int zoomValueWidth = bandZoomValueWidth;
    const auto offlineMode = processor.isOfflineMode();
    const auto showZoomControls = processor.areScopeZoomControlsVisible();
    const auto showMonitorControls = processor.areScopeMonitorControlsVisible();
    const auto showOtherControls = processor.areScopeOtherControlsVisible();
    const auto normalizationAvailable = offlineMode
        && showingOfflineSnapshot
        && displayedOfflineSnapshot != nullptr;
    const juce::ScopedValueSetter<bool> controlUpdate(updatingBandControls, true);

    for (size_t bandIndex = 0; bandIndex < bandModeButtons.size(); ++bandIndex)
    {
        const auto isActive = bandIndex < activeBandCount;
        const auto isVisibleBand = isActive
            && (fullSourceView
                ? bandIndex == 0
                : (singleViewBand < 0 || singleViewBand == static_cast<int>(bandIndex)));
        const auto selectedMode = processor.getScopeChannelMode(bandIndex);
        const auto verticalZoomDecibels = processor.getScopeVerticalZoomDecibels(bandIndex);
        const auto normalized = offlineMode && processor.isScopeBandNormalized(bandIndex);
        const auto laneBounds = isVisibleBand
            ? getBandBounds(bandIndex, activeBandCount).toNearestInt()
            : juce::Rectangle<int>();
        const auto showZoomSliders = isVisibleBand
            && shouldShowZoomSliders(bandIndex, activeBandCount);
        const auto controlsY = laneBounds.getY() + ana::ui::gap.pixels();
        ana::ui::FixedGapRow controlsRow({ 0, controlsY, getWidth(), buttonHeight });

        for (size_t modeIndex = 0; modeIndex < bandModeButtons[bandIndex].size(); ++modeIndex)
        {
            auto& button = *bandModeButtons[bandIndex][modeIndex];
            button.setVisible(isVisibleBand && showMonitorControls);
            button.setToggleState(selectedMode == scopeModeButtonModes[modeIndex],
                                  juce::dontSendNotification);

            if (isVisibleBand && showMonitorControls)
                button.setBounds(controlsRow.takeLeft(buttonWidth));
        }

        auto& clearButton = *bandClearButtons[bandIndex];
        clearButton.setVisible(isVisibleBand && showOtherControls);

        if (isVisibleBand && showOtherControls)
            clearButton.setBounds(controlsRow.takeLeft(otherButtonWidth));

        auto& singleViewButton = *bandSingleViewButtons[bandIndex];
        singleViewButton.setVisible(isVisibleBand && showOtherControls && ! fullSourceView);
        singleViewButton.setToggleState(singleViewBand == static_cast<int>(bandIndex),
                                        juce::dontSendNotification);

        if (isVisibleBand && showOtherControls)
            singleViewButton.setBounds(controlsRow.takeLeft(otherButtonWidth));

        auto& zoomSlider = *bandZoomSliders[bandIndex];
        auto& zoomValueLabel = *bandZoomValueLabels[bandIndex];
        auto& normalizeButton = *bandNormalizeButtons[bandIndex];
        zoomSlider.setVisible(showZoomSliders);
        zoomValueLabel.setVisible(isVisibleBand && showZoomControls);
        normalizeButton.setVisible(isVisibleBand && showZoomControls);
        normalizeButton.setEnabled(normalizationAvailable
                                   && ! (fullSourceView ? widebandCleared : clearedBands[bandIndex]));
        zoomSlider.setValue(verticalZoomDecibels, juce::dontSendNotification);
        zoomSlider.setTooltip("ZOOM " + formatZoomValue(verticalZoomDecibels));
        zoomValueLabel.setText(formatZoomValue(verticalZoomDecibels), juce::dontSendNotification);
        normalizeButton.setToggleState(normalized, juce::dontSendNotification);

        auto& rangeSlider = *bandRangeSliders[bandIndex];
        rangeSlider.setVisible(showZoomSliders);

        if (isVisibleBand && showZoomControls)
        {
            const auto buttonY = laneBounds.getY() + ana::ui::gap.pixels();
            const auto sliderX = showZoomSliders
                ? std::max(0, getWidth() - zoomSliderWidth)
                : getWidth();
            const auto zoomValueX = sliderX - (showZoomSliders ? ana::ui::gap.pixels() : 0)
                - zoomValueWidth;
            const auto normalizeButtonX = zoomValueX - ana::ui::gap.pixels() - buttonWidth;

            zoomValueLabel.setBounds(zoomValueX, buttonY, zoomValueWidth, buttonHeight);
            normalizeButton.setBounds(normalizeButtonX, buttonY, buttonWidth, buttonHeight);

            if (showZoomSliders)
            {
                const auto rangeBounds = juce::Rectangle<int>(
                    0,
                    laneBounds.getBottom() - ana::ui::gap.pixels() - bandRangeSliderHeight,
                    std::max(1, sliderX - ana::ui::gap.pixels()),
                    bandRangeSliderHeight);
                rangeSlider.setBounds(rangeBounds);
                zoomSlider.setBounds(sliderX, buttonY, zoomSliderWidth,
                                     std::max(1, rangeBounds.getBottom() - buttonY));
            }
        }
    }
}

juce::Rectangle<float> AnaMultibandScopeComponent::getBandBounds(
    const size_t bandIndex, const size_t activeBandCount) const noexcept
{
    if (activeBandCount == 0 || bandIndex >= activeBandCount)
        return {};

    if (fullSourceView)
    {
        if (bandIndex != 0)
            return {};

        return { 0.0f, 0.0f, static_cast<float>(std::max(0, getWidth() - 1)),
                 static_cast<float>(getHeight()) };
    }

    if (singleViewBand >= 0)
    {
        if (singleViewBand != static_cast<int>(bandIndex))
            return {};

        return { 0.0f, 0.0f, static_cast<float>(std::max(0, getWidth() - 1)),
                 static_cast<float>(getHeight()) };
    }

    auto totalWeight = 0.0f;
    auto precedingWeight = 0.0f;

    for (size_t index = 0; index < activeBandCount; ++index)
    {
        const auto weight = std::max(0.001f, bandHeightWeights[index]);
        totalWeight += weight;

        if (index < bandIndex)
            precedingWeight += weight;
    }

    const auto availableHeight = static_cast<float>(getHeight());
    const auto fixedHeight = std::min(static_cast<float>(minimumBandHeight),
                                      availableHeight / static_cast<float>(activeBandCount));
    const auto distributableHeight = std::max(0.0f,
        availableHeight - fixedHeight * static_cast<float>(activeBandCount));
    const auto top = fixedHeight * static_cast<float>(bandIndex)
        + distributableHeight * precedingWeight / totalWeight;
    const auto bottomWeight = precedingWeight + std::max(0.001f, bandHeightWeights[bandIndex]);
    const auto bottom = bandIndex + 1 == activeBandCount
        ? availableHeight
        : fixedHeight * static_cast<float>(bandIndex + 1)
            + distributableHeight * bottomWeight / totalWeight;
    return { 0.0f, top, static_cast<float>(std::max(0, getWidth() - 1)),
             std::max(0.0f, bottom - top) };
}

bool AnaMultibandScopeComponent::shouldShowZoomSliders(
    const size_t bandIndex, const size_t activeBandCount) const noexcept
{
    return processor.areScopeZoomControlsVisible()
        && getBandBounds(bandIndex, activeBandCount).getHeight()
            >= static_cast<float>(minimumBandHeight);
}

bool AnaMultibandScopeComponent::hasVisibleZoomSliders() const noexcept
{
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
        if (shouldShowZoomSliders(bandIndex, activeBandCount))
            return true;

    return false;
}

size_t AnaMultibandScopeComponent::getWaveformColumnCount() const noexcept
{
    const auto drawableWidth = hasVisibleZoomSliders()
        ? getWidth() - waveformRightInset
        : getWidth() - 1;
    return static_cast<size_t>(std::max(1, drawableWidth));
}

int AnaMultibandScopeComponent::findBandSeparator(
    const int y, const size_t activeBandCount) const noexcept
{
    if (fullSourceView || singleViewBand >= 0)
        return -1;

    constexpr int hitRadius = 5;

    for (size_t bandIndex = 1; bandIndex < activeBandCount; ++bandIndex)
    {
        const auto separatorY = juce::roundToInt(getBandBounds(bandIndex, activeBandCount).getY());

        if (std::abs(y - separatorY) <= hitRadius)
            return static_cast<int>(bandIndex - 1);
    }

    return -1;
}

void AnaMultibandScopeComponent::mouseMove(const juce::MouseEvent& event)
{
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    setMouseCursor(findBandSeparator(event.y, activeBandCount) >= 0
        ? juce::MouseCursor::UpDownResizeCursor
        : juce::MouseCursor::NormalCursor);
}

void AnaMultibandScopeComponent::mouseExit(const juce::MouseEvent&)
{
    if (draggedBandSeparator < 0)
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void AnaMultibandScopeComponent::mouseDown(const juce::MouseEvent& event)
{
    draggedBandSeparator = findBandSeparator(event.y, processor.getActiveSplitCount() + 1);

    if (draggedBandSeparator >= 0)
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void AnaMultibandScopeComponent::mouseDrag(const juce::MouseEvent& event)
{
    const auto activeBandCount = processor.getActiveSplitCount() + 1;

    if (draggedBandSeparator < 0
        || static_cast<size_t>(draggedBandSeparator + 1) >= activeBandCount)
        return;

    const auto upperIndex = static_cast<size_t>(draggedBandSeparator);
    const auto lowerIndex = upperIndex + 1;
    const auto upperBounds = getBandBounds(upperIndex, activeBandCount);
    const auto lowerBounds = getBandBounds(lowerIndex, activeBandCount);
    const auto combinedTop = upperBounds.getY();
    const auto combinedBottom = lowerBounds.getBottom();
    const auto combinedHeight = combinedBottom - combinedTop;

    if (combinedHeight <= 1.0f)
        return;

    const auto minimumHeight = std::min(static_cast<float>(minimumBandHeight),
                                        combinedHeight * 0.5f);
    const auto separatorY = juce::jlimit(combinedTop + minimumHeight,
                                         combinedBottom - minimumHeight,
                                         static_cast<float>(event.y));
    const auto distributableHeight = combinedHeight - minimumHeight * 2.0f;

    if (distributableHeight <= 0.0f)
        return;

    const auto upperRatio = (separatorY - combinedTop - minimumHeight) / distributableHeight;
    const auto combinedWeight = bandHeightWeights[upperIndex] + bandHeightWeights[lowerIndex];
    bandHeightWeights[upperIndex] = combinedWeight * upperRatio;
    bandHeightWeights[lowerIndex] = combinedWeight * (1.0f - upperRatio);
    refreshBandModeButtons();
    repaint();
}

void AnaMultibandScopeComponent::mouseUp(const juce::MouseEvent& event)
{
    draggedBandSeparator = -1;
    mouseMove(event);
}

std::array<float, ana::OfflineScopeSnapshot::numChannelModes>
AnaMultibandScopeComponent::getRealtimeModeSamples(const size_t bandIndex,
                                                   const size_t sampleIndex) const noexcept
{
    const auto left = incomingSamples[bandIndex][0][sampleIndex];
    const auto right = incomingSamples[bandIndex][1][sampleIndex];
    return { left, right, 0.5f * (left + right), 0.5f * (left - right) };
}

std::array<float, ana::OfflineScopeSnapshot::numChannelModes>
AnaMultibandScopeComponent::getRealtimeWidebandModeSamples(const size_t sampleIndex) const noexcept
{
    const auto left = incomingSamples.wideband[0][sampleIndex];
    const auto right = incomingSamples.wideband[1][sampleIndex];
    return { left, right, 0.5f * (left + right), 0.5f * (left - right) };
}

void AnaMultibandScopeComponent::appendHistoryColumn(const size_t activeBandCount)
{
    if (historyEnvelopes.front().front().minimums.empty())
        return;

    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
    {
        for (size_t modeIndex = 0; modeIndex < historyEnvelopes[bandIndex].size(); ++modeIndex)
        {
            auto& envelope = historyEnvelopes[bandIndex][modeIndex];

            if (envelope.minimums.size() > 1)
            {
                std::move(envelope.minimums.begin() + 1,
                          envelope.minimums.end(), envelope.minimums.begin());
                std::move(envelope.maximums.begin() + 1,
                          envelope.maximums.end(), envelope.maximums.begin());
            }

            envelope.minimums.back() = columnMinimums[bandIndex][modeIndex];
            envelope.maximums.back() = columnMaximums[bandIndex][modeIndex];
        }
        clearedBands[bandIndex] = false;
    }

    for (size_t modeIndex = 0; modeIndex < widebandHistoryEnvelopes.size(); ++modeIndex)
    {
        auto& envelope = widebandHistoryEnvelopes[modeIndex];
        if (envelope.minimums.size() > 1)
        {
            std::move(envelope.minimums.begin() + 1,
                      envelope.minimums.end(), envelope.minimums.begin());
            std::move(envelope.maximums.begin() + 1,
                      envelope.maximums.end(), envelope.maximums.begin());
        }

        envelope.minimums.back() = widebandColumnMinimums[modeIndex];
        envelope.maximums.back() = widebandColumnMaximums[modeIndex];
    }
    widebandCleared = false;

    historyContainsRecordedData = true;
}

void AnaMultibandScopeComponent::renderOfflineSnapshot(
    std::shared_ptr<const ana::OfflineScopeSnapshot> snapshot)
{
    if (snapshot == nullptr)
        return;

    const auto receivedNewRevision = offlineSnapshotRevision != snapshot->revision;
    historyBandCount = std::min(snapshot->activeBandCount,
                                processor.getActiveSplitCount() + 1);
    offlineSnapshotRevision = snapshot->revision;
    showingOfflineSnapshot = true;
    displayedOfflineSnapshot = std::move(snapshot);
    clearedBands.fill(false);
    widebandCleared = false;

    if (receivedNewRevision)
    {
        for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
            processor.setScopeBandNormalized(bandIndex, false);

        refreshBandModeButtons();

        if (onOfflineUpdateStatus)
            onOfflineUpdateStatus(
                "UPDATED " + juce::Time::getCurrentTime().formatted("%d.%m.%Y %H:%M:%S"));
    }

    repaint();
}

AnaCrossoverSettingsComponent::AnaCrossoverSettingsComponent(AnaAudioProcessor& processorRef)
    : processor(processorRef),
      styleControl(processorRef.getParameters(), AnaAudioProcessor::scopeStyleParameterId,
                   "STYLE",
                   [] (const double value)
                   {
                       return value < 0.5 ? juce::String("FILLED") : juce::String("OUTLINE");
                   }),
      opacityControl(processorRef.getParameters(), AnaAudioProcessor::scopeOpacityParameterId,
                     "OPACITY",
                     [] (const double value)
                     {
                         return juce::String(juce::roundToInt(value));
                     }),
      timeControl(processorRef.getParameters(), AnaAudioProcessor::scopeTimeParameterId,
                  "TIME",
                  [] (const double value)
                  {
                      const auto seconds = value / 1000.0;
                      return juce::String(seconds, seconds < 10.0 ? 1 : 0);
                  }),
      timeNoteControl(processorRef.getParameters(), AnaAudioProcessor::scopeNoteLengthParameterId,
                      "TIME",
                      [] (const double value)
                      {
                          static const juce::StringArray choices {
                              "1/16", "1/8", "1/4", "1/2", "1/1",
                              "2/1", "4/1", "8/1", "16/1"
                          };
                          return choices[juce::jlimit(0, choices.size() - 1,
                                                     juce::roundToInt(value))];
                      }),
      timeBaseControl(processorRef.getParameters(), AnaAudioProcessor::scopeTimeBaseParameterId,
                      "TIME-BASE",
                      [] (const double value)
                      {
                          return value < 0.5 ? juce::String("MS") : juce::String("NOTE");
                  }),
      frequencyBlockSizeControl(processorRef.getParameters(), AnaAudioProcessor::frequencyBlockSizeParameterId,
                                "BLOCK-SIZE", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      frequencyOverlapControl(processorRef.getParameters(), AnaAudioProcessor::frequencyOverlapParameterId,
                              "OVERLAP", [] (const double value) { return juce::String(juce::roundToInt(value * 100.0)); }),
      frequencyAverageTimeControl(processorRef.getParameters(), AnaAudioProcessor::frequencyAverageTimeParameterId,
                                  "AVG-TIME", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      frequencyFirstSpectrumTypeControl(processorRef.getParameters(), AnaAudioProcessor::frequencyFirstSpectrumTypeParameterId,
                                        "1ST-SPEC-TYPE", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      frequencySecondSpectrumTypeControl(processorRef.getParameters(), AnaAudioProcessor::frequencySecondSpectrumTypeParameterId,
                                         "2ND-SPEC-TYPE", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      frequencySlopeControl(processorRef.getParameters(), AnaAudioProcessor::frequencySlopeParameterId,
                            "SLOPE", [] (const double value) { return juce::String(value, 1); }),
      correlationBlockSizeControl(processorRef.getParameters(), AnaAudioProcessor::correlationBlockSizeParameterId,
                                  "BLOCK-SIZE", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      correlationOverlapControl(processorRef.getParameters(), AnaAudioProcessor::correlationOverlapParameterId,
                                "OVERLAP", [] (const double value) { return juce::String(juce::roundToInt(value * 100.0)); }),
      correlationAverageTimeControl(processorRef.getParameters(), AnaAudioProcessor::correlationAverageTimeParameterId,
                                    "AVG-TIME", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      correlationSmoothingControl(processorRef.getParameters(), AnaAudioProcessor::correlationSmoothingParameterId,
                                  "SMOOTHING", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      correlationFirstSpectrumTypeControl(processorRef.getParameters(), AnaAudioProcessor::correlationFirstSpectrumTypeParameterId,
                                          "1ST-GRAPH-TYPE", [] (const double value) { return juce::String(juce::roundToInt(value)); }),
      correlationSecondSpectrumTypeControl(processorRef.getParameters(), AnaAudioProcessor::correlationSecondSpectrumTypeParameterId,
                                           "2ND-GRAPH-TYPE", [] (const double value) { return juce::String(juce::roundToInt(value)); })
{
    setOpaque(false);
    settingsViewport.setViewedComponent(&settingsContent, false);
    settingsViewport.setScrollBarsShown(true, false, true, false);
    settingsViewport.setScrollBarThickness(ana::ui::gap.pixels());
    settingsViewport.setLookAndFeel(&focusedControlLookAndFeel);
    settingsViewport.setWantsKeyboardFocus(false);
    settingsViewport.setMouseClickGrabsKeyboardFocus(false);
    addAndMakeVisible(settingsViewport);

    const auto configureHeading = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(ana::ui::makeFont());
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, ana::ui::white);
        label.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label.setColour(juce::Label::outlineColourId, ana::ui::grey500);
        label.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
        label.setInterceptsMouseClicks(false, false);
        settingsContent.addAndMakeVisible(label);
    };
    configureHeading(headingLabel, "SETTINGS");
    configureHeading(generalHeadingLabel, "GENERAL");
    configureHeading(controlsVisibilityHeadingLabel, "CONTROLS VISIBILITY");
    configureHeading(realtimeHeadingLabel, "REALTIME");

    for (auto* component : std::array<juce::Component*, 37> {
             &addCrossoverButton, &removeCrossoverButton, &equalHeightButton,
             &styleControl, &opacityControl, &zoomControlsButton,
             &monitorControlsButton, &otherControlsButton, &timeControl,
             &timeNoteControl, &timeBaseControl, &frequencyBlockSizeControl,
             &frequencyOverlapControl, &frequencyAverageTimeControl,
             &frequencyFilledDisplayButton, &frequencySecondSpectrumButton,
             &frequencyFirstSpectrumTypeControl, &frequencySecondSpectrumTypeControl,
             &frequencyAntiAliasButton, &frequencySlopeControl, &frequencyHostClearButton,
             &frequencyRangesButton, &frequencyCursorButton,
             &frequencyMonitorControlsButton, &frequencyZoomControlsButton,
             &correlationBlockSizeControl, &correlationOverlapControl, &correlationAverageTimeControl,
             &correlationSmoothingControl, &correlationFirstSpectrumTypeControl,
             &correlationSecondSpectrumTypeControl,
             &correlationFilledDisplayButton, &correlationSecondSpectrumButton, &correlationHostClearButton,
             &correlationRangesButton, &correlationCursorButton,
             &correlationZoomControlsButton })
        settingsContent.addAndMakeVisible(*component);

    addAndMakeVisible(focusedParameterControl);
    addCrossoverButton.onClick = [this] { changeActiveSplitCount(1); };
    removeCrossoverButton.onClick = [this] { changeActiveSplitCount(-1); };
    equalHeightButton.onClick = [this]
    {
        if (onEqualBandHeights)
            onEqualBandHeights();
    };
    const auto displaySettingChanged = [this]
    {
        if (onDisplaySettingsChanged)
            onDisplaySettingsChanged();
    };
    styleControl.onValueChanged = displaySettingChanged;
    opacityControl.onValueChanged = displaySettingChanged;
    for (auto* button : std::array<AnaScopeButton*, 3> {
             &zoomControlsButton, &monitorControlsButton, &otherControlsButton })
    {
        button->setClickingTogglesState(true);
        button->onClick = displaySettingChanged;
    }
    zoomControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::scopeZoomControlsParameterId, zoomControlsButton);
    monitorControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::scopeMonitorControlsParameterId, monitorControlsButton);
    otherControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::scopeOtherControlsParameterId, otherControlsButton);
    for (auto* button : std::array<AnaScopeButton*, 8> {
             &frequencyFilledDisplayButton, &frequencySecondSpectrumButton,
             &frequencyAntiAliasButton, &frequencyHostClearButton, &frequencyRangesButton,
             &frequencyCursorButton, &frequencyMonitorControlsButton, &frequencyZoomControlsButton })
        button->setClickingTogglesState(true);
    frequencyFilledDisplayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::frequencyFilledDisplayParameterId, frequencyFilledDisplayButton);
    frequencySecondSpectrumAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::frequencySecondSpectrumParameterId, frequencySecondSpectrumButton);
    frequencyAntiAliasAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::frequencyAntiAliasParameterId, frequencyAntiAliasButton);
    frequencyRangesAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::frequencyRangesVisibleParameterId, frequencyRangesButton);
    frequencyHostClearAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::frequencyHostClearParameterId, frequencyHostClearButton);
    frequencyCursorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::frequencyCursorReadoutParameterId, frequencyCursorButton);
    frequencyMonitorControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::frequencyMonitorControlsParameterId, frequencyMonitorControlsButton);
    frequencyZoomControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::frequencyZoomControlsParameterId, frequencyZoomControlsButton);
    for (auto* button : std::array<AnaScopeButton*, 6> {
        &correlationFilledDisplayButton, &correlationHostClearButton, &correlationRangesButton,
        &correlationCursorButton, &correlationZoomControlsButton,
        &correlationSecondSpectrumButton })
        button->setClickingTogglesState(true);
    correlationFilledDisplayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::correlationFilledDisplayParameterId, correlationFilledDisplayButton);
    correlationSecondSpectrumAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::correlationSecondSpectrumParameterId, correlationSecondSpectrumButton);
    correlationHostClearAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::correlationHostClearParameterId, correlationHostClearButton);
    correlationRangesAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::correlationRangesVisibleParameterId, correlationRangesButton);
    correlationCursorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::correlationCursorReadoutParameterId, correlationCursorButton);
    correlationZoomControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), AnaAudioProcessor::correlationZoomControlsParameterId, correlationZoomControlsButton);
    const auto requestChoice = [this] (AnaParameterControl& control)
    {
        if (onChoiceRequested)
            onChoiceRequested(control);
    };
    styleControl.onChoiceRequested = requestChoice;
    timeNoteControl.onChoiceRequested = requestChoice;
    timeBaseControl.onChoiceRequested = requestChoice;
    frequencyBlockSizeControl.onChoiceRequested = requestChoice;
    frequencyFirstSpectrumTypeControl.onChoiceRequested = requestChoice;
    frequencySecondSpectrumTypeControl.onChoiceRequested = requestChoice;
    correlationBlockSizeControl.onChoiceRequested = requestChoice;
    correlationFirstSpectrumTypeControl.onChoiceRequested = requestChoice;
    correlationSecondSpectrumTypeControl.onChoiceRequested = requestChoice;

    const auto focusControl = [this] (AnaParameterControl& control)
    {
        focusParameterControl(control);
    };
    opacityControl.onFocusRequested = focusControl;
    timeControl.onFocusRequested = focusControl;
    frequencyAverageTimeControl.onFocusRequested = focusControl;
    frequencySlopeControl.onFocusRequested = focusControl;
    frequencyOverlapControl.onFocusRequested = focusControl;
    correlationOverlapControl.onFocusRequested = focusControl;
    correlationAverageTimeControl.onFocusRequested = focusControl;
    correlationSmoothingControl.onFocusRequested = focusControl;
    frequencyOverlapControl.getSlider().valueFromTextFunction = [] (const juce::String& text)
    {
        return text.getDoubleValue() * 0.01;
    };
    correlationOverlapControl.getSlider().valueFromTextFunction = [] (const juce::String& text)
    {
        return text.getDoubleValue() * 0.01;
    };
    timeBaseControl.onValueChanged = [this]
    {
        refreshExternalState();
        resized();

        if (onDisplaySettingsChanged)
            onDisplaySettingsChanged();
    };
    timeNoteControl.onValueChanged = displaySettingChanged;
    timeControl.getSlider().valueFromTextFunction = [] (const juce::String& text)
    {
        return text.getDoubleValue() * 1000.0;
    };

    focusedParameterControl.setLookAndFeel(&focusedControlLookAndFeel);
    focusedParameterControl.setSliderStyle(juce::Slider::LinearHorizontal);
    focusedParameterControl.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    focusedParameterControl.setRange(0.0, 1.0, 0.0);
    focusedParameterControl.setSliderSnapsToMousePosition(false);
    focusedParameterControl.setScrollWheelEnabled(true);
    focusedParameterControl.setWantsKeyboardFocus(false);
    focusedParameterControl.setMouseClickGrabsKeyboardFocus(false);
    focusedParameterControl.setEnabled(false);
    focusedParameterControl.onValueChange = [this]
    {
        if (updatingFocusedParameterControl || focusedParameterTarget == nullptr)
            return;

        auto& target = focusedParameterTarget->getSlider();
        const auto value = target.getNormalisableRange().convertFrom0to1(focusedParameterControl.getValue());
        target.setValue(value, juce::sendNotificationSync);
        syncFocusedParameterControl();
    };

    for (size_t index = 0; index < crossoverControls.size(); ++index)
    {
        auto control = std::make_unique<AnaParameterControl>(
            processor.getParameters(),
            AnaAudioProcessor::crossoverParameterIds[index],
            "XOVER-" + juce::String(static_cast<int>(index + 1)),
            [] (const double value) { return formatFrequency(value); });
        control->getSlider().valueFromTextFunction = [] (const juce::String& text)
        {
            return parseFrequency(text);
        };
        control->onValueChanged = [this, index] { constrainFrequency(index); };
        control->onFocusRequested = focusControl;
        settingsContent.addAndMakeVisible(*control);
        crossoverControls[index] = std::move(control);
    }

    for (size_t index = 0; index < processor.getActiveSplitCount(); ++index)
        constrainFrequency(index);

    refreshExternalState();
    focusParameterControl(opacityControl);
    settingsContent.addMouseListener(this, true);
    startTimerHz(15);
}

AnaCrossoverSettingsComponent::~AnaCrossoverSettingsComponent()
{
    settingsContent.removeMouseListener(this);
    settingsViewport.setViewedComponent(nullptr, false);
    settingsViewport.setLookAndFeel(nullptr);
    focusedParameterControl.setLookAndFeel(nullptr);
}

void AnaCrossoverSettingsComponent::setAnalyzerPage(const AnaAnalyzerPage page)
{
    if (analyzerPage == page)
        return;

    analyzerPage = page;
    generalHeadingLabel.setText(page == AnaAnalyzerPage::scope ? "GENERAL"
                               : page == AnaAnalyzerPage::frequency ? "FREQ" : "CORR",
                                juce::dontSendNotification);
    clearFocusedParameterControl();
    refreshExternalState();
    resized();
}

void AnaCrossoverSettingsComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ana::ui::grey800.withAlpha(0.97f));
    graphics.setColour(ana::ui::accent);
    graphics.drawRect(getLocalBounds(), 1);
}

void AnaCrossoverSettingsComponent::resized()
{
    constexpr int headingHeight = ana::ui::controlHeight;
    constexpr int rowHeight = ana::ui::controlHeight;
    const auto& fixedGap = ana::ui::gap;
    const auto scopePage = analyzerPage == AnaAnalyzerPage::scope;
    const auto frequencyPage = analyzerPage == AnaAnalyzerPage::frequency;
    const auto contentHeight = scopePage
        ? 18 * rowHeight + 18 * fixedGap.pixels()
        : frequencyPage ? 17 * rowHeight + 16 * fixedGap.pixels() : 15 * rowHeight + 14 * fixedGap.pixels();
    auto innerBounds = getLocalBounds().reduced(fixedGap.pixels());
    const auto potentiometerBounds = innerBounds.removeFromBottom(rowHeight);
    fixedGap.removeFromBottom(innerBounds);
    const auto viewportBounds = innerBounds;
    settingsViewport.setBounds(viewportBounds);
    const auto needsScrollbar = contentHeight > viewportBounds.getHeight();
    const auto scrollbarReserve = needsScrollbar
        ? settingsViewport.getScrollBarThickness() + fixedGap.pixels()
        : 0;
    const auto contentWidth = std::max(1, viewportBounds.getWidth()
        - scrollbarReserve);
    settingsContent.setSize(contentWidth, std::max(contentHeight, viewportBounds.getHeight()));
    focusedParameterControl.setBounds(viewportBounds.getX(), potentiometerBounds.getY(),
                                      contentWidth, rowHeight);
    auto area = settingsContent.getLocalBounds();

    headingLabel.setBounds(area.removeFromTop(headingHeight));
    fixedGap.removeFromTop(area);
    generalHeadingLabel.setBounds(area.removeFromTop(headingHeight));
    fixedGap.removeFromTop(area);

    if (frequencyPage)
    {
        frequencyBlockSizeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencyOverlapControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencyAverageTimeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencyFilledDisplayButton.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencySecondSpectrumButton.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencyFirstSpectrumTypeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencySecondSpectrumTypeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencyAntiAliasButton.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencySlopeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencyHostClearButton.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        controlsVisibilityHeadingLabel.setBounds(area.removeFromTop(headingHeight));
        fixedGap.removeFromTop(area);
        frequencyRangesButton.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencyCursorButton.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencyMonitorControlsButton.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        frequencyZoomControlsButton.setBounds(area.removeFromTop(rowHeight));
        return;
    }

    if (! scopePage)
    {
        correlationBlockSizeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        correlationOverlapControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        correlationAverageTimeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        correlationSmoothingControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        correlationFilledDisplayButton.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        correlationSecondSpectrumButton.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        correlationFirstSpectrumTypeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        correlationSecondSpectrumTypeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        correlationHostClearButton.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        controlsVisibilityHeadingLabel.setBounds(area.removeFromTop(headingHeight));
        fixedGap.removeFromTop(area);
        correlationRangesButton.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        correlationCursorButton.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        correlationZoomControlsButton.setBounds(area.removeFromTop(rowHeight));
        return;
    }

    auto crossoverButtons = area.removeFromTop(rowHeight);
    const auto crossoverButtonWidth = std::max(0, crossoverButtons.getWidth() - fixedGap.pixels()) / 2;
    addCrossoverButton.setBounds(crossoverButtons.removeFromLeft(crossoverButtonWidth));
    fixedGap.removeFromLeft(crossoverButtons);
    removeCrossoverButton.setBounds(crossoverButtons);
    fixedGap.removeFromTop(area);

    for (auto& control : crossoverControls)
    {
        control->setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
    }

    fixedGap.removeFromTop(area);
    equalHeightButton.setBounds(area.removeFromTop(rowHeight));
    fixedGap.removeFromTop(area);
    styleControl.setBounds(area.removeFromTop(rowHeight));
    fixedGap.removeFromTop(area);
    opacityControl.setBounds(area.removeFromTop(rowHeight));
    fixedGap.removeFromTop(area);

    controlsVisibilityHeadingLabel.setBounds(area.removeFromTop(headingHeight));
    fixedGap.removeFromTop(area);
    zoomControlsButton.setBounds(area.removeFromTop(rowHeight));
    fixedGap.removeFromTop(area);
    monitorControlsButton.setBounds(area.removeFromTop(rowHeight));
    fixedGap.removeFromTop(area);
    otherControlsButton.setBounds(area.removeFromTop(rowHeight));
    fixedGap.removeFromTop(area);

    realtimeHeadingLabel.setBounds(area.removeFromTop(headingHeight));
    fixedGap.removeFromTop(area);
    const auto timeBounds = area.removeFromTop(rowHeight);
    timeControl.setBounds(timeBounds);
    timeNoteControl.setBounds(timeBounds);
    fixedGap.removeFromTop(area);
    timeBaseControl.setBounds(area.removeFromTop(rowHeight));
}

void AnaCrossoverSettingsComponent::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent != &settingsContent && event.originalComponent != this)
        return;

    dismissParameterEditors();
    clearFocusedParameterControl();
}

void AnaCrossoverSettingsComponent::timerCallback()
{
    refreshExternalState();
    syncFocusedParameterControl();
}

void AnaCrossoverSettingsComponent::changeActiveSplitCount(const int delta)
{
    const auto currentCount = static_cast<int>(processor.getActiveSplitCount());
    const auto newCount = juce::jlimit(0, static_cast<int>(crossoverControls.size()), currentCount + delta);

    if (newCount != currentCount)
    {
        processor.setActiveSplitCount(static_cast<size_t>(newCount));

        for (int index = 0; index < newCount; ++index)
            constrainFrequency(static_cast<size_t>(index));
    }

    refreshExternalState();
}

void AnaCrossoverSettingsComponent::constrainFrequency(const size_t crossoverIndex)
{
    if (constrainingFrequency || crossoverIndex >= processor.getActiveSplitCount())
        return;

    const juce::ScopedValueSetter<bool> guard(constrainingFrequency, true);
    const auto activeSplitCount = processor.getActiveSplitCount();
    auto& slider = crossoverControls[crossoverIndex]->getSlider();
    auto lowerBound = slider.getMinimum();
    auto upperBound = slider.getMaximum();

    if (crossoverIndex > 0)
        lowerBound = crossoverControls[crossoverIndex - 1]->getSlider().getValue() + minimumCrossoverGapHz;

    if (crossoverIndex + 1 < activeSplitCount)
        upperBound = crossoverControls[crossoverIndex + 1]->getSlider().getValue() - minimumCrossoverGapHz;

    slider.setValue(juce::jlimit(lowerBound, std::max(lowerBound, upperBound), slider.getValue()),
                    juce::sendNotificationSync);
}

void AnaCrossoverSettingsComponent::refreshExternalState()
{
    const auto scopePage = analyzerPage == AnaAnalyzerPage::scope;
    const auto frequencyPage = analyzerPage == AnaAnalyzerPage::frequency;
    const auto correlationPage = analyzerPage == AnaAnalyzerPage::correlation;
    for (auto* component : std::array<juce::Component*, 11> {
             &addCrossoverButton, &removeCrossoverButton, &equalHeightButton,
             &styleControl, &opacityControl, &zoomControlsButton,
             &monitorControlsButton, &otherControlsButton, &timeControl,
             &timeNoteControl, &timeBaseControl })
        component->setVisible(scopePage);
    for (auto& control : crossoverControls)
        control->setVisible(scopePage);
    realtimeHeadingLabel.setVisible(scopePage);

    for (auto* component : std::array<juce::Component*, 14> {
             &frequencyBlockSizeControl, &frequencyOverlapControl, &frequencyAverageTimeControl,
             &frequencyFilledDisplayButton, &frequencySecondSpectrumButton,
             &frequencyFirstSpectrumTypeControl, &frequencySecondSpectrumTypeControl,
             &frequencyAntiAliasButton, &frequencySlopeControl, &frequencyHostClearButton,
             &frequencyRangesButton, &frequencyCursorButton,
             &frequencyMonitorControlsButton, &frequencyZoomControlsButton })
        component->setVisible(frequencyPage);

    for (auto* component : std::array<juce::Component*, 12> {
             &correlationBlockSizeControl, &correlationOverlapControl, &correlationAverageTimeControl,
             &correlationSmoothingControl, &correlationFirstSpectrumTypeControl,
             &correlationSecondSpectrumTypeControl,
             &correlationFilledDisplayButton, &correlationSecondSpectrumButton,
             &correlationHostClearButton, &correlationRangesButton,
             &correlationCursorButton, &correlationZoomControlsButton })
        component->setVisible(correlationPage);

    if (! scopePage)
        return;

    const auto activeSplitCount = processor.getActiveSplitCount();
    addCrossoverButton.setEnabled(activeSplitCount < crossoverControls.size());
    removeCrossoverButton.setEnabled(activeSplitCount > 0);

    for (size_t index = 0; index < crossoverControls.size(); ++index)
        crossoverControls[index]->setInteractionEnabled(index < activeSplitCount);

    const auto noteTime = processor.isScopeTimeNoteBased();
    timeControl.setVisible(! noteTime);
    timeNoteControl.setVisible(noteTime);

    if (noteTime && focusedParameterTarget == &timeControl)
        clearFocusedParameterControl();

    if (focusedParameterTarget != nullptr && ! focusedParameterTarget->isInteractionEnabled())
    {
        focusedParameterTarget->setSelected(false);
        focusedParameterTarget = nullptr;
        focusedParameterControl.setEnabled(false);
    }
}

void AnaCrossoverSettingsComponent::focusParameterControl(AnaParameterControl& control)
{
    if (! control.isInteractionEnabled() || ! control.supportsFocusedPotentiometer())
        return;

    if (focusedParameterTarget != nullptr)
        focusedParameterTarget->setSelected(false);

    focusedParameterTarget = &control;
    focusedParameterTarget->setSelected(true);

    focusedParameterControl.setEnabled(true);
    syncFocusedParameterControl();
}

void AnaCrossoverSettingsComponent::clearFocusedParameterControl()
{
    if (focusedParameterTarget != nullptr)
        focusedParameterTarget->setSelected(false);

    focusedParameterTarget = nullptr;
    focusedParameterControl.setEnabled(false);
}

void AnaCrossoverSettingsComponent::dismissParameterEditors()
{
    styleControl.commitPendingEditor();
    opacityControl.commitPendingEditor();
    timeControl.commitPendingEditor();
    timeNoteControl.commitPendingEditor();
    timeBaseControl.commitPendingEditor();
    frequencyBlockSizeControl.commitPendingEditor();
    frequencyOverlapControl.commitPendingEditor();
    frequencyAverageTimeControl.commitPendingEditor();
    frequencyFirstSpectrumTypeControl.commitPendingEditor();
    frequencySecondSpectrumTypeControl.commitPendingEditor();
    frequencySlopeControl.commitPendingEditor();
    correlationBlockSizeControl.commitPendingEditor();
    correlationOverlapControl.commitPendingEditor();
    correlationAverageTimeControl.commitPendingEditor();
    correlationSmoothingControl.commitPendingEditor();

    for (auto& control : crossoverControls)
        control->commitPendingEditor();
}

void AnaCrossoverSettingsComponent::syncFocusedParameterControl()
{
    if (focusedParameterTarget == nullptr
        || ! focusedParameterTarget->isInteractionEnabled()
        || ! focusedParameterTarget->supportsFocusedPotentiometer())
        return;

    const auto& target = focusedParameterTarget->getSlider();
    const auto normalisedValue = target.getNormalisableRange().convertTo0to1(target.getValue());

    if (std::abs(focusedParameterControl.getValue() - normalisedValue) > 1.0e-6)
    {
        const juce::ScopedValueSetter<bool> guard(updatingFocusedParameterControl, true);
        focusedParameterControl.setValue(normalisedValue, juce::dontSendNotification);
    }
}

AnaAudioProcessorEditor::AnaAudioProcessorEditor(AnaAudioProcessor& processorRef)
    : AudioProcessorEditor(&processorRef)
#if JucePlugin_Enable_ARA
    , AudioProcessorEditorARAExtension(&processorRef)
#endif
    , audioProcessor(processorRef),
      scopeDisplay(processorRef),
      crossoverSettings(processorRef),
      frequencyDisplay(processorRef),
      correlationDisplay(processorRef)
{
    for (auto* component : std::array<juce::Component*, 17> {
             &scopeDisplay, &crossoverSettings, &frequencyDisplay, &correlationDisplay,
             &offlineUpdateLabel, &frequencyButton, &phaseButton,
             &scopeButton, &settingsButton, &realtimeButton, &offlineButton, &sourceButton,
             &takeButton, &refreshButton, &fullSourceButton, &clearButton, &freezeButton })
        addAndMakeVisible(*component);

    frequencyButton.onClick = [this] { showAnalyzerPage(AnaAnalyzerPage::frequency, false); };
    phaseButton.onClick = [this] { showAnalyzerPage(AnaAnalyzerPage::correlation, false); };
    scopeButton.onClick = [this] { showAnalyzerPage(AnaAnalyzerPage::scope, false); };
    settingsButton.onClick = [this] { showAnalyzerSettings(! showingAnalyzerSettings); };

    offlineUpdateLabel.setFont(ana::ui::makeFont());
    offlineUpdateLabel.setJustificationType(juce::Justification::centred);
    offlineUpdateLabel.setColour(juce::Label::textColourId, ana::ui::white);
    offlineUpdateLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    offlineUpdateLabel.setColour(juce::Label::outlineColourId, ana::ui::grey500);
    offlineUpdateLabel.setBorderSize(
        juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    offlineUpdateLabel.setInterceptsMouseClicks(false, false);
    offlineUpdateLabel.setVisible(true);
    scopeDisplay.onOfflineUpdateStatus = [this] (const juce::String& status)
    {
        offlineUpdateLabel.setText(status, juce::dontSendNotification);
    };
    correlationDisplay.onOfflineUpdateStatus = [this] (const juce::String& status)
    {
        offlineUpdateLabel.setText(status, juce::dontSendNotification);
    };
    crossoverSettings.onDisplaySettingsChanged = [this] { scopeDisplay.refreshDisplaySettings(); };
    crossoverSettings.onEqualBandHeights = [this] { scopeDisplay.equalizeBandHeights(); };
    crossoverSettings.onChoiceRequested = [this] (AnaParameterControl& control)
    {
        showChoicePrompt(control);
    };

    freezeButton.setClickingTogglesState(true);
    freezeButton.onClick = [this]
    {
        if (activePage == AnaAnalyzerPage::frequency)
            audioProcessor.getFrequencySpectrum().setFrozen(freezeButton.getToggleState());
        else if (activePage == AnaAnalyzerPage::correlation)
            audioProcessor.getCorrelationSpectrum().setFrozen(freezeButton.getToggleState());
        else
        {
            scopeFrozen = freezeButton.getToggleState();
            scopeDisplay.setFrozen(scopeFrozen);
        }
    };
    refreshButton.onClick = [this] { scopeDisplay.refreshWaveform(); };
    fullSourceButton.onClick = [this]
    {
        const auto enabled = ! audioProcessor.isScopeFullSourceView();
        audioProcessor.setScopeFullSourceView(enabled);
        scopeDisplay.setFullSourceView(enabled);
    };
    clearButton.onClick = [this]
    {
        if (activePage == AnaAnalyzerPage::frequency)
            audioProcessor.clearFrequencySpectrum();
        else if (activePage == AnaAnalyzerPage::correlation)
            audioProcessor.clearCorrelationSpectrum();
        else
            scopeDisplay.clearHistory();
    };
    realtimeButton.onClick = [this] { audioProcessor.setOfflineMode(false); };
    realtimeButton.setTooltip("Realtime input mode");
    offlineButton.onClick = [this] { audioProcessor.setOfflineMode(true); };
    offlineButton.setTooltip("ARA offline source mode");
    sourceButton.onClick = [this] { showOfflineSourcePrompt(); };
    takeButton.onClick = [this] { showOfflineTakePrompt(); };

    showAnalyzerPage(static_cast<AnaAnalyzerPage>(audioProcessor.getAnalyzerPageState()),
                     false);
    timerCallback();
    startTimerHz(15);
    setResizable(true, false);
    setResizeLimits(minimumEditorWidth, minimumEditorHeight, maximumEditorSize, maximumEditorSize);
    const auto savedSize = audioProcessor.getLastEditorSize();
    setSize(juce::jlimit(minimumEditorWidth, maximumEditorSize,
                         savedSize.x > 0 ? savedSize.x : defaultEditorWidth),
            juce::jlimit(minimumEditorHeight, maximumEditorSize,
                         savedSize.y > 0 ? savedSize.y : defaultEditorHeight));
}

AnaAudioProcessorEditor::~AnaAudioProcessorEditor()
{
    if (getWidth() > 0 && getHeight() > 0)
        audioProcessor.setLastEditorSize(getWidth(), getHeight());
}

void AnaAudioProcessorEditor::showAnalyzerSettings(const bool shouldShowSettings)
{
    showAnalyzerPage(activePage, shouldShowSettings);
}

void AnaAudioProcessorEditor::showAnalyzerPage(const AnaAnalyzerPage page,
                                               const bool shouldShowSettings)
{
    if (! shouldShowSettings)
        dismissChoicePrompt();

    activePage = page;
    audioProcessor.setAnalyzerPageState(static_cast<int>(page));
    showingAnalyzerSettings = shouldShowSettings;
    scopeButton.setToggleState(page == AnaAnalyzerPage::scope, juce::dontSendNotification);
    frequencyButton.setToggleState(page == AnaAnalyzerPage::frequency, juce::dontSendNotification);
    phaseButton.setToggleState(page == AnaAnalyzerPage::correlation, juce::dontSendNotification);
    settingsButton.setToggleState(shouldShowSettings, juce::dontSendNotification);
    scopeDisplay.setVisible(page == AnaAnalyzerPage::scope);
    frequencyDisplay.setVisible(page == AnaAnalyzerPage::frequency);
    correlationDisplay.setVisible(page == AnaAnalyzerPage::correlation);
    fullSourceButton.setEnabled(page == AnaAnalyzerPage::scope);
    freezeButton.setToggleState(page == AnaAnalyzerPage::frequency
                                    ? audioProcessor.getFrequencySpectrum().isFrozen()
                                    : page == AnaAnalyzerPage::correlation
                                        ? audioProcessor.getCorrelationSpectrum().isFrozen()
                                    : scopeFrozen,
                                juce::dontSendNotification);
    crossoverSettings.setAnalyzerPage(page);
    crossoverSettings.setVisible(shouldShowSettings);

    if (shouldShowSettings)
        crossoverSettings.toFront(false);

    resized();
    repaint();
}

void AnaAudioProcessorEditor::showChoicePrompt(AnaParameterControl& control)
{
    const auto choices = control.getChoiceNames();

    if (choices.isEmpty())
        return;

    const auto anchorBounds = getLocalArea(&control, control.getValueBounds());
    juce::Component::SafePointer<AnaParameterControl> safeControl(&control);
    showChoicePrompt(anchorBounds, choices, control.getSelectedChoiceIndex(),
        [safeControl] (const int selectedIndex)
        {
            if (safeControl != nullptr)
                safeControl->setSelectedChoiceIndex(selectedIndex);
        });
}

void AnaAudioProcessorEditor::showChoicePrompt(
    const juce::Rectangle<int> anchorBounds,
    juce::StringArray choices,
    const int selectedIndex,
    std::function<void(int)> onSelect)
{
    if (choices.isEmpty())
        return;

    dismissChoicePrompt();
    choicePrompt = std::make_unique<AnaChoicePrompt>(
        anchorBounds,
        std::move(choices),
        selectedIndex,
        std::move(onSelect),
        [this] { dismissChoicePrompt(); });
    addAndMakeVisible(*choicePrompt);
    choicePrompt->setBounds(getLocalBounds());
    choicePrompt->toFront(true);
}

void AnaAudioProcessorEditor::showOfflineSourcePrompt()
{
    refreshOfflineSelectionButtons();
    juce::StringArray names { "ALL" };
    std::vector<juce::String> choiceIds { juce::String() };
    const auto selectedId = audioProcessor.getSelectedOfflineSourceId();
    auto selectedIndex = 0;

    for (const auto& choice : offlineSourceTakeChoices)
    {
        if (std::find(choiceIds.begin(), choiceIds.end(), choice.sourceId) != choiceIds.end())
            continue;

        choiceIds.push_back(choice.sourceId);
        names.add(choice.sourceName);

        if (choice.sourceId == selectedId)
            selectedIndex = names.size() - 1;
    }

    showChoicePrompt(getLocalArea(&sourceButton, sourceButton.getLocalBounds()),
                     std::move(names), selectedIndex,
                     [this, selectedIds = std::move(choiceIds)] (const int index)
                     {
                         if (! juce::isPositiveAndBelow(index, static_cast<int>(selectedIds.size())))
                             return;

                         audioProcessor.setSelectedOfflineSourceId(
                             selectedIds[static_cast<size_t>(index)]);
                         refreshOfflineSelectionButtons();
                         scopeDisplay.refreshWaveform();
                     });
}

void AnaAudioProcessorEditor::showOfflineTakePrompt()
{
    refreshOfflineSelectionButtons();
    juce::StringArray names;
    std::vector<juce::String> choiceIds;
    const auto selectedSourceId = audioProcessor.getSelectedOfflineSourceId();
    const auto selectedTakeId = audioProcessor.getSelectedOfflineTakeId();
    auto selectedIndex = -1;

    for (const auto& choice : offlineSourceTakeChoices)
    {
        if (selectedSourceId.isNotEmpty() && choice.sourceId != selectedSourceId)
            continue;
        const auto takeSelection = juce::String(choice.takeNumber);
        if (std::find(choiceIds.begin(), choiceIds.end(), takeSelection) != choiceIds.end())
            continue;

        choiceIds.push_back(takeSelection);
        names.add(takeSelection);

        if (takeSelection == selectedTakeId)
            selectedIndex = names.size() - 1;
    }

    showChoicePrompt(getLocalArea(&takeButton, takeButton.getLocalBounds()),
                     std::move(names), selectedIndex,
                     [this, selectedIds = std::move(choiceIds)] (const int index)
                     {
                         if (! juce::isPositiveAndBelow(index, static_cast<int>(selectedIds.size())))
                             return;

                         audioProcessor.setSelectedOfflineTakeId(
                             selectedIds[static_cast<size_t>(index)]);
                         refreshOfflineSelectionButtons();
                         scopeDisplay.refreshWaveform();
                     });
}

void AnaAudioProcessorEditor::refreshOfflineSelectionButtons()
{
    offlineSourceTakeChoices = audioProcessor.getOfflineSourceTakeChoices();
    auto sourceId = audioProcessor.getSelectedOfflineSourceId();
    auto takeId = audioProcessor.getSelectedOfflineTakeId();
    auto selectedTakeNumber = 0;
    std::vector<int> availableTakeNumbers;
    if (! offlineSourceTakeChoices.empty())
    {
        const auto sourceIsValid = sourceId.isEmpty()
            || std::any_of(offlineSourceTakeChoices.begin(), offlineSourceTakeChoices.end(),
                           [&sourceId] (const auto& choice)
                           {
                               return choice.sourceId == sourceId;
                           });

        if (! sourceIsValid)
        {
            audioProcessor.setSelectedOfflineSourceId({});
            sourceId.clear();
        }

        for (const auto& choice : offlineSourceTakeChoices)
        {
            if (sourceId.isNotEmpty() && choice.sourceId != sourceId)
                continue;

            if (std::find(availableTakeNumbers.begin(), availableTakeNumbers.end(),
                          choice.takeNumber) == availableTakeNumbers.end())
                availableTakeNumbers.push_back(choice.takeNumber);
        }

        std::sort(availableTakeNumbers.begin(), availableTakeNumbers.end());
        const auto requestedTakeNumber = takeId.getIntValue();
        const auto takeIsValid = std::find(availableTakeNumbers.begin(),
                                           availableTakeNumbers.end(),
                                           requestedTakeNumber) != availableTakeNumbers.end();
        if (takeIsValid)
        {
            selectedTakeNumber = requestedTakeNumber;
            takeId = juce::String(selectedTakeNumber);
            if (audioProcessor.getSelectedOfflineTakeId() != takeId)
                audioProcessor.setSelectedOfflineTakeId(takeId);
        }
        else if (! availableTakeNumbers.empty())
        {
            selectedTakeNumber = availableTakeNumbers.front();
            takeId = juce::String(selectedTakeNumber);
            audioProcessor.setSelectedOfflineTakeId(takeId);
        }
    }

    const auto araAvailable = audioProcessor.isARAAvailable();
    sourceButton.setEnabled(araAvailable && ! offlineSourceTakeChoices.empty());
    takeButton.setEnabled(araAvailable && ! availableTakeNumbers.empty());
    sourceButton.setToggleState(sourceId.isNotEmpty(), juce::dontSendNotification);
    takeButton.setToggleState(false, juce::dontSendNotification);
    sourceButton.setTooltip(sourceId.isEmpty() ? "SOURCE: ALL" : "SOURCE: SELECTED");
    takeButton.setTooltip(selectedTakeNumber > 0
        ? "TAKE: " + juce::String(selectedTakeNumber)
        : "TAKE: NONE");
}

void AnaAudioProcessorEditor::dismissChoicePrompt()
{
    choicePrompt.reset();
}

void AnaAudioProcessorEditor::timerCallback()
{
    const auto offline = audioProcessor.isOfflineMode();
    realtimeButton.setToggleState(! offline, juce::dontSendNotification);
    offlineButton.setToggleState(offline, juce::dontSendNotification);
    fullSourceButton.setToggleState(audioProcessor.isScopeFullSourceView(),
                                    juce::dontSendNotification);
    refreshOfflineSelectionButtons();
    if (offline && offlineSourceTakeChoices.empty())
        offlineUpdateLabel.setText("NO FILES", juce::dontSendNotification);
    else if (offline && offlineUpdateLabel.getText() == "NO FILES")
        offlineUpdateLabel.setText("UPDATING...", juce::dontSendNotification);
    else if (! offline && offlineUpdateLabel.getText() == "NO FILES")
        offlineUpdateLabel.setText({}, juce::dontSendNotification);
    sourceButton.setEnabled(offline && sourceButton.isEnabled());
    takeButton.setEnabled(offline && takeButton.isEnabled());
    refreshButton.setEnabled(offline);
    clearButton.setEnabled(! offline);
    freezeButton.setEnabled(! offline);
    fullSourceButton.setEnabled(activePage == AnaAnalyzerPage::scope);
    freezeButton.setToggleState(activePage == AnaAnalyzerPage::frequency
                                    ? audioProcessor.getFrequencySpectrum().isFrozen()
                                    : activePage == AnaAnalyzerPage::correlation
                                        ? audioProcessor.getCorrelationSpectrum().isFrozen()
                                    : scopeFrozen,
                                juce::dontSendNotification);

    if (editorSizeSavePending
        && juce::Time::getMillisecondCounterHiRes() >= editorSizeSaveDeadlineMilliseconds)
    {
        audioProcessor.setLastEditorSize(pendingEditorSize.x, pendingEditorSize.y);
        editorSizeSavePending = false;
    }
}

void AnaAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);
}

void AnaAudioProcessorEditor::resized()
{
    if (getWidth() > 0 && getHeight() > 0)
    {
        pendingEditorSize = { getWidth(), getHeight() };
        editorSizeSaveDeadlineMilliseconds = juce::Time::getMillisecondCounterHiRes() + 250.0;
        editorSizeSavePending = true;
    }

    auto area = getLocalBounds().reduced(ana::ui::gap.pixels());
    auto controlsRow = area.removeFromTop(ana::ui::controlHeight);
    auto modeArea = controlsRow.removeFromLeft(
        std::min(225, juce::roundToInt(static_cast<float>(controlsRow.getWidth()) * 0.34f)));
    const auto modeButtonWidth = std::max(0, modeArea.getWidth() - ana::ui::gap.pixels() * 2) / 3;
    frequencyButton.setBounds(modeArea.removeFromLeft(modeButtonWidth));
    ana::ui::gap.removeFromLeft(modeArea);
    phaseButton.setBounds(modeArea.removeFromLeft(modeButtonWidth));
    ana::ui::gap.removeFromLeft(modeArea);
    scopeButton.setBounds(modeArea);

    ana::ui::gap.removeFromLeft(controlsRow);

    ana::ui::FixedGapRow actionRow(controlsRow);
    realtimeButton.setBounds(actionRow.takeLeft(86));
    clearButton.setBounds(actionRow.takeLeft(60));
    freezeButton.setBounds(actionRow.takeLeft(72));
    fullSourceButton.setBounds(actionRow.takeLeft(60));
    offlineButton.setBounds(actionRow.takeLeft(76));
    sourceButton.setBounds(actionRow.takeLeft(76));
    takeButton.setBounds(actionRow.takeLeft(68));
    refreshButton.setBounds(actionRow.takeLeft(76));

    const auto updateWidth = std::max(0, actionRow.remaining().getWidth() - 95 - ana::ui::gap.pixels());
    offlineUpdateLabel.setBounds(actionRow.takeLeft(updateWidth));
    settingsButton.setBounds(actionRow.remaining());

    ana::ui::gap.removeFromTop(area);
    scopeDisplay.setBounds(area);
    frequencyDisplay.setBounds(area);
    correlationDisplay.setBounds(area);

    const auto popupWidth = std::min(area.getWidth(),
        std::min(420, std::max(320, juce::roundToInt(static_cast<float>(area.getWidth()) * 0.42f))));
    crossoverSettings.setBounds(area.removeFromRight(popupWidth));

    if (showingAnalyzerSettings)
        crossoverSettings.toFront(false);

    if (choicePrompt != nullptr)
    {
        choicePrompt->setBounds(getLocalBounds());
        choicePrompt->toFront(true);
    }
}
