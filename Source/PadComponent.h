#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

// One pad of the grid: a rubber pad that plays on click and accepts dropped
// files, and below it a strip with the pad number, the sample name and small
// keys: previous and next file in the folder, choose a file, mute and solo.
// Right click: copy or paste the pad (audio and settings).
class PadComponent final : public juce::Component,
                           public juce::FileDragAndDropTarget,
                           public juce::SettableTooltipClient
{
public:
    PadComponent (NzDrumProcessor& processor, int padIndex);

    // Called by the editor at 30 Hz to refresh the name, state and flash.
    void refresh();

    // Playing a pad also selects it, like on an MPC: the step row shows the
    // selected pad.
    std::function<void (int pad)> onSelect;
    void setSelected (bool shouldBeSelected);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void openFileChooser();
    void showPadMenu();

    NzDrumProcessor& nzProcessor;
    const int pad;

    juce::Rectangle<float> rubberBounds() const;

    ThemeKey previousButton { juce::String::fromUTF8 ("\xe2\x80\xb9") },   // ‹
             nextButton { juce::String::fromUTF8 ("\xe2\x80\xba") },       // ›
             loadButton { juce::String::fromUTF8 ("\xe2\x80\xa6") };       // …
    std::unique_ptr<juce::FileChooser> chooser;

    // Mute and solo are host parameters: the attachments keep button and
    // parameter in sync both ways (automation included).
    ThemeKey muteButton { "M" }, soloButton { "S" };
    juce::AudioProcessorValueTreeState::ButtonAttachment muteAttachment, soloAttachment;

    juce::String fileName;
    bool loading = false;
    bool missing = false;
    bool dragHover = false;
    bool selected = false;
    bool audible = true;
    bool chromatic = false;   // 16 levels on: the pad shows its semitone
    uint32_t lastTriggerCount = 0;
    float flash = 0.0f;   // 1 when triggered, fades out

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadComponent)
};
