#include <JuceHeader.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

class PluginHostComponent final : public juce::Component
{
public:
    PluginHostComponent()
        : processor(createPluginFilter())
    {
        jassert(processor != nullptr);
        player.setProcessor(processor.get());

        if (processor != nullptr)
        {
            editor.reset(processor->createEditorIfNeeded());
            if (editor != nullptr)
                addAndMakeVisible(*editor);
        }

        setSize(editor != nullptr ? editor->getWidth() : 1024,
                editor != nullptr ? editor->getHeight() : 720);

        const juce::Component::SafePointer<PluginHostComponent> safeThis(this);
        const auto initialiseAudio = [safeThis] (const bool microphoneAllowed)
        {
            if (safeThis == nullptr)
                return;

            const auto inputChannels = microphoneAllowed ? 2 : 0;
            auto error = safeThis->deviceManager.initialise(inputChannels, 2, nullptr, true);
            if (error.isNotEmpty() && inputChannels > 0)
                safeThis->deviceManager.initialise(0, 2, nullptr, true);
            safeThis->deviceManager.addAudioCallback(&safeThis->player);
        };

        if (juce::RuntimePermissions::isRequired(juce::RuntimePermissions::recordAudio)
            && ! juce::RuntimePermissions::isGranted(juce::RuntimePermissions::recordAudio))
            juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, initialiseAudio);
        else
            initialiseAudio(true);
    }

    ~PluginHostComponent() override
    {
        deviceManager.removeAudioCallback(&player);
        player.setProcessor(nullptr);
        editor.reset();
        processor.reset();
        deviceManager.closeAudioDevice();
    }

    void resized() override
    {
        if (editor != nullptr)
            editor->setBounds(getLocalBounds());
    }

private:
    std::unique_ptr<juce::AudioProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::AudioProcessorPlayer player;
    juce::AudioDeviceManager deviceManager;
};

class IosHostApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "ANA"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override { mainWindow.reset(); }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(std::move(name), juce::Colours::black,
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new PluginHostComponent(), true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(IosHostApplication)
