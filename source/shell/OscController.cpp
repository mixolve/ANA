#include "OscController.h"
#include "Processor.h"
#include "shared/spec/FrequencyScale.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr auto addressPrefix = "/ana/";
constexpr float valueTolerance = 1.0e-6f;

bool portAvailable(const int port)
{
    juce::DatagramSocket probe;
    return probe.bindToPort(port);
}
}

OscController::OscController(PluginProcessor& owner) : processor(owner)
{
    receiver.addListener(this);
}

OscController::~OscController()
{
    cancelPendingUpdate();
    stopTimer();
    receiver.removeListener(this);
    receiver.disconnect();
    sender.disconnect();
}

bool OscController::applySettings(const OscSettings& settings)
{
    auto desired = settings;
    desired.outputHost = desired.outputHost.trim();
    if (desired.inputPort < 1 || desired.inputPort > 65535
        || desired.outputPort < 1 || desired.outputPort > 65535
        || desired.outputHost.isEmpty() || desired.outputHost.length() > 253)
        return false;

    const auto previous = currentSettings;
    const auto hadInput = inputConnected;
    const auto hadOutput = outputConnected;
    if (inputConnected)
        receiver.disconnect();
    if (outputConnected)
        sender.disconnect();

    inputConnected = desired.enabled && receiver.connect(desired.inputPort);
    outputConnected = desired.enabled && sender.connect(desired.outputHost, desired.outputPort);
    if (desired.enabled && (! inputConnected || ! outputConnected))
    {
        if (inputConnected)
            receiver.disconnect();
        if (outputConnected)
            sender.disconnect();
        inputConnected = hadInput && receiver.connect(previous.inputPort);
        outputConnected = hadOutput && sender.connect(previous.outputHost, previous.outputPort);
        portBusy.store(! inputConnected, std::memory_order_relaxed);
        return false;
    }

    currentSettings = desired;
    portBusy.store(desired.enabled ? ! inputConnected : ! portAvailable(desired.inputPort),
                   std::memory_order_relaxed);
    lastSentValues.clear();
    if (outputConnected)
    {
        startTimerHz(30);
        sendCurrentState(true);
    }
    else
        stopTimer();
    return true;
}

void OscController::applySettingsAsync(const OscSettings& settings)
{
    {
        const juce::ScopedLock lock(pendingSettingsLock);
        pendingSettings = settings;
    }
    triggerAsyncUpdate();
}

void OscController::handleAsyncUpdate()
{
    OscSettings settings;
    {
        const juce::ScopedLock lock(pendingSettingsLock);
        settings = pendingSettings;
    }
    applySettings(settings);
}

bool OscController::isInputPortBusy() const
{
    if (currentSettings.enabled && inputConnected)
        return false;
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastPortProbeMs.load(std::memory_order_relaxed) >= 1000)
    {
        lastPortProbeMs.store(now, std::memory_order_relaxed);
        portBusy.store(! portAvailable(currentSettings.inputPort), std::memory_order_relaxed);
    }
    return portBusy.load(std::memory_order_relaxed);
}

bool OscController::readNumber(const juce::OSCArgument& argument, float& value)
{
    if (argument.isFloat32())
        value = argument.getFloat32();
    else if (argument.isInt32())
        value = static_cast<float>(argument.getInt32());
    else
        return false;
    return std::isfinite(value);
}

void OscController::oscMessageReceived(const juce::OSCMessage& message)
{
    const auto address = message.getAddressPattern().toString();
    if (! address.startsWith(addressPrefix))
        return;

    const auto parameterId = address.substring(juce::String(addressPrefix).length());
    if (message.size() < 1)
        return;
    auto* parameter = processor.getParameters().getParameter(parameterId);
    if (parameter == nullptr)
        return;

    float incoming = 0.0f;
    if (! readNumber(message[0], incoming))
        return;

    const auto isChoice = dynamic_cast<juce::AudioParameterChoice*>(parameter) != nullptr;
    const auto plainValue = incoming - (isChoice ? 1.0f : 0.0f);
    const auto& range = parameter->getNormalisableRange();
    const auto tolerance = std::max(valueTolerance, std::abs(range.end - range.start) * valueTolerance);
    if (plainValue < range.start - tolerance || plainValue > range.end + tolerance)
        return;
    const auto snapped = range.snapToLegalValue(plainValue);
    const auto discrete = isChoice || dynamic_cast<juce::AudioParameterInt*>(parameter) != nullptr
        || dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr;
    if (discrete && std::abs(snapped - plainValue) > tolerance)
        return;

    const auto leaf = parameterId.fromLastOccurrenceOf("/", false, false);
    if ((parameterId.startsWith("spec/") || parameterId.startsWith("corr/"))
        && (leaf == "low" || leaf == "high"))
    {
        const auto prefix = parameterId.upToLastOccurrenceOf("/", true, false);
        const auto lowId = prefix + "low";
        const auto highId = prefix + "high";
        const auto* low = processor.getParameters().getRawParameterValue(lowId);
        const auto* high = processor.getParameters().getRawParameterValue(highId);
        if (low == nullptr || high == nullptr)
            return;
        if (parameterId == lowId
            ? snapped > high->load(std::memory_order_relaxed) - ana::frequency_scale::minimumSpanHz
            : snapped < low->load(std::memory_order_relaxed) + ana::frequency_scale::minimumSpanHz)
            return;
    }

    for (size_t index = 0; index < PluginProcessor::crossoverParameterIds.size(); ++index)
    {
        if (parameterId != PluginProcessor::crossoverParameterIds[index])
            continue;
        const auto frequencies = processor.getCrossoverFrequencies();
        if ((index > 0 && snapped <= frequencies[index - 1] + 1.0f)
            || (index + 1 < frequencies.size() && snapped >= frequencies[index + 1] - 1.0f))
            return;
        break;
    }

    if (parameterId == PluginProcessor::moduleParameterId)
    {
        processor.setAnalyzerPageState(ana::analyzerPageFromIndex(juce::roundToInt(snapped)));
        return;
    }
    if (parameterId == PluginProcessor::analysisModeParameterId)
    {
        processor.setOfflineMode(snapped >= 0.5f);
        return;
    }
    if (parameterId == PluginProcessor::specViewParameterId)
    {
        processor.setSpecMapView(snapped >= 0.5f);
        return;
    }
    if (parameterId == PluginProcessor::corrModeParameterId)
    {
        processor.setCorrMode(juce::roundToInt(snapped));
        return;
    }

    const auto normalized = juce::jlimit(0.0f, 1.0f, parameter->convertTo0to1(snapped));
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(normalized);
    parameter->endChangeGesture();
}

void OscController::oscBundleReceived(const juce::OSCBundle& bundle)
{
    handleBundle(bundle);
}

void OscController::handleBundle(const juce::OSCBundle& bundle)
{
    for (const auto& element : bundle)
    {
        if (element.isMessage())
            oscMessageReceived(element.getMessage());
        else if (element.isBundle())
            handleBundle(element.getBundle());
    }
}

void OscController::timerCallback()
{
    sendCurrentState(false);
}

void OscController::sendCurrentState(const bool force)
{
    if (! outputConnected)
        return;

    auto& state = processor.getParameters();
    for (const auto& child : state.state)
    {
        const auto id = child.getProperty("id").toString();
        auto* parameter = state.getParameter(id);
        if (id.isEmpty() || parameter == nullptr)
            continue;

        const auto normalized = parameter->getValue();
        if (! force)
        {
            const auto previous = lastSentValues.find(id);
            if (previous != lastSentValues.end()
                && std::abs(previous->second - normalized) <= valueTolerance)
                continue;
        }

        auto plain = parameter->convertFrom0to1(normalized);
        if (dynamic_cast<juce::AudioParameterChoice*>(parameter) != nullptr)
            plain += 1.0f;
        if (sender.send(juce::OSCMessage(
                juce::String(addressPrefix) + id, plain)))
            lastSentValues[id] = normalized;
    }
}
