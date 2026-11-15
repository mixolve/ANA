#include "SquareControl.h"
#include "Controls.h"
#include "ValueRange.h"
#include "PointerInput.h"
#include "PopupFrame.h"
#include "SubmoduleButtonAttachment.h"
#include "Processor.h"
#include "Theme.h"

#include <algorithm>
#include <cmath>
#include <utility>

SubmoduleButtonAttachment::SubmoduleButtonAttachment(
    juce::AudioProcessorValueTreeState& stateIn, const juce::String& id, juce::Button& buttonIn, bool fixed)
    : state(stateIn), sourceId(id), button(buttonIn), fixedBinding(fixed)
{
    refreshBinding();
    button.addMouseListener(this, false);
    if (!fixedBinding) startTimerHz(10);
}

SubmoduleButtonAttachment::~SubmoduleButtonAttachment()
{
    stopTimer();
    button.removeMouseListener(this);
}

void SubmoduleButtonAttachment::refreshBinding()
{
    auto& processor = static_cast<PluginProcessor&>(state.processor);
    const auto id = fixedBinding ? sourceId : juce::String(processor.resolveParameterId(sourceId.toRawUTF8()));
    if (boundId == id)
        return;
    attachment.reset();
    boundId = id;
    button.getProperties().set("sourceParameterId", sourceId);
    button.getProperties().set("oscParameterId", id);
    if(auto* control=dynamic_cast<ControlButton*>(&button)) control->bindHostSlots(&processor.getHostSlots(),[safe=juce::Component::SafePointer<ControlButton>(control)] { return safe?safe->getProperties()["oscParameterId"].toString():juce::String(); });
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, id, button);
}

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

CopyPasteTextEditor::CopyPasteTextEditor()
{
    setInputFilter(new UppercaseEditorInputFilter(), true);
    setLookAndFeel(&editorLookAndFeel);
    setMouseCursor(juce::MouseCursor::NormalCursor);
    setIndents(2, 0);
    setBorder(juce::BorderSize<int>(1));
    setColour(juce::CaretComponent::caretColourId, ana::ui::white);
    setColour(juce::TextEditor::highlightColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::highlightedTextColourId, ana::ui::black);
}

void CopyPasteTextEditor::paint(juce::Graphics& g)
{
    juce::TextEditor::paint(g);
    if(bool(getProperties().getWithDefault("squareValueEditor",false)))
    {
        const auto selection=getHighlightedRegion();
        if(selection.isEmpty()) return;
        const juce::Graphics::ScopedSaveState saved(g);
        auto clip=getLocalBounds();
        for(auto* child:getChildren())
            if(auto* viewport=dynamic_cast<juce::Viewport*>(child)) clip=getLocalArea(viewport,viewport->getLocalBounds());
        g.reduceClipRegion(clip);
        g.setColour(ana::ui::background);
        for(const auto& bounds:getTextBounds(selection)) g.fillRect(bounds);
        return;
    }
    const auto selection=getHighlightedRegion();
    if(selection.isEmpty()) return;
    g.setColour(ana::ui::light.withMultipliedAlpha(hasKeyboardFocus(true)?1.0f:0.5f));
    g.reduceClipRegion(getLocalBounds().reduced(1));
    for(const auto& bounds:getTextBounds(selection))
        g.fillRect(bounds.reduced(0,juce::jmin(2,bounds.getHeight()/2)).withTrimmedTop(1));
}

CopyPasteTextEditor::~CopyPasteTextEditor()
{
    contextMenu.reset();
    setLookAndFeel(nullptr);
}

juce::CaretComponent* CopyPasteTextEditor::EditorLookAndFeel::createCaretComponent(juce::Component* owner)
{
    auto* caret = new juce::CaretComponent(owner);
    caret->setColour(juce::CaretComponent::caretColourId, owner->findColour(juce::CaretComponent::caretColourId));
    return caret;
}

void CopyPasteTextEditor::EditorLookAndFeel::drawTextEditorOutline(juce::Graphics& g,int width,int height,juce::TextEditor& editor)
{
    const auto focused=editor.hasKeyboardFocus(true) && !editor.isReadOnly();
    const auto colour=editor.findColour(focused?juce::TextEditor::focusedOutlineColourId:juce::TextEditor::outlineColourId);
    ControlAppearance style(false,0.0f,focused && !colour.isTransparent());
    style.normalFrameVisible=!colour.isTransparent();style.normalBorder=colour;
    style.paintFrame(g,juce::Rectangle<int>(0,0,width,height));
}

void EllipsisLabel::paintOverChildren(juce::Graphics& g)
{
    ControlAppearance style(false, 0.0f, isBeingEdited());
    style.normalFrameVisible=false;
    style.paintFrame(g,getLocalBounds());
}

void CopyPasteTextEditor::mouseDown(const juce::MouseEvent& event)
{
    if(event.mods.isCtrlDown() && !event.mods.isRightButtonDown()) return;
    if (!isPopupMenuEnabled() || !event.mods.isPopupMenu() || getParentComponent() == nullptr)
    {
        juce::TextEditor::mouseDown(event);
        return;
    }
    auto* scope = getTopLevelComponent();
    contextMenu.reset();
    const auto selection = getHighlightedRegion();
    auto anchor = juce::Rectangle<int>(std::max(96, getWidth()), ana::ui::controlHeight);
    anchor.setCentre(scope->getLocalPoint(this, event.getPosition()));
    auto popup = std::make_unique<ChoicePopup>(anchor, juce::StringArray { "COPY", "PASTE" },
        std::vector<bool> { !selection.isEmpty(), !isReadOnly() }, -1,
        [safeThis = juce::Component::SafePointer<CopyPasteTextEditor>(this), selection](int choice)
        {
            if (safeThis == nullptr) return;
            safeThis->setHighlightedRegion(selection);
            safeThis->performPopupMenuAction(choice == 0 ? juce::StandardApplicationCommandIDs::copy
                                                        : juce::StandardApplicationCommandIDs::paste);
            if (safeThis->isShowing()) safeThis->grabKeyboardFocus();
        },
        [safeThis = juce::Component::SafePointer<CopyPasteTextEditor>(this)]
        {
            if (safeThis == nullptr) return;
            safeThis->contextMenu.reset();
            if (safeThis->isShowing()) safeThis->grabKeyboardFocus();
        });
    popup->setName("text-edit-context-menu");
    scope->addAndMakeVisible(*popup);
    popup->setBounds(scope->getLocalBounds());
    popup->toFront(false);
    auto* menu = popup.get();
    contextMenu = std::move(popup);
    // ANA's numeric editors and editable labels are modal. The menu must be
    // above them in the modal stack so its actions remain clickable.
    menu->enterModalState(false, nullptr, false);
}

void CopyPasteTextEditor::addPopupMenuItems(juce::PopupMenu& menu, const juce::MouseEvent*)
{
    menu.addItem(juce::StandardApplicationCommandIDs::copy, "COPY", !getHighlightedRegion().isEmpty());
    menu.addItem(juce::StandardApplicationCommandIDs::paste, "PASTE", !isReadOnly());
}

void CopyPasteTextEditor::performPopupMenuAction(int id)
{
    if (id == juce::StandardApplicationCommandIDs::copy) copyToClipboard();
    else if (id == juce::StandardApplicationCommandIDs::paste) pasteFromClipboard();
}

ExplicitCommitTextEditor::ExplicitCommitTextEditor()
{
    setColour(juce::CaretComponent::caretColourId, ana::ui::white);
}

void ExplicitCommitTextEditor::inputAttemptWhenModal()
{
    auto* clicked=ana::ui::componentAtPointer(*this);
    if (!ana::ui::isEmptyEditorBackground(clicked)) { grabKeyboardFocus(); return; }
    juce::Component::SafePointer<ExplicitCommitTextEditor> safeThis(this);
    const auto commit=onReturnKey;
    if (!getText().trim().isEmpty() && commit) commit();
    if (safeThis!=nullptr && isCurrentlyModal(false))
    {
        const auto cancel=onEscapeKey;
        if (cancel) cancel();
    }
}

void EllipsisLabel::paint(juce::Graphics& graphics)
{
    if (isBeingEdited())
    {
        getLookAndFeel().drawLabel(graphics, *this);
        return;
    }

    if (hideWhenEmpty && getText().isEmpty())
        return;

    const auto hoverAmount = hoverEnabled
        ? hover.amount(isEnabled() && isEditable() && ana::ui::isMouseHovering(*this))
        : 0.0f;
    ControlAppearance style(false, hoverAmount);
    if (drawBackground)
    {
        graphics.fillAll(findColour(juce::Label::backgroundColourId));
        if (hoverAmount > 0.0f)
            style.paintBackground(graphics, getLocalBounds());
    }
    else if (hoverAmount > 0.0f)
        style.paintBackground(graphics, getLocalBounds());
    graphics.setColour(isEnabled() ? (hoverAmount > 0.0f ? style.contentColour()
                                                       : findColour(juce::Label::textColourId))
                                   : ana::ui::light);
    graphics.setFont(getFont());
    const auto bounds = getBorderSize().subtractedFrom(getLocalBounds()).translated(0, textVerticalOffset);
    if (ana::ui::drawLoopingText(graphics, uppercaseText(getText()), bounds, getFont(), getJustificationType()))
        ana::ui::scheduleMarqueeRepaint(*this, marqueePending);
    graphics.setColour(isEnabled() ? findColour(juce::Label::outlineColourId)
                                   : ana::ui::light);
    graphics.drawRect(getLocalBounds());
}

void EllipsisLabel::resized()
{
    juce::Label::resized();
    alignEditorToPaintedText();
}

void EllipsisLabel::alignEditorToPaintedText()
{
    auto* editor = getCurrentTextEditor();

    if (editor == nullptr)
        return;

    // Painted label text uses the border-inset bounds; JUCE centres the
    // editor's own line box, so move the editor until both texts coincide.
    const auto paintedText = getBorderSize().subtractedFrom(getLocalBounds());
    ana::ui::alignInlineEditorText(*editor, getLocalBounds(),
        paintedText.getCentreY() + textVerticalOffset);
}

juce::TextEditor* EllipsisLabel::createEditorComponent()
{
    auto* editor = new CopyPasteTextEditor();
    editor->setName(getName());
    editor->applyFontToAllText(getLookAndFeel().getLabelFont(*this));
    copyAllExplicitColoursTo(*editor);
    const auto copyEditingColour = [&](int from, int to)
    {
        if (isColourSpecified(from)) editor->setColour(to, findColour(from));
    };
    copyEditingColour(textWhenEditingColourId, juce::TextEditor::textColourId);
    copyEditingColour(backgroundWhenEditingColourId, juce::TextEditor::backgroundColourId);
    copyEditingColour(outlineWhenEditingColourId, juce::TextEditor::focusedOutlineColourId);
    return editor;
}

void EllipsisLabel::editorShown(juce::TextEditor* editor)
{
    if (editor == nullptr)
        return;

    editor->setText(uppercaseText(editor->getText()), false);
    const auto border = getBorderSize();
    editor->setBorder(juce::BorderSize<int>(border.getTop(), border.getLeft(),
                                            border.getBottom(), std::max(0, border.getRight() - 2)));
    editor->setIndents(border.getRight() >= 2 ? 0 : 2, 0);
    editor->setJustification(getJustificationType());
    editor->setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::highlightColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::highlightedTextColourId, ana::ui::black);
    editor->setColour(juce::CaretComponent::caretColourId, ana::ui::white);

    // Auxiliary windows pass shortcuts to the host except while inline text editing is active.
    if (onEditorVisibilityChanged)
        onEditorVisibilityChanged(true);

    alignEditorToPaintedText();
}

void EllipsisLabel::inputAttemptWhenModal()
{
    auto* clicked=ana::ui::componentAtPointer(*this);
    if (ana::ui::isEmptyEditorBackground(clicked)) { hideEditor(false); return; }
    if (auto* editor=getCurrentTextEditor()) editor->grabKeyboardFocus();
}

void EllipsisLabel::editorAboutToBeHidden(juce::TextEditor*)
{
    hover.inherit(false);
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
        // A long hold has already passed the minimum flash duration.
        safeThis->clickFlash = false;
        safeThis->repaint();
    }
    // For a quick tap the original timer will end the *same* flash at 100 ms.
}

void ControlButton::timerCallback()
{
    stopTimer();
    if (clickHeld)
        return; // Still pressed: white stays until mouseUp().
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
        // Flash over the full control (including its frame) for legible contrast.
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

    const auto originProportion =
        slider.valueToProportionOfLength(juce::jlimit(slider.getMinimum(), slider.getMaximum(), 0.0));
    const auto originX = juce::jmap(static_cast<float>(originProportion), bounds.getX(), bounds.getRight());
    graphics.setColour(ana::ui::light);
    graphics.fillRect(bounds.withLeft(std::min(markerX, originX))
                           .withRight(std::max(markerX, originX)));
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
    juce::ignoreUnused(isMouseDown);
    graphics.setColour(ana::ui::white);
    if (isScrollbarVertical)
        graphics.fillRect(x, thumbStartPosition, width, thumbSize);
    else graphics.fillRect(thumbStartPosition, y, thumbSize, height);
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

void ClickArmedSlider::mouseDown(const juce::MouseEvent& event)
{
    wheelArmed = event.mods.isLeftButtonDown();
    juce::Slider::mouseDown(event);
}

void ClickArmedSlider::mouseExit(const juce::MouseEvent& event)
{
    wheelArmed = false;
    juce::Slider::mouseExit(event);
}

void ClickArmedSlider::mouseWheelMove(const juce::MouseEvent& event,
                                      const juce::MouseWheelDetails& wheel)
{
    if (wheelArmed)
    {
        if (ValueRange::wheelMovement(wheel)==0.0) return;
        juce::Slider::ScopedDragNotification gesture(*this);
        ValueRange::applyWheel(*this,event,wheel,wheelRemainder,lastWheelValue,10.0);
    }
    else
        juce::Component::mouseWheelMove(event, wheel);
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

void RangeSlider::paint(juce::Graphics& graphics)
{
    const auto frameBounds = getLocalBounds();
    const auto bounds = frameBounds.reduced(1);
    if (bounds.isEmpty())
        return;

    graphics.setColour(ana::ui::dark);
    graphics.fillRect(bounds);
    graphics.setColour(ana::ui::light);

    constexpr int handleThickness = 8;

    if (orientation == Orientation::horizontal)
    {
        const auto startX = bounds.getX() + juce::roundToInt(rangeStart * static_cast<float>(bounds.getWidth()));
        const auto endX = bounds.getX() + juce::roundToInt(rangeEnd * static_cast<float>(bounds.getWidth()));
        graphics.fillRect(bounds.withLeft(startX).withRight(endX));

        const auto handleX = [bounds] (const int centreX)
        {
            return juce::jlimit(bounds.getX(), bounds.getRight() - handleThickness,
                                centreX - handleThickness / 2);
        };
        graphics.setColour(ana::ui::white);
        graphics.fillRect(handleX(startX), bounds.getY(), handleThickness, bounds.getHeight());
        graphics.fillRect(handleX(endX), bounds.getY(), handleThickness, bounds.getHeight());
    }
    else
    {
        const auto startY = bounds.getY() + juce::roundToInt(rangeStart * static_cast<float>(bounds.getHeight()));
        const auto endY = bounds.getY() + juce::roundToInt(rangeEnd * static_cast<float>(bounds.getHeight()));
        graphics.fillRect(bounds.withTop(startY).withBottom(endY));

        const auto handleY = [bounds] (const int centreY)
        {
            return juce::jlimit(bounds.getY(), bounds.getBottom() - handleThickness,
                                centreY - handleThickness / 2);
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
    wheelArmed = event.mods.isLeftButtonDown();
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

void RangeSlider::mouseExit(const juce::MouseEvent&)
{
    wheelArmed = false;
}

void RangeSlider::mouseWheelMove(const juce::MouseEvent& event,
                                 const juce::MouseWheelDetails& wheel)
{
    if (! wheelArmed || wheel.deltaY == 0.0f)
    {
        juce::Component::mouseWheelMove(event, wheel);
        return;
    }

    const auto currentWidth = rangeEnd - rangeStart;
    if (currentWidth >= 1.0f - 1.0e-6f)
        return;

    const auto delta = -wheel.deltaY * std::max(0.002f, currentWidth * 0.1f);
    const auto nextStart = juce::jlimit(0.0f, 1.0f - currentWidth, rangeStart + delta);
    if (std::abs(nextStart - rangeStart) > 1.0e-6f)
    {
        updateRange(nextStart, nextStart + currentWidth);
        if (onDragEnded)
            onDragEnded();
    }
}

void RangeSlider::setRange(const float newStart, const float newEnd)
{
    // UI controls periodically sync from host parameters. During a drag those
    // values may be rounded independently, and feeding them back into the
    // thumb positions makes the entire range jump or temporarily change size.
    // Keep the mouse-controlled range authoritative until mouseUp; the next
    // regular sync will pick up the final host parameter values.
    if (isDragging())
        return;

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
