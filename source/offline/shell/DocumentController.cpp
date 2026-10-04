#include "DocumentController.h"


namespace ana::offline
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
}

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<ana::offline::DocumentController>();
}
