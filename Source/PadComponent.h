#pragma once

#include "PluginProcessor.h"

// Un pad de la grilla: muestra el archivo cargado, suena con un click y acepta
// archivos arrastrados. Los botones de abajo cargan un archivo o recorren la
// carpeta del sample actual. Arriba a la derecha están mute (M) y solo (S).
// Click derecho: copiar o pegar el pad (audio y ajustes).
class PadComponent final : public juce::Component,
                           public juce::FileDragAndDropTarget
{
public:
    PadComponent (NzDrumProcessor& processor, int padIndex);

    // La llama el editor a 30 Hz para refrescar nombre, estado y destello.
    void refresh();

    // Tocar un pad también lo selecciona, como en una MPC: la fila de pasos
    // muestra el pad seleccionado.
    std::function<void (int pad)> onSelect;
    void setSelected (bool shouldBeSelected);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void openFileChooser();
    void showPadMenu();

    NzDrumProcessor& nzProcessor;
    const int pad;

    juce::TextButton previousButton { "<" }, loadButton { "..." }, nextButton { ">" };
    std::unique_ptr<juce::FileChooser> chooser;

    // Mute y solo son parámetros del host: los attachments mantienen el botón
    // y el parámetro sincronizados en los dos sentidos (incluida la
    // automatización).
    juce::TextButton muteButton { "M" }, soloButton { "S" };
    juce::AudioProcessorValueTreeState::ButtonAttachment muteAttachment, soloAttachment;

    juce::String fileName;
    bool loading = false;
    bool dragHover = false;
    bool selected = false;
    bool audible = true;
    uint32_t lastTriggerCount = 0;
    float flash = 0.0f;   // 1 al disparar, se apaga de a poco

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadComponent)
};
