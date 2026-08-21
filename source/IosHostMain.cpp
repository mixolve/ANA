#include <JuceHeader.h>

class AnaHostComponent final : public juce::Component
{
public:
    AnaHostComponent()
    {
        setSize(420, 260);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(juce::Colour(0xff121417));
        graphics.setColour(juce::Colour(0xffdce8ee));
        graphics.setFont(juce::FontOptions(34.0f, juce::Font::bold));
        graphics.drawFittedText("ANA", getLocalBounds(), juce::Justification::centred, 1);
    }
};

class AnaIosHostApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "ana"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override {}

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(std::move(name),
                             juce::Colour(0xff121417),
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new AnaHostComponent(), true);
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

START_JUCE_APPLICATION(AnaIosHostApplication)
