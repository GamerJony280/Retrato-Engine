# Importación y animación

## Qué admite cada formato

| Entrada | Geometría/jerarquía | Material de color y textura | Huesos/pesos | Clips |
|---|---|---|---|---|
| OBJ + MTL | Sí; triangulación mediante ufbx, normales/UV | Kd/map_Kd y UV; transparencia aproximada por recorte | El formato no transporta nuestro sistema esquelético | No |
| FBX binario/ASCII | Sí; unidades a metros, Y arriba; transformaciones geométricas e herencia con nodos auxiliares | Color base, emisión constante, textura externa/incrustada, transformaciones UV | Skinning lineal, cuatro influencias | Curvas horneadas a pistas TRS |
| glTF 2.0 / GLB | Sí; escena principal, nodos, triángulos/listas/tiras/abanicos | Factor base, color de vértice RGB, textura, emisión constante, unlit y KHR_texture_transform | Skinning lineal, cuatro influencias | STEP, LINEAR, CUBICSPLINE de traslación/rotación/escala |
| `.embasset` | Recursos precocinados propios | Incluidos | Incluidos | Incluidos |

PNG, JPEG, BMP y TGA están habilitados para las texturas de **modelos**. La UI conserva su adaptador de imágenes BMP independiente. Las texturas glTF pueden ser archivos externos, data URI base64 o estar dentro de GLB. No necesitas extraer manualmente los PNG de un GLB.

Hay muestreo nearest para normal/PS1/pixel y filtrado de tres muestras para N64. Los modos de repetición/clamp/mirror de glTF se preservan. No hay mipmaps, compresión de textura GPU ni reproducción físicamente exacta de los materiales PBR. El shader usa directamente los valores de color para el aspecto retro; no es un flujo PBR con gestión completa de color.

## Preparar un modelo

1. Crea y anima en tu herramienta habitual. Para personajes, exporta huesos, pesos y clips con nombres claros (`Idle`, `Walk`, `Attack`).
2. Hornea las restricciones/IK/controladores del rig a movimientos de huesos. Ember reproduce el esqueleto exportado; no incorpora un editor de rigging ni el evaluador de todos los modificadores de tu programa.
3. Exporta glTF/GLB o FBX. Con OBJ, conserva junto al modelo el MTL y sus imágenes.
4. Inspecciona el contenido y sus advertencias:

```sh
./build/cpu/ember_import personaje.fbx
```

Se imprimen número de vértices/triángulos, nodos con sus padres, memoria de imágenes y nombres/duración de clips. Los identificadores numéricos de nodos no tienen por qué coincidir con los del archivo original: internamente están ordenados padre antes que hijo. Usa sus nombres para acceder a huesos.

```sh
./build/cpu/ember_demo --backend cpu --model personaje.fbx \
  --clip "nombre exacto del clip" --time 0.3 --out personaje.ppm
```

En el visor sin `--clip`, se reproduce el primer clip si existe. `--time` selecciona una pose inicial para `--model`; no adelanta la simulación de un script Lua.

## Precocinar para el juego final

```sh
./build/cpu/ember_import personaje.fbx --out personaje.embasset
./build/cpu/ember_demo --backend cpu --model personaje.embasset \
  --clip "nombre exacto del clip" --out personaje.ppm
```

El formato `.embasset` incluye mallas, jerarquía, materiales, píxeles de imágenes, matrices de enlace y clips. Se libera la representación del importador al terminar la importación. Los enteros/flotantes se serializan explícitamente en little-endian; el lector comprueba versión, índices, jerarquía, tamaños y valores finitos.

Para excluir los lectores FBX/glTF/PNG del ejecutable, compila con `-DEMBER_IMPORTERS=OFF` o usa el preset `runtime`. Ese ejecutable carga `.embasset`; los formatos de autoría se convierten previamente. El antiguo cocinador de colisiones `ember_cook` sigue siendo independiente y recibe OBJ.

**No renombres un FBX a `.embasset`: hay que convertirlo.** Es un formato propio versionado, no una promesa de compatibilidad permanente con futuras versiones.

## Presupuestos de importación

Los valores por defecto de `ImportOptions` son límites por recurso, no objetivos de rendimiento:

- 100 000 vértices y 100 000 triángulos como máximos independientes; se cuentan los vértices expandidos por discontinuidades de normales/UV y por primitivas.
- 2 048 nodos.
- Texturas hasta 2 048 × 2 048 y 64 MiB de píxeles RGBA en total por recurso.
- Horneado FBX a hasta 30 muestras por segundo, con reducción de claves. Los movimientos se interpolan durante la reproducción.

Los presupuestos máximos no son objetivos de rendimiento. El contenido debe ajustarse al dispositivo destino.

```sh
./build/cpu/ember_import personaje.fbx --max-vertices 20000 \
  --texture-limit 512 --bake-hz 30 --out personaje.embasset
```

Las opciones de importación están en la herramienta/API C++, no mezcladas con las cuatro secciones de configuración del juego. El límite de textura no reduce imágenes automáticamente: informa del exceso para que el autor elija cómo reducirlas. El límite de memoria del buffer de texturas de la GPU también puede rechazar una escena que exceda sus capacidades.

## Uso desde Lua

```lua
local actor
function start()
    engine.clear()
    actor = engine.mesh("assets/robot.glb", 0, 0, 0)
    engine.play(actor, "Walk", 0, true)
    engine.camera(4, 3, -6, 0, 1, 0)
end
function key_down(time, key)
    if key == 119 then -- W
        engine.play(actor, "Wave", 0.2, true)
    end
end
```

El runtime avanza las animaciones después de los eventos de cada paso fijo. No necesitas escribir un bucle que deforme cada vértice.

| Función Lua | Uso |
|---|---|
| `engine.mesh(path,x,y,z)` | Carga cualquiera de los formatos admitidos y devuelve un objeto |
| `engine.clips(id)` | Devuelve una tabla con los nombres de clips |
| `engine.play(id,name,fade_seconds,loop)` | Reproduce; fade por defecto 0, loop por defecto true |
| `engine.seek(id,seconds)` | Selecciona el tiempo del clip; un clip en bucle envuelve su duración |
| `engine.pause_animation(id,true)` | Congela la pose de ese objeto |
| `engine.bone(id,name)` | Posición del origen del nodo/hueso en el mundo |
| `engine.attach(child,parent,bone)` | Une un objeto a un hueso; posición/yaw del hijo funcionan como ajuste local |

Las transiciones mezclan la pose de partida congelada hacia el clip entrante; no evalúan dos clips vivos simultáneamente. Las rotaciones lineales usan interpolación esférica de cuaterniones. No hay retargeting automático entre esqueletos diferentes, extracción automática de root motion, máscaras de capas ni blend trees.

`assets/animated_game.lua` muestra dos robots con datos compartidos y poses independientes, cambios de clip y una varilla unida a un brazo:

```sh
./build/desktop/ember_demo --backend cpu --window --lua assets/animated_game.lua
```

## Uso desde C++

```cpp
#include <ember/runtime.hpp>

ember::Runtime game(ember::Config{});
auto id = game.add_model("assets/robot.glb", {0, 0, 0});
auto& actor = game.scene.objects[id];
actor.animator->play("Walk", true, 0.15f);
// En el bucle de tu aplicación:
game.tick(1.0 / 60.0);
```

Para controlar directamente recursos y poses:

```cpp
auto asset = ember::import_asset("assets/robot.glb");
auto first = ember::instantiate(asset);
auto second = ember::instantiate(asset); // Asset compartido, Animator independiente
first.animator->play("Wave");
ember::update_animation(first, 1.0f / 60.0f);
auto hand = first.animator->node_transform("RightArm");
```

`node_transform()` devuelve una matriz en el espacio del recurso, incluyendo sus ancestros. Para colocar un objeto completamente orientado usa `Runtime::attach()`; obtener solo la posición no aplica su rotación. Los attachments no se anidan. No pueden llevar nuestros colliders de malla estáticos. Un collider cocinado de una malla animada es una instantánea y no sigue su deformación: hitboxes por hueso quedan para el módulo físico.

El C ABI añade `ember_add_model`, `ember_play`, `ember_seek`, `ember_pause_animation`, `ember_tick` y `ember_attach`. Conserva ABI v1 con funciones nuevas; compila nuevamente los consumidores de las estructuras C++ modificadas.

## Compatibilidad que todavía NO se promete

- Morph targets/blend shapes, simulación de telas, modificadores de geometría y constraints/IK en tiempo real. Se emiten avisos para morphs/constraints detectados.
- Dual quaternion de FBX: se aproxima con skinning lineal y se avisa.
- Más de cuatro influencias: FBX retiene las cuatro más fuertes; glTF conserva el primer conjunto JOINTS_0/WEIGHTS_0. Se normalizan y se avisa si hay conjuntos adicionales.
- Transparencia BLEND: recorte a 0.5, con advertencia; no mezcla ordenada de cristal/pelo translúcido. MASK conserva su umbral; las sombras respetan el recorte con muestreo nearest.
- Normal maps, mapas de oclusión/emisión/metallic-roughness y extensiones PBR avanzadas: el material retro no los reproduce. Se informa de mapas omitidos. Emisión constante sí se conserva.
- Extensiones glTF requeridas desconocidas: error explícito. `KHR_materials_unlit` y `KHR_texture_transform` están admitidas; Draco, Basis/KTX2, WebP y compresión meshopt no están integrados.
- Cámaras/luces de autoría no crean automáticamente cámaras/luces de juego.
- Ningún importador garantiza todos los archivos históricos o exportadores. Las pruebas incluidas definen la cobertura comprobada. Usa archivos de confianza; no es una sandbox de contenido hostil.
