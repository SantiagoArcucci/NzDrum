#pragma once

#include "PluginProcessor.h"
#include "PadComponent.h"
#include "StepSequencerComponent.h"
#include "PadEditorComponent.h"
#include "PatternBarComponent.h"

// The plugin window. It lives on the UI thread and never touches audio
// directly: it asks the processor for things and reads what it publishes.
class NzDrumEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit NzDrumEditor (NzDrumProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Polls at 30 Hz, a bit like a slower requestAnimationFrame.
    void timerCallback() override;
    void selectPad (int pad);

    NzDrumProcessor& nzProcessor;
    std::vector<std::unique_ptr<PadComponent>> pads;
    PatternBarComponent patternBar { nzProcessor };
    StepSequencerComponent sequencer { nzProcessor };
    PadEditorComponent padEditor { nzProcessor };
    juce::TooltipWindow tooltips { this };

    // Global swing, in the header.
    juce::Label swingLabel { {}, "Swing" };
    juce::Slider swingSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::AudioProcessorValueTreeState::SliderAttachment swingAttachment { nzProcessor.getParameters(), "swing", swingSlider };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NzDrumEditor)
};
