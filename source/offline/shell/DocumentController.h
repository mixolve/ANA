#pragma once

#include "EditorRenderer.h"
#include "PlaybackRenderer.h"
#include "ProcessingLock.h"


namespace ana::offline
{
class DocumentController final : public juce::ARADocumentControllerSpecialisation,
                                 private ProcessingLock
{
public:
    using juce::ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

protected:
    void willBeginEditing(juce::ARADocument* document) override;
    void didEndEditing(juce::ARADocument* document) override;
    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override;
    juce::ARAEditorRenderer* doCreateEditorRenderer() noexcept override;
    bool doRestoreObjectsFromStream(juce::ARAInputStream& input,
                                    const juce::ARARestoreObjectsFilter* filter) noexcept override;
    bool doStoreObjectsToStream(juce::ARAOutputStream& output,
                                const juce::ARAStoreObjectsFilter* filter) noexcept override;

private:
    juce::ReadWriteLock& getProcessingReadWriteLock() override;

    juce::ReadWriteLock processingLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DocumentController)
};
}
