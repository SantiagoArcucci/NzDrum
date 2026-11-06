#pragma once

#include "PluginProcessor.h"

// La barra de patrones: 16 botones para elegir patrón y 4 para su largo.
//   - El patrón elegido se pinta lleno. Si todavía suena el anterior
//     (el cambio espera al final del patrón), ese parpadea con un borde.
//   - Un puntito marca los patrones que tienen algún paso.
//   - Click derecho en un patrón: copiar, pegar o borrar.
//   - Rec: con el DAW en play, lo que se toca en los pads se graba en el
//     patrón que suena (overdub).
class PatternBarComponent final : public juce::Component,
                                  public juce::SettableTooltipClient
{
public:
    explicit PatternBarComponent (NzDrumProcessor& processor);

    // La llama el editor a 30 Hz.
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
