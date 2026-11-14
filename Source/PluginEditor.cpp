#include "PluginEditor.h"

NzDrumEditor::NzDrumEditor (NzDrumProcessor& p)
    : AudioProcessorEditor (p), nzProcessor (p)
{
    // The view must be a child before the look and feel is set, so its
    // components pick it up (sliders rebuild their text boxes with it).
    addAndMakeVisible (view);
    setLookAndFeel (&lookAndFeel);

    constexpr double aspect = (double) MainView::designWidth / (double) MainView::designHeight;
    setResizable (true, true);
    setResizeLimits (minWidth, (int) std::round (minWidth / aspect), maxWidth, (int) std::round (maxWidth / aspect));
    getConstrainer()->setFixedAspectRatio (aspect);

    // The size is saved with the session, so the window opens as it was left.
    const int width = initialWidth (nzProcessor.getEditorWidth());
    setSize (width, (int) std::round (width / aspect));
}

NzDrumEditor::~NzDrumEditor()
{
    setLookAndFeel (nullptr);
}

void NzDrumEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainView::designWidth;
    view.setTransform (juce::AffineTransform::scale (scale));
    nzProcessor.setEditorWidth (getWidth());
}
