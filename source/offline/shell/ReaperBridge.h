#pragma once

#include "offline/shell/SourceChoice.h"

#include <JuceHeader.h>

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

class ReaperHostBridge final : private juce::Timer
{
public:
    using ApplyTakeChoices = std::function<bool(const std::vector<ana::offline::SourceChoice>&)>;

    explicit ReaperHostBridge(ApplyTakeChoices applyTakeChoices, bool followSelectedTrack = false);
    ~ReaperHostBridge() override;

    void start();
    void stop();
    void setHostApplication(Steinberg::FUnknown* hostApplication);
    std::vector<ana::offline::SourceChoice> getTakeChoices() const;
    bool hasTakeCatalogue() const noexcept;
    bool isAvailable() const;
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
    std::optional<std::vector<ana::offline::SourceChoice>> scanTakeChoices(void* track) const;

    ApplyTakeChoices applyTakeChoices;
    mutable juce::CriticalSection hostLock;
    mutable juce::CriticalSection choicesLock;
    void* hostApplication = nullptr;
    std::vector<ana::offline::SourceChoice> cachedTakeChoices;
    TrackTarget cachedTarget;
    const bool followSelectedTrack;
    std::atomic<bool> enabled { true };
    std::atomic<int> lastProjectState { -1 };
    std::atomic<bool> takeChoicesInitialised { false };
    std::atomic<bool> takeChoicesCacheValid { false };
    std::atomic<int> emptyScanConfirmations { 0 };
};
