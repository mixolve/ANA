#include "AboutPopup.h"
#include "Theme.h"

#include <utility>
#include "MarkdownContent.h"

namespace {
class AboutTextRow final : public juce::Component,
                               public juce::SettableTooltipClient
{
public:
    explicit AboutTextRow(juce::String textIn) : text(std::move(textIn))
    {
        setOpaque(false);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }

    void setText(const juce::String& newText)
    {
        text = newText;
        cachedWidth = -1;
        repaint();
    }

    int getPreferredHeight(const int width) const
    {
        updateLayout(width);
        return juce::jmax(ana::ui::controlHeight, juce::roundToInt(layout.getHeight()));
    }

    void paint(juce::Graphics& graphics) override
    {
        updateLayout(getWidth());
        graphics.setColour(ana::ui::white);
        layout.draw(graphics, getLocalBounds().toFloat());
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (onClick != nullptr)
            onClick();
    }

    std::function<void()> onClick;

private:
    void updateLayout(const int width) const
    {
        const auto contentWidth = juce::jmax(1, width - ana::ui::gap.pixels() * 2);

        if (cachedWidth == contentWidth)
            return;

        juce::AttributedString attributed(text);
        attributed.setFont(ana::ui::makeFont());
        attributed.setColour(ana::ui::white);
        attributed.setJustification(juce::Justification::centred);
        attributed.setWordWrap(juce::AttributedString::byWord);

        layout.createLayout(attributed, static_cast<float>(contentWidth));
        cachedWidth = contentWidth;
    }

    juce::String text;
    mutable juce::TextLayout layout;
    mutable int cachedWidth = -1;
};

}


AboutPopup::AboutPopup(std::function<void()> closeCallback)
    : onClose(std::move(closeCallback))
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);

    webLink = std::make_unique<MarkdownLinkRow>("WEB", "https://mixolve.cc/");
    manualLink = std::make_unique<MarkdownLinkRow>("MANUAL", "ana-manual://");
    licensesLink = std::make_unique<MarkdownLinkRow>("LICENSES", "ana-licenses://");
    for (auto* link : {webLink.get(), manualLink.get(), licensesLink.get()}) addAndMakeVisible(*link);

    const auto aboutText = juce::String::fromUTF8(BinaryData::about_md, BinaryData::about_mdSize);
    for (const auto& line : juce::StringArray::fromLines(aboutText))
    {
        const auto trimmed = line.trim();
        if (trimmed.isEmpty())
            continue;
        if (trimmed.startsWith("[WEB]"))
        {
            contentRows.push_back(webLink.get());
            continue;
        }
        if (trimmed.startsWith("[MANUAL]"))
        {
            contentRows.push_back(manualLink.get());
            continue;
        }
        if (trimmed.startsWith("[LICENSES]"))
        {
            contentRows.push_back(licensesLink.get());
            continue;
        }

        auto label = std::make_unique<AboutTextRow>(uppercaseText(trimmed));
        addAndMakeVisible(*label);
        if (trimmed.containsChar('@'))
        {
            juce::String address;
            for (const auto& token : juce::StringArray::fromTokens(trimmed, " \t", ""))
            {
                if (! token.containsChar('@'))
                    continue;
                address = token.trimCharactersAtStart("(").trimCharactersAtEnd("),.;:");
                break;
            }
            if (address.isNotEmpty())
            {
                label->setTooltip("CLICK TO COPY");
                label->setMouseCursor(juce::MouseCursor::PointingHandCursor);
                juce::Component::SafePointer<AboutTextRow> safeLabel(label.get());
                emailCopyListener.onClick = [safeLabel, email = address.toLowerCase(),
                                             original = trimmed, token = address]
                {
                    juce::SystemClipboard::copyTextToClipboard(email);
                    if (safeLabel == nullptr)
                        return;
                    // Monospace font: centre COPIED in the address slot so the line stays put.
                    const auto padTotal = juce::jmax(0, token.length() - juce::String("COPIED").length());
                    const auto leftPad = padTotal / 2;
                    const auto copied = juce::String::repeatedString(" ", leftPad) + "COPIED"
                        + juce::String::repeatedString(" ", padTotal - leftPad);
                    safeLabel->setText(original.replace(token, copied));
                    juce::Timer::callAfterDelay(1200, [safeLabel, original]
                    {
                        if (safeLabel != nullptr)
                            safeLabel->setText(original);
                    });
                };
                label->addMouseListener(&emailCopyListener, false);
            }
        }
        contentRows.push_back(label.get());
        textLabels.push_back(std::move(label));
    }
}

void AboutPopup::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff666666));
}

void AboutPopup::resized()
{
    auto area = getLocalBounds().reduced(ana::ui::gap.pixels());
    for (size_t index = 0; index < contentRows.size(); ++index)
    {
        const auto* textRow = dynamic_cast<AboutTextRow*>(contentRows[index]);
        const auto height = textRow != nullptr ? textRow->getPreferredHeight(area.getWidth()) : ana::ui::controlHeight;
        contentRows[index]->setBounds(area.removeFromTop(height));
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

juce::URL AboutPopup::createLocalDocumentUrl(const char* const fileName,
                                              const void* const data,
                                              const size_t dataSize)
{
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("mixolve-ana");
    if (directory.createDirectory().failed())
        return {};
    const auto documentFile = directory.getChildFile(fileName);
    documentFile.setReadOnly(false);
    if (data == nullptr || dataSize == 0)
    {
        if (! documentFile.create().wasOk())
            return {};
    }
    else if (! documentFile.replaceWithData(data, dataSize))
        return {};
    documentFile.setReadOnly(true);
    return juce::URL(documentFile);
}

void AboutPopup::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent == this && event.mods.isLeftButtonDown()) requestClose();
}
