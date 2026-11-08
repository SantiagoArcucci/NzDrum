#pragma once

#include "PluginProcessor.h"

// The pattern bar: 16 buttons to pick a pattern and 4 for its length.
//   - The selected pattern is drawn filled. If the previous one is still
//     playing (the switch waits for the end of the pattern), it blinks with
//     an outline.
//   - A small dot marks patterns that have any steps.
//   - Right click on a pattern: copy, paste or clear.
//   - Rec: while the DAW plays, whatever is played on the pads is recorded
//     into the playing pattern (overdub).
class PatternBarComponent final : public juce::Component,
                                  public juce::SettableTooltipClient
{
public:
    explicit PatternBarComponent (NzDrumProcessor& processor);

    // Called by the editor at 30 Hz.
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void resized() override;

private:
    static constexpr std::array<int, 4> lengths { 4, 8, 16, 32 };

    juce::Rectangle<float> patternBounds (int pattern) const;
    juce::Rectangle<float> lengthBounds (int index) const;
    bool patternHasSteps (int pattern) const;
    void showPatternMenu (int pattern);

    NzDrumProcessor& nzProcessor;
    juce::TextButton recButton { "Rec" };
    int selected = 0;
    int playing = 0;
    int length = NzDrumProcessor::defaultPatternLength;
    bool blinkOn = false;
    int blinkCounter = 0;
    std::array<bool, NzDrumProcessor::numPatterns> hasSteps {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternBarComponent)
};
