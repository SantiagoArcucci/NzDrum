#pragma once

#include "PluginProcessor.h"
#include "PadComponent.h"
#include "StepSequencerComponent.h"
#include "PadEditorComponent.h"
#include "PatternBarComponent.h"

// The LCD in the header: playing pattern, the one coming next, transport and
// position in the song.
class TransportDisplay final : public juce::Component
{
public:
    explicit TransportDisplay (NzDrumProcessor& processor) : nzProcessor (processor) {}

    void refresh();   // 30 Hz
    void paint (juce::Graphics&) override;

    // "BAR 2 · 3.25" for a position in quarter notes and a time signature.
    static juce::String positionText (double ppq, int numerator, int denominator);

private:
    NzDrumProcessor& nzProcessor;
    juce::String lines[4];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportDisplay)
};

// The whole plugin UI, laid out at the size of the design (720 x 900). The
// editor scales it to the window size.
class MainView final : public juce::Component,
                       private juce::Timer
{
public:
    static constexpr int designWidth = 720;
    static constexpr int designHeight = 900;

    explicit MainView (NzDrumProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    void selectPad (int pad);

    // The editor draws this view scaled. Mouse movement arrives in unscaled
    // coordinates, so knobs need less of it at bigger sizes to feel the same.
    void setScale (float scale);

    // Reads the processor's state into every component. Called at 30 Hz.
    void refreshAll();

private:
    // Polls at 30 Hz, a bit like a slower requestAnimationFrame.
    void timerCallback() override { refreshAll(); }

    NzDrumProcessor& nzProcessor;
    std::vector<std::unique_ptr<PadComponent>> pads;
    int selectedPad = 0;
    PatternBarComponent patternBar { nzProcessor };
    StepSequencerComponent sequencer { nzProcessor };
    PadEditorComponent padEditor { nzProcessor };
    TransportDisplay transport { nzProcessor };

    // Global swing, in the header.
    juce::Slider swingSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxRight };
    juce::AudioProcessorValueTreeState::SliderAttachment swingAttachment { nzProcessor.getParameters(), "swing", swingSlider };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
