#pragma once

#include "shell/AraSourceChoice.h"

#include <JuceHeader.h>

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

class ReaperHostBridge final : private juce::Timer
{
public:
    using ApplyTakeChoices = std::function<bool(const std::vector<ana::ara::SourceChoice>&)>;

    explicit ReaperHostBridge(ApplyTakeChoices applyTakeChoices);
    ~ReaperHostBridge() override;

    void start();
    void stop();
    void setHostApplication(Steinberg::FUnknown* hostApplication);
    std::vector<ana::ara::SourceChoice> getTakeChoices() const;

private:
    void timerCallback() override;
    int getProjectStateChangeCount() const;
    std::optional<std::vector<ana::ara::SourceChoice>> scanTakeChoices() const;

    ApplyTakeChoices applyTakeChoices;
    mutable juce::CriticalSection hostLock;
    mutable juce::CriticalSection choicesLock;
    void* hostApplication = nullptr;
    std::vector<ana::ara::SourceChoice> cachedTakeChoices;
    std::atomic<int> lastProjectState { -1 };
    std::atomic<bool> takeChoicesInitialised { false };
    std::atomic<bool> takeChoicesCacheValid { false };
    std::atomic<int> emptyScanConfirmations { 0 };
};
