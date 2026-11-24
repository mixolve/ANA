#pragma once
#include "Artwork.h"
#include "State.h"
#include "shared/shell/Controls.h"
#include "shared/shell/ParameterControl.h"
#include "OpenGLContext.h"

// Software-painted sibling beneath the native GL view. It also covers the
// CachedComponentImage's empty paint while a GL surface is attached/detached.
class AnimationBackdrop final : public juce::Component
{
public:
    AnimationBackdrop();
    void paint(juce::Graphics&) override;
};

class AnimationOverlay final : public juce::Component
{
public:
    AnimationOverlay(juce::AudioProcessorValueTreeState& parameters, std::shared_ptr<AnimationSettings> settings);
    ~AnimationOverlay() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void setActive(bool active);
    AnimationArtwork& getArtwork() noexcept { return artwork; }
    std::function<void()> onClose, onSettingsRequested;
private:
    std::shared_ptr<AnimationSettings> settings;
    AnimationArtwork artwork;
    AnimationOpenGLContext context;
};

class AnimationSettingsPanel final : public juce::Component
{
public:
    AnimationSettingsPanel(juce::AudioProcessorValueTreeState& parameters);
    ~AnimationSettingsPanel() override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void setActive(bool active);
    void cancelEditors();
    int preferredEmbeddedHeight(int width) const;
    std::vector<ParameterControl*> parameterControls() const;
    std::function<void(ParameterControl&)> onChoiceRequested, onResetRequested;
    std::function<void()> onClose;
    std::function<void(bool)> onTextEditingChanged;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
