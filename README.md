# NzDrum

Drum machine VST3 de 16 pads con secuenciador por pasos y tres niveles de dinámica por paso (bajo, normal y acento). Hecha en C++ con [JUCE](https://juce.com).

> Estado: **etapa 2**. Sampler de 16 pads: cada pad carga un archivo de audio y suena con un click o con una nota MIDI. Todavía no hay secuenciador (llega en la etapa 3) y los samples cargados no se guardan con la sesión (etapa 6).

## Cómo se usa

- **Cargar un sample:** arrastrá un archivo de audio (WAV, AIFF, FLAC, Ogg o MP3) sobre un pad, o usá el botón `...`.
- **Recorrer la carpeta:** con `<` y `>` pasás al archivo anterior o siguiente de la misma carpeta, en el mismo orden que Finder. Al llegar al final vuelve al principio.
- **Tocar:** click en el pad, o notas MIDI. El pad 1 es la nota 36 (C1 en Studio Pro) y el 16 es la 51 (D#2). La grilla sigue la disposición de una MPC: el pad 1 abajo a la izquierda.

## Requisitos (macOS)

- macOS 11 o superior (Apple Silicon o Intel)
- Xcode con las Command Line Tools (`xcode-select --install`)
- CMake 3.22 o superior (`brew install cmake`)

JUCE no hace falta instalarlo: CMake lo descarga solo la primera vez, fijado a la versión 9.0.5.

## Compilar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target NzDrum_VST3 --parallel
```

La primera configuración tarda un poco porque descarga JUCE. Al terminar, el plugin se copia solo a `~/Library/Audio/Plug-Ins/VST3/NzDrum.vst3`. Para no copiarlo, configurá con `-DNZDRUM_COPY_AFTER_BUILD=OFF`.

También se genera una app standalone, útil para probar sin abrir el DAW:

```bash
cmake --build build --target NzDrum_Standalone --parallel
open build/NzDrum_artefacts/Release/Standalone/NzDrum.app
```

Si preferís depurar con Xcode: `cmake -S . -B build-xcode -G Xcode` y abrí `build-xcode/NzDrum.xcodeproj`.

## Pruebas

Las pruebas del motor cargan samples reales, tocan notas MIDI y revisan el audio que sale, sin necesidad de un DAW:

```bash
cmake --build build --target NzDrumTests --parallel
./build/NzDrumTests_artefacts/Release/NzDrumTests
```

## Probar en Fender Studio Pro

1. Compilá como arriba y reiniciá Studio Pro (o volvé a escanear plugins).
2. Arrastrá **NzDrum** a una pista de instrumento.
3. Cargá samples en algunos pads y tocalos con click o con notas desde C1.

## CI

Cada push y pull request compila el VST3 universal en GitHub Actions, corre las pruebas del motor, lo valida con [pluginval](https://github.com/Tracktion/pluginval) y lo deja como artifact descargable. Ese binario no está firmado ni notarizado: para usarlo, macOS te va a pedir habilitarlo a mano (`xattr -dr com.apple.quarantine NzDrum.vst3`). Para uso propio conviene compilarlo localmente.

## Licencia

El código de este repo es GPLv3 (ver `LICENSE`). JUCE se usa bajo AGPLv3, así que el plugin compilado se distribuye bajo los términos de la AGPLv3.
