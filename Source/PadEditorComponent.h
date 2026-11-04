#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// Forma de onda del pad seleccionado con dos marcadores, inicio y fin, que se
// arrastran con el mouse. Los marcadores son parámetros del host, así que
// también se mueven solos si el DAW los automatiza.
class WaveformView final : public juce::Component,
                           private juce::ChangeListener
{
public:
    explicit WaveformView (NzDrumProcessor& processor);
    ~WaveformView() override;

    void setPad (int pad);
    void refresh();   // la llama el editor a 30 Hz: detecta si cambió el archivo

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    enum class Marker { none, start, end };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    float xFor (float proportion) const;
    float proportionAt (float x) const;
    Marker markerNear (float x) const;
    juce::ParameterAttachment& attachmentFor (Marker marker);

    NzDrumProcessor& nzProcessor;
    juce::AudioThumbnailCache thumbnailCache { 8 };
    juce::AudioThumbnail thumbnail;
    juce::File shownFile;

    int pad = 0;
    float start = 0.0f, end = 1.0f;
    Marker dragging = Marker::none;

    // Conectan cada marcador con su parámetro: avisan al host del gesto
    // (para que grabe automatización) y nos avisan si el host lo cambia.
    std::unique_ptr<juce::ParameterAttachment> startAttachment, endAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};

// Panel inferior: forma de onda más perillas de volumen, pan, pitch y decay
// del pad seleccionado.
class PadEditorComponent final : public juce::Component
{
public:
    explicit PadEditorComponent (NzDrumProcessor& processor);

    void setPad (int pad);
    void refresh() { waveform.refresh(); }

    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        const char* parameterName = "";
    };

    NzDrumProcessor& nzProcessor;
    WaveformView waveform;
    std::array<Knob, 4> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadEditorComponent)
};
