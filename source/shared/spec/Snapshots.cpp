#include "Snapshots.h"
#include "shared/shell/StereoFftStream.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace ana::spec
{
namespace { constexpr const char* snapshotsStateKey = "ana.spec.snapshots"; }

SnapshotStore::SnapshotStore(juce::ValueTree* stateIn) : state(stateIn)
{
    synchroniseState();
}

juce::String SnapshotStore::getName(const size_t index) const
{
    return index < snapshots.size() ? snapshots[index].name : juce::String();
}

void SnapshotStore::setName(const size_t index, const juce::String& name)
{
    synchroniseState();
    if (index >= snapshots.size() || snapshots[index].name == name)
        return;
    snapshots[index].name = name;
    persistState();
}

void SnapshotStore::persistState()
{
    if (state == nullptr)
        return;
    juce::ValueTree saved("ANA_SNAPSHOTS");
    for (size_t index = 0; index < snapshots.size(); ++index)
    {
        const auto& snapshot = snapshots[index];
        juce::ValueTree slot("SNAPSHOT");
        slot.setProperty("name", snapshot.name, nullptr);
        slot.setProperty("colour", snapshot.colour.toString(), nullptr);
        slot.setProperty("gain", snapshot.gainDb, nullptr);
        slot.setProperty("visible", snapshot.visible, nullptr);
        juce::MemoryOutputStream payload;
        if (snapshot.hasData && writeSnapshot(index, payload))
            slot.setProperty("data", juce::var(payload.getMemoryBlock()), nullptr);
        saved.addChild(slot, -1, nullptr);
    }
    juce::MemoryOutputStream output;
    saved.writeToStream(output);
    lastState = juce::var(output.getMemoryBlock());
    state->setProperty(snapshotsStateKey, lastState, nullptr);
}

bool SnapshotStore::synchroniseState()
{
    if (state == nullptr)
        return false;
    const auto stored = state->getProperty(snapshotsStateKey);
    if (stored == lastState)
        return false;
    if (! loadState(stored)) return false;
    lastState = stored;
    return true;
}

bool SnapshotStore::loadState(const juce::var& stored)
{
    SnapshotStore loaded;
    if (! stored.isVoid())
    {
        const auto* bytes = stored.getBinaryData();
        if (bytes == nullptr) return false;
        const auto saved = juce::ValueTree::readFromData(bytes->getData(), bytes->getSize());
        if (! saved.hasType("ANA_SNAPSHOTS") || saved.getNumProperties() != 0) return false;
        for (const auto slot : saved)
        {
            if (! slot.hasType("SNAPSHOT") || slot.getNumChildren() != 0
                || ! slot.hasProperty("name") || ! slot.hasProperty("colour")
                || ! slot.hasProperty("gain") || ! slot.hasProperty("visible")) return false;
            const auto colour = slot.getProperty("colour").toString();
            const auto gain = static_cast<float>(slot.getProperty("gain"));
            const auto visible = slot.getProperty("visible").toString();
            if (! slot.getProperty("name").isString() || (colour.isEmpty() || colour.length() > 8)
                || ! colour.containsOnly("0123456789abcdefABCDEF")
                || (! slot.getProperty("gain").isDouble() && ! slot.getProperty("gain").isInt())
                || ! std::isfinite(gain) || gain < snapshotGainMinimumDb || gain > snapshotGainMaximumDb
                || (visible != "0" && visible != "1")) return false;
            loaded.snapshots.emplace_back();
            const auto index = loaded.snapshots.size() - 1;
            if (slot.hasProperty("data"))
            {
                const auto* data = slot.getProperty("data").getBinaryData();
                if (data == nullptr) return false;
                juce::MemoryInputStream input(*data, false);
                if (! loaded.readSnapshot(index, input)) return false;
            }
            auto& snapshot = loaded.snapshots[index];
            snapshot.name = slot.getProperty("name").toString();
            snapshot.colour = juce::Colour::fromString(colour);
            snapshot.gainDb = gain;
            snapshot.visible = visible == "1";
        }
    }
    snapshots = std::move(loaded.snapshots);
    return true;
}

size_t SnapshotStore::addSnapshotSlot()
{
    synchroniseState();
    snapshots.emplace_back();
    persistState();
    return snapshots.size() - 1;
}

bool SnapshotStore::removeSnapshotSlot(const size_t snapshotIndex)
{
    synchroniseState();
    if (snapshotIndex >= snapshots.size())
        return false;

    snapshots.erase(snapshots.begin() + static_cast<std::ptrdiff_t>(snapshotIndex));
    persistState();
    return true;
}

void SnapshotStore::setSnapshotVisible(const size_t snapshotIndex, const bool shouldBeVisible)
{
    synchroniseState();
    if (snapshotIndex >= snapshots.size())
        return;

    if (snapshots[snapshotIndex].visible == shouldBeVisible)
        return;

    snapshots[snapshotIndex].visible = shouldBeVisible;
    persistState();
}

void SnapshotStore::setSnapshotColour(const size_t snapshotIndex, const juce::Colour colour)
{
    synchroniseState();
    if (snapshotIndex >= snapshots.size())
        return;

    if (snapshots[snapshotIndex].colour == colour)
        return;

    snapshots[snapshotIndex].colour = colour;
    persistState();
}

void SnapshotStore::setSnapshotGain(const size_t snapshotIndex, const float gainDb)
{
    synchroniseState();
    if (snapshotIndex >= snapshots.size() || ! std::isfinite(gainDb))
        return;

    const auto clampedGain = juce::jlimit(snapshotGainMinimumDb, snapshotGainMaximumDb, gainDb);
    if (std::abs(snapshots[snapshotIndex].gainDb - clampedGain) < 0.0001f)
        return;

    snapshots[snapshotIndex].gainDb = clampedGain;
    persistState();
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
    synchroniseState();
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
    loaded.name = snapshots[snapshotIndex].name;
    snapshots[snapshotIndex] = std::move(loaded);
    persistState();
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
