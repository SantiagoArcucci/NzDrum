#include "PluginEditor.h"

NzDrumEditor::NzDrumEditor (NzDrumProcessor& p)
    : AudioProcessorEditor (p), nzProcessor (p)
{
    setSize (520, 240);
    startTimerHz (30);
}

void NzDrumEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1d22));

    auto area = getLocalBounds().reduced (24);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText ("NzDrum", area.removeFromTop (48), juce::Justification::centredLeft);

    const auto transport = nzProcessor.getTransport();

    juce::String info;
    info << (transport.playing ? "Play" : "Stop")
         << "   |   " << juce::String (transport.bpm, 1) << " BPM"
         << "   |   negra " << juce::String (transport.ppq, 2);

    g.setFont (juce::FontOptions (18.0f));
    g.drawText (info, area.removeFromTop (40), juce::Justification::centredLeft);

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Etapa 1: toca notas MIDI para escuchar un blip de prueba.",
                area.removeFromTop (28), juce::Justification::centredLeft);
}
