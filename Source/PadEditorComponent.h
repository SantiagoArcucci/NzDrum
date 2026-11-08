#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// Waveform of the selected pad with two markers, start and end, that can be
// dragged with the mouse (or moved with the wheel). The markers are host
// parameters, so they also move on their own when the DAW automates them.
class WaveformView final : public juce::Component,
                           private juce::ChangeListener
{
public:
    explicit WaveformView (NzDrumProcessor& processor);
    ~WaveformView() override;

    void setPad (int pad);
    void refresh();   // called by the editor at 30 Hz: detects a file change

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Marker { none, start, end };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    float xFor (float proportion) const;
    float proportionAt (float x) const;
    Marker markerNear (float x) const;
    void moveMarker (Marker marker, float value);
    juce::ParameterAttachment& attachmentFor (Marker marker);

    NzDrumProcessor& nzProcessor;
    juce::AudioThumbnailCache thumbnailCache { 8 };
    juce::AudioThumbnail thumbnail;
    juce::File shownFile;

    int pad = 0;
    float start = 0.0f, end = 1.0f;
    Marker dragging = Marker::none;

    // Connect each marker to its parameter: they tell the host about the
    // gesture (so it can record automation) and tell us when the host changes it.
    std::unique_ptr<juce::ParameterAttachment> startAttachment, endAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};

// Bottom panel: waveform plus level, pan, pitch and decay knobs for the
// selected pad, and the 16 levels (chromatic) mode button.
class PadEditorComponent final : public juce::Component
{
public:
    explicit PadEditorComponent (NzDrumProcessor& processor);

    void setPad (int pad);
    void refresh();

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
    juce::TextButton chromaticButton { "16 Levels" };
    int currentPad = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadEditorComponent)
};
