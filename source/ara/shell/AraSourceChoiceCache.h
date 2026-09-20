#pragma once

#include "shell/AraSourceChoice.h"
#include "ProcessingLock.h"

#include <JuceHeader.h>

#if JucePlugin_Enable_ARA

#include <utility>
#include <vector>

namespace ana::ara
{
class AraSourceChoiceCache
{
public:
    template <typename Builder>
    std::vector<ara::SourceChoice> get(ProcessingLock& processingLock,
                                                   Builder&& builder) const
    {
        const auto processingReadLock = processingLock.tryProcessingReadLock();
        if (! processingReadLock.isLocked())
        {
            const juce::ScopedLock scopedLock(cacheLock);
            return cachedChoices;
        }

        auto choices = std::forward<Builder>(builder)();
        {
            const juce::ScopedLock scopedLock(cacheLock);
            cachedChoices = choices;
        }
        return choices;
    }

private:
    mutable juce::CriticalSection cacheLock;
    mutable std::vector<ara::SourceChoice> cachedChoices;
};
}

#endif
