#include "TablerIcons.h"

#include <algorithm>

namespace
{
juce::Image cropToVisiblePixels(const juce::Image& image)
{
    if (image.isNull())
        return {};

    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    auto left = image.getWidth();
    auto top = image.getHeight();
    auto right = -1;
    auto bottom = -1;

    for (int y = 0; y < image.getHeight(); ++y)
    {
        for (int x = 0; x < image.getWidth(); ++x)
        {
            if (pixels.getPixelColour(x, y).getAlpha() == 0)
                continue;

            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }

    if (right < left || bottom < top)
        return {};

    return image.getClippedImage({ left, top, right - left + 1, bottom - top + 1 });
}

const char* findIconData(const juce::String& fileName, int& dataSize)
{
    for (int index = 0; index < BinaryData::namedResourceListSize; ++index)
    {
        if (fileName == BinaryData::originalFilenames[index])
            return BinaryData::getNamedResource(BinaryData::namedResourceList[index], dataSize);
    }

    dataSize = 0;
    return nullptr;
}
}

juce::Image loadTablerIcon(const juce::String& iconName, const float pointSize)
{
    int dataSize = 0;
    const auto* data = findIconData(iconName + ".svg", dataSize);
    if (data == nullptr || dataSize <= 0)
        return {};

    auto drawable = juce::Drawable::createFromImageData(data, static_cast<size_t>(dataSize));
    if (drawable == nullptr)
        return {};

    constexpr auto renderScale = 4.0f;
    const auto pixelSize = std::max(1, juce::roundToInt(pointSize * renderScale));
    juce::Image image(juce::Image::ARGB, pixelSize, pixelSize, true);
    juce::Graphics graphics(image);
    drawable->drawWithin(graphics, image.getBounds().toFloat(),
                         juce::RectanglePlacement::centred, 1.0f);
    return cropToVisiblePixels(image);
}
