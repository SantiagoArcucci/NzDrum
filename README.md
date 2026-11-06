# NzDrum

Drum machine VST3 de 16 pads con secuenciador por pasos y tres niveles de dinámica por paso (bajo, normal y acento). Hecha en C++ con [JUCE](https://juce.com).

> Estado: **etapa 6**. Sampler de 16 pads con un banco de 16 patrones de 4, 8, 16 o 32 pasos, tres niveles por paso y swing global, sincronizado con el host. Por pad: volumen, pan, pitch, decay, inicio y fin del sample, mute y solo. Los patrones se encadenan automatizando el parámetro "Patrón" en el DAW, y todo se guarda con la sesión.

## Cómo se usa

- **Cargar un sample:** arrastrá un archivo de audio (WAV, AIFF, FLAC, Ogg o MP3) sobre un pad, o usá el botón `...`.
- **Recorrer la carpeta:** con `<` y `>` pasás al archivo anterior o siguiente de la misma carpeta, en el mismo orden que Finder. Al llegar al final vuelve al principio.
- **Secuenciar:** tocar un pad lo selecciona, y abajo aparece la fila de 16 pasos de ese pad. Click pone un paso en nivel normal, Shift-click en acento y Option-click en bajo; repetir el mismo click lo apaga. La rueda del mouse sobre un paso prendido le cambia el semitono (de −8 a +7, el mismo rango que los 16 niveles); el número aparece en el paso. El secuenciador sigue el play y el tempo del DAW.
- **Patrones:** la fila "Patrón" elige uno de los 16 patrones del banco; los que tienen algún paso llevan un puntito. Si cambiás de patrón mientras suena, el cambio espera a que termine el patrón actual (el que sigue sonando parpadea). Con el DAW detenido, cambia enseguida.
- **Encadenar patrones (canción):** el patrón elegido es el parámetro del host **Patrón** (1 a 16). En Studio Pro, abrí la automatización de la pista de NzDrum, elegí "Patrón" y dibujá en qué patrón está cada tramo. También podés grabarlo: con la automatización en modo escritura, los clicks en los botones de patrón quedan grabados. El cambio entra al terminar el patrón que suena, así que conviene poner cada cambio sobre la línea de compás o un poco antes. Si cae apenas después, espera una vuelta entera del patrón.
- **Largo:** los botones "Pasos" ponen el largo del patrón elegido en 4, 8, 16 o 32 semicorcheas. Con 32, los pasos se muestran en dos filas.
- **Copiar, pegar y borrar patrones:** click derecho (o Control-click) en un botón de patrón. Copiar guarda una foto de los pasos y el largo; borrar apaga todos los pasos y vuelve a 16.
- **Tocar un pad cromáticamente ("16 niveles"):** seleccioná un pad y prendé `16 niveles`, al lado de las perillas. Los 16 pads tocan ese sample, un semitono por pad: el pad 9 en su afinación original, del 1 al 8 más grave y del 10 al 16 más agudo. Funciona con clicks y con MIDI; el secuenciador sigue tocando los pads normales. Mientras está prendido, tocar un pad no cambia la selección.
- **Copiar un pad en otro:** click derecho en un pad. Se copian el audio y los ajustes (volumen, pan, pitch, decay, inicio y fin), pero no el mute ni el solo ni los pasos.
- **Swing:** el control del encabezado vale para todo el banco. 50 % es recto y 75 % es el shuffle más marcado; corre las semicorcheas pares (2, 4, 6...), como en una MPC. Es un parámetro del host.
- **Ajustar el pad seleccionado:** abajo de todo está la forma de onda del sample, con dos marcadores naranjas que se arrastran: dónde empieza y dónde termina. También se mueven con la rueda del mouse sobre la forma de onda: rueda sola para el inicio, con Shift para el fin y con Alt más fino. Al lado, perillas de volumen, pan, pitch (±24 semitonos) y decay (`off` = suena entero). El inicio y el fin se guardan como proporción del archivo, así que se mantienen al pasar a otro sample con `<` y `>`. Doble click en una perilla la vuelve a su valor inicial. En el cuadro de texto de cada perilla se puede escribir un valor, con o sin unidad (`-6 dB`, `250 ms`, `30 L`).
- **Mute y solo:** cada pad tiene botones `M` y `S`. Un pad muteado no suena por ningún lado, ni con el secuenciador, ni por MIDI, ni con click. Si hay pads en solo, suenan solo esos (salvo que además estén muteados). El cambio se suaviza en 5 ms para que no haga click.
- **Automatizar:** todos los ajustes por pad (incluidos mute y solo) son parámetros del host, así que se pueden automatizar en Studio Pro.
- **Guardar:** al guardar la sesión de Studio Pro se guardan los 16 patrones con sus largos, el swing, el patrón elegido, los ajustes de cada pad y la **ruta** del sample de cada pad (el audio no se copia adentro de la sesión). Si al abrir la sesión un archivo ya no está, el pad muestra "Falta: nombre" en rojo y queda mudo; con `...`, `<` o `>` lo volvés a cargar. El portapapeles no se guarda.
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
