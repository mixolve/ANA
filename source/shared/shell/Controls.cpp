#include "Controls.h"
#include "TablerIcons.h"
#include "Theme.h"

#include <algorithm>
#include <cmath>
#include <utility>

void LongPressGesture::begin(const bool shouldArmLongPress)
{
    stopTimer();
    active = true;
    dragged = false;
    armed = false;

    if (shouldArmLongPress)
        startTimer(delayMs);
}

void LongPressGesture::markDragged()
{
    if (! active)
        return;

    dragged = true;
    armed = false;
    stopTimer();
}

void LongPressGesture::cancelArming()
{
    armed = false;
    stopTimer();
}

void LongPressGesture::cancel()
{
    stopTimer();
    active = false;
    dragged = false;
    armed = false;
}

LongPressGesture::ReleaseResult LongPressGesture::release()
{
    if (! active)
        return ReleaseResult::none;

    stopTimer();
    const auto result = dragged ? ReleaseResult::cancelled
                                : armed ? ReleaseResult::longPress
                                        : ReleaseResult::shortPress;
    active = false;
    dragged = false;
    armed = false;
    return result;
}

void LongPressGesture::timerCallback()
{
    stopTimer();

    if (! active || dragged)
        return;

    armed = true;
    if (onArmed)
        onArmed();
}

void EllipsisLabel::paint(juce::Graphics& graphics)
{
    if (isBeingEdited())
    {
        getLookAndFeel().drawLabel(graphics, *this);
        return;
    }

    if (drawBackground)
        graphics.fillAll(findColour(juce::Label::backgroundColourId));
    graphics.setColour(isEnabled() ? findColour(juce::Label::textColourId)
                                   : ana::ui::light);
    graphics.setFont(getFont());
    graphics.drawText(getText(), getBorderSize().subtractedFrom(getLocalBounds())
                                     .translated(0, textVerticalOffset),
                      getJustificationType(), true);
    graphics.setColour(isEnabled() ? findColour(juce::Label::outlineColourId)
                                   : ana::ui::light);
    graphics.drawRect(getLocalBounds());
}

void EllipsisLabel::editorShown(juce::TextEditor* editor)
{
    if (editor == nullptr)
        return;

    editor->setBorder(juce::BorderSize<int>(0));
    editor->setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::highlightColourId, juce::Colour(0xff444444));
    editor->setColour(juce::TextEditor::highlightedTextColourId, ana::ui::white);

    // Auxiliary windows pass shortcuts to the host except while inline text editing is active.
    if (onEditorVisibilityChanged)
        onEditorVisibilityChanged(true);

}

void EllipsisLabel::editorAboutToBeHidden(juce::TextEditor*)
{
    // Re-enable host shortcut passthrough after Label finishes TextEditor teardown.
    juce::Component::SafePointer<EllipsisLabel> safeLabel(this);
    juce::MessageManager::callAsync([safeLabel]
    {
        if (safeLabel != nullptr && ! safeLabel->isBeingEdited()
            && safeLabel->onEditorVisibilityChanged)
            safeLabel->onEditorVisibilityChanged(false);
    });
}

ControlButton::ControlButton(juce::String text)
    : juce::Button(std::move(text))
{
    iconButton = getButtonText() == "adjustments-alt"
        || getButtonText() == "snowflake"
        || getButtonText() == "refresh"
        || getButtonText() == "x"
        || getButtonText() == "hexagons"
        || getButtonText() == "plus"
        || getButtonText() == "camera"
        || getButtonText() == "eye-off"
        || getButtonText() == "palette"
        || getButtonText() == "arrows-up-down"
        || getButtonText() == "arrows-down"
        || getButtonText() == "browser-maximize"
        || getButtonText() == "eraser"
        || getButtonText() == "layout-rows";
    if (iconButton)
        symbolImage = loadTablerIcon(getButtonText(), ana::ui::iconFontSize);
    setWantsKeyboardFocus(false);
}

int ControlButton::getPreferredWidth() const noexcept
{
    return iconButton ? ana::ui::iconControlSize : ana::ui::textControlWidth(getButtonText());
}

void ControlButton::paintButton(juce::Graphics& graphics,
                                 const bool shouldDrawButtonAsHighlighted,
                                 const bool shouldDrawButtonAsDown)
{
    const auto bounds = getLocalBounds();
    const auto hovered = isEnabled()
        && (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown);
    const auto fill = hovered ? ana::ui::light : ana::ui::dark;

    graphics.setColour(fill);
    graphics.fillRect(bounds);
    const auto active = isEnabled() && getToggleState();
    graphics.setColour(active ? ana::ui::white : ana::ui::light);
    graphics.drawRect(bounds, active ? ana::ui::activeBorderWidth : 1);
    const auto foreground = ! isEnabled() ? ana::ui::light
        : hovered ? juce::Colours::black : ana::ui::white;
    graphics.setColour(foreground);
    if (symbolImage.isValid())
    {
        juce::DrawableImage drawable;
        drawable.setImage(symbolImage);
        drawable.setOverlayColour(foreground);
        const auto iconSize = juce::roundToInt(ana::ui::iconFontSize);
        drawable.drawWithin(graphics, getLocalBounds().withSizeKeepingCentre(iconSize, iconSize).toFloat(),
                            juce::RectanglePlacement::centred, 1.0f);
    }
    else
    {
        auto font = ana::ui::makeFont();
        if (iconButton)
            font.setHeight(ana::ui::iconFontSize);
        graphics.setFont(font);
        graphics.drawText(getButtonText(), getLocalBounds().reduced(ana::ui::gap.pixels(), 1),
                          juce::Justification::centred, true);
    }
}

juce::Slider::SliderLayout SliderLookAndFeel::getSliderLayout(juce::Slider& slider)
{
    if (slider.getSliderStyle() == juce::Slider::LinearBarVertical)
        return { slider.getLocalBounds(), {} };

    return juce::LookAndFeel_V4::getSliderLayout(slider);
}

void SliderLookAndFeel::drawLinearSlider(juce::Graphics& graphics,
                                            const int x, const int y, const int width, const int height,
                                            const float sliderPosition,
                                            const float minimumSliderPosition,
                                            const float maximumSliderPosition,
                                            const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearBarVertical)
    {
        const auto frameBounds = juce::Rectangle<int>(x, y, width, height);
        const auto bounds = frameBounds.toFloat();
        const auto markerY = juce::jlimit(bounds.getY(), bounds.getBottom(), sliderPosition);
        const auto neutralProportion = static_cast<float>(slider.valueToProportionOfLength(0.0));
        const auto neutralY = juce::jmap(neutralProportion, bounds.getBottom(), bounds.getY());
        const auto fillTop = std::min(markerY, neutralY);
        const auto fillBottom = std::max(markerY, neutralY);
        graphics.setColour(ana::ui::dark);
        graphics.fillRect(bounds);
        graphics.setColour(slider.isEnabled() ? ana::ui::light : ana::ui::dark);
        graphics.fillRect(bounds.withTop(fillTop).withBottom(fillBottom));
        graphics.setColour(ana::ui::light);
        graphics.fillRect(bounds.getX(), neutralY - 0.5f, bounds.getWidth(), 1.0f);
        constexpr float markerThickness = 8.0f;
        const auto markerTop = juce::jlimit(bounds.getY(), bounds.getBottom() - markerThickness,
                                            markerY - markerThickness * 0.5f);
        graphics.setColour(slider.isEnabled() ? ana::ui::white : ana::ui::light);
        graphics.fillRect(bounds.getX(), markerTop, bounds.getWidth(), markerThickness);
        graphics.setColour(ana::ui::light);
        graphics.drawRect(frameBounds, 1);
        return;
    }

    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
    {
        juce::LookAndFeel_V4::drawLinearSlider(graphics, x, y, width, height,
                                               sliderPosition, minimumSliderPosition, maximumSliderPosition,
                                               style, slider);
        return;
    }

    const auto frameBounds = juce::Rectangle<int>(x, y, width, height);
    const auto bounds = frameBounds.toFloat();
    const auto markerX = juce::jlimit(bounds.getX(), bounds.getRight(), sliderPosition);

    graphics.setColour(ana::ui::dark);
    graphics.fillRect(bounds);
    if (! slider.isEnabled())
    {
        graphics.setColour(ana::ui::light);
        graphics.drawRect(frameBounds, 1);
        return;
    }

    graphics.setColour(ana::ui::light);
    graphics.fillRect(bounds.withRight(markerX));
    constexpr float markerThickness = 8.0f;
    const auto markerLeft = juce::jlimit(bounds.getX(), bounds.getRight() - markerThickness,
                                         markerX - markerThickness * 0.5f);
    graphics.setColour(ana::ui::white);
    graphics.fillRect(markerLeft, bounds.getY(), markerThickness, bounds.getHeight());
    graphics.setColour(ana::ui::light);
    graphics.drawRect(frameBounds, 1);

    if (style == juce::Slider::LinearBar)
    {
        graphics.setColour(ana::ui::white);
        graphics.setFont(ana::ui::makeFont());
        graphics.drawText(slider.getTextFromValue(slider.getValue()),
                          ana::ui::readoutTextBounds(bounds.toNearestInt().reduced(ana::ui::gap.pixels(), 1)),
                          juce::Justification::centred, true);
    }
}

void SliderLookAndFeel::drawScrollbar(juce::Graphics& graphics,
                                         juce::ScrollBar&,
                                         const int x, const int y, const int width, const int height,
                                         const bool isScrollbarVertical,
                                         const int thumbStartPosition,
                                         const int thumbSize,
                                         const bool,
                                         const bool isMouseDown)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height);
    graphics.setColour(ana::ui::dark);
    graphics.fillRect(bounds);
    graphics.setColour(ana::ui::light);
    graphics.drawRect(bounds, 1);

    if (thumbSize <= 0)
        return;

    auto thumb = isScrollbarVertical
        ? juce::Rectangle<int>(x + 2, thumbStartPosition, std::max(1, width - 4), thumbSize)
        : juce::Rectangle<int>(thumbStartPosition, y + 2, thumbSize, std::max(1, height - 4));
    graphics.setColour(isMouseDown ? ana::ui::white : ana::ui::light);
    graphics.fillRect(thumb);
}

juce::Label* SliderLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(ana::ui::makeFont());
    label->setJustificationType(juce::Justification::centred);
    label->setColour(juce::Label::backgroundColourId, ana::ui::dark);
    label->setColour(juce::Label::outlineColourId, ana::ui::light);
    label->setColour(juce::Label::textColourId, ana::ui::white);
    label->setColour(juce::Label::textWhenEditingColourId, ana::ui::white);
    return label;
}

FocusedPotentiometer::FocusedPotentiometer()
{
    setLookAndFeel(&lookAndFeel);
    setSliderStyle(juce::Slider::LinearHorizontal);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setRange(0.0, 1.0, 0.0);
    setSliderSnapsToMousePosition(false);
    setScrollWheelEnabled(true);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setEnabled(false);
}

FocusedPotentiometer::~FocusedPotentiometer()
{
    setLookAndFeel(nullptr);
}

ChoicePopup::ChoicePopup(juce::Rectangle<int> anchorBoundsIn,
                                 juce::StringArray choicesIn,
                                 std::vector<bool> enabledChoices,
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
        auto button = std::make_unique<ControlButton>(choices[index]);
        const auto enabled = enabledChoices.empty()
            || (static_cast<size_t>(index) < enabledChoices.size()
                && enabledChoices[static_cast<size_t>(index)]);
        button->setEnabled(enabled);
        button->setToggleState(enabled && index == selectedIndex,
                               juce::dontSendNotification);
        button->onClick = [this, index] { choose(index); };
        addAndMakeVisible(*button);
        choiceButtons.push_back(std::move(button));
    }
}

void ChoicePopup::paintOverChildren(juce::Graphics& graphics)
{
    graphics.setColour(ana::ui::white);
    graphics.drawRect(panelBounds, 2);
}

void ChoicePopup::resized()
{
    constexpr int popupItemGap = 0;
    const auto itemCount = static_cast<int>(choiceButtons.size());
    const auto contentHeight = itemCount * ana::ui::controlHeight
        + std::max(0, itemCount - 1) * popupItemGap;
    auto choiceWidth = ana::ui::textControlWidth(1);
    for (const auto& choice : choices)
        choiceWidth = std::max(choiceWidth, ana::ui::textControlWidth(choice));
    const auto desiredPanelWidth = choiceWidth;
    const auto panelWidth = std::max(1, std::min(
        std::max(anchorBounds.getWidth(), desiredPanelWidth),
        getWidth() - ana::ui::gap.pixels() * 2));
    const auto panelHeight = std::min(getHeight() - ana::ui::gap.pixels() * 2,
                                      contentHeight);
    panelBounds = juce::Rectangle<int>(panelWidth, std::max(1, panelHeight));
    panelBounds.setPosition(anchorBounds.getPosition());
    panelBounds = panelBounds.constrainedWithin(getLocalBounds().reduced(ana::ui::gap.pixels()));

    auto area = panelBounds;
    for (size_t index = 0; index < choiceButtons.size(); ++index)
    {
        const auto row = area.removeFromTop(ana::ui::controlHeight);
        choiceButtons[index]->setBounds(row);

        if (index + 1 < choiceButtons.size())
            area.removeFromTop(popupItemGap);
    }
}

void ChoicePopup::mouseDown(const juce::MouseEvent& event)
{
    if (! panelBounds.contains(event.getPosition()))
        close();
}

void ChoicePopup::choose(const int index)
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

void ChoicePopup::close()
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

void RangeSlider::paint(juce::Graphics& graphics)
{
    const auto frameBounds = getLocalBounds();
    const auto bounds = frameBounds.toFloat();

    graphics.setColour(ana::ui::dark);
    graphics.fillRect(bounds);
    graphics.setColour(ana::ui::light);

    constexpr float handleThickness = 8.0f;

    if (orientation == Orientation::horizontal)
    {
        const auto startX = juce::jmap(rangeStart, bounds.getX(), bounds.getRight());
        const auto endX = juce::jmap(rangeEnd, bounds.getX(), bounds.getRight());
        graphics.fillRect(bounds.withLeft(startX).withRight(endX));

        const auto handleX = [bounds] (const float centreX)
        {
            return juce::jlimit(bounds.getX(), bounds.getRight() - handleThickness,
                                centreX - handleThickness * 0.5f);
        };
        graphics.setColour(ana::ui::white);
        graphics.fillRect(handleX(startX), bounds.getY(), handleThickness, bounds.getHeight());
        graphics.fillRect(handleX(endX), bounds.getY(), handleThickness, bounds.getHeight());
    }
    else
    {
        const auto startY = juce::jmap(rangeStart, bounds.getY(), bounds.getBottom());
        const auto endY = juce::jmap(rangeEnd, bounds.getY(), bounds.getBottom());
        graphics.fillRect(bounds.withTop(startY).withBottom(endY));

        const auto handleY = [bounds] (const float centreY)
        {
            return juce::jlimit(bounds.getY(), bounds.getBottom() - handleThickness,
                                centreY - handleThickness * 0.5f);
        };
        graphics.setColour(ana::ui::white);
        graphics.fillRect(bounds.getX(), handleY(startY), bounds.getWidth(), handleThickness);
        graphics.fillRect(bounds.getX(), handleY(endY), bounds.getWidth(), handleThickness);
    }

    graphics.setColour(ana::ui::light);
    graphics.drawRect(frameBounds, 1);
}

void RangeSlider::mouseDown(const juce::MouseEvent& event)
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

void RangeSlider::mouseDrag(const juce::MouseEvent& event)
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

void RangeSlider::mouseUp(const juce::MouseEvent&)
{
    const auto wasDragging = dragMode != DragMode::none;
    dragMode = DragMode::none;

    if (wasDragging && onDragEnded)
        onDragEnded();
}

void RangeSlider::setRange(const float newStart, const float newEnd)
{
    const auto start = juce::jlimit(0.0f, 1.0f, std::min(newStart, newEnd));
    const auto end = juce::jlimit(0.0f, 1.0f, std::max(newStart, newEnd));
    updateRange(start, end);
}

void RangeSlider::updateRange(const float newStart, const float newEnd)
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
