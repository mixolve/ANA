#pragma once

#include <JuceHeader.h>
#include <map>

class PluginProcessor;

struct OscSettings
{
    static constexpr int defaultInputPort = 9000;
    static constexpr int defaultOutputPort = 9001;
    bool enabled = false;
    int inputPort = defaultInputPort;
    juce::String outputHost { "127.0.0.1" };
    int outputPort = defaultOutputPort;
};

class OscController final : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>,
                            private juce::Timer, private juce::AsyncUpdater
{
public:
    explicit OscController(PluginProcessor&);
    ~OscController() override;

    bool applySettings(const OscSettings&);
    void applySettingsAsync(const OscSettings&);
    bool isInputPortBusy() const;

private:
    void oscMessageReceived(const juce::OSCMessage&) override;
    void oscBundleReceived(const juce::OSCBundle&) override;
    void handleBundle(const juce::OSCBundle&);
    void timerCallback() override;
    void handleAsyncUpdate() override;
    void sendCurrentState(bool force);
    static bool readNumber(const juce::OSCArgument&, float&);

    PluginProcessor& processor;
    juce::OSCReceiver receiver { "ANA REALTIME OSC receiver" };
    juce::OSCSender sender;
    OscSettings currentSettings;
    OscSettings pendingSettings;
    juce::CriticalSection pendingSettingsLock;
    std::map<juce::String, float> lastSentValues;
    bool inputConnected = false;
    bool outputConnected = false;
    mutable std::atomic<bool> portBusy { false };
    mutable std::atomic<uint32_t> lastPortProbeMs { 0 };
};
