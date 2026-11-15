#pragma once
#include "realtime/scop/View.h"
#include "offline/scop/View.h"
#include "shell/Processor.h"
class ScopView final : public juce::Component
{
public:
    explicit ScopView(PluginProcessor& owner) : processor(owner), realtime(owner), offline(owner)
    {
        addChildComponent(realtime); addChildComponent(offline);
        refreshMode();
    }
    void refreshMode() { realtime.setVisible(!processor.isOfflineMode()); offline.setVisible(processor.isOfflineMode()); resized(); }
    void resized() override { realtime.setBounds(getLocalBounds()); offline.setBounds(getLocalBounds()); }
    void setFrozen(bool value) { realtime.setFrozen(value); }
    void clearHistory() { realtime.clearHistory(); }
    void refreshWaveform() { if (processor.isOfflineMode()) offline.refreshWaveform(); }
    void refreshBandLayout() { realtime.refreshBandLayout(); offline.refreshBandLayout(); }
    void setLinkedNormalization(bool enabled)
    {
        if (processor.isOfflineMode()) offline.setLinkedNormalization(enabled);
        else realtime.setLinkedNormalization(enabled);
    }
    void refreshDisplaySettings() { if (processor.isOfflineMode()) offline.refreshDisplaySettings(); else realtime.refreshDisplaySettings(); }
    void setFullSourceView(bool value) { realtime.setFullSourceView(value); offline.setFullSourceView(value); }
private:
    PluginProcessor& processor;
    RealtimeScopView realtime;
    OfflineScopView offline;
};
