#if ANA_VARIANT_RTM
#include "rtm/spec/Processor.h"
#else
#include "ara/spec/Processor.h"
#endif

#include <algorithm>

namespace ana::spec
{
namespace
{
template <typename Channels>
void fillChannels(Channels& channels, const float value) noexcept
{
    for (auto& channel : channels)
        channel.fill(value);
}

template <typename AtomicChannels>
void storeChannels(AtomicChannels& channels, const float value) noexcept
{
    for (auto& channel : channels)
        for (auto& bin : channel)
            bin.store(value, std::memory_order_relaxed);
}
}

SpecProcessor::SpecProcessor() { reset(); }

void SpecProcessor::prepare(const double newSampleRate)
{
    fftStream.prepare(newSampleRate);
    mapFftStream.prepare(newSampleRate);
    reset();
}

void SpecProcessor::reset()
{
    fftStream.reset();
    mapFftStream.reset();
   #if ANA_VARIANT_RTM
    resetRtmState();
    resetMapState();
   #else
    resetAraAccumulators();
    storeChannels(publishedAverageLevels, minimumDecibels);
    storeChannels(publishedCurrentLevels, minimumDecibels);
   #endif
    resetMaximums();
    storeChannels(publishedMaximumLevels, minimumDecibels);

    publishedFftSize.store(0, std::memory_order_release);
   #if ANA_VARIANT_RTM
    publishedMapFftSize.store(0, std::memory_order_release);
   #endif
    lastFrameSize = 0;
    clearRevision.fetch_add(1, std::memory_order_release);
    revision.fetch_add(1, std::memory_order_release);
   #if ANA_VARIANT_RTM
    mapRevision.fetch_add(1, std::memory_order_release);
   #endif
}

void SpecProcessor::resetMaximums() noexcept
{
    fillChannels(maximumLevels, minimumDecibels);
}

void SpecProcessor::requestClear() noexcept
{
    clearRequested.store(true, std::memory_order_release);
}

void SpecProcessor::setFrozen(const bool shouldFreeze) noexcept
{
    frozen.store(shouldFreeze, std::memory_order_release);
}

bool SpecProcessor::isFrozen() const noexcept
{
    return frozen.load(std::memory_order_acquire);
}

void SpecProcessor::copySpectrum(const Channel channel,
                                 const DisplayType type,
                                 std::vector<float>& destination,
                                 int& fftSize) const
{
    fftSize = publishedFftSize.load(std::memory_order_acquire);
    const auto binCount = fftSize > 0 ? fftSize / 2 + 1 : 0;
    destination.resize(static_cast<size_t>(binCount));

    const auto index = std::min(channelIndex(channel), channelCount - 1);
    const auto* source = &publishedMaximumLevels[index];
    if (type == DisplayType::average)
        source = &publishedAverageLevels[index];
    else if (type == DisplayType::current)
        source = &publishedCurrentLevels[index];

    for (int bin = 0; bin < binCount; ++bin)
        destination[static_cast<size_t>(bin)] =
            (*source)[static_cast<size_t>(bin)].load(std::memory_order_acquire);
}

double SpecProcessor::getSampleRate() const noexcept
{
    return fftStream.getSampleRate();
}

uint64_t SpecProcessor::getRevision() const noexcept
{
    return revision.load(std::memory_order_acquire);
}

uint64_t SpecProcessor::getClearRevision() const noexcept
{
    return clearRevision.load(std::memory_order_acquire);
}
}
