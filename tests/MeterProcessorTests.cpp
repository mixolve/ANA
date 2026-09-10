#include "lvls/MeterProcessor.h"
#include "ara/BandlimitedResampler.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 480;

struct TestSignal
{
    ana::lvls::MeterProcessor meter;
    double phase = 0.0;

    TestSignal()
    {
        meter.prepare(sampleRate, blockSize);
    }

    void processSine(const float peakDecibels, const double durationSeconds)
    {
        processSineAtFrequency(peakDecibels, 1000.0, durationSeconds);
    }

    void processSineAtFrequency(const float peakDecibels,
                                const double frequency,
                                const double durationSeconds,
                                const double fadeInSeconds = 0.0,
                                const ana::lvls::MeterProcessor::ProcessingOptions options = {})
    {
        juce::AudioBuffer<float> buffer(2, blockSize);
        const auto amplitude = juce::Decibels::decibelsToGain(peakDecibels);
        const auto phaseIncrement = juce::MathConstants<double>::twoPi * frequency / sampleRate;
        const auto totalSamples = juce::roundToInt(durationSeconds * sampleRate);
        const auto fadeSamples = juce::roundToInt(fadeInSeconds * sampleRate);

        for (auto processed = 0; processed < totalSamples; processed += blockSize)
        {
            const auto samples = std::min(blockSize, totalSamples - processed);
            buffer.clear();
            for (auto sample = 0; sample < samples; ++sample)
            {
                const auto fade = fadeSamples > 0
                    ? juce::jlimit(0.0f, 1.0f, static_cast<float>(processed + sample)
                        / static_cast<float>(fadeSamples))
                    : 1.0f;
                const auto value = amplitude * fade * static_cast<float>(std::sin(phase));
                phase += phaseIncrement;
                if (phase >= juce::MathConstants<double>::twoPi)
                    phase -= juce::MathConstants<double>::twoPi;
                buffer.setSample(0, sample, value);
                buffer.setSample(1, sample, value);
            }
            meter.processBlock(buffer, 300.0f, 1000.0f, false, options);
        }
    }

    void processStereoSine(const float leftPeakDecibels,
                           const float rightPeakDecibels,
                           const double durationSeconds,
                           const float rightPolarity = 1.0f)
    {
        juce::AudioBuffer<float> buffer(2, blockSize);
        const auto leftAmplitude = juce::Decibels::decibelsToGain(leftPeakDecibels);
        const auto rightAmplitude = juce::Decibels::decibelsToGain(rightPeakDecibels);
        const auto phaseIncrement = juce::MathConstants<double>::twoPi * 1000.0 / sampleRate;
        const auto totalSamples = juce::roundToInt(durationSeconds * sampleRate);

        for (auto processed = 0; processed < totalSamples; processed += blockSize)
        {
            buffer.clear();
            for (auto sample = 0; sample < blockSize; ++sample)
            {
                const auto sine = static_cast<float>(std::sin(phase));
                phase += phaseIncrement;
                if (phase >= juce::MathConstants<double>::twoPi)
                    phase -= juce::MathConstants<double>::twoPi;
                buffer.setSample(0, sample, leftAmplitude * sine);
                buffer.setSample(1, sample, rightPolarity * rightAmplitude * sine);
            }
            meter.processBlock(buffer, 300.0f, 1000.0f, false);
        }
    }
};

bool expectNear(const std::string_view test,
                const std::string_view measurement,
                const float actual,
                const float expected,
                const float tolerance)
{
    const auto passed = std::abs(actual - expected) <= tolerance;
    std::cout << (passed ? "PASS " : "FAIL ") << test << " / " << measurement
              << ": " << actual << " (expected " << expected << " +/- " << tolerance << ")\n";
    return passed;
}

bool expectTrue(const std::string_view test, const std::string_view measurement, const bool actual)
{
    std::cout << (actual ? "PASS " : "FAIL ") << test << " / " << measurement << '\n';
    return actual;
}

bool testProcessingOptions()
{
    auto passed = true;
    {
        TestSignal signal;
        signal.processSineAtFrequency(-23.0f, 1000.0, 5.0, 0.0, { false, true, false });
        const auto values = signal.meter.getValues();
        std::vector<float> history;
        signal.meter.copyHistory(ana::lvls::MeterProcessor::shortTermHistory, history);
        passed &= expectNear("loudness only", "RMS disabled", values.rmsDecibels[0], -120.0f, 0.01f);
        passed &= expectNear("loudness only", "integrated", values.integratedLufs, -23.0f, 0.1f);
        passed &= expectTrue("loudness only", "history disabled", history.empty());
    }
    {
        TestSignal signal;
        signal.processSineAtFrequency(-23.0f, 1000.0, 5.0, 0.0, { false, false, true });
        const auto values = signal.meter.getValues();
        std::vector<float> history;
        signal.meter.copyHistory(ana::lvls::MeterProcessor::shortTermHistory, history);
        passed &= expectNear("history only", "RMS disabled", values.rmsDecibels[0], -120.0f, 0.01f);
        passed &= expectNear("history only", "loudness readout disabled", values.shortTermLufs, -120.0f, 0.01f);
        passed &= expectNear("history only", "TP available", values.peakMaximumDecibels[0], -23.0f, 0.1f);
        passed &= expectTrue("history only", "history available", ! history.empty());
    }
    {
        TestSignal signal;
        signal.processSineAtFrequency(-23.0f, 1000.0, 5.0, 0.0, { true, false, false });
        const auto values = signal.meter.getValues();
        std::vector<float> history;
        signal.meter.copyHistory(ana::lvls::MeterProcessor::shortTermHistory, history);
        passed &= expectNear("peak RMS only", "RMS", values.rmsDecibels[0], -23.0f, 0.1f);
        passed &= expectNear("peak RMS only", "loudness disabled", values.integratedLufs, -120.0f, 0.01f);
        passed &= expectTrue("peak RMS only", "history disabled", history.empty());
    }
    return passed;
}

bool testSteadySine(const float level)
{
    TestSignal signal;
    signal.processSine(level, 20.0);
    const auto values = signal.meter.getValues();
    auto passed = true;
    passed &= expectNear("steady sine", "momentary", values.momentaryLufs, level, 0.1f);
    passed &= expectNear("steady sine", "short-term", values.shortTermLufs, level, 0.1f);
    passed &= expectNear("steady sine", "integrated", values.integratedLufs, level, 0.1f);
    passed &= expectNear("steady sine", "true peak L", values.peakMaximumDecibels[0], level, 0.1f);
    passed &= expectNear("steady sine", "true peak R", values.peakMaximumDecibels[1], level, 0.1f);
    return passed;
}

bool testRelativeGate()
{
    TestSignal signal;
    signal.processSine(-36.0f, 10.0);
    signal.processSine(-23.0f, 60.0);
    signal.processSine(-36.0f, 10.0);
    return expectNear("relative gate", "integrated",
                      signal.meter.getValues().integratedLufs, -23.0f, 0.1f);
}

bool testAbsoluteAndRelativeGate()
{
    TestSignal signal;
    signal.processSine(-72.0f, 10.0);
    signal.processSine(-36.0f, 10.0);
    signal.processSine(-23.0f, 60.0);
    signal.processSine(-36.0f, 10.0);
    signal.processSine(-72.0f, 10.0);
    return expectNear("absolute and relative gate", "integrated",
                      signal.meter.getValues().integratedLufs, -23.0f, 0.1f);
}

bool testRelativeGateBoundary()
{
    TestSignal signal;
    signal.processSine(-26.0f, 20.0);
    signal.processSine(-20.0f, 20.1);
    signal.processSine(-26.0f, 20.0);
    return expectNear("relative gate boundary", "integrated",
                      signal.meter.getValues().integratedLufs, -23.0f, 0.1f);
}

bool testTruePeakEstimate()
{
    auto passed = true;
    for (const auto frequency : std::array<double, 4> { 997.0, 6001.0, 12001.0, 18001.0 })
    {
        TestSignal signal;
        signal.processSineAtFrequency(-1.0f, frequency, 2.0, 0.1);
        const auto name = "true peak " + std::to_string(juce::roundToInt(frequency)) + " Hz";
        passed &= expectNear(name, "maximum", signal.meter.getValues().peakMaximumDecibels[0],
                             -1.0f, 0.3f);
    }
    return passed;
}

bool testRmsWindow()
{
    TestSignal signal;
    signal.processSineAtFrequency(-1.0f, 1000.0, 1.0);
    // AES-17 references RMS to a full-scale sine wave.
    const auto expectedRms = -1.0f;
    const auto values = signal.meter.getValues();
    auto passed = true;
    passed &= expectNear("steady sine", "RMS L", values.rmsDecibels[0], expectedRms, 0.05f);
    passed &= expectNear("steady sine", "RMS R", values.rmsDecibels[1], expectedRms, 0.05f);
    return passed;
}

bool testIndependentDynamicRmsWindows()
{
    TestSignal signal;
    signal.processStereoSine(-4.0f, -10.0f, 0.6);
    auto values = signal.meter.getValues();
    auto passed = true;
    passed &= expectNear("independent RMS", "L", values.rmsDecibels[0], -4.0f, 0.05f);
    passed &= expectNear("independent RMS", "R", values.rmsDecibels[1], -10.0f, 0.05f);

    signal.processStereoSine(-16.0f, -7.0f, 0.3);
    values = signal.meter.getValues();
    passed &= expectNear("updated RMS", "L", values.rmsDecibels[0], -16.0f, 0.05f);
    passed &= expectNear("updated RMS", "R", values.rmsDecibels[1], -7.0f, 0.05f);
    passed &= expectNear("RMS maximum", "L", values.rmsMaximumDecibels[0], -4.0f, 0.05f);
    passed &= expectNear("RMS maximum", "R", values.rmsMaximumDecibels[1], -7.0f, 0.05f);
    return passed;
}

bool testMidSideMeters()
{
    auto passed = true;
    {
        TestSignal signal;
        signal.processStereoSine(-6.0f, -120.0f, 1.0);
        const auto values = signal.meter.getValues();
        passed &= expectNear("mid/side compensation", "peak M", values.peakMaximumDecibels[2],
                             -12.0206f, 0.1f);
        passed &= expectNear("mid/side compensation", "peak S", values.peakMaximumDecibels[3],
                             -12.0206f, 0.1f);
    }
    {
        TestSignal signal;
        signal.processStereoSine(-6.0f, -6.0f, 1.0);
        const auto values = signal.meter.getValues();
        passed &= expectNear("mid/side in phase", "peak M", values.peakMaximumDecibels[2], -6.0f, 0.1f);
        passed &= expectNear("mid/side in phase", "RMS M", values.rmsDecibels[2], -6.0f, 0.05f);
        passed &= expectNear("mid/side in phase", "peak S", values.peakMaximumDecibels[3], -120.0f, 0.1f);
    }
    {
        TestSignal signal;
        signal.processStereoSine(-6.0f, -6.0f, 1.0, -1.0f);
        const auto values = signal.meter.getValues();
        passed &= expectNear("mid/side anti phase", "peak M", values.peakMaximumDecibels[2], -120.0f, 0.1f);
        passed &= expectNear("mid/side anti phase", "peak S", values.peakMaximumDecibels[3], -6.0f, 0.1f);
        passed &= expectNear("mid/side anti phase", "RMS S", values.rmsDecibels[3], -6.0f, 0.05f);
    }
    return passed;
}

bool testLoudnessRange()
{
    TestSignal steady;
    steady.processSine(-23.0f, 20.0);
    auto passed = expectNear("steady sine", "LRA", steady.meter.getValues().loudnessRange,
                             0.0f, 0.1f);

    TestSignal dynamic;
    dynamic.processSine(-30.0f, 20.0);
    dynamic.processSine(-20.0f, 20.0);
    passed &= expectNear("two-level programme", "LRA", dynamic.meter.getValues().loudnessRange,
                         10.0f, 0.3f);
    return passed;
}

bool testMomentaryMaximumBetweenStandardUpdates()
{
    TestSignal signal;
    signal.processSine(-60.0f, 0.05);
    signal.processSine(-23.0f, 0.4);
    signal.processSine(-60.0f, 0.05);
    return expectNear("momentary maximum between 100 ms updates", "maximum",
                      signal.meter.getValues().momentaryMaximumLufs, -23.0f, 0.1f);
}

bool testBandlimitedAraResampling()
{
    constexpr auto sourceRate = 44100.0;
    constexpr auto destinationRate = 48000.0;
    constexpr auto frequency = 18000.0;
    constexpr auto durationSeconds = 1.0;
    constexpr auto sourceSampleCount = static_cast<int>(sourceRate * durationSeconds)
        + ana::ara::BandlimitedResampler::kernelRadius * 2;
    constexpr auto destinationSampleCount = static_cast<int>(destinationRate * durationSeconds);
    constexpr auto sourceIncrement = sourceRate / destinationRate;

    std::vector<float> source(static_cast<size_t>(sourceSampleCount));
    for (auto sample = 0; sample < sourceSampleCount; ++sample)
        source[static_cast<size_t>(sample)] = static_cast<float>(std::sin(
            juce::MathConstants<double>::twoPi * frequency * static_cast<double>(sample) / sourceRate));

    auto inputPower = 0.0;
    for (auto sample = ana::ara::BandlimitedResampler::kernelRadius;
         sample < sourceSampleCount - ana::ara::BandlimitedResampler::kernelRadius;
         ++sample)
        inputPower += static_cast<double>(source[static_cast<size_t>(sample)])
            * static_cast<double>(source[static_cast<size_t>(sample)]);
    inputPower /= static_cast<double>(sourceSampleCount
        - ana::ara::BandlimitedResampler::kernelRadius * 2);

    auto outputPower = 0.0;
    for (auto sample = 0; sample < destinationSampleCount; ++sample)
    {
        const auto position = static_cast<double>(ana::ara::BandlimitedResampler::kernelRadius)
            + static_cast<double>(sample) * sourceIncrement;
        const auto value = ana::ara::BandlimitedResampler::interpolate(
            source.data(), sourceSampleCount, position, sourceIncrement);
        outputPower += static_cast<double>(value) * static_cast<double>(value);
    }
    outputPower /= static_cast<double>(destinationSampleCount);

    const auto levelChange = static_cast<float>(10.0 * std::log10(outputPower / inputPower));
    return expectNear("ARA 44.1 to 48 kHz resampling", "18 kHz level change",
                      levelChange, 0.0f, 0.1f);
}


int measureAudioFile(const juce::File& file, const double offsetSeconds, const double durationSeconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
    {
        std::cerr << "Cannot open " << file.getFullPathName() << '\n';
        return 2;
    }

    ana::lvls::MeterProcessor meter;
    meter.prepare(reader->sampleRate, blockSize);
    const auto firstSample = static_cast<juce::int64>(std::llround(offsetSeconds * reader->sampleRate));
    const auto requestedSamples = static_cast<juce::int64>(std::llround(durationSeconds * reader->sampleRate));
    const auto availableSamples = std::max<juce::int64>(0, reader->lengthInSamples - firstSample);
    const auto samplesToProcess = std::min(requestedSamples, availableSamples);

    for (juce::int64 processed = 0; processed < samplesToProcess; processed += blockSize)
    {
        const auto samples = static_cast<int>(std::min<juce::int64>(blockSize, samplesToProcess - processed));
        juce::AudioBuffer<float> buffer(2, samples);
        if (! reader->read(&buffer, 0, samples, firstSample + processed, true, true))
            return 3;
        meter.processBlock(buffer, 300.0f, 1000.0f, false);
    }

    const auto values = meter.getValues();
    std::cout << "FILE " << file.getFullPathName() << '\n'
              << "peak max L/R " << values.peakMaximumDecibels[0] << ' '
              << values.peakMaximumDecibels[1] << '\n'
              << "rms current L/R " << values.rmsDecibels[0] << ' '
              << values.rmsDecibels[1] << '\n'
              << "rms max L/R " << values.rmsMaximumDecibels[0] << ' '
              << values.rmsMaximumDecibels[1] << '\n'
              << "M current/max " << values.momentaryLufs << ' '
              << values.momentaryMaximumLufs << '\n'
              << "S current/max " << values.shortTermLufs << ' '
              << values.shortTermMaximumLufs << '\n'
              << "I current/max " << values.integratedLufs << ' '
              << values.integratedMaximumLufs << '\n';
    return 0;
}

int measureResampledAudioFile(const juce::File& file,
                              const double offsetSeconds,
                              const double durationSeconds,
                              const double destinationSampleRate,
                              const float gain)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || destinationSampleRate <= 0.0)
        return 2;

    const auto sourceIncrement = reader->sampleRate / destinationSampleRate;
    const auto destinationSamples = static_cast<juce::int64>(std::llround(
        durationSeconds * destinationSampleRate));
    const auto firstSourcePosition = offsetSeconds * reader->sampleRate;
    const auto lastSourcePosition = firstSourcePosition
        + static_cast<double>(std::max<juce::int64>(0, destinationSamples - 1)) * sourceIncrement;
    const auto firstRequiredSample = static_cast<juce::int64>(std::floor(firstSourcePosition))
        - ana::ara::BandlimitedResampler::kernelRadius + 1;
    const auto sourceReadEnd = static_cast<juce::int64>(std::floor(lastSourcePosition))
        + ana::ara::BandlimitedResampler::kernelRadius + 1;
    const auto sourceBufferSamples = static_cast<int>(sourceReadEnd - firstRequiredSample);
    juce::AudioBuffer<float> sourceBuffer(2, sourceBufferSamples);
    sourceBuffer.clear();

    const auto readableStart = std::max<juce::int64>(0, firstRequiredSample);
    const auto readableEnd = std::min<juce::int64>(reader->lengthInSamples, sourceReadEnd);
    const auto destinationOffset = static_cast<int>(readableStart - firstRequiredSample);
    const auto readableSamples = static_cast<int>(std::max<juce::int64>(0, readableEnd - readableStart));
    if (readableSamples > 0
        && ! reader->read(&sourceBuffer, destinationOffset, readableSamples,
                          readableStart, true, true))
        return 3;

    ana::lvls::MeterProcessor meter;
    meter.prepare(destinationSampleRate, blockSize);
    juce::AudioBuffer<float> output(2, blockSize);
    for (juce::int64 processed = 0; processed < destinationSamples; processed += blockSize)
    {
        const auto samples = static_cast<int>(std::min<juce::int64>(
            blockSize, destinationSamples - processed));
        output.clear();
        for (auto sample = 0; sample < samples; ++sample)
        {
            const auto sourcePosition = firstSourcePosition
                - static_cast<double>(firstRequiredSample)
                + static_cast<double>(processed + sample) * sourceIncrement;
            for (auto channel = 0; channel < 2; ++channel)
                output.setSample(channel, sample, gain * ana::ara::BandlimitedResampler::interpolate(
                    sourceBuffer.getReadPointer(channel), sourceBufferSamples,
                    sourcePosition, sourceIncrement));
        }
        meter.processBlock(output, 300.0f, 1000.0f, false);
    }

    const auto values = meter.getValues();
    std::cout << "RESAMPLED " << reader->sampleRate << " -> " << destinationSampleRate << '\n'
              << "peak max L/R " << values.peakMaximumDecibels[0] << ' '
              << values.peakMaximumDecibels[1] << '\n'
              << "rms max L/R " << values.rmsMaximumDecibels[0] << ' '
              << values.rmsMaximumDecibels[1] << '\n'
              << "M current/max " << values.momentaryLufs << ' '
              << values.momentaryMaximumLufs << '\n'
              << "S current/max " << values.shortTermLufs << ' '
              << values.shortTermMaximumLufs << '\n'
              << "I current/max " << values.integratedLufs << ' '
              << values.integratedMaximumLufs << '\n';
    return 0;
}
} // namespace

int main(const int argc, char** argv)
{
    if (argc == 4)
        return measureAudioFile(juce::File(argv[1]), std::stod(argv[2]), std::stod(argv[3]));
    if (argc == 6)
        return measureResampledAudioFile(juce::File(argv[1]), std::stod(argv[2]), std::stod(argv[3]),
                                         std::stod(argv[4]), std::stof(argv[5]));

    auto passed = true;
    passed &= testSteadySine(-23.0f);
    passed &= testSteadySine(-33.0f);
    passed &= testRelativeGate();
    passed &= testAbsoluteAndRelativeGate();
    passed &= testRelativeGateBoundary();
    passed &= testTruePeakEstimate();
    passed &= testRmsWindow();
    passed &= testIndependentDynamicRmsWindows();
    passed &= testMidSideMeters();
    passed &= testLoudnessRange();
    passed &= testMomentaryMaximumBetweenStandardUpdates();
    passed &= testProcessingOptions();
    passed &= testBandlimitedAraResampling();
    return passed ? 0 : 1;
}
