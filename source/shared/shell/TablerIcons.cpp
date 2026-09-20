#include "TablerIcons.h"

#include <algorithm>

namespace
{
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

    const auto pixelSize = std::max(1, juce::roundToInt(pointSize * 2.0f));
    juce::Image image(juce::Image::ARGB, pixelSize, pixelSize, true);
    juce::Graphics graphics(image);
    drawable->drawWithin(graphics, image.getBounds().toFloat(),
                         juce::RectanglePlacement::centred, 1.0f);
    return image;
}
