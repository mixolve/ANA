#pragma once

#include <JuceHeader.h>
#include "UppercaseText.h"
#include "HoverAnimation.h"
#include "SlotIndicator.h"

#include <functional>
#include <memory>
#include <vector>

class LongPressGesture final : private juce::Timer
{
public:
    enum class ReleaseResult { none, shortPress, longPress, cancelled };

    explicit LongPressGesture(int delayMsIn = 500) : delayMs(delayMsIn) {}
    ~LongPressGesture() override { stopTimer(); }

    void begin(bool shouldArmLongPress = true);
    void markDragged();
    void cancelArming();
    void cancel();
    ReleaseResult release();

    bool isActive() const noexcept { return active; }

    std::function<void()> onArmed;

private:
    void timerCallback() override;

    int delayMs = 500;
    bool active = false;
    bool dragged = false;
    bool armed = false;
};

class CopyPasteTextEditor : public juce::TextEditor
{
public:
    CopyPasteTextEditor();
    void paint(juce::Graphics& g) override;
    void setText(const juce::String& text, bool sendChangeMessage = true)
    { juce::TextEditor::setText(uppercaseText(text), sendChangeMessage); }
    ~CopyPasteTextEditor() override;
    juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::NormalCursor; }
    void mouseDown(const juce::MouseEvent& event) override;
    void addPopupMenuItems(juce::PopupMenu&, const juce::MouseEvent*) override;
    void performPopupMenuAction(int menuItemID) override;
private:
    class EditorLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        juce::CaretComponent* createCaretComponent(juce::Component* owner) override;
        void drawTextEditorOutline(juce::Graphics&,int,int,juce::TextEditor&) override;
    } editorLookAndFeel;
    std::unique_ptr<juce::Component> contextMenu;
};

class ExplicitCommitTextEditor final : public CopyPasteTextEditor
{
public:
    ExplicitCommitTextEditor();
    void inputAttemptWhenModal() override;
};

class EllipsisLabel final : public juce::Label
{
public:
    EllipsisLabel() { setColour(juce::Label::backgroundColourId,juce::Colour(0xff444444)); }
    void paintOverChildren(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void setDrawBackground(const bool shouldDraw) noexcept
    {
        drawBackground = shouldDraw;
        repaint();
    }
    void setHoverEnabled(const bool enabled) noexcept
    {
        hoverEnabled = enabled;
        hover.inherit(false);
        repaint();
    }
    void setHideWhenEmpty(const bool shouldHide) noexcept
    {
        hideWhenEmpty = shouldHide;
        repaint();
    }
    void setTextVerticalOffset(const int offset) noexcept
    {
        textVerticalOffset = offset;
        alignEditorToPaintedText();
        repaint();
    }
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    juce::TextEditor* createEditorComponent() override;
    void editorShown(juce::TextEditor* editor) override;
    void editorAboutToBeHidden(juce::TextEditor* editor) override;
    void inputAttemptWhenModal() override;
    void textEditorTextChanged(juce::TextEditor&) override {}
    void textEditorFocusLost(juce::TextEditor&) override {}
    std::function<void(bool)> onEditorVisibilityChanged;

private:
    HoverAnimation hover { *this };
    void alignEditorToPaintedText();
    bool marqueePending = false;
    int textVerticalOffset = 0;
    bool drawBackground = true;
    bool hoverEnabled = true;
    bool hideWhenEmpty = false;
};

class ControlButton final : public juce::Button, private juce::Timer
{
public:
    explicit ControlButton(juce::String text);

    void paintButton(juce::Graphics& graphics,
                     bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;
    int getPreferredWidth() const noexcept;
    void setInteractionEnabled(bool enabled)
    {
        if (interactionEnabled == enabled) return;
        interactionEnabled=enabled;
        if(!enabled) { clickHeld=false; clickFlash=false; }
        repaint();
    }
    bool isInteractionEnabled() const { return interactionEnabled; }
    void setSquareStyle(bool enabled) { squareStyle=enabled; resized(); repaint(); }
    void setSquareRailVisible(bool visible) { squareRailVisible=visible; repaint(); }
    void resized() override { slotIndicator.layout(getLocalBounds(),squareStyle && bottomSlotVisible); }
    void setSlotIndicatorVisible(bool visible) { bottomSlotVisible=visible; resized(); repaint(); }
    void bindHostSlots(HostSlots* slots,std::function<juce::String()> id) { slotIndicator.bind(slots,std::move(id)); resized(); }
    void copyHostSlotBinding(const ControlButton& source) { bottomSlotVisible=source.bottomSlotVisible; slotIndicator.copyBindingFrom(source.slotIndicator); resized(); }
    std::function<juce::String()> valueTextProvider;
    std::function<bool()> isMarked;
    std::function<void()> onMark;
    void setDrawBorder(bool shouldDraw) noexcept { drawBorder = shouldDraw; repaint(); }
    void setTextHorizontalInset(int inset) { textHorizontalInset=juce::jmax(0,inset); repaint(); }
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    void timerCallback() override;
    SlotIndicator slotIndicator;
    int textHorizontalInset=8;
    bool interactionEnabled=true;
    bool bottomSlotVisible=true;
    bool clickHeld = false;
    bool clickFlash = false;
    HoverAnimation hoverAnimation { *this };
    bool marqueePending = false;
    bool drawBorder = true;
    bool squareStyle = false, squareRailVisible = false, marking = false;
    double squareHoverAt = 0.0;
};

class SliderLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    int getSliderThumbRadius(juce::Slider&) override { return 0; }
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override;
    void drawLinearSlider(juce::Graphics& graphics,
                          int x, int y, int width, int height,
                          float sliderPosition, float minimumSliderPosition, float maximumSliderPosition,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override;
    void drawScrollbar(juce::Graphics& graphics,
                       juce::ScrollBar& scrollbar,
                       int x, int y, int width, int height,
                       bool isScrollbarVertical,
                       int thumbStartPosition,
                       int thumbSize,
                       bool isMouseOver,
                       bool isMouseDown) override;
    juce::Label* createSliderTextBox(juce::Slider& slider) override;
};

class ClickArmedSlider final : public juce::Slider
{
public:
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

private:
    bool wheelArmed = false;
    double wheelRemainder=0.0,lastWheelValue=0.0;
};

class ChoicePopup final : public juce::Component
{
public:
    ChoicePopup(juce::Rectangle<int> anchorBounds,
                    juce::StringArray choices,
                    std::vector<bool> enabledChoices,
                    int selectedIndex,
                    std::function<void(int)> onSelect,
                    std::function<void()> onClose);

    void paintOverChildren(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    float scrollRemainder = 0.0f;
    void choose(int index);
    void close();
    void updateReveal();
    void completeClose();

    juce::Component revealClip, choiceContent;
    class RowViewport final : public juce::Viewport
    {
    public:
        std::function<void(const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        { if (onWheel) onWheel(e, w); }
    } choiceViewport;
    DisclosureAnimation reveal;
    int selectedIndex = -1;
    int pendingChoice = -1;
    bool animationStarted = false, closePosted = false;
    juce::Rectangle<int> anchorBounds;
    juce::StringArray choices;
    std::vector<std::unique_ptr<ControlButton>> choiceButtons;
    juce::Rectangle<int> panelBounds;
    std::function<void(int)> onSelect;
    std::function<void()> onClose;
    bool closing = false;
};

class RangeSlider final : public juce::Component
{
public:
    enum class Orientation { horizontal, vertical };

    explicit RangeSlider(Orientation newOrientation = Orientation::horizontal)
        : orientation(newOrientation) {}

    float getRangeStart() const noexcept { return rangeStart; }
    float getRangeEnd() const noexcept { return rangeEnd; }
    bool isDragging() const noexcept { return dragMode != DragMode::none; }
    void setRange(float newStart, float newEnd);

    void paint(juce::Graphics& graphics) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;
    std::function<void()> onRangeChanged;
    std::function<void()> onDragEnded;

private:
    enum class DragMode { none, start, end, range };

    void updateRange(float newStart, float newEnd);

    float rangeStart = 0.0f;
    float rangeEnd = 1.0f;
    float dragStartRangeStart = 0.0f;
    float dragStartRangeEnd = 1.0f;
    DragMode dragMode = DragMode::none;
    Orientation orientation;
    bool wheelArmed = false;
};
