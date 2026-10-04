#include "Editor.h"
#include "Processor.h"
#include "OscParameterSections.h"
#include "shared/shell/Theme.h"
#include "shared/shell/EditorLayout.h"
#include "shared/shell/AuxiliaryWindowFocus.h"
#include "shared/shell/GraphColours.h"
#include "shared/shell/EdgeResizeContent.h"
#include "shared/scop/Layout.h"
#include "OscController.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <utility>

namespace
{
constexpr int minimumEditorHeight = ana::scop::minimumEditorHeightForBands(
    ana::scop::minimumRealtimeBandHeight);
constexpr int maximumEditorSize = 32768;
constexpr int defaultEditorHeight = minimumEditorHeight;
constexpr int editorResizeHandleThickness = ana::ui::gap.pixels();
constexpr int settingsWindowWidth = 300;
constexpr int maximumSettingsWindowHeight = 1200;
constexpr int minimumSnapshotsWindowWidth = 300;
constexpr int oscWindowWidth = settingsWindowWidth;
constexpr int maximumSnapshotsWindowHeight = 1200;
constexpr int snapshotFileMagic = 0x414e4153; // "ANAS"
constexpr int snapshotFileVersion = 3;
constexpr int maximumSnapshotNameLength = 256;
constexpr const char* snapshotFileExtension = ".anasnapshot";
constexpr const char* snapshotFileWildcard = "*.anasnapshot";

int minimumEditorWidth() noexcept
{
    return ana::ui::minimumMainEditorWidth();
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
        numberLabel.setColour(juce::TextEditor::highlightColourId, ana::ui::light);
        numberLabel.setColour(juce::TextEditor::highlightedTextColourId, ana::ui::black);
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
        gainLabel.setColour(juce::TextEditor::highlightColourId, ana::ui::light);
        gainLabel.setColour(juce::TextEditor::highlightedTextColourId, ana::ui::black);
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
        graphics.fillAll(ana::ui::background);
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

        showAt(owner, getBounds());
    }

    void showAt(juce::Component& owner, const juce::Rectangle<int> bounds)
    {
        setBounds(bounds);
        setVisible(true);
       #if JUCE_MAC
        enableAuxiliaryMouseMoveEvents(*this);
        matchAuxiliaryWindowLevel(*this, owner);
       #else
        juce::ignoreUnused(owner);
       #endif
        toFront(false);
    }

    void hideAuxiliaryWindow()
    {
        setVisible(false);
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
        : HostShortcutDocumentWindow(ana::ui::background, Side::right),
          panelRef(panel),
          content(panel),
          closeCallback(std::move(closeCallbackIn))
    {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        setDropShadowEnabled(false);
        setResizable(false, false);
        setResizeLimits(settingsWindowWidth, minimumEditorHeight,
                        settingsWindowWidth, maximumSettingsWindowHeight);

        resizeContent = std::make_unique<ana::ui::EdgeResizeContent>(
            *this, *getConstrainer(), content, false);
        resizeContent->setSize(settingsWindowWidth, minimumEditorHeight);
        setContentNonOwned(resizeContent.get(), false);
        setSize(settingsWindowWidth, minimumEditorHeight);
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

    void refreshFor(juce::Component& owner)
    {
        content.dismissChoicePrompt();
        content.dismissResetPrompt();
        showFor(owner);
        // Finish any deferred inline-editor focus restoration before raising
        // the updated settings once. Do not pin it above the main window.
        juce::Component::SafePointer<SettingsWindow> safeWindow(this);
        juce::MessageManager::callAsync([safeWindow]
        {
            if (safeWindow != nullptr && safeWindow->isVisible())
                safeWindow->toFront(false);
        });
    }

    void showResetPrompt(ParameterControl& control)
    {
        content.showResetPrompt(control);
    }

    void hideWindow()
    {
        content.dismissChoicePrompt();
        content.dismissResetPrompt();
        hideAuxiliaryWindow();
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
    std::unique_ptr<ana::ui::EdgeResizeContent> resizeContent;
    std::function<void()> closeCallback;
};

class SnapshotsWindow final : public HostShortcutDocumentWindow
{
public:
    SnapshotsWindow(SpecView& specView, std::function<void()> closeCallbackIn)
        : HostShortcutDocumentWindow(ana::ui::background, Side::right),
          content(specView),
          closeCallback(std::move(closeCallbackIn))
    {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        setDropShadowEnabled(false);
        setResizable(false, false);
        setResizeLimits(minimumSnapshotsWindowWidth, minimumEditorHeight,
                        maximumEditorSize, maximumSnapshotsWindowHeight);

        content.onCloseRequested = [this]
        {
            if (closeCallback)
                closeCallback();
            else
                hideWindow();
        };
        resizeContent = std::make_unique<ana::ui::EdgeResizeContent>(
            *this, *getConstrainer(), content);
        resizeContent->setSize(minimumSnapshotsWindowWidth, minimumEditorHeight);
        setContentNonOwned(resizeContent.get(), false);
        setSize(minimumSnapshotsWindowWidth, minimumEditorHeight);
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
        hideAuxiliaryWindow();
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
    std::unique_ptr<ana::ui::EdgeResizeContent> resizeContent;
    std::function<void()> closeCallback;
};

class OscWindow final : public HostShortcutDocumentWindow
{
public:
    OscWindow(PluginProcessor& processor, std::function<void()> closeCallbackIn,
              std::function<void()> listCallbackIn)
        : HostShortcutDocumentWindow(ana::ui::background, Side::right),
          content(processor), closeCallback(std::move(closeCallbackIn)),
          listCallback(std::move(listCallbackIn))
    {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        setDropShadowEnabled(false);
        setResizable(false, false);
        setResizeLimits(oscWindowWidth, minimumEditorHeight,
                        oscWindowWidth, maximumSettingsWindowHeight);
        content.onCloseRequested = [this] { closeButtonPressed(); };
        content.onListRequested = [this]
        {
            if (listCallback)
                listCallback();
        };
        content.onTextEditingChanged = [this] (const bool editing)
        {
            setTextInputActive(editing);
        };
        resizeContent = std::make_unique<ana::ui::EdgeResizeContent>(
            *this, *getConstrainer(), content, false);
        resizeContent->setSize(oscWindowWidth, minimumEditorHeight);
        setContentNonOwned(resizeContent.get(), false);
        setSize(oscWindowWidth, minimumEditorHeight);
        initialiseDesktopPeer();
    }

    ~OscWindow() override
    {
        content.onCloseRequested = nullptr;
        content.onListRequested = nullptr;
        content.onTextEditingChanged = nullptr;
        clearContentComponent();
    }

    void showFor(juce::Component& owner) { showBeside(owner); }
    void setListVisible(const bool visible) { content.setListVisible(visible); }
    void hideWindow()
    {
        content.dismissEditor();
        hideAuxiliaryWindow();
    }

    void closeButtonPressed() override
    {
        if (closeCallback)
            closeCallback();
        else
            hideWindow();
    }

private:
    OscPanel content;
    std::unique_ptr<ana::ui::EdgeResizeContent> resizeContent;
    std::function<void()> closeCallback;
    std::function<void()> listCallback;
};

class OscListContent final : public juce::Component,
                             private juce::TableListBoxModel,
                             private juce::Timer
{
    class CentredHeaderLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void drawTableHeaderBackground(juce::Graphics& graphics,
                                       juce::TableHeaderComponent& header) override
        {
            graphics.fillAll(header.findColour(juce::TableHeaderComponent::backgroundColourId));
        }

        void drawTableHeaderColumn(juce::Graphics& graphics,
                                   juce::TableHeaderComponent& header,
                                   const juce::String& columnName,
                                   int columnId, int width, int height,
                                   bool, bool, int) override
        {
            graphics.setColour(header.findColour(juce::TableHeaderComponent::textColourId));
            graphics.setFont(ana::ui::makeFont());
            graphics.drawFittedText(columnName,
                                    { 4, 0, juce::jmax(0, width - 8), height },
                                    juce::Justification::centred, 1);
            graphics.setColour(ana::ui::light);
            if (columnId == nameColumn)
                graphics.fillRect(width - 1, 0, 1, height);
            graphics.fillRect(0, height - 1, width, 1);
        }
    };

    struct ParameterEntry
    {
        juce::String name;
        juce::String values;
        juce::String address;

        bool operator==(const ParameterEntry&) const = default;
    };

    class InputListener final : public juce::MouseListener
    {
    public:
        explicit InputListener(OscListContent& ownerIn) : owner(ownerIn) {}

        void mouseDown(const juce::MouseEvent& event) override { owner.handleMouseDown(event); }
        void mouseDrag(const juce::MouseEvent& event) override { owner.handleMouseDrag(event); }
        void mouseUp(const juce::MouseEvent& event) override { owner.handleMouseUp(event); }

    private:
        OscListContent& owner;
    };

public:
    explicit OscListContent(PluginProcessor& processorIn)
        : processor(processorIn), inputListener(*this)
    {
        setOpaque(true);
        addMouseListener(&inputListener, true);

        table.setModel(this);
        table.setRowHeight(ana::ui::controlHeight);
        table.setOutlineThickness(1);
        table.setMultipleSelectionEnabled(false);
        table.setColour(juce::ListBox::backgroundColourId, ana::ui::background);
        table.setColour(juce::ListBox::outlineColourId, ana::ui::light);
        table.getViewport()->setScrollBarsShown(false, false, true, false);

        auto& header = table.getHeader();
        header.setColour(juce::TableHeaderComponent::backgroundColourId, ana::ui::background);
        header.setColour(juce::TableHeaderComponent::textColourId, ana::ui::white);
        header.setColour(juce::TableHeaderComponent::outlineColourId, ana::ui::light);
        header.setColour(juce::TableHeaderComponent::highlightColourId, ana::ui::background);
        header.setLookAndFeel(&headerLookAndFeel);
        constexpr int columnFlags = juce::TableHeaderComponent::visible;
        header.addColumn("NAME", nameColumn, 1, 1, 10000, columnFlags);
        header.addColumn("VALUES", valuesColumn, 1, 1, 10000, columnFlags);
        addAndMakeVisible(table);

        refreshParameters();
        startTimerHz(4);
    }

    ~OscListContent() override
    {
        stopTimer();
        removeMouseListener(&inputListener);
        table.getHeader().setLookAndFeel(nullptr);
        table.setModel(nullptr);
    }

    void refreshParameters()
    {
        std::vector<ParameterEntry> refreshed;
        struct Group
        {
            const char* title;
            const char* prefix;
            std::vector<ParameterEntry> mainEntries;
            std::vector<ParameterEntry> settingsEntries;
        };
        std::array groups {
            Group { "GLOBAL", "module", {}, {} },
            Group { "SPEC", "spec/view", {}, {} },
            Group { "SPEC - FREQ", "spec/freq/", {}, {} },
            Group { "SPEC - MAP", "spec/map/", {}, {} },
            Group { "CORR", "corr/mode", {}, {} },
            Group { "CORR - SIGNED", "corr/signed/", {}, {} },
            Group { "CORR - PHASE", "corr/phase/", {}, {} },
            Group { "CORR - FREQ", "corr/freq/", {}, {} },
            Group { "LVLS - PEAK/RMS", "lvls/peak-rms/", {}, {} },
            Group { "LVLS - LOUDNESS", "lvls/loudness/", {}, {} },
            Group { "LVLS - HISTORY", "lvls/history/", {}, {} },
            Group { "SCOP - CROSSOVER", "scop/cross-", {}, {} },
            Group { "SCOP - BAND 1", "scop/band-1/", {}, {} },
            Group { "SCOP - BAND 2", "scop/band-2/", {}, {} },
            Group { "SCOP - BAND 3", "scop/band-3/", {}, {} },
            Group { "SCOP - BAND 4", "scop/band-4/", {}, {} },
            Group { "SCOP - BAND 5", "scop/band-5/", {}, {} },
            Group { "SCOP - BAND 6", "scop/band-6/", {}, {} },
            Group { "SCOP", "scop/", {}, {} }
        };
        std::vector<ParameterEntry> otherParameters;
        auto& state = processor.getParameters();
        for (const auto& child : state.state)
        {
            const auto id = child.getProperty("id").toString();
            auto* parameter = state.getParameter(id);
            if (id.isEmpty() || parameter == nullptr)
                continue;
            ParameterEntry entry { id.fromLastOccurrenceOf("/", false, false),
                                   acceptedValues(*parameter), "/ana/" + id };
            auto grouped = false;
            for (auto& group : groups)
                if (id.startsWith(group.prefix)
                    || ((id == PluginProcessor::cleanViewParameterId
                         || id == PluginProcessor::analysisModeParameterId)
                        && juce::String(group.title) == "GLOBAL"))
                {
                    auto& entries = ana::osc::parameterSection(id) == ana::osc::ParameterSection::main
                        ? group.mainEntries : group.settingsEntries;
                    entries.push_back(std::move(entry));
                    grouped = true;
                    break;
                }
            if (! grouped)
                otherParameters.push_back(std::move(entry));
        }
        const auto appendSection = [&refreshed] (const juce::String& title,
                                                 const std::vector<ParameterEntry>& entries)
        {
            if (entries.empty())
                return;
            refreshed.push_back({ title, {}, {} });
            refreshed.insert(refreshed.end(), entries.begin(), entries.end());
        };
        for (const auto& group : groups)
        {
            appendSection(ana::osc::sectionTitle(group.title, ana::osc::ParameterSection::main),
                          group.mainEntries);
            appendSection(ana::osc::sectionTitle(group.title, ana::osc::ParameterSection::settings),
                          group.settingsEntries);
        }
        appendSection("OTHER", otherParameters);

        if (refreshed == parameters)
            return;

        parameters = std::move(refreshed);
        pressedRow = -1;
        copiedRow = -1;
        ++longPressGeneration;
        ++copiedGeneration;
        updateColumnWidths();
        table.updateContent();
        table.repaint();
    }

    int getPreferredWindowWidth() const noexcept { return preferredWindowWidth; }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(ana::ui::background);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(contentInset);
        table.setBounds(bounds);
    }

    void paintOverChildren(juce::Graphics& graphics) override
    {
        graphics.setColour(ana::ui::light);
        graphics.drawRect(getLocalBounds(), 1);
    }

private:
    static juce::String formatValue(const float value)
    {
        const auto rounded = std::round(value);
        if (std::abs(value - rounded) <= 1.0e-6f)
            return juce::String(static_cast<int>(rounded));

        return juce::String(value, 6).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    }

    static juce::String acceptedValues(const juce::RangedAudioParameter& parameter)
    {
        const auto& range = parameter.getNormalisableRange();
        if (dynamic_cast<const juce::AudioParameterBool*>(&parameter) != nullptr)
            return "0, 1";

        const auto choice = dynamic_cast<const juce::AudioParameterChoice*>(&parameter) != nullptr;
        const auto discrete = choice
            || dynamic_cast<const juce::AudioParameterInt*>(&parameter) != nullptr;
        auto text = formatValue(range.start + (choice ? 1.0f : 0.0f))
            + " " + juce::String::charToString(0x2014) + " "
            + formatValue(range.end + (choice ? 1.0f : 0.0f));
        if (discrete)
            text += ", d";
        return text;
    }

    void timerCallback() override { refreshParameters(); }

    void updateColumnWidths()
    {
        const auto font = ana::ui::makeFont();
        const auto measure = [&font] (const juce::String& text)
        {
            return juce::GlyphArrangement::getStringWidthInt(font, text)
                + ana::ui::gap.pixels() * 2;
        };

        auto nameWidth = measure("NAME");
        auto valuesWidth = measure("VALUES");
        for (const auto& parameter : parameters)
        {
            nameWidth = juce::jmax(nameWidth, measure(parameter.name));
            valuesWidth = juce::jmax(valuesWidth, measure(parameter.values));
        }

        auto& header = table.getHeader();
        header.setColumnWidth(nameColumn, nameWidth);
        header.setColumnWidth(valuesColumn, valuesWidth);
        preferredWindowWidth = nameWidth + valuesWidth + contentInset * 2 + 2;
    }

    int getNumRows() override { return static_cast<int>(parameters.size()); }

    void paintRowBackground(juce::Graphics& graphics, int rowNumber,
                            int width, int height, bool) override
    {
        const auto isHeading = juce::isPositiveAndBelow(rowNumber, static_cast<int>(parameters.size()))
            && parameters[static_cast<size_t>(rowNumber)].address.isEmpty();
        graphics.fillAll(isHeading ? ana::ui::dark : ana::ui::background);
        graphics.setColour(ana::ui::light);
        graphics.fillRect(0, height - 1, width, 1);
    }

    void paintCell(juce::Graphics& graphics, int rowNumber, int columnId,
                   int width, int height, bool) override
    {
        if (! juce::isPositiveAndBelow(rowNumber, static_cast<int>(parameters.size())))
            return;

        const auto& parameter = parameters[static_cast<size_t>(rowNumber)];
        if (parameter.address.isEmpty() && columnId == valuesColumn)
            return;
        const auto text = rowNumber == copiedRow && columnId == nameColumn
            ? juce::String("copied")
            : columnId == nameColumn ? parameter.name : parameter.values;
        graphics.setColour(ana::ui::white);
        graphics.setFont(ana::ui::makeFont());
        const auto leftInset = ana::ui::gap.pixels();
        graphics.drawFittedText(text,
                                { leftInset, 0,
                                  juce::jmax(0, width - leftInset - ana::ui::gap.pixels()), height },
                                juce::Justification::centredLeft, 1, 1.0f);
        if (columnId == nameColumn)
        {
            graphics.setColour(ana::ui::light);
            graphics.fillRect(width - 1, 0, 1, height);
        }
    }

    int getRowAt(const juce::MouseEvent& event)
    {
        const auto relative = event.getEventRelativeTo(&table);
        if (! table.getLocalBounds().contains(relative.getPosition())
            || table.getHeader().getBounds().contains(relative.getPosition()))
            return -1;

        const auto row = table.getRowContainingPosition(relative.x, relative.y);
        return juce::isPositiveAndBelow(row, static_cast<int>(parameters.size()))
            && parameters[static_cast<size_t>(row)].address.isNotEmpty() ? row : -1;
    }

    void handleMouseDown(const juce::MouseEvent& event)
    {
        if (! event.mods.isLeftButtonDown())
            return;

        if (auto* topLevel = getTopLevelComponent())
            topLevel->toFront(false);

        pressedRow = getRowAt(event);
        ++longPressGeneration;
        if (pressedRow >= 0)
        {
            const auto generation = longPressGeneration;
            juce::Timer::callAfterDelay(longPressDelayMs,
                                        [safeThis = juce::Component::SafePointer<OscListContent>(this), generation]
                                        {
                                            if (safeThis != nullptr)
                                                safeThis->performLongPress(generation);
                                        });
        }
    }

    void handleMouseDrag(const juce::MouseEvent& event)
    {
        if (pressedRow >= 0 && event.getDistanceFromDragStart() >= longPressDragTolerance)
        {
            pressedRow = -1;
            ++longPressGeneration;
        }
    }

    void handleMouseUp(const juce::MouseEvent&)
    {
        pressedRow = -1;
        ++longPressGeneration;
    }

    void performLongPress(const uint32_t generation)
    {
        if (generation != longPressGeneration
            || ! juce::isPositiveAndBelow(pressedRow, static_cast<int>(parameters.size())))
            return;

        const auto row = pressedRow;
        const auto& parameter = parameters[static_cast<size_t>(row)];
        if (parameter.address.isEmpty())
            return;

        juce::SystemClipboard::copyTextToClipboard(parameter.address);
        copiedRow = row;
        table.repaintRow(row);
        const auto copiedToken = ++copiedGeneration;
        juce::Timer::callAfterDelay(copiedDisplayMs,
                                    [safeThis = juce::Component::SafePointer<OscListContent>(this), copiedToken, row]
                                    {
                                        if (safeThis == nullptr || copiedToken != safeThis->copiedGeneration)
                                            return;
                                        if (safeThis->copiedRow == row)
                                        {
                                            safeThis->copiedRow = -1;
                                            safeThis->table.repaintRow(row);
                                        }
                                    });
    }

    static constexpr int contentInset = ana::ui::gap.pixels() + 1;
    static constexpr int nameColumn = 1;
    static constexpr int valuesColumn = 2;
    static constexpr int longPressDelayMs = 500;
    static constexpr int copiedDisplayMs = 500;
    static constexpr int longPressDragTolerance = 4;

    PluginProcessor& processor;
    CentredHeaderLookAndFeel headerLookAndFeel;
    juce::TableListBox table;
    InputListener inputListener;
    std::vector<ParameterEntry> parameters;
    int pressedRow = -1;
    int copiedRow = -1;
    uint32_t longPressGeneration = 0;
    uint32_t copiedGeneration = 0;
    int preferredWindowWidth = 500;
};

class OscListWindow final : public HostShortcutDocumentWindow, private juce::Timer
{
public:
    OscListWindow(PluginProcessor& processorIn, std::function<void()> closeCallbackIn)
        : HostShortcutDocumentWindow(ana::ui::background, Side::right),
          content(processorIn), closeCallback(std::move(closeCallbackIn))
    {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        setDropShadowEnabled(false);
        setResizable(false, false);
        setContentNonOwned(&content, false);
        setSize(content.getPreferredWindowWidth(), minimumEditorHeight);
        initialiseDesktopPeer();
    }

    ~OscListWindow() override
    {
        stopTimer();
       #if JUCE_MAC
        detachAuxiliaryWindowFromOwner(*this);
       #endif
        clearContentComponent();
    }

    void showFor(juce::Component& owner)
    {
        content.refreshParameters();
        ownerComponent = &owner;
        const auto ownerBounds = owner.getScreenBounds();
        auto targetBounds = getBounds().withSize(content.getPreferredWindowWidth(),
                                                 ownerBounds.getHeight());
        targetBounds.setPosition(ownerBounds.getRight() + ana::ui::gap.pixels(), ownerBounds.getY());
        showAt(owner, targetBounds);
       #if JUCE_MAC
        attachAuxiliaryWindowToOwner(*this, owner);
       #endif
        startTimerHz(60);
    }

    void hideWindow()
    {
        stopTimer();
       #if JUCE_MAC
        detachAuxiliaryWindowFromOwner(*this);
       #endif
        ownerComponent = nullptr;
        hideAuxiliaryWindow();
    }

    void closeButtonPressed() override
    {
        if (closeCallback)
            closeCallback();
        else
            hideWindow();
    }

private:
    void timerCallback() override
    {
        if (ownerComponent == nullptr)
            return;

        auto targetBounds = getBounds().withSize(content.getPreferredWindowWidth(),
                                                 ownerComponent->getHeight());
        const auto ownerBounds = ownerComponent->getScreenBounds();
        targetBounds.setPosition(ownerBounds.getRight() + ana::ui::gap.pixels(),
                                 ownerBounds.getY());
        if (targetBounds != getBounds())
            setBounds(targetBounds);
    }

    OscListContent content;
    juce::Component::SafePointer<juce::Component> ownerComponent;
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
    for (auto* component : std::array<juce::Component*, 15> {
             &scopDisplay, &specDisplay, &corrDisplay, &lvlsDisplay,
             &specPageButton, &corrPageButton, &lvlsPageButton, &scopPageButton,
             &snapshotsWindowButton, &settingsButton, &fullSourceButton,
             &clearButton, &freezeButton, &oscWindowButton, &aboutButton })
        addAndMakeVisible(*component);

    specPageButton.onClick = [this] { showAnalyzerPage(ana::AnalyzerPage::spec, showingAnalyzerSettings); };
    corrPageButton.onClick = [this] { showAnalyzerPage(ana::AnalyzerPage::corr, showingAnalyzerSettings); };
    lvlsPageButton.onClick = [this] { showAnalyzerPage(ana::AnalyzerPage::lvls, showingAnalyzerSettings); };
    scopPageButton.onClick = [this] { showAnalyzerPage(ana::AnalyzerPage::scop, showingAnalyzerSettings); };
    snapshotsWindowButton.onClick = [this] { showSnapshotsWindow(! showingSnapshotsWindow); };
    oscWindowButton.onClick = [this] { showOscWindow(! showingOscWindow); };
    settingsButton.onClick = [this] { showAnalyzerSettings(! showingAnalyzerSettings); };
    aboutButton.setTooltip("ABOUT");
    snapshotsWindowButton.setTooltip("SNAPSHOTS");
    settingsButton.setTooltip("SETTINGS");
    freezeButton.setTooltip("FREEZE");
    oscWindowButton.setTooltip("OSC");
    fullSourceButton.setTooltip("FULL");
    clearButton.setTooltip("CLEAR");
    aboutButton.onClick = [this] { showAboutPopup(); };
    for (auto* component : std::array<juce::Component*, 9> {
             &modeButton, &sourceButton, &takeButton, &keepSecondTakeButton, &refreshButton,
             &locationButton, &offlineTrackNumberLabel, &offlineLocationLabel, &offlineUpdateLabel })
        addChildComponent(*component);
    modeButton.onClick = [this] { showModePrompt(); };
    modeButton.setTooltip("MODE: REALTIME / OFFLINE");
    offlineUpdateLabel.setFont(ana::ui::makeFont());
    offlineUpdateLabel.setJustificationType(juce::Justification::centred);
    offlineUpdateLabel.setMinimumHorizontalScale(1.0f);
    offlineUpdateLabel.setColour(juce::Label::textColourId, ana::ui::white);
    offlineUpdateLabel.setColour(juce::Label::backgroundColourId, ana::ui::dark);
    offlineUpdateLabel.setColour(juce::Label::outlineColourId, ana::ui::light);
    offlineUpdateLabel.setDrawBackground(false);
    offlineUpdateLabel.setBorderSize(
        juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    offlineUpdateLabel.setInterceptsMouseClicks(false, false);
    offlineUpdateLabel.setVisible(true);
    offlineTrackNumberLabel.setFont(ana::ui::makeFont());
    offlineTrackNumberLabel.setJustificationType(juce::Justification::centred);
    offlineTrackNumberLabel.setMinimumHorizontalScale(1.0f);
    offlineTrackNumberLabel.setColour(juce::Label::textColourId, ana::ui::white);
    offlineTrackNumberLabel.setColour(juce::Label::outlineColourId, ana::ui::light);
    offlineTrackNumberLabel.setDrawBackground(false);
    offlineTrackNumberLabel.setBorderSize(
        juce::BorderSize<int>(1, ana::ui::textPadding, 1, ana::ui::textPadding));
    offlineTrackNumberLabel.setInterceptsMouseClicks(false, false);

    refreshButton.addMouseListener(this, false);
    refreshPress.onArmed = [this]
    {
        audioProcessor.setOfflineAutomaticRefresh(! audioProcessor.isOfflineAutomaticRefresh());
        refreshButton.setToggleState(audioProcessor.isOfflineAutomaticRefresh(),
                                     juce::dontSendNotification);
        refreshOfflineSelectionButtons();
    };
    refreshButton.onClick = [this]
    {
        const auto release = refreshPress.release();
        if (release == LongPressGesture::ReleaseResult::longPress
            || release == LongPressGesture::ReleaseResult::cancelled)
            return;
        audioProcessor.forceOfflineRefresh();
        refreshOfflineSelectionButtons();
        offlineUpdateLabel.setText("00", juce::dontSendNotification);
    };

    sourceButton.onClick = [this] { showOfflineSourcePrompt(); };
    locationButton.onClick = [this] { showOfflineLocationPrompt(); };
    takeButton.onClick = [this] { showOfflineTakePrompt(); };
    keepSecondTakeButton.setTooltip("KEEP SECOND TAKE");
    keepSecondTakeButton.onClick = [this]
    {
        audioProcessor.setOfflineKeepSecondTake(! audioProcessor.isOfflineKeepSecondTake());
        refreshOfflineSelectionButtons();
    };



    settingsComponent.onDisplaySettingsChanged = [this]
    {
        scopDisplay.refreshDisplaySettings();
        specDisplay.resized();
        specDisplay.repaint();
        corrDisplay.resized();
        corrDisplay.repaint();
        lvlsDisplay.resized();
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
    freezeButton.onClick = [this]
    {
        if (audioProcessor.isOfflineMode()) return;
        if (activePage == ana::AnalyzerPage::spec)
            audioProcessor.getSpecProcessor().setFrozen(freezeButton.getToggleState());
        else if (activePage == ana::AnalyzerPage::corr)
            audioProcessor.getCorrProcessor().setFrozen(freezeButton.getToggleState());
        else if (activePage == ana::AnalyzerPage::lvls)
            audioProcessor.setLvlsFrozen(freezeButton.getToggleState());
        else
        {
            scopFrozen = freezeButton.getToggleState();
            scopDisplay.setFrozen(scopFrozen);
        }
    };
    fullSourceButton.onClick = [this]
    {
        if (audioProcessor.getScopSingleViewBand() >= 0)
            return;
        const auto enabled = ! audioProcessor.isScopFullSourceView();
        audioProcessor.setScopFullSourceView(enabled);
        scopDisplay.setFullSourceView(enabled);
    };
    clearButton.onClick = [this]
    {
        if (activePage != ana::AnalyzerPage::scop)
            return;
        scopDisplay.clearHistory();
    };

    showAnalyzerPage(audioProcessor.getAnalyzerPageState(),
                     false);


    timerCallback();
    startTimerHz(15);
    setResizable(true, false);
    rightEdgeResizer = std::make_unique<InvisibleResizableEdgeComponent>(
        this, getConstrainer(), juce::ResizableEdgeComponent::rightEdge);
    bottomEdgeResizer = std::make_unique<InvisibleResizableEdgeComponent>(
        this, getConstrainer(), juce::ResizableEdgeComponent::bottomEdge);
    rightEdgeResizer->setAlwaysOnTop(true);
    bottomEdgeResizer->setAlwaysOnTop(true);
    addAndMakeVisible(*rightEdgeResizer);
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
    refreshButton.removeMouseListener(this);
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

void PluginEditor::showOscWindow(const bool shouldShowWindow)
{
    if (! shouldShowWindow)
        showOscListWindow(false);
    if (shouldShowWindow && oscWindow == nullptr)
        oscWindow = std::make_unique<OscWindow>(
            audioProcessor, [this] { showOscWindow(false); },
            [this] { showOscListWindow(! showingOscListWindow); });

    showingOscWindow = shouldShowWindow;
    oscWindowButton.setToggleState(showingOscWindow, juce::dontSendNotification);
    if (oscWindow != nullptr)
    {
        if (showingOscWindow)
            oscWindow->showFor(*this);
        else
            oscWindow->hideWindow();
    }
}

void PluginEditor::showOscListWindow(const bool shouldShowWindow)
{
    if (shouldShowWindow && oscWindow == nullptr)
        return;
    if (shouldShowWindow && oscListWindow == nullptr)
        oscListWindow = std::make_unique<OscListWindow>(
            audioProcessor, [this] { showOscListWindow(false); });

    showingOscListWindow = shouldShowWindow;
    if (oscWindow != nullptr)
        oscWindow->setListVisible(shouldShowWindow);
    if (oscListWindow != nullptr)
    {
        if (shouldShowWindow)
            oscListWindow->showFor(*oscWindow);
        else
            oscListWindow->hideWindow();
    }
}

void PluginEditor::showAnalyzerPage(const ana::AnalyzerPage page,
                                               const bool shouldShowSettings)
{
    if (! shouldShowSettings || page != activePage)
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
    oscWindowButton.setEnabled(true);
    snapshotsWindowButton.setToggleState(showingSnapshotsWindow, juce::dontSendNotification);
    oscWindowButton.setToggleState(showingOscWindow, juce::dontSendNotification);
    scopDisplay.setVisible(page == ana::AnalyzerPage::scop);
    specDisplay.setVisible(page == ana::AnalyzerPage::spec);
    corrDisplay.setVisible(page == ana::AnalyzerPage::corr);
    lvlsDisplay.setVisible(page == ana::AnalyzerPage::lvls);
    fullSourceButton.setEnabled(page == ana::AnalyzerPage::scop
        && audioProcessor.getScopSingleViewBand() < 0);
    freezeButton.setToggleState(page == ana::AnalyzerPage::spec
                                    ? audioProcessor.getSpecProcessor().isFrozen()
                                    : page == ana::AnalyzerPage::corr
                                        ? audioProcessor.getCorrProcessor().isFrozen()
                                    : page == ana::AnalyzerPage::lvls
                                        ? audioProcessor.getLvlsProcessor().isFrozen()
                                    : scopFrozen,
                                juce::dontSendNotification);
    const auto settingsContextChanged = settingsComponent.setAnalyzerContext(
        page, getActiveSettingsViewMode());
    if (settingsWindow != nullptr)
    {
        if (showingAnalyzerSettings)
        {
            if (settingsContextChanged)
                settingsWindow->refreshFor(*this);
            else
                settingsWindow->showFor(*this);
        }
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
    const auto offline = audioProcessor.isOfflineMode();
    const auto modeAvailable = audioProcessor.isOfflineAvailable();
    if (displayedOffline != offline || displayedModeAvailable != modeAvailable)
    {
        if (displayedOffline && ! offline)
            audioProcessor.cancelOfflineAnalysis();
        displayedOffline = offline;
        displayedModeAvailable = modeAvailable;
        dismissChoicePrompt();
        specDisplay.refreshMode();
        corrDisplay.refreshMode();
        scopDisplay.refreshMode();
        settingsComponent.refreshExternalState();
        settingsComponent.resized();
        lvlsDisplay.resized();
        resized();
        repaint();
    }
    if (offline)
    {
        refreshOfflineSelectionButtons();
        refreshOfflineUpdateStatus();
        refreshButton.setToggleState(audioProcessor.isOfflineAutomaticRefresh(), juce::dontSendNotification);
    }

    const auto clean = audioProcessor.isCleanView();
    if (cleanViewActive != clean)
    {
        cleanViewActive = clean;
        if (clean)
        {
            dismissChoicePrompt();
            dismissAboutPopup();
        }
        resized();
        repaint();
    }

    const auto restoredPage = audioProcessor.getAnalyzerPageState();
    if (restoredPage != activePage)
        showAnalyzerPage(restoredPage, showingAnalyzerSettings);

    fullSourceButton.setToggleState(audioProcessor.isScopFullSourceView(),
                                    juce::dontSendNotification);
    clearButton.setEnabled(!audioProcessor.isOfflineMode() && activePage == ana::AnalyzerPage::scop);
    freezeButton.setEnabled(!audioProcessor.isOfflineMode());
    fullSourceButton.setEnabled(activePage == ana::AnalyzerPage::scop
        && audioProcessor.getScopSingleViewBand() < 0);
    settingsButton.setEnabled(true);
    snapshotsWindowButton.setEnabled(true);
    oscWindowButton.setEnabled(true);
    if (settingsComponent.setAnalyzerContext(activePage, getActiveSettingsViewMode())
        && showingAnalyzerSettings && settingsWindow != nullptr)
        settingsWindow->refreshFor(*this);
    freezeButton.setToggleState(activePage == ana::AnalyzerPage::spec
                                    ? audioProcessor.getSpecProcessor().isFrozen()
                                    : activePage == ana::AnalyzerPage::corr
                                        ? audioProcessor.getCorrProcessor().isFrozen()
                                    : activePage == ana::AnalyzerPage::lvls
                                        ? audioProcessor.getLvlsProcessor().isFrozen()
                                    : scopFrozen,
                                juce::dontSendNotification);

    if (editorSizeSavePending
        && juce::Time::getMillisecondCounterHiRes() >= editorSizeSaveDeadlineMilliseconds)
    {
        audioProcessor.setLastEditorSize(pendingEditorSize.x, pendingEditorSize.y);
        editorSizeSavePending = false;
    }
}

void PluginEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ana::ui::background);
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
    const auto modeVisible = audioProcessor.isOfflineAvailable() && ! audioProcessor.isCleanView();
    modeButton.setVisible(modeVisible);
    modeButton.setBounds({});
    const auto navigationWidth = ana::ui::mainNavigationWidth()
        + (modeVisible ? modeButton.getPreferredWidth() + ana::ui::gap.pixels() : 0);
    for (auto* component : std::array<juce::Component*, 8> {
             &sourceButton, &takeButton, &keepSecondTakeButton, &refreshButton,
             &locationButton, &offlineTrackNumberLabel, &offlineLocationLabel, &offlineUpdateLabel })
    { component->setVisible(false); component->setBounds({}); }
    if (audioProcessor.isOfflineMode())
    {
    if (! audioProcessor.isCleanView())
    {
        auto controlsRow = area.removeFromTop(ana::ui::controlHeight);
        const auto scopActions = activePage == ana::AnalyzerPage::scop;
        const auto essentialRightControlsWidth = aboutButton.getPreferredWidth()
            + oscWindowButton.getPreferredWidth() + ana::ui::gap.pixels()
            + settingsButton.getPreferredWidth() + snapshotsWindowButton.getPreferredWidth()
            + ana::ui::textControlWidth(2) + refreshButton.getPreferredWidth()
            + takeButton.getPreferredWidth() + keepSecondTakeButton.getPreferredWidth()
            + sourceButton.getPreferredWidth()
            + 7 * ana::ui::gap.pixels();
        const auto inlineScopActions = scopActions
            && navigationWidth + ana::ui::gap.pixels()
                + essentialRightControlsWidth + fullSourceButton.getPreferredWidth()
                + ana::ui::gap.pixels() <= controlsRow.getWidth();
        const auto baseRightControlsWidth = essentialRightControlsWidth
            + (inlineScopActions ? fullSourceButton.getPreferredWidth() + ana::ui::gap.pixels() : 0);
        const auto locationWidth = locationButton.getPreferredWidth() + ana::ui::gap.pixels();
        const auto numberWidth = ana::ui::textControlWidth(3);
        constexpr int minimumTrackNameWidth = 40;
        auto showTrackName = true;
        auto showLocation = true;
        auto showTrackNumber = true;
        const auto requiredWidth = [&]
        {
            return navigationWidth
                + (showTrackNumber ? ana::ui::gap.pixels() + numberWidth : 0)
                + (showTrackName ? ana::ui::gap.pixels() + minimumTrackNameWidth : 0)
                + ana::ui::gap.pixels() + baseRightControlsWidth
                + (showLocation ? locationWidth : 0);
        };
        if (requiredWidth() >= controlsRow.getWidth())
            showTrackName = false;
        if (requiredWidth() > controlsRow.getWidth())
            showLocation = false;
        if (requiredWidth() > controlsRow.getWidth())
            showTrackNumber = false;
        const auto wrapRightControls = requiredWidth() > controlsRow.getWidth();
        auto rightControlsRow = controlsRow;
        if (wrapRightControls)
        {
            ana::ui::gap.removeFromTop(area);
            rightControlsRow = area.removeFromTop(ana::ui::controlHeight);
        }
        const auto placeRight = [&] (juce::Component& component, const int width)
        {
            component.setVisible(true);
            component.setBounds(rightControlsRow.removeFromRight(width));
            ana::ui::gap.removeFromRight(rightControlsRow);
        };

        for (auto* component : std::array<juce::Component*, 11> {
                 &clearButton, &freezeButton, &fullSourceButton,
                 &sourceButton, &takeButton, &keepSecondTakeButton, &refreshButton, &offlineUpdateLabel,
                 &locationButton, &offlineTrackNumberLabel, &offlineLocationLabel })
        {
            component->setVisible(false);
            component->setBounds({});
        }

        placeRight(aboutButton, aboutButton.getPreferredWidth());
        placeRight(settingsButton, settingsButton.getPreferredWidth());
        placeRight(snapshotsWindowButton, snapshotsWindowButton.getPreferredWidth());
        placeRight(oscWindowButton, oscWindowButton.getPreferredWidth());
        placeRight(offlineUpdateLabel, ana::ui::textControlWidth(2));
        placeRight(refreshButton, refreshButton.getPreferredWidth());
        placeRight(keepSecondTakeButton, keepSecondTakeButton.getPreferredWidth());
        placeRight(takeButton, takeButton.getPreferredWidth());
        placeRight(sourceButton, sourceButton.getPreferredWidth());
        if (inlineScopActions)
            placeRight(fullSourceButton, fullSourceButton.getPreferredWidth());
        if (showLocation)
            placeRight(locationButton, locationButton.getPreferredWidth());

        if (! wrapRightControls)
            controlsRow = rightControlsRow;

        ana::ui::FixedGapRow navigationRow(controlsRow);
        if (modeVisible)
            modeButton.setBounds(navigationRow.takeLeft(modeButton.getPreferredWidth()));
        for (auto* button : std::array<ControlButton*, 4> {
                 &specPageButton, &corrPageButton, &lvlsPageButton, &scopPageButton })
        {
            button->setBounds(navigationRow.takeLeft(button->getPreferredWidth()));
            button->setVisible(true);
        }
        if (showTrackNumber)
        {
            offlineTrackNumberLabel.setBounds(scopPageButton.getRight() + ana::ui::gap.pixels(),
                                          controlsRow.getY(), numberWidth, ana::ui::controlHeight);
            offlineTrackNumberLabel.setVisible(true);
        }
        if (showTrackName)
        {
            const auto nameLeft = offlineTrackNumberLabel.getRight() + ana::ui::gap.pixels();
            offlineLocationLabel.setBounds(nameLeft,
                                       controlsRow.getY(), rightControlsRow.getRight() - nameLeft,
                                       ana::ui::controlHeight);
            offlineLocationLabel.setVisible(true);
        }

        ana::ui::gap.removeFromTop(area);
        if (scopActions && ! inlineScopActions)
        {
            // Keep navigation and SOURCE joined at the common minimum width.
            auto actionsRow = area.removeFromTop(ana::ui::controlHeight);
            fullSourceButton.setBounds(actionsRow.removeFromRight(fullSourceButton.getPreferredWidth()));
            fullSourceButton.setVisible(true);
            ana::ui::gap.removeFromTop(area);
        }
    }
    else
    {
        for (auto* component : std::array<juce::Component*, 19> {
                 &specPageButton, &corrPageButton, &lvlsPageButton, &scopPageButton,
                 &snapshotsWindowButton, &settingsButton, &fullSourceButton,
                 &clearButton, &freezeButton, &aboutButton,
                 &sourceButton, &takeButton, &refreshButton, &offlineUpdateLabel,
                 &locationButton, &offlineLocationLabel, &offlineTrackNumberLabel, &keepSecondTakeButton,
                 &oscWindowButton })
        {
            component->setVisible(false);
            component->setBounds({});
        }
    }

    }
    else
    {
    if (! audioProcessor.isCleanView())
    {
        auto controlsRow = area.removeFromTop(ana::ui::controlHeight);
        const auto scopActions = activePage == ana::AnalyzerPage::scop;
        const auto rightControlsWidth = aboutButton.getPreferredWidth()
            + settingsButton.getPreferredWidth() + snapshotsWindowButton.getPreferredWidth()
            + freezeButton.getPreferredWidth() + oscWindowButton.getPreferredWidth()
            + (scopActions ? clearButton.getPreferredWidth() + fullSourceButton.getPreferredWidth() : 0)
            + (scopActions ? 7 : 5) * ana::ui::gap.pixels();
        const auto wrapRightControls = navigationWidth + rightControlsWidth > controlsRow.getWidth();
        auto rightControlsRow = controlsRow;
        if (wrapRightControls)
        {
            ana::ui::gap.removeFromTop(area);
            rightControlsRow = area.removeFromTop(ana::ui::controlHeight);
        }
        const auto placeRight = [&] (juce::Component& component, const int width)
        {
            component.setVisible(true);
            component.setBounds(rightControlsRow.removeFromRight(width));
            ana::ui::gap.removeFromRight(rightControlsRow);
        };

        for (auto* component : std::array<juce::Component*, 3> {
                 &clearButton, &freezeButton, &fullSourceButton })
        {
            component->setVisible(false);
            component->setBounds({});
        }

        placeRight(aboutButton, aboutButton.getPreferredWidth());
        placeRight(settingsButton, settingsButton.getPreferredWidth());
        placeRight(snapshotsWindowButton, snapshotsWindowButton.getPreferredWidth());
        placeRight(oscWindowButton, oscWindowButton.getPreferredWidth());
        placeRight(freezeButton, freezeButton.getPreferredWidth());
        if (scopActions)
        {
            placeRight(clearButton, clearButton.getPreferredWidth());
            placeRight(fullSourceButton, fullSourceButton.getPreferredWidth());
        }

        if (! wrapRightControls)
            controlsRow = rightControlsRow;

        ana::ui::FixedGapRow navigationRow(controlsRow);
        if (modeVisible)
            modeButton.setBounds(navigationRow.takeLeft(modeButton.getPreferredWidth()));
        for (auto* button : std::array<ControlButton*, 4> {
                 &specPageButton, &corrPageButton, &lvlsPageButton, &scopPageButton })
        {
            button->setBounds(navigationRow.takeLeft(button->getPreferredWidth()));
            button->setVisible(true);
        }

        ana::ui::gap.removeFromTop(area);
    }
    else
    {
        for (auto* component : std::array<juce::Component*, 11> {
                 &specPageButton, &corrPageButton, &lvlsPageButton, &scopPageButton,
                 &snapshotsWindowButton, &settingsButton, &fullSourceButton,
                 &clearButton, &freezeButton, &aboutButton, &oscWindowButton })
        {
            component->setVisible(false);
            component->setBounds({});
        }
    }

    }

    auto scopBounds = area;
    scopBounds.setTop(scopBounds.getY() - ana::ui::gap.pixels());
    scopBounds.setBottom(getHeight());
    scopDisplay.setBounds(scopBounds);
    specDisplay.setBounds(area);
    corrDisplay.setBounds(area);
    lvlsDisplay.setBounds(area);

    if (rightEdgeResizer != nullptr)
    {
        rightEdgeResizer->setBounds(getWidth() - editorResizeHandleThickness, 0,
                                    editorResizeHandleThickness, getHeight());
        rightEdgeResizer->toFront(false);
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

void PluginEditor::showOfflineLocationPrompt()
{
    if (! audioProcessor.isOfflineLocationAvailable())
        return;
    showChoicePrompt(getLocalArea(&locationButton, locationButton.getLocalBounds()),
                     { "TRACK", "MONITOR" }, audioProcessor.isOfflineMonitorLocation() ? 1 : 0,
                     [this] (const int index)
                     {
                         audioProcessor.setOfflineMonitorLocation(index == 1);
                         refreshOfflineSelectionButtons();
                         scopDisplay.refreshWaveform();
                     });
}

void PluginEditor::showOfflineSourcePrompt()
{
    refreshOfflineSelectionButtons();
    juce::StringArray names { "ALL" };
    std::vector<juce::String> choiceIds { juce::String() };
    const auto selectedId = audioProcessor.getSelectedOfflineSourceId();
    auto selectedIndex = 0;

    for (const auto& choice : offlineSourceChoices)
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

                         audioProcessor.setSelectedOfflineSourceId(
                             sourceIds[static_cast<size_t>(index)]);
                         refreshOfflineSelectionButtons();
                         scopDisplay.refreshWaveform();
                     });
}

void PluginEditor::showOfflineTakePrompt()
{
    refreshOfflineSelectionButtons();
    juce::StringArray names;
    std::vector<int> choiceNumbers;
    const auto selectedSourceId = audioProcessor.getSelectedOfflineSourceId();
    const auto selectedTakeNumber = audioProcessor.getSelectedOfflineTakeNumber();
    auto selectedIndex = -1;

    for (const auto& choice : offlineSourceChoices)
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

    if (names.isEmpty())
        return;

    showChoicePrompt(getLocalArea(&takeButton, takeButton.getLocalBounds()),
                     std::move(names), selectedIndex,
                     [this, takeNumbers = std::move(choiceNumbers)] (const int index)
                     {
                         if (! juce::isPositiveAndBelow(index, static_cast<int>(takeNumbers.size())))
                             return;

                         audioProcessor.setSelectedOfflineTakeNumber(
                             takeNumbers[static_cast<size_t>(index)]);
                         refreshOfflineSelectionButtons();
                         scopDisplay.refreshWaveform();
                     });
}

void PluginEditor::refreshOfflineSelectionButtons()
{
    auto latestChoices = audioProcessor.getSourceChoices();
    audioProcessor.refreshOfflineSelection(latestChoices);
    const auto choicesChanged = latestChoices != offlineSourceChoices;
    offlineSourceChoices = std::move(latestChoices);
    if (choicesChanged && ! offlineSourceChoices.empty())
    {
        offlineUpdateLabel.setText("00", juce::dontSendNotification);
    }
    const auto sourceId = audioProcessor.getSelectedOfflineSourceId();
    const auto selectedTakeNumber = audioProcessor.getSelectedOfflineTakeNumber();
    std::vector<int> availableTakeNumbers;
    if (! offlineSourceChoices.empty())
    {
        for (const auto& choice : offlineSourceChoices)
        {
            if (sourceId.isNotEmpty() && choice.sourceId != sourceId)
                continue;

            if (std::find(availableTakeNumbers.begin(), availableTakeNumbers.end(),
                          choice.takeNumber) == availableTakeNumbers.end())
                availableTakeNumbers.push_back(choice.takeNumber);
        }

        std::sort(availableTakeNumbers.begin(), availableTakeNumbers.end());
    }

    const auto locationText = audioProcessor.getOfflineTrackName();
    offlineLocationLabel.setText(locationText);
    offlineLocationLabel.setTooltip(locationText);
    offlineTrackNumberLabel.setText(juce::String(audioProcessor.getOfflineTrackNumber()).paddedLeft('0', 3),
                                juce::dontSendNotification);
    locationButton.setToggleState(false, juce::dontSendNotification);
    locationButton.setEnabled(audioProcessor.isOfflineLocationAvailable());
    locationButton.setTooltip(audioProcessor.isOfflineMonitorLocation() ? "LOCATION: MONITOR" : "LOCATION: TRACK");
    sourceButton.setButtonText("SOURCE");
    takeButton.setButtonText("TAKE");

    const auto offlineAvailable = audioProcessor.isOfflineSourceAvailable();
    sourceButton.setEnabled(offlineAvailable && ! offlineSourceChoices.empty());
    takeButton.setEnabled(offlineAvailable && ! availableTakeNumbers.empty());
    sourceButton.setToggleState(sourceId.isNotEmpty(), juce::dontSendNotification);
    takeButton.setToggleState(false, juce::dontSendNotification);
    keepSecondTakeButton.setToggleState(audioProcessor.isOfflineKeepSecondTake(), juce::dontSendNotification);
    sourceButton.setTooltip(sourceId.isEmpty() ? "SOURCE: ALL" : "SOURCE: SELECTED");
    takeButton.setTooltip("TAKE: " + juce::String(selectedTakeNumber));
}



void PluginEditor::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent == &refreshButton && event.mods.isLeftButtonDown())
        refreshPress.begin();
    else
        juce::AudioProcessorEditor::mouseDown(event);
}

void PluginEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (event.originalComponent == &refreshButton)
    {
        if (event.getDistanceFromDragStart() >= 5)
            refreshPress.markDragged();
    }
    else
        juce::AudioProcessorEditor::mouseDrag(event);
}


void PluginEditor::refreshOfflineUpdateStatus()
{
    const auto progress = juce::jlimit(0, 100, audioProcessor.getOfflineAnalysisProgress());
    offlineUpdateLabel.setText(progress >= 100 ? juce::String("UD")
                                          : juce::String(progress).paddedLeft('0', 2),
                           juce::dontSendNotification);
}


void PluginEditor::showModePrompt()
{
    if (! audioProcessor.isOfflineAvailable())
        return;
    showChoicePrompt(getLocalArea(&modeButton, modeButton.getLocalBounds()),
                     {"REALTIME", "OFFLINE"}, audioProcessor.isOfflineMode() ? 1 : 0,
                     [this](int index) { audioProcessor.setOfflineMode(index == 1); timerCallback(); });
}
