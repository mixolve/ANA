#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cstddef>

namespace ana::spec
{
struct SpectrumSnapshot
{
    std::vector<float> primary;
    std::vector<float> secondary;
    int fftSize = 0;
    double sampleRate = 0.0;
    bool drawSecondGraph = false;
    juce::Colour colour { juce::Colours::white };
    float gainDb = 0.0f;
    bool visible = true;
    bool hasData = false;
};


class SnapshotStore
{
public:
    static constexpr float snapshotGainMinimumDb = -48.0f;
    static constexpr float snapshotGainMaximumDb = 48.0f;
    size_t addSnapshotSlot();
    bool removeSnapshotSlot(size_t snapshotIndex);
    void setSnapshotVisible(size_t snapshotIndex, bool shouldBeVisible);
    void setSnapshotColour(size_t snapshotIndex, juce::Colour colour);
    void setSnapshotGain(size_t snapshotIndex, float gainDb);
    bool writeSnapshot(size_t snapshotIndex, juce::OutputStream& output) const;
    bool readSnapshot(size_t snapshotIndex, juce::InputStream& input);
    bool hasSnapshotData(size_t snapshotIndex) const noexcept;
    bool isSnapshotVisible(size_t snapshotIndex) const noexcept;
    juce::Colour getSnapshotColour(size_t snapshotIndex) const noexcept;
    float getSnapshotGain(size_t snapshotIndex) const noexcept;

    std::vector<SpectrumSnapshot>& getSnapshots() noexcept { return snapshots; }
private:
    std::vector<SpectrumSnapshot> snapshots;
};
}
