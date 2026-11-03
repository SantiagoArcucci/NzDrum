#pragma once

#include "PluginProcessor.h"

// La ventana del plugin. Vive en el hilo de UI (el "main thread", como en el
// navegador) y nunca toca el audio directamente: solo lee lo que el
// procesador publica en atómicos.
class NzDrumEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit NzDrumEditor (NzDrumProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    // Polling a 30 Hz, parecido a un requestAnimationFrame más lento.
    void timerCallback() override { repaint(); }

    NzDrumProcessor& nzProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NzDrumEditor)
};
