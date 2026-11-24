#pragma once
#include <JuceHeader.h>
#include "AuxiliaryWindowFocus.h"
#include "AuxiliaryBackgroundDrag.h"
#include "SvgExport.h"
#include "ViewportOverflow.h"
class HostShortcutDocumentWindow : public juce::DocumentWindow, public ana::svg_export::Layout, private juce::FocusChangeListener
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
        juce::Desktop::getInstance().addFocusChangeListener(this);
       #if JUCE_MAC
        svgMonitor = ana::svg_export::installKeyboardShortcut(*this);
       #endif
    }

    ~HostShortcutDocumentWindow() override
    {
       #if JUCE_MAC
        detachAuxiliaryWindowFromOwner(*this);
       #endif
        backgroundDrag.detach();
        juce::Desktop::getInstance().removeFocusChangeListener(this);
       #if JUCE_MAC
        ana::svg_export::removeKeyboardShortcut(svgMonitor);
       #endif
       #if JUCE_MAC
        if (previousKeyWindow != nullptr)
            setAuxiliaryTextInputActive(*this, false, previousKeyWindow);
       #endif
    }

    int getSvgExportCanvasHeight() const override
    { return getHeight() + gui_export_layout::viewportOverflow(*this); }
    void prepareSvgExportLayout(int height) override
    {
        if (auto* content = getContentComponent())
        {
            exportContent = content;
            originalContentBounds = content->getBounds();
            content->setSize(content->getWidth(), content->getHeight() + juce::jmax(0, height - getHeight()));
        }
    }
    void finishSvgExportLayout() override
    {
        if (exportContent != nullptr) exportContent->setBounds(originalContentBounds);
        exportContent = nullptr;
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        const auto mods = key.getModifiers();
        if (mods.isCtrlDown() && mods.isAltDown() && mods.isShiftDown()
           #if JUCE_MAC
            && !mods.isCommandDown()
           #endif
            && (key.getKeyCode() == 'E' || key.getKeyCode() == 'e'))
        { ana::svg_export::chooseFile(*this); return true; }
        return juce::DocumentWindow::keyPressed(key);
    }

    juce::BorderSize<int> getBorderThickness() const override
    {
        return {};
    }

protected:
    void setBackgroundDragSnappingEnabled(bool enabled) { backgroundDrag.setSnappingEnabled(enabled); }

    void enableBackgroundDragging(juce::Component& content)
    {
        backgroundDrag.attach(content);
    }
    void disableBackgroundDragging()
    {
        backgroundDrag.detach();
    }

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
        // The host can have transparent native views/windows above its editor
        // (including after a REAPER screenset restore). Equal window levels
        // alone do not guarantee that the auxiliary dock wins mouse hit-tests.
        // Cocoa child windows are ordered above their owner and receive their
        // own mouse events, regardless of the dock's position over the host.
        attachAuxiliaryWindowToOwner(*this, owner);
       #else
        juce::ignoreUnused(owner);
       #endif
        toFront(false);
    }

    void hideAuxiliaryWindow()
    {
       #if JUCE_MAC
        detachAuxiliaryWindowFromOwner(*this);
       #endif
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
    void childBoundsChanged(juce::Component* child) override
    {
        // Expanding owned export content must not resize the native host window.
        if (exportContent == nullptr) juce::DocumentWindow::childBoundsChanged(child);
    }
    juce::Component::SafePointer<juce::Component> exportContent;
    juce::Rectangle<int> originalContentBounds;
   #if JUCE_MAC
    void* svgMonitor = nullptr;
   #endif
    void globalFocusChanged(juce::Component* focused) override
    {
        const auto active = focused != nullptr && isParentOf(focused)
            && dynamic_cast<juce::TextEditor*>(focused) != nullptr;
        if (active == focusTextInput) return;
        focusTextInput = active;
        setTextInputActive(active);
        if (active)
            if (auto* peer = getPeer()) peer->grabFocus();
    }
    bool focusTextInput = false;
    void positionBeside(juce::Component& owner)
    {
        const auto ownerBounds = owner.getScreenBounds();
        auto targetBounds = getBounds();
        const auto x = side == Side::right
            ? ownerBounds.getRight() + 8
            : ownerBounds.getX() - targetBounds.getWidth() - 8;
        targetBounds.setPosition(x, ownerBounds.getY());

        if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(ownerBounds))
            targetBounds = targetBounds.constrainedWithin(display->userArea);

        setBounds(targetBounds);
    }

    Side side;
    AuxiliaryBackgroundDrag backgroundDrag { *this };
    int activeTextInputs = 0;
    bool passShortcutsToHost = true;
   #if JUCE_MAC
    void* previousKeyWindow = nullptr;
   #endif
    bool hasBeenShown = false;
};

