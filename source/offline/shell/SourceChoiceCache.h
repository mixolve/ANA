#pragma once

#include "offline/shell/SourceChoice.h"
#include "ProcessingLock.h"

#include <JuceHeader.h>


#include <utility>
#include <vector>

namespace ana::offline
{
class SourceChoiceCache
{
public:
    template <typename Builder>
    std::vector<offline::SourceChoice> get(ProcessingLock& processingLock,
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
    mutable std::vector<offline::SourceChoice> cachedChoices;
};
}
