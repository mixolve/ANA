#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <memory>

class ValueRange
{
public:
    static constexpr double stepsForFullRange = 20.0;
    static double move(double position, double steps, const juce::ModifierKeys& mods, double fullRangeSteps=stepsForFullRange)
    {
        const auto step = (mods.isShiftDown() ? 0.1 : 1.0) / fullRangeSteps;
        return juce::jlimit(0.0, 1.0, position + steps * step);
    }
    static double rangePosition(juce::Slider& slider)
    {
        const auto range = slider.getNormalisableRange();
        if (range.end <= range.start) return 0.0;
        const auto value = juce::jlimit(range.start, range.end, slider.getValue());
        if (range.start > 0.0 && range.skew < 1.0 && !range.symmetricSkew)
            return juce::jlimit(0.0, 1.0, std::log(value / range.start) / std::log(range.end / range.start));
        return juce::jlimit(0.0, 1.0, slider.valueToProportionOfLength(value));
    }

    static void paint(juce::Graphics& g, juce::Rectangle<int> bounds,
                                    juce::Slider& slider, juce::Colour colour)
    {
        g.setColour(colour);
        g.fillRect(markerBounds(bounds,slider));
    }


    static double valueAt(juce::Slider& slider, double position)
    {
        position = juce::jlimit(0.0, 1.0, position);
        const auto range = slider.getNormalisableRange();
        if (range.start > 0.0 && range.skew < 1.0 && !range.symmetricSkew)
        {
            if (position <= 0.0) return range.start;
            if (position >= 1.0) return range.end;
            return range.start * std::exp(position * std::log(range.end / range.start));
        }
        return slider.proportionOfLengthToValue(position);
    }

    static juce::Rectangle<int> markerBounds(juce::Rectangle<int> bounds, juce::Slider& slider)
    {
        if (bounds.getWidth() < 4 || slider.getMaximum() <= slider.getMinimum()) return {};
        if(bounds.getWidth()==80 && bounds.getHeight()==80)
        {
            const auto x=bounds.getX()+4+juce::roundToInt(60*rangePosition(slider));
            return { x,bounds.getY()+4,12,12 };
        }
        const auto x=bounds.getX()+juce::roundToInt((bounds.getWidth()-4)*rangePosition(slider));
        return { x,bounds.getY()-4,4,8 };
    }
    static bool contains(const juce::MouseEvent& event,juce::Rectangle<int> bounds,juce::Slider& slider)
    { return markerBounds(bounds,slider).contains(event.getPosition()); }

    static double wheelMovement(const juce::MouseWheelDetails& wheel)
    {
        auto delta=std::abs(wheel.deltaY)>=std::abs(wheel.deltaX)?wheel.deltaY:-wheel.deltaX;
        if (wheel.isInertial || std::abs(delta)<1.0e-6f) return 0.0;
        if (wheel.isReversed) delta=-delta;
        return wheel.isSmooth?double(delta):(delta>0.0f?1.0:-1.0);
    }

    static void applyWheel(juce::Slider& slider,const juce::MouseEvent& event,
                          const juce::MouseWheelDetails& wheel,double& remainder,double& lastValue,
                          double fullRangeSteps=stepsForFullRange)
    {
        const auto movement=wheelMovement(wheel);
        if (movement==0.0 || slider.getMaximum()<=slider.getMinimum()) return;
        if (!juce::approximatelyEqual(lastValue, slider.getValue())) remainder = 0.0;
        const auto desired = move(rangePosition(slider) + remainder,movement,event.mods,fullRangeSteps);
        slider.setValue(valueAt(slider, desired), juce::sendNotificationSync);
        // Accumulate changes smaller than a legal step for discrete parameters.
        remainder = desired - rangePosition(slider);
        lastValue = slider.getValue();
    }

    bool begin(const juce::MouseEvent& event, juce::Rectangle<int> bounds, juce::Slider& slider)
    {
        if (!event.mods.isLeftButtonDown() || !contains(event, bounds, slider)) return false;
        target = &slider;
        position = rangePosition(slider);
        lastX = event.getScreenX();
        travel = juce::jmax(1, bounds.getWidth() - 4);
        return true;
    }

    bool isActive() const { return target != nullptr; }
    void drag(const juce::MouseEvent& event)
    {
        if (target == nullptr || !event.mouseWasDraggedSinceMouseDown()) return;
        if (gesture == nullptr) gesture = std::make_unique<juce::Slider::ScopedDragNotification>(*target);
        const auto steps = double(event.getScreenX() - lastX) * stepsForFullRange / travel;
        position = move(position, steps, event.mods);
        lastX = event.getScreenX();
        target->setValue(valueAt(*target, position), juce::sendNotificationSync);
    }
    void end() { gesture.reset(); target = nullptr; }

private:
    juce::Component::SafePointer<juce::Slider> target;
    std::unique_ptr<juce::Slider::ScopedDragNotification> gesture;
    double position = 0.0;
    int lastX = 0, travel = 1;
};

