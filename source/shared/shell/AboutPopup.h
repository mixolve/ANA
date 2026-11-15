#pragma once

#include "Theme.h"

#include <functional>
#include <memory>
#include <vector>

class AboutPopup final : public juce::Component
{
public:
    explicit AboutPopup(std::function<void()> closeCallback);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void requestClose();
    static juce::URL createLocalDocumentUrl(const char* fileName,
                                             const void* data,
                                             size_t dataSize);

    std::unique_ptr<juce::Component> webLink, manualLink, licensesLink;
    std::vector<std::unique_ptr<juce::Component>> textLabels;
    std::vector<juce::Component*> contentRows;
    std::function<void()> onClose;

    struct EmailCopyListener final : juce::MouseListener
    {
        void mouseUp(const juce::MouseEvent&) override
        {
            if (onClick != nullptr)
                onClick();
        }

        std::function<void()> onClick;
    };
    EmailCopyListener emailCopyListener;
};
