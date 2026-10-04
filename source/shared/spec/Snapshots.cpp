#include "Snapshots.h"
#include "shared/shell/StereoFftStream.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace ana::spec
{
size_t SnapshotStore::addSnapshotSlot()
{
    snapshots.emplace_back();
    return snapshots.size() - 1;
}

bool SnapshotStore::removeSnapshotSlot(const size_t snapshotIndex)
{
    if (snapshotIndex >= snapshots.size())
        return false;

    snapshots.erase(snapshots.begin() + static_cast<std::ptrdiff_t>(snapshotIndex));
    return true;
}

void SnapshotStore::setSnapshotVisible(const size_t snapshotIndex, const bool shouldBeVisible)
{
    if (snapshotIndex >= snapshots.size())
        return;

    if (snapshots[snapshotIndex].visible == shouldBeVisible)
        return;

    snapshots[snapshotIndex].visible = shouldBeVisible;
}

void SnapshotStore::setSnapshotColour(const size_t snapshotIndex, const juce::Colour colour)
{
    if (snapshotIndex >= snapshots.size())
        return;

    if (snapshots[snapshotIndex].colour == colour)
        return;

    snapshots[snapshotIndex].colour = colour;
}

void SnapshotStore::setSnapshotGain(const size_t snapshotIndex, const float gainDb)
{
    if (snapshotIndex >= snapshots.size() || ! std::isfinite(gainDb))
        return;

    const auto clampedGain = juce::jlimit(snapshotGainMinimumDb, snapshotGainMaximumDb, gainDb);
    if (std::abs(snapshots[snapshotIndex].gainDb - clampedGain) < 0.0001f)
        return;

    snapshots[snapshotIndex].gainDb = clampedGain;
}

bool SnapshotStore::writeSnapshot(const size_t snapshotIndex, juce::OutputStream& output) const
{
    if (snapshotIndex >= snapshots.size())
        return false;

    const auto& snapshot = snapshots[snapshotIndex];
    const auto expectedBinCount = snapshot.fftSize > 0
        ? static_cast<size_t>(snapshot.fftSize / 2 + 1)
        : 0;
    if (! snapshot.hasData
        || ! ana::fft::StereoFftStream::isSupportedFftSize(snapshot.fftSize)
        || ! std::isfinite(snapshot.sampleRate) || snapshot.sampleRate <= 0.0
        || ! std::isfinite(snapshot.gainDb) || snapshot.gainDb < snapshotGainMinimumDb || snapshot.gainDb > snapshotGainMaximumDb
        || snapshot.primary.size() != expectedBinCount
        || (snapshot.drawSecondGraph ? snapshot.secondary.size() != expectedBinCount
                                    : ! snapshot.secondary.empty()))
        return false;

    const auto writeValues = [&output] (const std::vector<float>& values)
    {
        if (values.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
            return false;

        if (! output.writeInt(static_cast<int>(values.size())))
            return false;

        for (const auto value : values)
            if (! std::isfinite(value) || ! output.writeFloat(value))
                return false;

        return true;
    };

    return output.writeInt(snapshot.fftSize)
        && output.writeDouble(snapshot.sampleRate)
        && output.writeByte(snapshot.drawSecondGraph ? 1 : 0)
        && output.writeInt(static_cast<int>(snapshot.colour.getARGB()))
        && output.writeByte(snapshot.visible ? 1 : 0)
        && output.writeFloat(snapshot.gainDb)
        && writeValues(snapshot.primary)
        && writeValues(snapshot.secondary);
}

bool SnapshotStore::readSnapshot(const size_t snapshotIndex, juce::InputStream& input)
{
    if (snapshotIndex >= snapshots.size())
        return false;

    constexpr juce::int64 fixedHeaderSize = 4 + 8 + 1 + 4 + 1 + 4;
    if (input.getNumBytesRemaining() < fixedHeaderSize)
        return false;

    SpectrumSnapshot loaded;
    loaded.fftSize = input.readInt();
    loaded.sampleRate = input.readDouble();
    const auto drawSecondGraph = input.readByte();
    loaded.colour = juce::Colour(static_cast<juce::uint32>(input.readInt()));
    const auto visible = input.readByte();
    loaded.gainDb = input.readFloat();

    const auto isBooleanByte = [] (const char value) noexcept
    {
        return value == 0 || value == 1;
    };
    if (! isBooleanByte(drawSecondGraph) || ! isBooleanByte(visible))
        return false;

    loaded.drawSecondGraph = drawSecondGraph == 1;
    loaded.visible = visible == 1;

    if (! ana::fft::StereoFftStream::isSupportedFftSize(loaded.fftSize)
        || ! std::isfinite(loaded.gainDb) || loaded.gainDb < snapshotGainMinimumDb || loaded.gainDb > snapshotGainMaximumDb
        || ! std::isfinite(loaded.sampleRate) || loaded.sampleRate <= 0.0)
        return false;

    const auto expectedBinCount = static_cast<size_t>(loaded.fftSize / 2 + 1);
    const auto readValues = [&input, expectedBinCount] (std::vector<float>& values, const bool required)
    {
        if (input.getNumBytesRemaining() < 4)
            return false;
        const auto count = input.readInt();
        const auto expectedCount = required ? expectedBinCount : 0;
        if (count < 0 || static_cast<size_t>(count) != expectedCount
            || input.getNumBytesRemaining() < static_cast<juce::int64>(count) * 4)
            return false;

        values.resize(expectedCount);
        for (auto& value : values)
        {
            value = input.readFloat();
            if (! std::isfinite(value))
                return false;
        }
        return true;
    };

    if (! readValues(loaded.primary, true)
        || ! readValues(loaded.secondary, loaded.drawSecondGraph)
        || input.getNumBytesRemaining() != 0)
        return false;

    loaded.hasData = true;
    snapshots[snapshotIndex] = std::move(loaded);
    return true;
}

bool SnapshotStore::hasSnapshotData(const size_t snapshotIndex) const noexcept
{
    return snapshotIndex < snapshots.size() && snapshots[snapshotIndex].hasData;
}

bool SnapshotStore::isSnapshotVisible(const size_t snapshotIndex) const noexcept
{
    return snapshotIndex < snapshots.size() && snapshots[snapshotIndex].visible;
}

juce::Colour SnapshotStore::getSnapshotColour(const size_t snapshotIndex) const noexcept
{
    return snapshotIndex < snapshots.size()
        ? snapshots[snapshotIndex].colour
        : juce::Colours::white;
}

float SnapshotStore::getSnapshotGain(const size_t snapshotIndex) const noexcept
{
    return snapshotIndex < snapshots.size() ? snapshots[snapshotIndex].gainDb : 0.0f;
}


}
