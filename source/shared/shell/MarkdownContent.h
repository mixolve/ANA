#pragma once
#include "Theme.h"

#include <cmath>
#include <functional>
#include <vector>

namespace
{
constexpr int promptPanelPadding = ana::ui::gap.pixels();
constexpr int promptItemGap = ana::ui::gap.pixels();

struct MarkdownLink
{
    juce::String label;
    juce::String url;
};

juce::URL createOfflineDocumentUrl(const char* fileName, const void* data, const size_t dataSize)
{
    const auto documentDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                       .getChildFile("mixolve-ana");

    if (documentDirectory.createDirectory().failed())
        return {};

    const auto documentFile = documentDirectory.getChildFile(fileName);
    documentFile.setReadOnly(false);

    if (data == nullptr || dataSize == 0)
    {
        // JUCE replaceWithData with 0 bytes deletes the target instead of
        // writing; an empty document must still open as an empty file.
        if (! documentFile.create().wasOk())
            return {};
    }
    else if (! documentFile.replaceWithData(data, dataSize))
        return {};

    documentFile.setReadOnly(true);
    return juce::URL(documentFile);
}

juce::String getDisplayNameFromUrl(const juce::String& urlText)
{
    auto displayName = urlText.trim();

    if (displayName.startsWithIgnoreCase("https://"))
        displayName = displayName.substring(8);
    else if (displayName.startsWithIgnoreCase("http://"))
        displayName = displayName.substring(7);

    return displayName.upToFirstOccurrenceOf("/", false, false).trim();
}

MarkdownLink parseMarkdownLinkLine(const juce::String& line)
{
    MarkdownLink link;
    const auto trimmed = line.trim();

    if (trimmed.startsWithChar('['))
    {
        const auto labelEnd = trimmed.indexOfChar(']');
        const auto openParen = trimmed.indexOfChar('(');
        const auto closeParen = trimmed.lastIndexOfChar(')');

        if (labelEnd > 1 && openParen > labelEnd && closeParen > openParen)
        {
            link.label = trimmed.substring(1, labelEnd).trim();
            link.url = trimmed.substring(openParen + 1, closeParen).trim();
            return link;
        }
    }

    if (trimmed.startsWithIgnoreCase("https://") || trimmed.startsWithIgnoreCase("http://"))
    {
        link.label = getDisplayNameFromUrl(trimmed);
        link.url = trimmed;
    }

    return link;
}

struct MarkdownBlock
{
    enum class Kind
    {
        text,
        link,
        spacer
    };

    Kind kind = Kind::text;
    juce::String text;
    juce::String url;
    int headingLevel = 0;
};

MarkdownBlock parseMarkdownBlock(const juce::String& line)
{
    MarkdownBlock block;
    const auto trimmed = line.trim();

    if (trimmed.isEmpty())
    {
        block.kind = MarkdownBlock::Kind::spacer;
        return block;
    }

    int headingLevel = 0;

    while (headingLevel < trimmed.length() && trimmed[headingLevel] == '#')
        ++headingLevel;

    if (headingLevel > 0
        && headingLevel <= 6
        && (headingLevel == trimmed.length()
            || trimmed[headingLevel] == ' '
            || trimmed[headingLevel] == '\t'))
    {
        block.kind = MarkdownBlock::Kind::text;
        block.headingLevel = headingLevel;
        block.text = trimmed.substring(headingLevel).trim();
        return block;
    }

    const auto link = parseMarkdownLinkLine(trimmed);

    if (link.url.isNotEmpty())
    {
        block.kind = MarkdownBlock::Kind::link;
        block.text = link.label;
        block.url = link.url;
        return block;
    }

    block.kind = MarkdownBlock::Kind::text;
    block.text = trimmed;
    return block;
}

std::vector<MarkdownBlock> parseMarkdownBlocks(const juce::String& markdownText)
{
    std::vector<MarkdownBlock> blocks;
    juce::String paragraphText;
    bool lastWasSpacer = false;

    const auto flushParagraph = [&]
    {
        if (paragraphText.isEmpty())
            return;

        MarkdownBlock block;
        block.kind = MarkdownBlock::Kind::text;
        block.text = paragraphText.trimEnd();
        blocks.push_back(std::move(block));
        paragraphText.clear();
        lastWasSpacer = false;
    };

    const auto lines = juce::StringArray::fromLines(markdownText);

    for (const auto& rawLine : lines)
    {
        const auto parsed = parseMarkdownBlock(rawLine);

        if (parsed.kind == MarkdownBlock::Kind::spacer)
        {
            flushParagraph();

            if (! blocks.empty() && ! lastWasSpacer)
            {
                MarkdownBlock spacer;
                spacer.kind = MarkdownBlock::Kind::spacer;
                blocks.push_back(std::move(spacer));
                lastWasSpacer = true;
            }

            continue;
        }

        if (parsed.kind == MarkdownBlock::Kind::text && parsed.headingLevel == 0)
        {
            if (paragraphText.isNotEmpty())
                paragraphText << '\n';

            paragraphText << parsed.text;
            lastWasSpacer = false;
            continue;
        }

        flushParagraph();
        blocks.push_back(parsed);
        lastWasSpacer = false;
    }

    flushParagraph();

    while (! blocks.empty() && blocks.front().kind == MarkdownBlock::Kind::spacer)
        blocks.erase(blocks.begin());

    while (! blocks.empty() && blocks.back().kind == MarkdownBlock::Kind::spacer)
        blocks.pop_back();

    if (blocks.empty())
    {
        MarkdownBlock fallback;
        fallback.kind = MarkdownBlock::Kind::link;
        fallback.text = "mixolve.cc";
        fallback.url = "https://mixolve.cc/";
        blocks.push_back(std::move(fallback));
    }

    return blocks;
}

class MarkdownRowComponent : public juce::Component
{
public:
    ~MarkdownRowComponent() override = default;

    virtual int getPreferredHeight(int width) const = 0;
};

class MarkdownTextRowBase : public MarkdownRowComponent
{
public:
    MarkdownTextRowBase(juce::String textIn,
                        juce::Font fontIn,
                        juce::Colour colourIn)
        : text(std::move(textIn)),
          font(std::move(fontIn)),
          colour(colourIn)
    {
        setOpaque(false);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setInterceptsMouseClicks(false, false);
    }

    int getPreferredHeight(int width) const override
    {
        updateLayout(width);
        return juce::jmax(1, juce::roundToInt(layout.getHeight()));
    }

protected:
    void paint(juce::Graphics& g) override
    {
        updateLayout(getWidth());
        g.setColour(colour);
        layout.draw(g, getLocalBounds().toFloat());
    }

    void updateLayout(int width) const
    {
        const auto contentWidth = juce::jmax(1, width);

        if (cachedWidth == contentWidth)
            return;

        juce::AttributedString attributed(text);
        attributed.setFont(font);
        attributed.setColour(colour);
        attributed.setJustification(juce::Justification::centred);
        attributed.setWordWrap(juce::AttributedString::byWord);

        layout.createLayout(attributed, static_cast<float>(contentWidth));
        cachedWidth = contentWidth;
    }

    juce::String text;
    juce::Font font;
    juce::Colour colour;
    mutable juce::TextLayout layout;
    mutable int cachedWidth = -1;
};

class MarkdownTextRow final : public MarkdownTextRowBase
{
public:
    MarkdownTextRow(juce::String rowText,
                    juce::Font rowFont,
                    juce::Colour rowColour)
        : MarkdownTextRowBase(std::move(rowText), std::move(rowFont), rowColour)
    {
        setInterceptsMouseClicks(false, false);
    }
};

class MarkdownLinkRow final : public MarkdownRowComponent
{
public:
    MarkdownLinkRow(juce::String text, juce::String urlText, juce::Font rowFont = ana::ui::makeFont())
        : linkButton(std::move(text),
                     urlText.startsWithIgnoreCase("ana-manual://")
                         || urlText.startsWithIgnoreCase("ana-licenses://")
                         ? juce::URL {}
                         : juce::URL(urlText))
    {
        setOpaque(false);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setInterceptsMouseClicks(false, true);

        const auto linkFont = std::move(rowFont);
        linkButton.setFont(linkFont, false);
        linkButton.setJustificationType(juce::Justification::centred);
        linkButton.setColour(juce::HyperlinkButton::textColourId, ana::ui::white);
        linkButton.setMouseClickGrabsKeyboardFocus(false);
        linkButton.setWantsKeyboardFocus(false);
        linkButton.setSize(1, ana::ui::controlHeight);
        linkButton.changeWidthToFitText();

        if (urlText.startsWithIgnoreCase("ana-manual://"))
        {
            linkButton.onClick = []
            {
                const auto manualUrl = createOfflineDocumentUrl(
                    "manual.md", BinaryData::manual_md,
                    static_cast<size_t>(BinaryData::manual_mdSize));

                if (manualUrl.isWellFormed())
                    manualUrl.launchInDefaultBrowser();
            };
        }
        else if (urlText.startsWithIgnoreCase("ana-licenses://"))
        {
            linkButton.onClick = []
            {
                const auto licensesUrl = createOfflineDocumentUrl(
                    "licenses.md", BinaryData::licenses_md,
                    static_cast<size_t>(BinaryData::licenses_mdSize));

                if (licensesUrl.isWellFormed())
                    licensesUrl.launchInDefaultBrowser();
            };
        }

        addAndMakeVisible(linkButton);
    }

    int getPreferredHeight(int) const override
    {
        return ana::ui::controlHeight;
    }

    void resized() override
    {
        const auto buttonBounds = linkButton.getBounds().withCentre(getLocalBounds().getCentre());
        linkButton.setBounds(buttonBounds);
    }

private:
    juce::HyperlinkButton linkButton;
};

class MarkdownSpacerRow final : public MarkdownRowComponent
{
public:
    explicit MarkdownSpacerRow(const int spacerHeightIn)
        : spacerHeight(spacerHeightIn)
    {
        setOpaque(false);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setInterceptsMouseClicks(false, false);
    }

    int getPreferredHeight(int) const override
    {
        return juce::jmax(0, spacerHeight);
    }

private:
    int spacerHeight = 0;
};

class MarkdownContentView final : public juce::Component
{
public:
    explicit MarkdownContentView(juce::String markdownText)
    {
        setOpaque(false);
        setInterceptsMouseClicks(false, true);
        setMarkdownText(std::move(markdownText));
    }

    void setMarkdownText(juce::String markdownText)
    {
        rows.clear();

        const auto blocks = parseMarkdownBlocks(markdownText);
        rows.reserve(blocks.size());

        for (const auto& block : blocks)
        {
            if (block.kind == MarkdownBlock::Kind::spacer)
            {
                rows.push_back(std::make_unique<MarkdownSpacerRow>(promptItemGap));
            }
            else if (block.kind == MarkdownBlock::Kind::link)
            {
                rows.push_back(std::make_unique<MarkdownLinkRow>(block.text, block.url));
            }
            else
            {
                rows.push_back(std::make_unique<MarkdownTextRow>(block.text, ana::ui::makeFont(), ana::ui::white));
            }

            addAndMakeVisible(*rows.back());
        }

        resized();
        repaint();
    }


    void resized() override
    {
        const auto bounds = getLocalBounds().reduced(contentPadding);
        const auto contentWidth = bounds.getWidth();
        auto y = bounds.getY();

        for (size_t index = 0; index < rows.size(); ++index)
        {
            const auto preferredHeight = rows[index]->getPreferredHeight(contentWidth);
            rows[index]->setBounds(bounds.getX(), y, contentWidth, preferredHeight);
            y += preferredHeight;

            if (index + 1 < rows.size())
                y += promptItemGap;
        }
    }

private:

private:
    std::vector<std::unique_ptr<MarkdownRowComponent>> rows;
    int contentPadding = promptPanelPadding;
};
}
