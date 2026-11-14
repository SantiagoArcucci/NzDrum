#pragma once

#include "PluginProcessor.h"
#include "MainView.h"
#include "Theme.h"

// The plugin window. It holds the UI at its design size and scales it to
// whatever size the window has, keeping the proportions. The UI lives on the
// UI thread and never touches audio directly: it asks the processor for
// things and reads what it publishes.
class NzDrumEditor final : public juce::AudioProcessorEditor
{
public:
    explicit NzDrumEditor (NzDrumProcessor&);
    ~NzDrumEditor() override;

    void resized() override;

    MainView& getView() noexcept { return view; }

    static constexpr int minWidth = 540;    // 75 %
    static constexpr int maxWidth = 1440;   // 200 %

    // The width the window opens at, from the one saved with the session.
    static int initialWidth (int savedWidth) noexcept { return juce::jlimit (minWidth, maxWidth, savedWidth); }

private:
    NzDrumProcessor& nzProcessor;
    NzLookAndFeel lookAndFeel;
    MainView view { nzProcessor };
    juce::TooltipWindow tooltips { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NzDrumEditor)
};
