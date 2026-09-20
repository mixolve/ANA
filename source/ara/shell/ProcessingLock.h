#pragma once

#include <JuceHeader.h>

#if JucePlugin_Enable_ARA

namespace ana::ara
{
class ProcessingLock
{
public:
    virtual ~ProcessingLock() = default;
    virtual juce::ReadWriteLock& getProcessingReadWriteLock() = 0;

    juce::ScopedTryReadLock tryProcessingReadLock()
    {
        return juce::ScopedTryReadLock(getProcessingReadWriteLock());
    }
};
}

#endif
