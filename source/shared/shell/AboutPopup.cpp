#include "AboutPopup.h"
#include "Theme.h"

#include <utility>

AboutPopup::AboutPopup(std::function<void()> closeCallback)
    : onClose(std::move(closeCallback))
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);

    const auto configureLink = [this] (juce::HyperlinkButton& link)
    {
        link.setFont(ana::ui::makeFont(), false);
        link.setJustificationType(juce::Justification::centred);
        link.setColour(juce::HyperlinkButton::textColourId, ana::ui::white);
        link.setWantsKeyboardFocus(false);
        addAndMakeVisible(link);
    };
    configureLink(webLink);
    configureLink(manualLink);
    manualLink.onClick = []
    {
        const auto url = createLocalManualUrl();
        if (url.isWellFormed())
            url.launchInDefaultBrowser();
    };

    const auto aboutText = juce::String::fromUTF8(BinaryData::about_md, BinaryData::about_mdSize);
    for (const auto& line : juce::StringArray::fromLines(aboutText))
    {
        const auto trimmed = line.trim();
        if (trimmed.isEmpty())
            continue;
        if (trimmed.startsWith("[WEB]"))
        {
            contentRows.push_back(&webLink);
            continue;
        }
        if (trimmed.startsWith("[MANUAL]"))
        {
            contentRows.push_back(&manualLink);
            continue;
        }

        auto label = std::make_unique<EllipsisLabel>();
        label->setText(trimmed, juce::dontSendNotification);
        label->setFont(ana::ui::makeFont());
        label->setJustificationType(juce::Justification::centred);
        label->setColour(juce::Label::textColourId, ana::ui::white);
        label->setColour(juce::Label::backgroundColourId, ana::ui::black);
        label->setColour(juce::Label::outlineColourId, ana::ui::black);
        addAndMakeVisible(*label);
        contentRows.push_back(label.get());
        textLabels.push_back(std::move(label));
    }

    okButton.onClick = [this] { requestClose(); };
    addAndMakeVisible(okButton);
}

void AboutPopup::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);
}

void AboutPopup::resized()
{
    auto area = getLocalBounds().reduced(ana::ui::gap.pixels());
    okButton.setBounds(area.removeFromBottom(ana::ui::controlHeight));
    ana::ui::gap.removeFromBottom(area);
    for (size_t index = 0; index < contentRows.size(); ++index)
    {
        contentRows[index]->setBounds(area.removeFromTop(ana::ui::controlHeight));
        if (index + 1 < contentRows.size())
            ana::ui::gap.removeFromTop(area);
    }
}

bool AboutPopup::keyPressed(const juce::KeyPress& key)
{
    if (key != juce::KeyPress::escapeKey)
        return false;
    requestClose();
    return true;
}

void AboutPopup::requestClose()
{
    auto deferredClose = std::move(onClose);
    onClose = {};
    juce::MessageManager::callAsync([callback = std::move(deferredClose)]
    {
        if (callback != nullptr)
            callback();
    });
}

juce::URL AboutPopup::createLocalManualUrl()
{
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("mixolve-ana");
    if (directory.createDirectory().failed())
        return {};
    const auto manualFile = directory.getChildFile("manual.md");
    manualFile.setReadOnly(false);
    if (! manualFile.replaceWithData(BinaryData::manual_md,
                                     static_cast<size_t>(BinaryData::manual_mdSize)))
        return {};
    manualFile.setReadOnly(true);
    return juce::URL(manualFile);
}
