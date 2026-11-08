#pragma once

#include "PluginProcessor.h"

// The selected pad's steps in the selected pattern. Up to 16 steps fit in one
// row; 32 use two.
//   click                -> normal (turns it off if it was already normal)
//   Shift + click        -> accent
//   Option + click (Alt) -> soft
//   wheel over an active step -> raises or lowers it by a semitone
// The modifiers come inside the same mouse event, so the DAW can't "steal"
// them the way it sometimes does with standalone keyboard shortcuts.
class StepSequencerComponent final : public juce::Component,
                                     public juce::SettableTooltipClient
{
public:
    explicit StepSequencerComponent (NzDrumProcessor& processor);

    void setPad (int newPad);
    int getPad() const noexcept { return pad; }

    // Called by the editor at 30 Hz: follows the selected pattern, its length
    // and the playing step.
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // The level a step that had `current` gets when clicked with these
    // modifiers. A pure function so it can be tested on its own.
    static StepLevel levelAfterClick (StepLevel current, const juce::ModifierKeys& modifiers) noexcept;

private:
    juce::Rectangle<float> stepBounds (int step) const;
    int stepAt (juce::Point<float> position) const;

    NzDrumProcessor& nzProcessor;
    int pad = 0;
    int pattern = 0;
    int length = NzDrumProcessor::defaultPatternLength;
    int playingStep = -1;   // -1 if this pattern isn't playing
    std::array<StepLevel, NzDrumProcessor::maxSteps> shownSteps {};   // what was drawn last
    std::array<int, NzDrumProcessor::maxSteps> shownPitches {};

    // A trackpad sends many small wheel movements: they add up until they
    // make a semitone.
    float wheelAccumulator = 0.0f;
    int wheelStep = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepSequencerComponent)
};
