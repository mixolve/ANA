#include "AnaTheme.h"

namespace ana::ui
{
juce::Font makeFont()
{
#if JUCE_TARGET_HAS_BINARY_DATA
    if (auto typeface = juce::Typeface::createSystemTypefaceFor(
            BinaryData::SometypeMonoRegular_ttf,
            static_cast<size_t>(BinaryData::SometypeMonoRegular_ttfSize)))
        return juce::Font(juce::FontOptions(typeface).withHeight(baseFontSize));
#endif

    return juce::Font(juce::FontOptions("Sometype Mono", baseFontSize, juce::Font::plain));
}
} // namespace ana::ui
