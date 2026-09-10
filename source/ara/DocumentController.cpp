#include "DocumentController.h"

#if JucePlugin_Enable_ARA

namespace ana::ara
{
void DocumentController::willBeginEditing(juce::ARADocument*)
{
    processingLock.enterWrite();
}

void DocumentController::didEndEditing(juce::ARADocument*)
{
    processingLock.exitWrite();
}

juce::ARAPlaybackRenderer* DocumentController::doCreatePlaybackRenderer() noexcept
{
    return new PlaybackRenderer(getDocumentController(), *this);
}

juce::ARAEditorRenderer* DocumentController::doCreateEditorRenderer() noexcept
{
    return new EditorRenderer(getDocumentController(), *this);
}

bool DocumentController::doRestoreObjectsFromStream(juce::ARAInputStream&,
                                                     const juce::ARARestoreObjectsFilter*) noexcept
{
    return true;
}

bool DocumentController::doStoreObjectsToStream(juce::ARAOutputStream&,
                                                 const juce::ARAStoreObjectsFilter*) noexcept
{
    return true;
}

juce::ReadWriteLock& DocumentController::getProcessingReadWriteLock()
{
    return processingLock;
}
} // namespace ana::ara

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<ana::ara::DocumentController>();
}

#endif
