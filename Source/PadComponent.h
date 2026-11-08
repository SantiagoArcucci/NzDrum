#pragma once

#include "PluginProcessor.h"

// One pad of the grid: shows the loaded file, plays on click and accepts
// dropped files. The buttons at the bottom load a file or step through the
// current sample's folder. Mute (M) and solo (S) sit at the top right.
// Right click: copy or paste the pad (audio and settings).
class PadComponent final : public juce::Component,
                           public juce::FileDragAndDropTarget
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

    juce::TextButton previousButton { "<" }, loadButton { "..." }, nextButton { ">" };
    std::unique_ptr<juce::FileChooser> chooser;

    // Mute and solo are host parameters: the attachments keep button and
    // parameter in sync both ways (automation included).
    juce::TextButton muteButton { "M" }, soloButton { "S" };
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
