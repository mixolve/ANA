#include "Controls.h"
#include "Theme.h"
#include "ValueRange.h"
#include "SquareControl.h"
#include "PointerInput.h"

#include <algorithm>
#include <cmath>

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
