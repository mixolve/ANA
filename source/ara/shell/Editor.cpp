#include "Editor.h"
#include "Processor.h"
#include "shared/shell/Theme.h"
#include "shared/shell/AuxiliaryWindowFocus.h"
#include "shared/shell/GraphColours.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <utility>

namespace
{
constexpr int minimumEditorHeight = 300;
constexpr int maximumEditorSize = 32768;
constexpr int defaultEditorHeight = minimumEditorHeight;
constexpr int editorResizeHandleThickness = ana::ui::gap.pixels();
constexpr int settingsWindowWidth = 300;
constexpr int defaultSettingsWindowHeight = 800;
constexpr int minimumSettingsWindowHeight = 300;
constexpr int maximumSettingsWindowHeight = 1200;
constexpr int snapshotsWindowWidth = 600;
constexpr int defaultSnapshotsWindowHeight = 360;
constexpr int minimumSnapshotsWindowHeight = 180;
constexpr int maximumSnapshotsWindowHeight = 1200;
constexpr int snapshotFileMagic = 0x414e4153; // "ANAS"
constexpr int snapshotFileVersion = 3;
constexpr int maximumSnapshotNameLength = 256;
constexpr const char* snapshotFileExtension = ".anasnapshot";
constexpr const char* snapshotFileWildcard = "*.anasnapshot";

int navigationWidth() noexcept
{
    return ana::ui::textControlWidth("SPEC") + ana::ui::textControlWidth("CORR")
        + ana::ui::textControlWidth("LVLS") + ana::ui::textControlWidth("SCOP")
        + 3 * ana::ui::gap.pixels();
}

int minimumEditorWidth() noexcept
{
    constexpr int regularRightControlCount = 7;
    const auto regularRightControlsWidth = 3 * ana::ui::iconControlSize
        + ana::ui::textControlWidth("I")
        + ana::ui::textControlWidth("TAKE")
        + ana::ui::textControlWidth("SOURCE")
        + ana::ui::textControlWidth(11)
        + regularRightControlCount * ana::ui::gap.pixels();
    const auto regularWidth = 2 * ana::ui::gap.pixels()
        + navigationWidth() + regularRightControlsWidth;
    return regularWidth + ana::ui::iconControlSize + ana::ui::gap.pixels();
}

const auto& snapshotColourOptions = ana::ui::graphColourOptions;

int snapshotGainFieldWidth() noexcept { return ana::ui::textControlWidth(6); }
const juce::Colour snapshotFieldBorderColour { 0xffbbbbbb };

juce::String formatSnapshotGain(const float gainDb)
{
    const auto clamped = juce::jlimit(SpecView::snapshotGainMinimumDb, SpecView::snapshotGainMaximumDb, gainDb);
    const auto absolute = juce::String(std::abs(clamped), 2).paddedLeft('0', 5);
    return juce::String(clamped >= 0.0f ? "+" : "-") + absolute;
}

class InvisibleResizableEdgeComponent final : public juce::ResizableEdgeComponent
{
public:
    using juce::ResizableEdgeComponent::ResizableEdgeComponent;
    void paint(juce::Graphics&) override {}
};

class SettingsWindowContent final : public juce::Component
{
public:
    explicit SettingsWindowContent(SettingsPanel& panelIn)
        : panel(panelIn)
    {
        addAndMakeVisible(panel);
    }

    void showChoicePrompt(ParameterControl& control)
    {
        const auto choices = control.getChoiceNames();
        if (choices.isEmpty())
            return;

        dismissChoicePrompt();
        dismissResetPrompt();
        const auto anchorBounds = getLocalArea(&control, control.getValueBounds());
        juce::Component::SafePointer<ParameterControl> safeControl(&control);
        choicePrompt = std::make_unique<ChoicePopup>(
            anchorBounds, choices, std::vector<bool> {}, control.getSelectedChoiceIndex(),
            [safeControl] (const int selectedIndex)
            {
                if (safeControl != nullptr)
                    safeControl->setSelectedChoiceIndex(selectedIndex);
            },
            [safeContent = juce::Component::SafePointer<SettingsWindowContent>(this)]
            {
                if (safeContent != nullptr)
                    safeContent->dismissChoicePrompt();
            });
        addAndMakeVisible(*choicePrompt);
        choicePrompt->setBounds(getLocalBounds());
        choicePrompt->toFront(true);
    }

    void dismissChoicePrompt()
    {
        choicePrompt.reset();
    }

    void showResetPrompt(ParameterControl& control)
    {
        dismissChoicePrompt();
        dismissResetPrompt();

        const auto anchorBounds = getLocalArea(&control, control.getTitleBounds());
        if (anchorBounds.isEmpty())
            return;

        juce::Component::SafePointer<ParameterControl> safeControl(&control);
        resetPrompt = std::make_unique<ChoicePopup>(
            anchorBounds, juce::StringArray { "R?" }, std::vector<bool> { true }, -1,
            [safeControl] (const int selectedIndex)
            {
                if (safeControl != nullptr && selectedIndex == 0)
                    safeControl->resetToDefault();
            },
            [safeContent = juce::Component::SafePointer<SettingsWindowContent>(this)]
            {
                if (safeContent != nullptr)
                    safeContent->dismissResetPrompt();
            });
        addAndMakeVisible(*resetPrompt);
        resetPrompt->setBounds(getLocalBounds());
        resetPrompt->toFront(true);
    }

    void dismissResetPrompt()
    {
        resetPrompt.reset();
    }

    void resized() override
    {
        panel.setBounds(getLocalBounds());
        if (choicePrompt != nullptr)
        {
            choicePrompt->setBounds(getLocalBounds());
            choicePrompt->toFront(true);
        }
        if (resetPrompt != nullptr)
        {
            resetPrompt->setBounds(getLocalBounds());
            resetPrompt->toFront(true);
        }
    }

    void paintOverChildren(juce::Graphics& graphics) override
    {
        graphics.setColour(ana::ui::white);
        graphics.drawRect(getLocalBounds(), 1);
    }

private:
    SettingsPanel& panel;
    std::unique_ptr<ChoicePopup> choicePrompt;
    std::unique_ptr<ChoicePopup> resetPrompt;
};

class SnapshotRow final : public juce::Component
{
public:
    SnapshotRow(SpecView& specViewIn, const size_t snapshotIndexIn,
                    const int initialColourIndex)
        : specView(specViewIn), snapshotIndex(snapshotIndexIn)
    {
        visibilityPress.onArmed = [this]
        {
            if (onClearPromptRequested)
                onClearPromptRequested(*this);

            visibilityPress.cancel();
        };

        gainPress.onArmed = [this]
        {
            if (onGainResetPromptRequested)
                onGainResetPromptRequested(*this);

            gainPress.cancel();
        };

        numberLabel.setText("SNAPSHOT " + juce::String(static_cast<int>(snapshotIndex + 1)),
                            juce::dontSendNotification);
        numberLabel.setFont(ana::ui::makeFont());
        numberLabel.setJustificationType(juce::Justification::centredLeft);
        numberLabel.setColour(juce::Label::textColourId, ana::ui::white);
        numberLabel.setColour(juce::Label::textWhenEditingColourId, ana::ui::white);
        numberLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        numberLabel.setColour(juce::Label::outlineColourId, snapshotFieldBorderColour);
        numberLabel.setColour(juce::TextEditor::highlightColourId, juce::Colour(0xff444444));
        numberLabel.setBorderSize(juce::BorderSize<int>(0, ana::ui::gap.pixels(), 0, ana::ui::gap.pixels()));
        numberLabel.setDrawBackground(false);
        numberLabel.setEditable(false, true, false);
        numberLabel.setTooltip("RENAME SNAPSHOT");
        addAndMakeVisible(numberLabel);

        gainLabel.setText(formatSnapshotGain(0.0f), juce::dontSendNotification);
        gainLabel.setFont(ana::ui::makeFont());
        gainLabel.setJustificationType(juce::Justification::centred);
        gainLabel.setColour(juce::Label::textColourId, ana::ui::white);
        gainLabel.setColour(juce::Label::textWhenEditingColourId, ana::ui::white);
        gainLabel.setColour(juce::Label::backgroundColourId, ana::ui::dark);
        gainLabel.setColour(juce::Label::outlineColourId, snapshotFieldBorderColour);
        gainLabel.setColour(juce::TextEditor::highlightColourId, juce::Colour(0xff444444));
        gainLabel.setBorderSize(juce::BorderSize<int>(0));
        gainLabel.setDrawBackground(true);
        gainLabel.setEditable(false, true, false);
        gainLabel.setTooltip("SNAPSHOT GAIN");
        gainLabel.onTextChange = [this]
        {
            if (updatingGainLabel)
                return;

            setGainDb(static_cast<float>(gainLabel.getText().trim().getDoubleValue()), true);
        };
        gainLabel.addMouseListener(this, false);
        addAndMakeVisible(gainLabel);

        const auto textEditingChanged = [this] (const bool isEditing)
        {
            if (onTextEditingChanged)
                onTextEditingChanged(isEditing);
        };
        numberLabel.onEditorVisibilityChanged = textEditingChanged;
        gainLabel.onEditorVisibilityChanged = textEditingChanged;

        cameraButton.setTooltip("CAPTURE SNAPSHOT");
        cameraButton.onClick = [this]
        {
            const auto captured = specView.captureSnapshot(snapshotIndex);
            cameraButton.setToggleState(false, juce::dontSendNotification);
            if (captured)
            {
                visibilityButton.setToggleState(false, juce::dontSendNotification);
                updateVisibilityTooltip();
            }
        };
        addAndMakeVisible(cameraButton);

        colourButton.setTooltip("SNAPSHOT COLOUR");
        colourButton.onClick = [this]
        {
            if (onColourRequested)
                onColourRequested(*this);
        };
        addAndMakeVisible(colourButton);

        transferButton.setTooltip("IMPORT / EXPORT SNAPSHOT");
        transferButton.onClick = [this]
        {
            if (onTransferRequested)
                onTransferRequested(*this);
        };
        addAndMakeVisible(transferButton);

        visibilityButton.setClickingTogglesState(false);
        // Keep row actions in the mouse listener so release cannot consume a long-press action.
        visibilityButton.onClick = [] {};
        visibilityButton.addMouseListener(this, false);
        updateVisibilityTooltip();
        addAndMakeVisible(visibilityButton);

        setColourIndex(initialColourIndex);
        syncGainFromSnapshot();
    }

    ~SnapshotRow() override
    {
        visibilityPress.cancel();
        gainPress.cancel();
        gainLabel.removeMouseListener(this);
        visibilityButton.removeMouseListener(this);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(1);
        gainLabel.setBounds(area.removeFromRight(snapshotGainFieldWidth()));
        ana::ui::gap.removeFromRight(area);
        visibilityButton.setBounds(area.removeFromRight(ana::ui::iconControlSize));
        ana::ui::gap.removeFromRight(area);
        transferButton.setBounds(area.removeFromRight(ana::ui::iconControlSize));
        ana::ui::gap.removeFromRight(area);
        colourButton.setBounds(area.removeFromRight(ana::ui::iconControlSize));
        ana::ui::gap.removeFromRight(area);
        cameraButton.setBounds(area.removeFromRight(ana::ui::iconControlSize));
        ana::ui::gap.removeFromRight(area);
        numberLabel.setBounds(area);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.originalComponent == &gainLabel)
        {
            if (! event.mods.isLeftButtonDown() || event.getNumberOfClicks() > 1)
                return;

            gainPress.begin(true);
            return;
        }

        if (event.originalComponent != &visibilityButton || ! event.mods.isLeftButtonDown())
            return;

        visibilityPress.begin(true);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (event.originalComponent == &gainLabel && gainPress.isActive()
            && event.mouseWasDraggedSinceMouseDown())
        {
            gainPress.markDragged();
            return;
        }

        if (event.originalComponent == &visibilityButton && visibilityPress.isActive()
            && event.mouseWasDraggedSinceMouseDown())
            visibilityPress.markDragged();
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (event.originalComponent == &gainLabel && gainPress.isActive())
        {
            const auto releaseResult = gainPress.release();
            if (releaseResult == LongPressGesture::ReleaseResult::shortPress
                && onGainFocusRequested)
                onGainFocusRequested(*this);
            return;
        }

        if (event.originalComponent != &visibilityButton || ! visibilityPress.isActive())
            return;

        const auto releaseResult = visibilityPress.release();
        if (releaseResult != LongPressGesture::ReleaseResult::shortPress)
            return;

        const auto shouldHide = ! visibilityButton.getToggleState();
        visibilityButton.setToggleState(shouldHide, juce::dontSendNotification);
        specView.setSnapshotVisible(snapshotIndex, ! shouldHide);
        updateVisibilityTooltip();
    }

    void mouseExit(const juce::MouseEvent& event) override
    {
        if (event.originalComponent == &gainLabel && gainPress.isActive())
        {
            gainPress.cancelArming();
            return;
        }

        if (event.originalComponent == &visibilityButton && visibilityPress.isActive())
            visibilityPress.cancelArming();
    }

    size_t getSnapshotIndex() const noexcept { return snapshotIndex; }
    void setSnapshotIndex(const size_t newSnapshotIndex) noexcept { snapshotIndex = newSnapshotIndex; }
    int getColourIndex() const noexcept { return colourIndex; }
    float getGainDb() const noexcept { return specView.getSnapshotGain(snapshotIndex); }
    juce::String getSnapshotName() const { return numberLabel.getText().trim().substring(0, maximumSnapshotNameLength); }
    juce::Rectangle<int> getColourButtonBounds() const noexcept { return colourButton.getBounds(); }
    juce::Rectangle<int> getTransferButtonBounds() const noexcept { return transferButton.getBounds(); }
    juce::Rectangle<int> getVisibilityButtonBounds() const noexcept { return visibilityButton.getBounds(); }
    juce::Rectangle<int> getGainLabelBounds() const noexcept { return gainLabel.getBounds(); }

    void setSnapshotName(juce::String name)
    {
        name = name.trim().substring(0, maximumSnapshotNameLength);
        if (name.isEmpty())
            name = "SNAPSHOT " + juce::String(static_cast<int>(snapshotIndex + 1));
        numberLabel.setText(std::move(name), juce::dontSendNotification);
    }

    void setColourIndex(const int newIndex)
    {
        colourIndex = juce::jlimit(0, static_cast<int>(snapshotColourOptions.size()) - 1, newIndex);
        specView.setSnapshotColour(snapshotIndex, ana::ui::graphColour(colourIndex));
    }

    void setGainDb(const float gainDb, const bool notifyFocusOwner = false)
    {
        const auto clamped = juce::jlimit(SpecView::snapshotGainMinimumDb, SpecView::snapshotGainMaximumDb, gainDb);
        const auto quantised = std::round(clamped * 100.0f) * 0.01f;
        specView.setSnapshotGain(snapshotIndex, quantised);
        updateGainLabel(quantised);

        if (notifyFocusOwner && onGainChanged)
            onGainChanged(*this);
    }

    void setGainSelected(const bool shouldBeSelected)
    {
        if (gainSelected == shouldBeSelected)
            return;

        gainSelected = shouldBeSelected;
        gainLabel.setColour(juce::Label::outlineColourId,
                            gainSelected ? juce::Colours::transparentBlack
                                         : snapshotFieldBorderColour);
        repaint(gainLabel.getBounds().expanded(2));
    }

    void paintOverChildren(juce::Graphics& graphics) override
    {
        if (! gainSelected)
            return;

        graphics.setColour(ana::ui::white);
        graphics.drawRect(gainLabel.getBounds(), 2);
    }

    void syncFromSnapshot()
    {
        cameraButton.setToggleState(false, juce::dontSendNotification);
        visibilityButton.setToggleState(! specView.isSnapshotVisible(snapshotIndex),
                                        juce::dontSendNotification);
        updateVisibilityTooltip();
        syncGainFromSnapshot();

        const auto snapshotColour = specView.getSnapshotColour(snapshotIndex);
        for (size_t index = 0; index < snapshotColourOptions.size(); ++index)
        {
            if (ana::ui::graphColour(static_cast<int>(index)) == snapshotColour)
            {
                colourIndex = static_cast<int>(index);
                break;
            }
        }
    }

    std::function<void(SnapshotRow&)> onColourRequested;
    std::function<void(SnapshotRow&)> onTransferRequested;
    std::function<void(SnapshotRow&)> onClearPromptRequested;
    std::function<void(SnapshotRow&)> onGainResetPromptRequested;
    std::function<void(SnapshotRow&)> onGainFocusRequested;
    std::function<void(SnapshotRow&)> onGainChanged;
    std::function<void(bool)> onTextEditingChanged;

private:
    void updateVisibilityTooltip()
    {
        visibilityButton.setTooltip(visibilityButton.getToggleState() ? "SHOW" : "HIDE");
    }

    void syncGainFromSnapshot()
    {
        updateGainLabel(specView.getSnapshotGain(snapshotIndex));
    }

    void updateGainLabel(const float gainDb)
    {
        const juce::ScopedValueSetter<bool> guard(updatingGainLabel, true);
        gainLabel.setText(formatSnapshotGain(gainDb), juce::dontSendNotification);
    }

    SpecView& specView;
    size_t snapshotIndex = 0;
    int colourIndex = 0;
    bool updatingGainLabel = false;
    bool gainSelected = false;
    LongPressGesture visibilityPress;
    LongPressGesture gainPress;
    EllipsisLabel numberLabel;
    EllipsisLabel gainLabel;
    ControlButton cameraButton { "camera" };
    ControlButton colourButton { "palette" };
    ControlButton transferButton { "arrows-up-down" };
    ControlButton visibilityButton { "eye-off" };
};
class SnapshotsWindowContent final : public juce::Component
{
public:
    explicit SnapshotsWindowContent(SpecView& specViewIn)
        : specView(specViewIn)
    {
        setOpaque(true);

        addButton.setTooltip("ADD SNAPSHOT");
        addButton.onClick = [this] { addSnapshotRow(); };
        addAndMakeVisible(addButton);

        bulkImportButton.setTooltip("IMPORT SNAPSHOTS");
        bulkImportButton.onClick = [this] { beginBulkSnapshotImport(); };
        addAndMakeVisible(bulkImportButton);

        closeButton.setTooltip("CLOSE");
        closeButton.onClick = [this]
        {
            if (onCloseRequested)
                onCloseRequested();
        };
        addAndMakeVisible(closeButton);

        gainPotentiometer.onValueChange = [this]
        {
            if (updatingGainPotentiometer || focusedGainRow == nullptr)
                return;

            const auto normalised = static_cast<float>(gainPotentiometer.getValue());
            const auto gainDb = SpecView::snapshotGainMinimumDb
                + normalised * (SpecView::snapshotGainMaximumDb - SpecView::snapshotGainMinimumDb);
            focusedGainRow->setGainDb(gainDb);
        };
        addAndMakeVisible(gainPotentiometer);

        rowsViewport.setViewedComponent(&rowsContent, false);
        rowsViewport.setScrollBarsShown(false, false, true, false);
        rowsViewport.setWantsKeyboardFocus(false);
        rowsViewport.setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(rowsViewport);

        rowsContent.addMouseListener(this, false);
    }

    ~SnapshotsWindowContent() override
    {
        rowsContent.removeMouseListener(this);
        rowsViewport.setViewedComponent(nullptr, false);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(ana::ui::black);
    }

    void paintOverChildren(juce::Graphics& graphics) override
    {
        graphics.setColour(ana::ui::white);
        graphics.drawRect(getLocalBounds(), 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(ana::ui::gap.pixels());

        auto footer = area.removeFromBottom(ana::ui::controlHeight);
        addButton.setBounds(footer.removeFromLeft(ana::ui::iconControlSize));
        ana::ui::gap.removeFromLeft(footer);
        bulkImportButton.setBounds(footer.removeFromLeft(ana::ui::iconControlSize));
        ana::ui::gap.removeFromLeft(footer);
        closeButton.setBounds(footer.removeFromRight(ana::ui::iconControlSize));
        ana::ui::gap.removeFromRight(footer);
        gainPotentiometer.setBounds(footer);
        ana::ui::gap.removeFromBottom(area);
        rowsViewport.setBounds(area);

        const auto rowStride = ana::ui::controlHeight + ana::ui::gap.pixels();
        const auto rowsHeight = snapshotRows.empty()
            ? 0
            : static_cast<int>(snapshotRows.size()) * rowStride - ana::ui::gap.pixels();
        rowsContent.setSize(std::max(1, area.getWidth()), std::max(area.getHeight(), rowsHeight));

        auto rowArea = rowsContent.getLocalBounds();
        for (auto& row : snapshotRows)
        {
            row->setBounds(rowArea.removeFromTop(ana::ui::controlHeight));
            ana::ui::gap.removeFromTop(rowArea);
        }

        if (colourPrompt != nullptr)
        {
            colourPrompt->setBounds(getLocalBounds());
            colourPrompt->toFront(true);
        }
        if (transferPrompt != nullptr)
        {
            transferPrompt->setBounds(getLocalBounds());
            transferPrompt->toFront(true);
        }
        if (clearPrompt != nullptr)
        {
            clearPrompt->setBounds(getLocalBounds());
            clearPrompt->toFront(true);
        }
        if (gainResetPrompt != nullptr)
        {
            gainResetPrompt->setBounds(getLocalBounds());
            gainResetPrompt->toFront(true);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.originalComponent != &rowsContent && event.originalComponent != this)
            return;

        clearInteractionSelection();

        draggedWindow = getTopLevelComponent();
        if (draggedWindow != nullptr && draggedWindow != this)
        {
            draggingWindow = true;
            windowDragger.startDraggingComponent(draggedWindow, event);
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (! draggingWindow || draggedWindow == nullptr)
            return;

        windowDragger.dragComponent(draggedWindow, event, nullptr);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        draggingWindow = false;
        draggedWindow = nullptr;
    }

    void dismissColourPrompt()
    {
        colourPrompt.reset();
    }

    void dismissTransferPrompt()
    {
        transferPrompt.reset();
    }

    void dismissClearPrompt()
    {
        clearPrompt.reset();
    }

    void dismissGainResetPrompt()
    {
        gainResetPrompt.reset();
    }

    std::function<void()> onCloseRequested;
    std::function<void(bool)> onTextEditingChanged;

private:
    SnapshotRow* addSnapshotRow()
    {
        const auto snapshotIndex = specView.addSnapshotSlot();
        const auto initialColourIndex = ana::ui::defaultGraphColourIndex;
        auto row = std::make_unique<SnapshotRow>(specView, snapshotIndex, initialColourIndex);
        row->onColourRequested = [this] (SnapshotRow& requestedRow)
        {
            showColourPrompt(requestedRow);
        };
        row->onTransferRequested = [this] (SnapshotRow& requestedRow)
        {
            showTransferPrompt(requestedRow);
        };
        row->onClearPromptRequested = [this] (SnapshotRow& requestedRow)
        {
            showClearPrompt(requestedRow);
        };
        row->onGainResetPromptRequested = [this] (SnapshotRow& requestedRow)
        {
            showGainResetPrompt(requestedRow);
        };
        row->onGainFocusRequested = [this] (SnapshotRow& requestedRow)
        {
            focusGainRow(requestedRow);
        };
        row->onGainChanged = [this] (SnapshotRow& changedRow)
        {
            if (focusedGainRow == &changedRow)
                syncGainPotentiometer();
        };
        row->onTextEditingChanged = [this] (const bool isEditing)
        {
            if (onTextEditingChanged)
                onTextEditingChanged(isEditing);
        };
        auto* rowPtr = row.get();
        rowsContent.addAndMakeVisible(*row);
        snapshotRows.push_back(std::move(row));
        resized();
        return rowPtr;
    }

    void showColourPrompt(SnapshotRow& row)
    {
        dismissColourPrompt();
        dismissTransferPrompt();
        dismissClearPrompt();
        dismissGainResetPrompt();

        juce::StringArray names;
        for (const auto& option : snapshotColourOptions)
            names.add(option.name);

        const auto anchorBounds = getLocalArea(&row, row.getColourButtonBounds());
        juce::Component::SafePointer<SnapshotRow> safeRow(&row);
        colourPrompt = std::make_unique<ChoicePopup>(
            anchorBounds, std::move(names), std::vector<bool> {}, row.getColourIndex(),
            [safeRow] (const int selectedIndex)
            {
                if (safeRow != nullptr)
                    safeRow->setColourIndex(selectedIndex);
            },
            [safeContent = juce::Component::SafePointer<SnapshotsWindowContent>(this)]
            {
                if (safeContent != nullptr)
                    safeContent->dismissColourPrompt();
            });
        addAndMakeVisible(*colourPrompt);
        colourPrompt->setBounds(getLocalBounds());
        colourPrompt->toFront(true);
    }

    void showTransferPrompt(SnapshotRow& row)
    {
        dismissColourPrompt();
        dismissTransferPrompt();
        dismissClearPrompt();
        dismissGainResetPrompt();

        const auto anchorBounds = getLocalArea(&row, row.getTransferButtonBounds());
        juce::Component::SafePointer<SnapshotRow> safeRow(&row);
        transferPrompt = std::make_unique<ChoicePopup>(
            anchorBounds, juce::StringArray { "IMPORT", "EXPORT" },
            std::vector<bool> { true, specView.hasSnapshotData(row.getSnapshotIndex()) }, -1,
            [safeContent = juce::Component::SafePointer<SnapshotsWindowContent>(this), safeRow]
            (const int selectedIndex)
            {
                if (safeContent == nullptr || safeRow == nullptr)
                    return;

                if (selectedIndex == 0)
                    safeContent->beginSnapshotImport(*safeRow);
                else if (selectedIndex == 1)
                    safeContent->beginSnapshotExport(*safeRow);
            },
            [safeContent = juce::Component::SafePointer<SnapshotsWindowContent>(this)]
            {
                if (safeContent != nullptr)
                    safeContent->dismissTransferPrompt();
            });
        addAndMakeVisible(*transferPrompt);
        transferPrompt->setBounds(getLocalBounds());
        transferPrompt->toFront(true);
    }

    void showClearPrompt(SnapshotRow& row)
    {
        dismissColourPrompt();
        dismissTransferPrompt();
        dismissClearPrompt();
        dismissGainResetPrompt();

        const auto anchorBounds = getLocalArea(&row, row.getVisibilityButtonBounds());
        juce::Component::SafePointer<SnapshotRow> safeRow(&row);
        clearPrompt = std::make_unique<ChoicePopup>(
            anchorBounds, juce::StringArray { "D" }, std::vector<bool> { true }, -1,
            [safeContent = juce::Component::SafePointer<SnapshotsWindowContent>(this), safeRow]
            (const int selectedIndex)
            {
                if (safeContent != nullptr && safeRow != nullptr && selectedIndex == 0)
                    safeContent->deleteSnapshotRow(*safeRow);
            },
            [safeContent = juce::Component::SafePointer<SnapshotsWindowContent>(this)]
            {
                if (safeContent != nullptr)
                    safeContent->dismissClearPrompt();
            });
        addAndMakeVisible(*clearPrompt);
        clearPrompt->setBounds(getLocalBounds());
        clearPrompt->toFront(true);
    }

    void showGainResetPrompt(SnapshotRow& row)
    {
        dismissColourPrompt();
        dismissTransferPrompt();
        dismissClearPrompt();
        dismissGainResetPrompt();

        const auto anchorBounds = getLocalArea(&row, row.getGainLabelBounds());
        juce::Component::SafePointer<SnapshotRow> safeRow(&row);
        gainResetPrompt = std::make_unique<ChoicePopup>(
            anchorBounds, juce::StringArray { "R?" }, std::vector<bool> { true }, -1,
            [safeRow] (const int selectedIndex)
            {
                if (safeRow != nullptr && selectedIndex == 0)
                    safeRow->setGainDb(0.0f, true);
            },
            [safeContent = juce::Component::SafePointer<SnapshotsWindowContent>(this)]
            {
                if (safeContent != nullptr)
                    safeContent->dismissGainResetPrompt();
            });
        addAndMakeVisible(*gainResetPrompt);
        gainResetPrompt->setBounds(getLocalBounds());
        gainResetPrompt->toFront(true);
    }

    void deleteSnapshotRow(SnapshotRow& row)
    {
        const auto it = std::find_if(snapshotRows.begin(), snapshotRows.end(),
                                     [&row] (const auto& candidate) { return candidate.get() == &row; });
        if (it == snapshotRows.end())
            return;

        const auto snapshotIndex = row.getSnapshotIndex();
        if (! specView.removeSnapshotSlot(snapshotIndex))
            return;

        if (focusedGainRow == &row)
        {
            focusedGainRow = nullptr;
            gainPotentiometer.setEnabled(false);
        }

        const auto erasedPosition = static_cast<size_t>(std::distance(snapshotRows.begin(), it));
        snapshotRows.erase(it);
        for (size_t index = erasedPosition; index < snapshotRows.size(); ++index)
            snapshotRows[index]->setSnapshotIndex(index);

        resized();
    }

    void clearInteractionSelection()
    {
        dismissColourPrompt();
        dismissTransferPrompt();
        dismissClearPrompt();
        dismissGainResetPrompt();

        if (focusedGainRow != nullptr)
            focusedGainRow->setGainSelected(false);

        focusedGainRow = nullptr;
        gainPotentiometer.setEnabled(false);
        juce::Component::unfocusAllComponents();
    }

    void focusGainRow(SnapshotRow& row)
    {
        if (focusedGainRow != nullptr && focusedGainRow != &row)
            focusedGainRow->setGainSelected(false);

        focusedGainRow = &row;
        focusedGainRow->setGainSelected(true);
        syncGainPotentiometer();
        gainPotentiometer.setEnabled(true);
    }

    void syncGainPotentiometer()
    {
        if (focusedGainRow == nullptr)
        {
            gainPotentiometer.setEnabled(false);
            return;
        }

        const auto gainDb = focusedGainRow->getGainDb();
        const auto normalised = juce::jlimit(0.0f, 1.0f,
            (gainDb - SpecView::snapshotGainMinimumDb) / (SpecView::snapshotGainMaximumDb - SpecView::snapshotGainMinimumDb));
        const juce::ScopedValueSetter<bool> guard(updatingGainPotentiometer, true);
        gainPotentiometer.setValue(normalised, juce::dontSendNotification);
    }

    void beginBulkSnapshotImport()
    {
        const auto startDirectory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        snapshotFileChooser = std::make_unique<juce::FileChooser>(
            "IMPORT SNAPSHOTS", startDirectory, snapshotFileWildcard);

        juce::Component::SafePointer<SnapshotsWindowContent> safeContent(this);
        snapshotFileChooser->launchAsync(
            juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::canSelectMultipleItems,
            [safeContent] (const juce::FileChooser& chooser)
            {
                if (safeContent == nullptr)
                    return;

                const auto files = chooser.getResults();
                for (const auto& file : files)
                {
                    if (file.getFullPathName().isEmpty())
                        continue;

                    auto* row = safeContent->addSnapshotRow();
                    if (row != nullptr && ! safeContent->importSnapshotFromFile(*row, file))
                        safeContent->deleteSnapshotRow(*row);
                }
            });
    }

    void beginSnapshotExport(SnapshotRow& row)
    {
        if (! specView.hasSnapshotData(row.getSnapshotIndex()))
            return;

        auto stem = juce::File::createLegalFileName(row.getSnapshotName().trim());
        if (stem.isEmpty())
            stem = "snapshot";

        const auto defaultFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile(stem + snapshotFileExtension);
        snapshotFileChooser = std::make_unique<juce::FileChooser>(
            "EXPORT SNAPSHOT", defaultFile, snapshotFileWildcard);

        juce::Component::SafePointer<SnapshotsWindowContent> safeContent(this);
        juce::Component::SafePointer<SnapshotRow> safeRow(&row);
        snapshotFileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [safeContent, safeRow] (const juce::FileChooser& chooser)
            {
                if (safeContent == nullptr || safeRow == nullptr)
                    return;

                const auto file = chooser.getResult();
                if (file.getFullPathName().isNotEmpty())
                    safeContent->exportSnapshotToFile(*safeRow, file);
            });
    }

    void beginSnapshotImport(SnapshotRow& row)
    {
        const auto startDirectory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        snapshotFileChooser = std::make_unique<juce::FileChooser>(
            "IMPORT SNAPSHOT", startDirectory, snapshotFileWildcard);

        juce::Component::SafePointer<SnapshotsWindowContent> safeContent(this);
        juce::Component::SafePointer<SnapshotRow> safeRow(&row);
        snapshotFileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeContent, safeRow] (const juce::FileChooser& chooser)
            {
                if (safeContent == nullptr || safeRow == nullptr)
                    return;

                const auto file = chooser.getResult();
                if (file.getFullPathName().isNotEmpty())
                    safeContent->importSnapshotFromFile(*safeRow, file);
            });
    }

    bool exportSnapshotToFile(SnapshotRow& row, const juce::File& requestedFile)
    {
        if (! specView.hasSnapshotData(row.getSnapshotIndex()))
            return false;

        const auto file = requestedFile.hasFileExtension("anasnapshot")
            ? requestedFile
            : requestedFile.withFileExtension(snapshotFileExtension);

        juce::MemoryOutputStream output;
        if (! output.writeInt(snapshotFileMagic)
            || ! output.writeInt(snapshotFileVersion)
            || ! output.writeString(row.getSnapshotName())
            || ! specView.writeSnapshot(row.getSnapshotIndex(), output))
            return false;

        return file.replaceWithData(output.getData(), output.getDataSize());
    }

    bool importSnapshotFromFile(SnapshotRow& row, const juce::File& file)
    {
        juce::FileInputStream input(file);
        if (input.failedToOpen())
            return false;

        if (input.readInt() != snapshotFileMagic)
            return false;

        const auto fileVersion = input.readInt();
        if (fileVersion != snapshotFileVersion)
            return false;

        auto importedName = input.readString().trim();
        if (importedName.length() > maximumSnapshotNameLength)
            return false;

        if (! specView.readSnapshot(row.getSnapshotIndex(), input))
            return false;

        row.setSnapshotName(std::move(importedName));
        row.syncFromSnapshot();
        if (focusedGainRow == &row)
            syncGainPotentiometer();
        return true;
    }

    SpecView& specView;
    ControlButton addButton { "plus" };
    ControlButton bulkImportButton { "arrows-down" };
    ControlButton closeButton { "x" };
    FocusedPotentiometer gainPotentiometer;
    juce::Component rowsContent;
    juce::Viewport rowsViewport;
    std::vector<std::unique_ptr<SnapshotRow>> snapshotRows;
    std::unique_ptr<ChoicePopup> colourPrompt;
    std::unique_ptr<ChoicePopup> transferPrompt;
    std::unique_ptr<ChoicePopup> clearPrompt;
    std::unique_ptr<ChoicePopup> gainResetPrompt;
    std::unique_ptr<juce::FileChooser> snapshotFileChooser;
    SnapshotRow* focusedGainRow = nullptr;
    bool updatingGainPotentiometer = false;
    juce::Component* draggedWindow = nullptr;
    juce::ComponentDragger windowDragger;
    bool draggingWindow = false;
};
}

class HostShortcutDocumentWindow : public juce::DocumentWindow
{
public:
    enum class Side
    {
        left,
        right
    };

    HostShortcutDocumentWindow(const juce::Colour backgroundColour, const Side sideIn)
        : juce::DocumentWindow(juce::String(), backgroundColour, 0, false),
          side(sideIn)
    {
    }

    ~HostShortcutDocumentWindow() override
    {
       #if JUCE_MAC
        if (previousKeyWindow != nullptr)
            setAuxiliaryTextInputActive(*this, false, previousKeyWindow);
       #endif
    }

    juce::BorderSize<int> getBorderThickness() const override
    {
        return {};
    }

protected:
    void initialiseDesktopPeer()
    {
        addToDesktop();
    }

    void showBeside(juce::Component& owner)
    {
        if (! hasBeenShown)
        {
            positionBeside(owner);
            hasBeenShown = true;
        }

        setAlwaysOnTop(true);
        setVisible(true);
        toFront(false);
    }

    void setTextInputActive(const bool isActive)
    {
        if (isActive)
            ++activeTextInputs;
        else if (activeTextInputs > 0)
            --activeTextInputs;

        const auto shouldPassShortcuts = activeTextInputs == 0;
        if (passShortcutsToHost == shouldPassShortcuts)
            return;

        passShortcutsToHost = shouldPassShortcuts;
       #if JUCE_MAC
        setAuxiliaryTextInputActive(*this, ! shouldPassShortcuts, previousKeyWindow);
       #else
        if (isOnDesktop())
            recreateDesktopWindow();
       #endif
    }

    juce::BorderSize<int> getContentComponentBorder() const override
    {
        return {};
    }

    int getDesktopWindowStyleFlags() const override
    {
        auto flags = juce::DocumentWindow::getDesktopWindowStyleFlags();
       #if JUCE_MAC
        flags |= juce::ComponentPeer::windowIgnoresKeyPresses;
       #else
        if (passShortcutsToHost)
            flags |= juce::ComponentPeer::windowIgnoresKeyPresses;
       #endif
        return flags;
    }

private:
    void positionBeside(juce::Component& owner)
    {
        const auto ownerBounds = owner.getScreenBounds();
        auto targetBounds = getBounds();
        const auto x = side == Side::right
            ? ownerBounds.getRight() + ana::ui::gap.pixels()
            : ownerBounds.getX() - targetBounds.getWidth() - ana::ui::gap.pixels();
        targetBounds.setPosition(x, ownerBounds.getY());

        if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(ownerBounds))
            targetBounds = targetBounds.constrainedWithin(display->userArea);

        setBounds(targetBounds);
    }

    Side side;
    int activeTextInputs = 0;
    bool passShortcutsToHost = true;
   #if JUCE_MAC
    void* previousKeyWindow = nullptr;
   #endif
    bool hasBeenShown = false;
};

class SettingsWindow final : public HostShortcutDocumentWindow
{
public:
    SettingsWindow(SettingsPanel& panel, std::function<void()> closeCallbackIn)
        : HostShortcutDocumentWindow(ana::ui::black, Side::right),
          panelRef(panel),
          content(panel),
          closeCallback(std::move(closeCallbackIn))
    {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        setDropShadowEnabled(false);
        setAlwaysOnTop(true);
        setResizable(true, false);
        setResizeLimits(settingsWindowWidth, minimumSettingsWindowHeight,
                        settingsWindowWidth, maximumSettingsWindowHeight);

        content.setSize(settingsWindowWidth, defaultSettingsWindowHeight);
        setContentNonOwned(&content, true);
        setSize(settingsWindowWidth, defaultSettingsWindowHeight);
        panelRef.onTextEditingChanged = [this] (const bool isEditing)
        {
            setTextInputActive(isEditing);
        };
        initialiseDesktopPeer();
    }

    ~SettingsWindow() override
    {
        panelRef.onTextEditingChanged = nullptr;
        clearContentComponent();
    }

    void showFor(juce::Component& owner)
    {
        showBeside(owner);
    }

    void showChoicePrompt(ParameterControl& control)
    {
        content.showChoicePrompt(control);
    }

    void showResetPrompt(ParameterControl& control)
    {
        content.showResetPrompt(control);
    }

    void hideWindow()
    {
        content.dismissChoicePrompt();
        content.dismissResetPrompt();
        setVisible(false);
    }

    void closeButtonPressed() override
    {
        if (closeCallback)
            closeCallback();
        else
            hideWindow();
    }

private:
    SettingsPanel& panelRef;
    SettingsWindowContent content;
    std::function<void()> closeCallback;
};

class SnapshotsWindow final : public HostShortcutDocumentWindow
{
public:
    SnapshotsWindow(SpecView& specView, std::function<void()> closeCallbackIn)
        : HostShortcutDocumentWindow(ana::ui::black, Side::left),
          content(specView),
          closeCallback(std::move(closeCallbackIn))
    {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        setDropShadowEnabled(false);
        setAlwaysOnTop(true);
        setResizable(true, false);
        setResizeLimits(snapshotsWindowWidth, minimumSnapshotsWindowHeight,
                        snapshotsWindowWidth, maximumSnapshotsWindowHeight);

        content.onCloseRequested = [this]
        {
            if (closeCallback)
                closeCallback();
            else
                hideWindow();
        };
        content.setSize(snapshotsWindowWidth, defaultSnapshotsWindowHeight);
        setContentNonOwned(&content, true);
        setSize(snapshotsWindowWidth, defaultSnapshotsWindowHeight);
        content.onTextEditingChanged = [this] (const bool isEditing)
        {
            setTextInputActive(isEditing);
        };
        initialiseDesktopPeer();
    }

    ~SnapshotsWindow() override
    {
        content.onCloseRequested = nullptr;
        content.onTextEditingChanged = nullptr;
        clearContentComponent();
    }

    void showFor(juce::Component& owner)
    {
        showBeside(owner);
    }

    void hideWindow()
    {
        content.dismissColourPrompt();
        content.dismissTransferPrompt();
        content.dismissClearPrompt();
        content.dismissGainResetPrompt();
        setVisible(false);
    }

    void closeButtonPressed() override
    {
        if (closeCallback)
            closeCallback();
        else
            hideWindow();
    }

private:
    SnapshotsWindowContent content;
    std::function<void()> closeCallback;
};

PluginEditor::PluginEditor(PluginProcessor& processorRef)
    : AudioProcessorEditor(&processorRef)
    , AudioProcessorEditorARAExtension(&processorRef)
    , audioProcessor(processorRef),
      scopDisplay(processorRef),
      settingsComponent(processorRef),
      specDisplay(processorRef),
      corrDisplay(processorRef),
      lvlsDisplay(processorRef)
{
    for (auto* component : std::array<juce::Component*, 14> {
             &scopDisplay, &specDisplay, &corrDisplay, &lvlsDisplay,
             &specPageButton, &corrPageButton, &lvlsPageButton, &scopPageButton,
             &snapshotsWindowButton, &settingsButton, &fullSourceButton,
             &clearButton, &freezeButton, &aboutButton })
        addAndMakeVisible(*component);
    for (auto* component : std::array<juce::Component*, 4> {
             &araUpdateLabel, &sourceButton, &takeButton, &refreshButton })
        addAndMakeVisible(*component);

    specPageButton.onClick = [this] { showAnalyzerPage(ana::AnalyzerPage::spec, showingAnalyzerSettings); };
    corrPageButton.onClick = [this] { showAnalyzerPage(ana::AnalyzerPage::corr, showingAnalyzerSettings); };
    lvlsPageButton.onClick = [this] { showAnalyzerPage(ana::AnalyzerPage::lvls, showingAnalyzerSettings); };
    scopPageButton.onClick = [this] { showAnalyzerPage(ana::AnalyzerPage::scop, showingAnalyzerSettings); };
    snapshotsWindowButton.onClick = [this] { showSnapshotsWindow(! showingSnapshotsWindow); };
    settingsButton.onClick = [this] { showAnalyzerSettings(! showingAnalyzerSettings); };
    aboutButton.setTooltip("ABOUT");
    snapshotsWindowButton.setTooltip("SNAPSHOTS");
    settingsButton.setTooltip("SETTINGS");
    freezeButton.setTooltip("FREEZE");
    refreshButton.setTooltip("REFRESH");
    fullSourceButton.setTooltip("FULL");
    clearButton.setTooltip("CLEAR");
    aboutButton.onClick = [this] { showAboutPopup(); };

    araUpdateLabel.setFont(ana::ui::makeFont());
    araUpdateLabel.setJustificationType(juce::Justification::centred);
    araUpdateLabel.setMinimumHorizontalScale(1.0f);
    araUpdateLabel.setColour(juce::Label::textColourId, ana::ui::white);
    araUpdateLabel.setColour(juce::Label::backgroundColourId, ana::ui::dark);
    araUpdateLabel.setColour(juce::Label::outlineColourId, ana::ui::light);
    araUpdateLabel.setBorderSize(
        juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    araUpdateLabel.setInterceptsMouseClicks(false, false);
    araUpdateLabel.setVisible(true);
    scopDisplay.onAraUpdateStatus = [this] (const juce::String& status)
    {
        araUpdateLabel.setText(status, juce::dontSendNotification);
    };
    specDisplay.onAraUpdateStatus = [this] (const juce::String& status)
    {
        araUpdateLabel.setText(status, juce::dontSendNotification);
    };
    corrDisplay.onAraUpdateStatus = [this] (const juce::String& status)
    {
        araUpdateLabel.setText(status, juce::dontSendNotification);
    };
    settingsComponent.onDisplaySettingsChanged = [this]
    {
        scopDisplay.refreshDisplaySettings();
        specDisplay.resized();
        specDisplay.repaint();
        corrDisplay.resized();
        corrDisplay.repaint();
        lvlsDisplay.resized();
        scopDisplay.refreshWaveform();
    };
    settingsComponent.onEqualBandHeights = [this] { scopDisplay.equalizeBandHeights(); };
    settingsComponent.onCenterLvlsSections = [this] { lvlsDisplay.centerParts(); };
    settingsComponent.onCloseRequested = [this] { showAnalyzerSettings(false); };
    settingsComponent.onChoiceRequested = [this] (ParameterControl& control)
    {
        if (settingsWindow != nullptr)
            settingsWindow->showChoicePrompt(control);
    };
    settingsComponent.onResetRequested = [this] (ParameterControl& control)
    {
        if (settingsWindow != nullptr)
            settingsWindow->showResetPrompt(control);
    };

    freezeButton.setClickingTogglesState(true);
    refreshButton.onClick = [this] { scopDisplay.refreshWaveform(); };
    fullSourceButton.onClick = [this]
    {
        const auto enabled = ! audioProcessor.isScopFullSourceView();
        audioProcessor.setScopFullSourceView(enabled);
        scopDisplay.setFullSourceView(enabled);
    };
    clearButton.onClick = [this]
    {
        if (activePage != ana::AnalyzerPage::scop)
            return;
        return;
    };
    sourceButton.onClick = [this] { showAraSourcePrompt(); };
    takeButton.onClick = [this] { showAraTakePrompt(); };

    showAnalyzerPage(audioProcessor.getAnalyzerPageState(),
                     false);

    // Seed recreated REAPER peers from the processor catalogue to avoid a false source-change refresh.
    araSourceChoices = audioProcessor.getAraSourceChoices();
    if (! araSourceChoices.empty() && audioProcessor.getAraAnalysisProgress() >= 100)
        araUpdateLabel.setText("UPDATED", juce::dontSendNotification);

    timerCallback();
    startTimerHz(15);
    setResizable(true, false);
    leftEdgeResizer = std::make_unique<InvisibleResizableEdgeComponent>(
        this, getConstrainer(), juce::ResizableEdgeComponent::leftEdge);
    rightEdgeResizer = std::make_unique<InvisibleResizableEdgeComponent>(
        this, getConstrainer(), juce::ResizableEdgeComponent::rightEdge);
    topEdgeResizer = std::make_unique<InvisibleResizableEdgeComponent>(
        this, getConstrainer(), juce::ResizableEdgeComponent::topEdge);
    bottomEdgeResizer = std::make_unique<InvisibleResizableEdgeComponent>(
        this, getConstrainer(), juce::ResizableEdgeComponent::bottomEdge);
    leftEdgeResizer->setAlwaysOnTop(true);
    rightEdgeResizer->setAlwaysOnTop(true);
    topEdgeResizer->setAlwaysOnTop(true);
    bottomEdgeResizer->setAlwaysOnTop(true);
    addAndMakeVisible(*leftEdgeResizer);
    addAndMakeVisible(*rightEdgeResizer);
    addAndMakeVisible(*topEdgeResizer);
    addAndMakeVisible(*bottomEdgeResizer);
    const auto minimumWidth = minimumEditorWidth();
    setResizeLimits(minimumWidth, minimumEditorHeight, maximumEditorSize, maximumEditorSize);
    const auto savedSize = audioProcessor.getLastEditorSize();
    setSize(juce::jlimit(minimumWidth, maximumEditorSize,
                         savedSize.x > 0 ? savedSize.x : minimumWidth),
            juce::jlimit(minimumEditorHeight, maximumEditorSize,
                         savedSize.y > 0 ? savedSize.y : defaultEditorHeight));
}

PluginEditor::~PluginEditor()
{
    if (getWidth() > 0 && getHeight() > 0)
        audioProcessor.setLastEditorSize(getWidth(), getHeight());
}

void PluginEditor::showAnalyzerSettings(const bool shouldShowSettings)
{
    if (shouldShowSettings && settingsWindow == nullptr)
    {
        settingsWindow = std::make_unique<SettingsWindow>(
            settingsComponent, [this] { showAnalyzerSettings(false); });
    }

    showAnalyzerPage(activePage, shouldShowSettings);
}

void PluginEditor::showSnapshotsWindow(const bool shouldShowWindow)
{
    if (shouldShowWindow && snapshotsWindow == nullptr)
    {
        snapshotsWindow = std::make_unique<SnapshotsWindow>(
            specDisplay, [this] { showSnapshotsWindow(false); });
    }

    showingSnapshotsWindow = shouldShowWindow;
    snapshotsWindowButton.setToggleState(showingSnapshotsWindow, juce::dontSendNotification);
    if (snapshotsWindow != nullptr)
    {
        if (showingSnapshotsWindow)
            snapshotsWindow->showFor(*this);
        else
            snapshotsWindow->hideWindow();
    }
}

void PluginEditor::showAnalyzerPage(const ana::AnalyzerPage page,
                                               const bool shouldShowSettings)
{
    if (! shouldShowSettings)
        dismissChoicePrompt();

    activePage = page;
    audioProcessor.setAnalyzerPageState(page);
    specPageButton.setToggleState(page == ana::AnalyzerPage::spec, juce::dontSendNotification);
    corrPageButton.setToggleState(page == ana::AnalyzerPage::corr, juce::dontSendNotification);
    lvlsPageButton.setToggleState(page == ana::AnalyzerPage::lvls, juce::dontSendNotification);
    scopPageButton.setToggleState(page == ana::AnalyzerPage::scop, juce::dontSendNotification);
    showingAnalyzerSettings = shouldShowSettings;
    settingsButton.setEnabled(true);
    settingsButton.setToggleState(showingAnalyzerSettings, juce::dontSendNotification);
    snapshotsWindowButton.setEnabled(true);
    snapshotsWindowButton.setToggleState(showingSnapshotsWindow, juce::dontSendNotification);
    scopDisplay.setVisible(page == ana::AnalyzerPage::scop);
    specDisplay.setVisible(page == ana::AnalyzerPage::spec);
    corrDisplay.setVisible(page == ana::AnalyzerPage::corr);
    lvlsDisplay.setVisible(page == ana::AnalyzerPage::lvls);
    fullSourceButton.setEnabled(page == ana::AnalyzerPage::scop);
    freezeButton.setToggleState(page == ana::AnalyzerPage::spec
                                    ? audioProcessor.getSpecProcessor().isFrozen()
                                    : page == ana::AnalyzerPage::corr
                                        ? audioProcessor.getCorrProcessor().isFrozen()
                                    : page == ana::AnalyzerPage::lvls
                                        ? audioProcessor.getLvlsProcessor().isFrozen()
                                    : scopFrozen,
                                juce::dontSendNotification);
    settingsComponent.setAnalyzerContext(page, getActiveSettingsViewMode());
    if (settingsWindow != nullptr)
    {
        if (showingAnalyzerSettings)
            settingsWindow->showFor(*this);
        else
            settingsWindow->hideWindow();
    }

    resized();
    repaint();
}

juce::String PluginEditor::getActiveSettingsViewMode() const
{
    if (activePage == ana::AnalyzerPage::spec)
        return specDisplay.getSettingsViewModeName();
    if (activePage == ana::AnalyzerPage::corr)
        return corrDisplay.getSettingsViewModeName();
    return {};
}

void PluginEditor::showChoicePrompt(
    const juce::Rectangle<int> anchorBounds,
    juce::StringArray choices,
    const int selectedIndex,
    std::function<void(int)> onSelect,
    std::vector<bool> enabledChoices)
{
    if (choices.isEmpty())
        return;

    dismissChoicePrompt();
    choicePrompt = std::make_unique<ChoicePopup>(
        anchorBounds,
        std::move(choices),
        std::move(enabledChoices),
        selectedIndex,
        std::move(onSelect),
        [this] { dismissChoicePrompt(); });
    addAndMakeVisible(*choicePrompt);
    choicePrompt->setBounds(getLocalBounds());
    choicePrompt->toFront(true);
}


void PluginEditor::showAraSourcePrompt()
{
    refreshAraSelectionButtons();
    juce::StringArray names { "ALL" };
    std::vector<juce::String> choiceIds { juce::String() };
    const auto selectedId = audioProcessor.getSelectedAraSourceId();
    auto selectedIndex = 0;

    for (const auto& choice : araSourceChoices)
    {
        if (std::find(choiceIds.begin(), choiceIds.end(), choice.sourceId) != choiceIds.end())
            continue;

        choiceIds.push_back(choice.sourceId);
        names.add(choice.sourceName);

        if (choice.sourceId == selectedId)
            selectedIndex = names.size() - 1;
    }

    showChoicePrompt(getLocalArea(&sourceButton, sourceButton.getLocalBounds()),
                     std::move(names), selectedIndex,
                     [this, sourceIds = std::move(choiceIds)] (const int index)
                     {
                         if (! juce::isPositiveAndBelow(index, static_cast<int>(sourceIds.size())))
                             return;

                         audioProcessor.setSelectedAraSourceId(
                             sourceIds[static_cast<size_t>(index)]);
                         refreshAraSelectionButtons();
                         scopDisplay.refreshWaveform();
                     });
}

void PluginEditor::showAraTakePrompt()
{
    refreshAraSelectionButtons();
    juce::StringArray names;
    std::vector<int> choiceNumbers;
    const auto selectedSourceId = audioProcessor.getSelectedAraSourceId();
    const auto selectedTakeNumber = audioProcessor.getSelectedAraTakeNumber();
    auto selectedIndex = -1;

    for (const auto& choice : araSourceChoices)
    {
        if (selectedSourceId.isNotEmpty() && choice.sourceId != selectedSourceId)
            continue;
        if (std::find(choiceNumbers.begin(), choiceNumbers.end(), choice.takeNumber) != choiceNumbers.end())
            continue;

        choiceNumbers.push_back(choice.takeNumber);
        names.add(juce::String(choice.takeNumber));

        if (choice.takeNumber == selectedTakeNumber)
            selectedIndex = names.size() - 1;
    }

    showChoicePrompt(getLocalArea(&takeButton, takeButton.getLocalBounds()),
                     std::move(names), selectedIndex,
                     [this, takeNumbers = std::move(choiceNumbers)] (const int index)
                     {
                         if (! juce::isPositiveAndBelow(index, static_cast<int>(takeNumbers.size())))
                             return;

                         audioProcessor.setSelectedAraTakeNumber(
                             takeNumbers[static_cast<size_t>(index)]);
                         refreshAraSelectionButtons();
                         scopDisplay.refreshWaveform();
                     });
}

void PluginEditor::refreshAraSelectionButtons()
{
    auto latestChoices = audioProcessor.getAraSourceChoices();
    const auto choicesChanged = latestChoices != araSourceChoices;
    araSourceChoices = std::move(latestChoices);
    if (choicesChanged && ! araSourceChoices.empty())
    {
        araUpdateStatusMinimumEndMilliseconds =
            juce::Time::getMillisecondCounterHiRes() + 250.0;
        araUpdateLabel.setText("UPDATING 00", juce::dontSendNotification);
    }
    auto sourceId = audioProcessor.getSelectedAraSourceId();
    auto selectedTakeNumber = audioProcessor.getSelectedAraTakeNumber();
    std::vector<int> availableTakeNumbers;
    if (! araSourceChoices.empty())
    {
        const auto sourceIsValid = sourceId.isEmpty()
            || std::any_of(araSourceChoices.begin(), araSourceChoices.end(),
                           [&sourceId] (const auto& choice)
                           {
                               return choice.sourceId == sourceId;
                           });

        if (! sourceIsValid)
        {
            audioProcessor.setSelectedAraSourceId({});
            sourceId.clear();
        }

        for (const auto& choice : araSourceChoices)
        {
            if (sourceId.isNotEmpty() && choice.sourceId != sourceId)
                continue;

            if (std::find(availableTakeNumbers.begin(), availableTakeNumbers.end(),
                          choice.takeNumber) == availableTakeNumbers.end())
                availableTakeNumbers.push_back(choice.takeNumber);
        }

        std::sort(availableTakeNumbers.begin(), availableTakeNumbers.end());
        const auto takeIsValid = std::find(availableTakeNumbers.begin(),
                                           availableTakeNumbers.end(),
                                           selectedTakeNumber) != availableTakeNumbers.end();
        if (! takeIsValid && ! availableTakeNumbers.empty())
        {
            selectedTakeNumber = availableTakeNumbers.front();
            audioProcessor.setSelectedAraTakeNumber(selectedTakeNumber);
        }
    }

    const auto araAvailable = audioProcessor.isARAAvailable();
    sourceButton.setEnabled(araAvailable && ! araSourceChoices.empty());
    takeButton.setEnabled(araAvailable && ! availableTakeNumbers.empty());
    sourceButton.setToggleState(sourceId.isNotEmpty(), juce::dontSendNotification);
    takeButton.setToggleState(false, juce::dontSendNotification);
    sourceButton.setTooltip(sourceId.isEmpty() ? "SOURCE: ALL" : "SOURCE: SELECTED");
    takeButton.setTooltip(selectedTakeNumber > 0
        ? "TAKE: " + juce::String(selectedTakeNumber)
        : "TAKE: NONE");
}


void PluginEditor::dismissChoicePrompt()
{
    choicePrompt.reset();
}

void PluginEditor::showAboutPopup()
{
    dismissChoicePrompt();
    dismissAboutPopup();
    aboutPopup = std::make_unique<AboutPopup>(
        [safeEditor = juce::Component::SafePointer<PluginEditor>(this)]
        {
            if (safeEditor != nullptr)
                safeEditor->dismissAboutPopup();
        });
    addAndMakeVisible(*aboutPopup);
    aboutPopup->setBounds(getLocalBounds());
    aboutPopup->toFront(true);
    aboutPopup->grabKeyboardFocus();
}

void PluginEditor::dismissAboutPopup()
{
    aboutPopup.reset();
    repaint();
}

void PluginEditor::timerCallback()
{
    refreshAraSelectionButtons();
    if (araSourceChoices.empty())
        araUpdateLabel.setText("NO FILES", juce::dontSendNotification);
    else if (araUpdateLabel.getText() == "NO FILES")
        araUpdateLabel.setText("UPDATING 00", juce::dontSendNotification);
    if (! araSourceChoices.empty())
    {
        const auto progress = juce::jlimit(0, 100, audioProcessor.getAraAnalysisProgress());
        const auto minimumStatusTimeActive = juce::Time::getMillisecondCounterHiRes()
            < araUpdateStatusMinimumEndMilliseconds;
        if (progress < 100 || minimumStatusTimeActive)
        {
            const auto displayedProgress = progress >= 100 ? 99 : progress;
            araUpdateLabel.setText(
                "UPDATING " + juce::String(displayedProgress).paddedLeft('0', 2),
                juce::dontSendNotification);
        }
        else if (araUpdateLabel.getText().startsWith("UPDATING"))
        {
            araUpdateLabel.setText("UPDATED", juce::dontSendNotification);
        }
    }
    refreshButton.setEnabled(true);
    fullSourceButton.setToggleState(audioProcessor.isScopFullSourceView(),
                                    juce::dontSendNotification);
    clearButton.setEnabled(false);
    freezeButton.setEnabled(false);
    fullSourceButton.setEnabled(activePage == ana::AnalyzerPage::scop);
    settingsButton.setEnabled(true);
    snapshotsWindowButton.setEnabled(true);
    settingsComponent.setAnalyzerContext(activePage, getActiveSettingsViewMode());
    freezeButton.setToggleState(false, juce::dontSendNotification);

    if (editorSizeSavePending
        && juce::Time::getMillisecondCounterHiRes() >= editorSizeSaveDeadlineMilliseconds)
    {
        audioProcessor.setLastEditorSize(pendingEditorSize.x, pendingEditorSize.y);
        editorSizeSavePending = false;
    }
}

void PluginEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);
}

void PluginEditor::resized()
{
    if (getWidth() > 0 && getHeight() > 0)
    {
        pendingEditorSize = { getWidth(), getHeight() };
        editorSizeSaveDeadlineMilliseconds = juce::Time::getMillisecondCounterHiRes() + 250.0;
        editorSizeSavePending = true;
    }

    auto area = getLocalBounds().reduced(ana::ui::gap.pixels());
    auto controlsRow = area.removeFromTop(ana::ui::controlHeight);
    const auto placeRight = [&] (juce::Component& component, const int width)
    {
        const auto fits = controlsRow.getWidth() >= width;
        component.setVisible(fits);
        component.setBounds(fits ? controlsRow.removeFromRight(width) : juce::Rectangle<int>());
        if (fits)
            ana::ui::gap.removeFromRight(controlsRow);
    };

    for (auto* component : std::array<juce::Component*, 7> {
             &clearButton, &freezeButton, &fullSourceButton,
             &sourceButton, &takeButton, &refreshButton, &araUpdateLabel })
    {
        component->setVisible(false);
        component->setBounds({});
    }

    placeRight(aboutButton, aboutButton.getPreferredWidth());
    placeRight(settingsButton, settingsButton.getPreferredWidth());
    placeRight(snapshotsWindowButton, snapshotsWindowButton.getPreferredWidth());
    placeRight(araUpdateLabel, ana::ui::textControlWidth(11));
    placeRight(refreshButton, refreshButton.getPreferredWidth());
    placeRight(takeButton, takeButton.getPreferredWidth());
    placeRight(sourceButton, sourceButton.getPreferredWidth());
    if (activePage == ana::AnalyzerPage::scop)
        placeRight(fullSourceButton, fullSourceButton.getPreferredWidth());

    ana::ui::FixedGapRow navigationRow(controlsRow);
    for (auto* button : std::array<ControlButton*, 4> {
             &specPageButton, &corrPageButton, &lvlsPageButton, &scopPageButton })
    {
        button->setBounds(navigationRow.takeLeft(button->getPreferredWidth()));
        button->setVisible(! button->getBounds().isEmpty());
    }

    ana::ui::gap.removeFromTop(area);
    scopDisplay.setBounds(area);
    specDisplay.setBounds(area);
    corrDisplay.setBounds(area);
    lvlsDisplay.setBounds(area);

    if (leftEdgeResizer != nullptr)
    {
        leftEdgeResizer->setBounds(0, 0, editorResizeHandleThickness, getHeight());
        leftEdgeResizer->toFront(false);
    }
    if (rightEdgeResizer != nullptr)
    {
        rightEdgeResizer->setBounds(getWidth() - editorResizeHandleThickness, 0,
                                    editorResizeHandleThickness, getHeight());
        rightEdgeResizer->toFront(false);
    }
    if (topEdgeResizer != nullptr)
    {
        topEdgeResizer->setBounds(0, 0, getWidth(), editorResizeHandleThickness);
        topEdgeResizer->toFront(false);
    }
    if (bottomEdgeResizer != nullptr)
    {
        bottomEdgeResizer->setBounds(0, getHeight() - editorResizeHandleThickness,
                                     getWidth(), editorResizeHandleThickness);
        bottomEdgeResizer->toFront(false);
    }

    if (choicePrompt != nullptr)
    {
        choicePrompt->setBounds(getLocalBounds());
        choicePrompt->toFront(true);
    }
    if (aboutPopup != nullptr)
    {
        aboutPopup->setBounds(getLocalBounds());
        aboutPopup->toFront(true);
    }
}
