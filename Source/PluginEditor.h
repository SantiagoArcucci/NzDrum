#pragma once

#include "PluginProcessor.h"
#include "PadComponent.h"
#include "StepSequencerComponent.h"
#include "PadEditorComponent.h"
#include "PatternBarComponent.h"

// La ventana del plugin. Vive en el hilo de UI y nunca toca el audio
// directamente: le pide cosas al procesador y lee lo que este publica.
class NzDrumEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit NzDrumEditor (NzDrumProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Polling a 30 Hz, parecido a un requestAnimationFrame más lento.
    void timerCallback() override;
    void selectPad (int pad);

    NzDrumProcessor& nzProcessor;
    std::vector<std::unique_ptr<PadComponent>> pads;
    PatternBarComponent patternBar { nzProcessor };
    StepSequencerComponent sequencer { nzProcessor };
    PadEditorComponent padEditor { nzProcessor };
    juce::TooltipWindow tooltips { this };

    // Swing global, en el encabezado.
    juce::Label swingLabel { {}, "Swing" };
    juce::Slider swingSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::AudioProcessorValueTreeState::SliderAttachment swingAttachment { nzProcessor.getParameters(), "swing", swingSlider };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NzDrumEditor)
};
