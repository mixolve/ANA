#pragma once

#include <JuceHeader.h>


namespace ana::offline
{
class ProcessingLock
{
public:
    virtual ~ProcessingLock() = default;
    virtual juce::ReadWriteLock& getProcessingReadWriteLock() = 0;

    template <typename Cancel, typename Read>
    auto withProcessingReadLock(Cancel&& shouldCancel, Read&& read)
    {
        // ARA may destroy a renderer while holding the document write lock.
        // Waiting for readers must remain cancellable so shutdown can join safely.
        while (! shouldCancel())
        {
            {
                const auto guard = tryProcessingReadLock();
                if (guard.isLocked())
                    return read();
            }
            juce::Thread::sleep(1);
        }
        return decltype(read()) {};
    }

    juce::ScopedTryReadLock tryProcessingReadLock()
    {
        return juce::ScopedTryReadLock(getProcessingReadWriteLock());
    }
};
}
