#include "UiStyle.h"

namespace ana::ui
{
juce::Font makeFont(const float height, const bool bold)
{
#if JUCE_TARGET_HAS_BINARY_DATA
    const auto* data = bold ? BinaryData::SometypeMonoBold_ttf : BinaryData::SometypeMonoRegular_ttf;
    const auto size = bold ? BinaryData::SometypeMonoBold_ttfSize : BinaryData::SometypeMonoRegular_ttfSize;

    if (auto typeface = juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size)))
        return juce::Font(juce::FontOptions(typeface).withHeight(height));
#endif

    return juce::Font(juce::FontOptions("Sometype Mono", height,
                                        bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace ana::ui
