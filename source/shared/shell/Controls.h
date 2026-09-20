#pragma once

#include <JuceHeader.h>

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

class EllipsisLabel final : public juce::Label
{
public:
    void setDrawBackground(const bool shouldDraw) noexcept
    {
        drawBackground = shouldDraw;
        repaint();
    }
    void setTextVerticalOffset(const int offset) noexcept
    {
        textVerticalOffset = offset;
        repaint();
    }
    void paint(juce::Graphics& graphics) override;
    void editorShown(juce::TextEditor* editor) override;
    void editorAboutToBeHidden(juce::TextEditor* editor) override;
    std::function<void(bool)> onEditorVisibilityChanged;

private:
    int textVerticalOffset = 0;
    bool drawBackground = true;
};

class ControlButton final : public juce::Button
{
public:
    explicit ControlButton(juce::String text);

    void paintButton(juce::Graphics& graphics,
                     bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;
    int getPreferredWidth() const noexcept;

private:
    juce::Image symbolImage;
    bool iconButton = false;
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

class FocusedPotentiometer final : public juce::Slider
{
public:
    FocusedPotentiometer();
    ~FocusedPotentiometer() override;

private:
    SliderLookAndFeel lookAndFeel;
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
    void choose(int index);
    void close();

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
    void setRange(float newStart, float newEnd);

    void paint(juce::Graphics& graphics) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
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
};
