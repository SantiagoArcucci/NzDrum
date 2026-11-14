// Renders the plugin UI to a PNG without a DAW, to check the look:
//   NzDrumTests --snapshot out.png [scale]
// It loads generated samples on most pads and fills a pattern, so every
// state of the design shows up.

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

namespace
{
    // A short decaying noise burst, like a drum hit.
    juce::File writeHit (const juce::File& folder, const juce::String& name, int length, float tone)
    {
        const auto file = folder.getChildFile (name + ".wav");
        juce::AudioBuffer<float> audio (1, length);
        juce::Random random (length);

        for (int i = 0; i < length; ++i)
        {
            const float envelope = std::exp (-6.0f * (float) i / (float) length);
            const float noise = random.nextFloat() * 2.0f - 1.0f;
            const float sine = std::sin ((float) i * tone);
            audio.setSample (0, i, 0.9f * envelope * (0.5f * noise + 0.5f * sine));
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (44100.0)
                                                                                 .withNumChannels (1)
                                                                                 .withBitsPerSample (16));
        writer->writeFromAudioSampleBuffer (audio, 0, length);
        return file;
    }
}

int runSnapshot (const juce::File& output, float scale)
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NzDrumSnapshot");
    folder.deleteRecursively();
    folder.createDirectory();

    NzDrumProcessor p;
    p.prepareToPlay (48000.0, 512);

    const char* names[] { "Kick", "Kick 2", "Snare Tight", "Rim", "Clap", "CH Hat", "OH Hat", "Ride",
                          "Tom Lo", "Tom Mid", "Tom Hi", "Crash", "Shaker", nullptr, "Vox", nullptr };

    for (int pad = 0; pad < NzDrumProcessor::numPads; ++pad)
        if (names[pad] != nullptr)
            p.loadSample (pad, writeHit (folder, names[pad], 8000 + pad * 1500, 0.02f + 0.01f * (float) pad));

    for (int i = 0; i < 300 && [&] { for (int pad = 0; pad < 16; ++pad) if (p.isLoading (pad)) return true; return false; }(); ++i)
        juce::Thread::sleep (10);

    // Pattern 1: a beat on pad 3 with every level and two semitones.
    p.setStep (0, 2, 4, StepLevel::normal);
    p.setStep (0, 2, 7, StepLevel::low);
    p.setStep (0, 2, 10, StepLevel::low);
    p.setStep (0, 2, 12, StepLevel::normal);
    p.setStepPitch (0, 2, 12, -3);
    p.setStep (0, 2, 13, StepLevel::normal);
    p.setStep (0, 2, 15, StepLevel::accent);
    p.setStepPitch (0, 2, 15, 2);
    p.setStep (1, 0, 0, StepLevel::normal);
    p.setStep (4, 0, 0, StepLevel::normal);

    auto& params = p.getParameters();
    params.getParameter ("swing")->setValueNotifyingHost (params.getParameter ("swing")->convertTo0to1 (56.0f));
    params.getParameter (NzDrumProcessor::paramId (12, "mute"))->setValueNotifyingHost (1.0f);
    params.getParameter (NzDrumProcessor::paramId (2, "level"))->setValueNotifyingHost (
        params.getParameter (NzDrumProcessor::paramId (2, "level"))->convertTo0to1 (-6.0f));
    params.getParameter (NzDrumProcessor::paramId (2, "start"))->setValueNotifyingHost (0.06f);
    params.getParameter (NzDrumProcessor::paramId (2, "end"))->setValueNotifyingHost (0.72f);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    auto* nzEditor = dynamic_cast<NzDrumEditor*> (editor.get());
    nzEditor->setSize (MainView::designWidth, MainView::designHeight);
    nzEditor->getView().selectPad (2);
    nzEditor->getView().refreshAll();

    // The waveform thumbnail reads the file on its own thread.
    juce::Thread::sleep (1000);
    nzEditor->getView().refreshAll();

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
    output.deleteFile();
    juce::FileOutputStream stream (output);
    juce::PNGImageFormat png;
    const bool ok = stream.openedOk() && png.writeImageToStream (image, stream);

    editor.reset();
    folder.deleteRecursively();
    return ok ? 0 : 1;
}
