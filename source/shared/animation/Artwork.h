#pragma once
#include <JuceHeader.h>
#include "Settings.h"
#include <functional>
#include <memory>

class AnimationArtwork final : public juce::Component, private juce::Timer
{
public:
    AnimationArtwork();
    ~AnimationArtwork() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void setGpuRenderingEnabled(bool enabled);
    void setAnimationSettings(std::shared_ptr<AnimationSettings> settings);
    void setAnimationSource(AnimationArtwork& source);
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    std::function<void()> onSettingsRequested;
    std::function<void(float)> onCropChanged;

private:
    void timerCallback() override;
    juce::Rectangle<float> getCropBounds() const;
    struct Animation;
    std::unique_ptr<Animation> animation;
    juce::Component::SafePointer<AnimationArtwork> animationSource;
    bool cropDragActive = false;
    float cropDragStartY = 0, cropDragStartPosition = 0;
};
