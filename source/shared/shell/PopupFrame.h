#pragma once

#include <JuceHeader.h>

namespace popup_list
{
inline constexpr float lineThickness=1.0f;

inline void fillLine(juce::Graphics& graphics,float left,float top,float right,float bottom)
{
    graphics.fillRect(juce::Rectangle<float>(left,top,right-left,bottom-top));
}

inline void paintFrame(juce::Graphics& graphics,juce::Rectangle<float> bounds)
{
    fillLine(graphics,bounds.getX(),bounds.getY(),bounds.getRight(),bounds.getY()+lineThickness);
    fillLine(graphics,bounds.getX(),bounds.getBottom()-lineThickness,bounds.getRight(),bounds.getBottom());
    fillLine(graphics,bounds.getX(),bounds.getY(),bounds.getX()+lineThickness,bounds.getBottom());
    fillLine(graphics,bounds.getRight()-lineThickness,bounds.getY(),bounds.getRight(),bounds.getBottom());
}
}
