#include "SquareControl.h"
#include "Controls.h"
#include "ValueRange.h"
#include "PointerInput.h"
#include "PopupFrame.h"
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

ControlButton::ControlButton(juce::String text)
    : juce::Button(std::move(text))
{
    addChildComponent(slotIndicator);
    slotIndicator.onMark=[this] { if(onMark) onMark(); };
    setWantsKeyboardFocus(false);
}

int ControlButton::getPreferredWidth() const noexcept
{
    return ana::ui::textControlWidth(getButtonText());
}

void ControlButton::mouseDown(const juce::MouseEvent& event)
{
    if(squareStyle && onMark && event.mods.isCtrlDown() && !event.mods.isShiftDown()
       && !event.mods.isAltDown() && !event.mods.isCommandDown()
       && (event.mods.isLeftButtonDown() || event.mods.isRightButtonDown())) { marking=true; return; }
    marking=false;
    if (!interactionEnabled) return;
    if (isEnabled() && event.mods.isLeftButtonDown())
    {
        // Start one white flash on press, not a second one on release.
        // Hold the white fill for as long as the mouse button remains down.
        clickHeld = true;
        clickFlash = true;
        repaint();
        startTimer(100);
    }
    juce::Button::mouseDown(event);
}

void ControlButton::mouseUp(const juce::MouseEvent& event)
{
    if(std::exchange(marking,false))
    { if(!event.mouseWasDraggedSinceMouseDown() && getLocalBounds().contains(event.getPosition()) && onMark) onMark(); repaint(); return; }
    if (!interactionEnabled) return;
    clickHeld = false;
    // The click callback may delete this button (e.g. when it closes a window).
    juce::Component::SafePointer<ControlButton> safeThis(this);
    juce::Button::mouseUp(event);
    if (safeThis != nullptr && ! safeThis->isTimerRunning())
    {
        safeThis->clickFlash = false;
        safeThis->repaint();
    }
}

void ControlButton::timerCallback()
{
    stopTimer();
    if (clickHeld)
        return;
    clickFlash = false;
    repaint();
}

void ControlButton::paintButton(juce::Graphics& graphics,
                                 const bool shouldDrawButtonAsHighlighted,
                                 const bool shouldDrawButtonAsDown)
{
    const auto bounds = getLocalBounds();
    if(squareStyle)
    {
        ana::ui::square::paintBody(graphics,bounds);
        if(isMarked && isMarked()) { graphics.setColour(ana::ui::white); graphics.drawRect(bounds,1); }
        const auto hasRail=getClickingTogglesState() || squareRailVisible;
        if(hasRail) ana::ui::square::paintRail(graphics,bounds,
            (clickHeld ? !getToggleState() : getToggleState()) ? 1.0 : 0.0,(isEnabled() && interactionEnabled),clickHeld,
            ana::ui::square::pointerOver(*this,ana::ui::square::marker(bounds,getToggleState()?1.0:0.0)));
        if(hasRail && isMarked && isMarked())
        {
            const auto track=ana::ui::square::rail(bounds);
            graphics.setColour(ana::ui::white);
            graphics.fillRect(track.getX(),track.getCentreY()-1,track.getWidth(),2);
            const auto marker=ana::ui::square::marker(bounds,getToggleState()?1.0:0.0);
            graphics.fillRect(marker);
            if((isEnabled() && interactionEnabled) && !clickHeld && ana::ui::square::pointerOver(*this,marker))
            { graphics.setColour(ana::ui::square::panelBackground); graphics.fillRect(marker); graphics.setColour(ana::ui::white); graphics.drawRect(marker,1); }
        }
        auto textArea=ana::ui::square::textBounds(bounds,hasRail);
        if(bottomSlotVisible) textArea.removeFromBottom(14);
        const auto hovered=(isEnabled() && interactionEnabled) && !ana::ui::square::hoverBlocked()
            && ana::ui::square::pointerOver(*this,bounds);
        const auto now=juce::Time::getMillisecondCounterHiRes();
        if(!hovered) squareHoverAt=0.0;
        else if(squareHoverAt==0.0)
        {
            squareHoverAt=now+50.0;
            juce::Timer::callAfterDelay(50,[safe=juce::Component::SafePointer<ControlButton>(this)]
            { if(safe!=nullptr) safe->repaint(); });
        }
        ana::ui::square::paintTextButton(graphics,textArea,
            hovered && now>=squareHoverAt,
            (isEnabled() && interactionEnabled) && clickHeld,(isEnabled() && interactionEnabled));
        const auto caption=hovered && now>=squareHoverAt && valueTextProvider!=nullptr
            ? valueTextProvider() : getButtonText();
        // L TO R uses the normal 22px font, on a single line, with the full
        // square available instead of shrinking or splitting the caption.
        const auto captionBounds = caption == "L TO R"
            ? juce::Rectangle<int>(bounds.getX(), textArea.getY(), bounds.getWidth(), textArea.getHeight())
            : textArea;
        if(ana::ui::square::drawCaption(graphics,caption,captionBounds))
            ana::ui::scheduleMarqueeRepaint(*this,marqueePending);
        return;
    }
    const auto hovered = (isEnabled() && interactionEnabled) && isShowing()
        && (isMouseOver(true) || shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown);
    const auto active = (isEnabled() && interactionEnabled) && getToggleState();
    const auto amount = hoverAnimation.amount(hovered);
    ControlAppearance style(active, amount);
    style.normalFrameVisible = drawBorder;
    style.paint(graphics, bounds);
    if (clickFlash && (isEnabled() && interactionEnabled))
    {
        graphics.setColour(juce::Colour(0xffffffff));
        graphics.fillRect(bounds);
    }
    const auto foreground = (isEnabled() && interactionEnabled)
        ? (clickFlash ? juce::Colour(0xff222222) : style.contentColour())
        : ana::ui::light;
    graphics.setColour(foreground);
    const auto font = ana::ui::makeFont();
    graphics.setFont(font);
    const auto text = uppercaseText(getButtonText());
    const auto textBounds = getLocalBounds().reduced(textHorizontalInset, 1);
    if (ana::ui::drawLoopingText(graphics, text, textBounds, font))
        ana::ui::scheduleMarqueeRepaint(*this, marqueePending);
}

ChoicePopup::ChoicePopup(juce::Rectangle<int> anchorBoundsIn,
                                 juce::StringArray choicesIn,
                                 std::vector<bool> enabledChoices,
                                 const int selectedIndexIn,
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
    selectedIndex = selectedIndexIn;
    getProperties().set("preserveUnderlyingHover", true);
    choiceViewport.setViewedComponent(&choiceContent, false);
    choiceViewport.setScrollBarThickness(0);
    choiceViewport.onWheel = [this](const juce::MouseEvent& e, const juce::MouseWheelDetails& w) { mouseWheelMove(e, w); };
    choiceViewport.setScrollBarsShown(false, false);
    choiceViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    choiceViewport.setWantsKeyboardFocus(false);
    choiceViewport.setMouseClickGrabsKeyboardFocus(false);
    revealClip.getProperties().set("rangeIndicatorOccluder",true);
    revealClip.addAndMakeVisible(choiceViewport);
    revealClip.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(revealClip);
    reveal.onUpdate = [this]
    {
        updateReveal();
        repaint();
        if (closing && !reveal.isRunning()) completeClose();
    };
    choiceButtons.reserve(static_cast<size_t>(choices.size()));

    for (int index = 0; index < choices.size(); ++index)
    {
        auto button = std::make_unique<ControlButton>(choices[index]);
        button->setDrawBorder(false);
        const auto enabled = enabledChoices.empty()
            || (static_cast<size_t>(index) < enabledChoices.size()
                && enabledChoices[static_cast<size_t>(index)]);
        button->setToggleState(index == selectedIndex,
                               juce::dontSendNotification);
        button->setEnabled(enabled);
        button->onClick = [this, index] { choose(index); };
        choiceContent.addAndMakeVisible(*button);
        choiceButtons.push_back(std::move(button));
    }
}

void ChoicePopup::paintOverChildren(juce::Graphics& graphics)
{
    juce::Graphics::ScopedSaveState saved(graphics);
    graphics.reduceClipRegion(revealClip.getBounds());
    graphics.setColour(ana::ui::white.withMultipliedAlpha(static_cast<float>(reveal.value())));
    popup_list::paintFrame(graphics,panelBounds.toFloat());
    for(size_t index=1;index<choiceButtons.size();++index) {
        const auto row=getLocalArea(&choiceContent,choiceButtons[index]->getBounds());
        if(!row.isEmpty()) popup_list::fillLine(graphics,float(panelBounds.getX()),
            float(row.getY())-popup_list::lineThickness,float(panelBounds.getRight()),float(row.getY()));
    }

}

void ChoicePopup::resized()
{
    constexpr int popupItemGap = 0;
    const auto itemCount = static_cast<int>(choiceButtons.size());
    const auto contentHeight = itemCount * ana::ui::controlHeight
        + std::max(0, itemCount - 1) * popupItemGap;
    const auto panelWidth = std::max(1, std::min(anchorBounds.getWidth(),
        getWidth() - ana::ui::gap.pixels() * 2));
    const auto visibleRows = std::min(itemCount, std::max(0,
        (getHeight() - ana::ui::gap.pixels() * 2) / ana::ui::controlHeight));
    const auto panelHeight = visibleRows * ana::ui::controlHeight;
    panelBounds = juce::Rectangle<int>(panelWidth, std::max(1, panelHeight));
    const auto selected = juce::jlimit(0, juce::jmax(0, itemCount - 1), selectedIndex);
    panelBounds.setPosition(anchorBounds.getX(), anchorBounds.getCentreY()
        - selected * ana::ui::controlHeight - ana::ui::controlHeight / 2);
    panelBounds = panelBounds.constrainedWithin(getLocalBounds().reduced(ana::ui::gap.pixels()));
    choiceContent.setSize(panelBounds.getWidth(), contentHeight);
    choiceViewport.setBounds(panelBounds.withPosition(0, 0));
    auto rows = choiceContent.getLocalBounds();
    for (auto& button : choiceButtons)
        button->setBounds(rows.removeFromTop(ana::ui::controlHeight));
    if (!animationStarted)
        choiceViewport.setViewPosition(0, juce::jlimit(0, juce::jmax(0, contentHeight - panelHeight),
            selected * ana::ui::controlHeight - std::max(0, visibleRows - 1) / 2 * ana::ui::controlHeight));

    if (!animationStarted && !panelBounds.isEmpty())
    {
        animationStarted = true;
        reveal.setExpanded(true);
    }
    updateReveal();
}

void ChoicePopup::updateReveal()
{
    revealClip.setBounds(panelBounds);
    revealClip.setAlpha(static_cast<float>(reveal.value()));
    choiceViewport.setBounds(panelBounds.withPosition(0, 0));
}

void ChoicePopup::mouseDown(const juce::MouseEvent& event)
{
    if (!revealClip.getBounds().contains(event.getPosition())) close();
}

void ChoicePopup::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!panelBounds.contains(event.getEventRelativeTo(this).getPosition())) return;
    const auto delta = wheel.isReversed ? wheel.deltaY : -wheel.deltaY;
    scrollRemainder += delta * (wheel.isSmooth ? 220.0f : 48.0f) / ana::ui::controlHeight;
    const auto rows = static_cast<int>(scrollRemainder);
    if (rows == 0) return;
    scrollRemainder -= static_cast<float>(rows);
    const auto maximum = std::max(0, choiceContent.getHeight() - choiceViewport.getHeight());
    choiceViewport.setViewPosition(0, juce::jlimit(0, maximum,
        choiceViewport.getViewPositionY() / ana::ui::controlHeight * ana::ui::controlHeight
            + rows * ana::ui::controlHeight));
}

void ChoicePopup::choose(const int index)
{
    if (closing) return;
    pendingChoice = index;
    close();
}

void ChoicePopup::close()
{
    if (closing) return;
    closing = true;
    reveal.setExpanded(false);
    if (!reveal.isRunning()) completeClose();
}

void ChoicePopup::completeClose()
{
    if (closePosted) return;
    closePosted = true;
    auto selectCallback = std::move(onSelect);
    auto closeCallback = std::move(onClose);
    const auto index = pendingChoice;
    juce::MessageManager::callAsync([index, select = std::move(selectCallback),
                                     closePrompt = std::move(closeCallback)]() mutable
    {
        if (closePrompt) closePrompt();
        if (index >= 0 && select) select(index);
    });
}
