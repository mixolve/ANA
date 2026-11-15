#pragma once

#include "offline/shell/SourceChoice.h"

#include <JuceHeader.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class ReaperHostBridge final : private juce::Timer
{
public:
    using ApplyTakeChoices = std::function<bool(const std::vector<ana::offline::SourceChoice>&)>;

    using ShouldScan = std::function<bool()>;

    explicit ReaperHostBridge(ApplyTakeChoices applyTakeChoices, bool followSelectedTrack = false,
                              ShouldScan shouldScan = {});
    ~ReaperHostBridge() override;

    void start();
    void stop();
    void setHostApplication(Steinberg::FUnknown* hostApplication);
    // REAPER's native window-screenset registration (optional outside REAPER).
    using ScreensetCallback = std::intptr_t (*)(int, const char*, void*, void*, int);
    bool registerScreensetWindow(const char* id, ScreensetCallback callback, void* context) const;
    void unregisterScreensetWindow(void* context) const;
    std::vector<ana::offline::SourceChoice> getTakeChoices() const;
    bool hasTakeCatalogue() const noexcept;
    bool isAvailable() const noexcept;
    void setEnabled(bool enabled);
    void refresh();
    int getTrackNumber() const;
    juce::String getTrackName() const;

private:
    struct TrackTarget
    {
        void* track = nullptr;
        juce::String id;
        juce::String name;
        int number = 0;
        bool operator==(const TrackTarget&) const = default;
    };
    void timerCallback() override;
    void updateCatalogue(bool forceScan);
    int getProjectStateChangeCount() const;
    std::optional<TrackTarget> getTargetTrack() const;
    std::optional<std::vector<void*>> getSelectedItems() const;
    std::optional<std::vector<ana::offline::SourceChoice>> scanTakeChoices(
        void* track, const std::vector<void*>& selectedItems) const;

    ApplyTakeChoices applyTakeChoices;
    ShouldScan shouldScan;
    mutable juce::CriticalSection hostLock;
    mutable juce::CriticalSection choicesLock;
    void* hostApplication = nullptr;
    // Checked on the host audio callback: never acquire hostLock here.
    std::atomic<bool> hostInterfaceAvailable { false };
    std::vector<ana::offline::SourceChoice> cachedTakeChoices;
    std::vector<void*> cachedSelectedItems;
    TrackTarget cachedTarget;
    const bool followSelectedTrack;
    std::atomic<bool> enabled { true };
    std::atomic<int> lastProjectState { -1 };
    std::atomic<bool> takeChoicesInitialised { false };
    std::atomic<bool> takeChoicesCacheValid { false };
    std::atomic<int> emptyScanConfirmations { 0 };
};
