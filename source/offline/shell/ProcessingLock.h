#pragma once

#include <JuceHeader.h>


namespace ana::offline
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
