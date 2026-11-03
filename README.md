# NzDrum

Drum machine VST3 de 16 pads con secuenciador por pasos y tres niveles de dinámica por paso (bajo, normal y acento). Hecha en C++ con [JUCE](https://juce.com).

> Estado: **etapa 1**. Por ahora el plugin toca un blip de prueba por cada nota MIDI y muestra el tempo y la posición del host. Sirve para validar que el toolchain y la carga en el DAW funcionan.

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

## Probar en Fender Studio Pro

1. Compilá como arriba y reiniciá Studio Pro (o volvé a escanear plugins).
2. Arrastrá **NzDrum** a una pista de instrumento.
3. Tocá notas desde un teclado MIDI o un clip: cada nota suena como un blip corto.
4. Dale play: en la ventana del plugin tienen que aparecer el tempo y la posición en negras.

## CI

Cada push y pull request compila el VST3 universal en GitHub Actions, lo valida con [pluginval](https://github.com/Tracktion/pluginval) y lo deja como artifact descargable. Ese binario no está firmado ni notarizado: para usarlo, macOS te va a pedir habilitarlo a mano (`xattr -dr com.apple.quarantine NzDrum.vst3`). Para uso propio conviene compilarlo localmente.

## Licencia

El código de este repo es GPLv3 (ver `LICENSE`). JUCE se usa bajo AGPLv3, así que el plugin compilado se distribuye bajo los términos de la AGPLv3.
