#pragma once

#include <JuceHeader.h>

inline juce::String uppercaseText(const juce::String& text)
{
    auto upper = text.toUpperCase();
    juce::String result;
    for (auto characters = upper.getCharPointer(); ! characters.isEmpty();)
    {
        auto character = characters.getAndAdvance();
        // Cyrillic casing must also work when the host uses the C locale.
        if (character >= 0x0430 && character <= 0x044f) character -= 0x20;
        else if (character >= 0x0450 && character <= 0x045f) character -= 0x50;
        else if (character == 0x04cf) character = 0x04c0;
        else if (character >= 0x04c2 && character <= 0x04ce && (character & 1) == 0) --character;
        else if (((character >= 0x0460 && character <= 0x0481)
                  || (character >= 0x048a && character <= 0x04bf)
                  || (character >= 0x04d0 && character <= 0x052f)) && (character & 1) != 0) --character;
        result += juce::String::charToString(character);
    }
    return result;
}


class UppercaseEditorInputFilter final : public juce::TextEditor::InputFilter
{
public:
    juce::String filterNewText(juce::TextEditor&, const juce::String& text) override
    { return uppercaseText(text); }
};
