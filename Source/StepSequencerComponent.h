#pragma once

#include "PluginProcessor.h"

// Los pasos del pad seleccionado, en el patrón elegido. Hasta 16 pasos van
// en una fila; con 32, en dos.
//   click         -> normal (si ya era normal, lo apaga)
//   Shift + click -> acento
//   Option + click (Alt) -> bajo
// Los modificadores vienen dentro del mismo evento de mouse, así que el DAW no
// puede "robarlos" como a veces pasa con atajos de teclado sueltos.
class StepSequencerComponent final : public juce::Component
{
public:
    explicit StepSequencerComponent (NzDrumProcessor& processor);

    void setPad (int newPad);
    int getPad() const noexcept { return pad; }

    // La llama el editor a 30 Hz: sigue el patrón elegido, su largo y el paso
    // que suena.
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    // Qué nivel queda al hacer click con estos modificadores sobre un paso
    // que tenía `current`. Es una función pura para poder probarla sola.
    static StepLevel levelAfterClick (StepLevel current, const juce::ModifierKeys& modifiers) noexcept;

private:
    juce::Rectangle<float> stepBounds (int step) const;
    int stepAt (juce::Point<float> position) const;

    NzDrumProcessor& nzProcessor;
    int pad = 0;
    int pattern = 0;
    int length = NzDrumProcessor::defaultPatternLength;
    int playingStep = -1;   // -1 si no suena este patrón

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepSequencerComponent)
};
