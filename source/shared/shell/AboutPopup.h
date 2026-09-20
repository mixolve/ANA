#pragma once

#include "Controls.h"

#include <functional>
#include <memory>
#include <vector>

class AboutPopup final : public juce::Component
{
public:
    explicit AboutPopup(std::function<void()> closeCallback);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void requestClose();
    static juce::URL createLocalManualUrl();

    juce::HyperlinkButton webLink { "WEB", juce::URL("https://mixolve.cc/") };
    juce::HyperlinkButton manualLink { "MANUAL", juce::URL() };
    std::vector<std::unique_ptr<EllipsisLabel>> textLabels;
    std::vector<juce::Component*> contentRows;
    ControlButton okButton { "OK" };
    std::function<void()> onClose;
};
