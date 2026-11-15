#pragma once
#include "realtime/spec/View.h"
#include "offline/spec/View.h"
#include "shared/spec/Snapshots.h"
#include "shell/Processor.h"

class SpecView final : public juce::Component, private juce::Timer
{
public:
    static constexpr float snapshotGainMinimumDb = ana::spec::SnapshotStore::snapshotGainMinimumDb;
    static constexpr float snapshotGainMaximumDb = ana::spec::SnapshotStore::snapshotGainMaximumDb;

    explicit SpecView(PluginProcessor& owner)
        : processor(owner),
          snapshots(&owner.getParameters().state),
          realtime(owner, snapshots.getSnapshots()),
          offline(owner, snapshots.getSnapshots())
    {
        addChildComponent(realtime);
        addChildComponent(offline);
        refreshMode();
        startTimerHz(20);
    }
    ~SpecView() override { stopTimer(); }
    void refreshMode()
    {
        const auto isOffline = processor.isOfflineMode();
        realtime.setVisible(! isOffline);
        offline.setVisible(isOffline);
        resized();
    }
    void resized() override
    {
        const auto bounds = getLocalBounds();
        for (auto* child : std::array<juce::Component*, 2> { &realtime, &offline })
        {
            if (child->getBounds() == bounds)
                child->resized();
            else
                child->setBounds(bounds);
        }
    }
    juce::String getSettingsViewModeName() const { return processor.isSpecMapView() ? "MAP" : "FREQ"; }
    PluginProcessor& getProcessor() noexcept { return processor; }
    size_t getSnapshotCount() const noexcept { return snapshots.size(); }
    juce::String getSnapshotName(size_t i) const { return snapshots.getName(i); }
    void setSnapshotName(size_t i, const juce::String& name) { snapshots.setName(i, name); }
    std::function<void()> onSnapshotStateRestored;
    size_t addSnapshotSlot() { return snapshots.addSnapshotSlot(); }
    bool removeSnapshotSlot(size_t i)
    {
        const auto removed = snapshots.removeSnapshotSlot(i);
        repaintSnapshots();
        return removed;
    }
    bool captureSnapshot(size_t i)
    {
        snapshots.synchroniseState();
        const auto captured = processor.isOfflineMode() ? offline.captureSnapshot(i) : realtime.captureSnapshot(i);
        if (captured) snapshots.persistState();
        repaintSnapshots();
        return captured;
    }
    void setSnapshotVisible(size_t i, bool value) { snapshots.setSnapshotVisible(i, value); repaintSnapshots(); }
    void setSnapshotColour(size_t i, juce::Colour value) { snapshots.setSnapshotColour(i, value); repaintSnapshots(); }
    void setSnapshotGain(size_t i, float value) { snapshots.setSnapshotGain(i, value); repaintSnapshots(); }
    bool writeSnapshot(size_t i, juce::OutputStream& output) const { return snapshots.writeSnapshot(i, output); }
    bool readSnapshot(size_t i, juce::InputStream& input)
    {
        const auto loaded = snapshots.readSnapshot(i, input);
        repaintSnapshots();
        return loaded;
    }
    bool hasSnapshotData(size_t i) const { return snapshots.hasSnapshotData(i); }
    bool isSnapshotVisible(size_t i) const { return snapshots.isSnapshotVisible(i); }
    juce::Colour getSnapshotColour(size_t i) const { return snapshots.getSnapshotColour(i); }
    float getSnapshotGain(size_t i) const { return snapshots.getSnapshotGain(i); }
    void refreshSnapshots(bool forceNotify = false)
    {
        if (snapshots.synchroniseState() || forceNotify)
        {
            if (onSnapshotStateRestored) onSnapshotStateRestored();
            repaintSnapshots();
        }
    }
private:
    void timerCallback() override { if (isShowing()) refreshSnapshots(); }
    void repaintSnapshots() { realtime.repaint(); offline.repaint(); }
    PluginProcessor& processor;
    ana::spec::SnapshotStore snapshots;
    RealtimeSpecView realtime;
    OfflineSpecView offline;
};
