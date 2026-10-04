#pragma once

#include <JuceHeader.h>

namespace ana::offline
{
inline std::unique_ptr<juce::AudioFormatReader> createSourceFileReader(const juce::String& path)
{
    if (! juce::File::isAbsolutePath(path))
        return {};
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(juce::File(path)));
}
}
