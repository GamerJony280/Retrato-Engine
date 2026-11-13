# Retrato Engine, an AI Slop Vibe Coded Game Engine in C++, Lua, own language and more stuff.

**Motor modular C++17 para juegos 3D low-poly, gameplay 2.5D y animación en tiempo real.**

[English — referencia técnica principal](README.md) · [Español](README.es.md)

**0.7.0: primera versión de desarrollo.** Vulkan es el backend GPU prioritario;
OpenGL y CPU comparten la misma API. C++ puede implementar todo el juego. Lua 5.4
y **Nova**, el lenguaje del proyecto que genera C++ nativo, son opcionales y
pueden compartir el mismo Runtime con código C++.

El título es una broma. Los resultados y límites técnicos se describen por separado.

## Objetivo y capacidades

El motor está pensado para geometría moderada, iluminación estilizada y
presupuestos explícitos de recursos. Se puede usar en juegos low-poly 3D,
juegos de movimiento 2D con escenario 3D y escenas animadas en tiempo real.
La aplicación define sus reglas y estructura: no hay editor visual obligatorio,
ECS impuesto, servicios de cuenta ni lenguaje obligatorio.

| Área | Implementado | Límite |
|---|---|---|
| Render | Vulkan, OpenGL 3.3 y CPU; normal, PS1, N64, pixel y CRT | Materiales simplificados; preparación/sombras en CPU |
| Presentación de escritorio | GPU directa desde el adaptador de aplicación | Factorías offscreen con captura explícita; Android todavía copia píxeles |
| Recursos | OBJ/MTL, FBX, glTF/GLB y `.embasset` | No cubre todas las extensiones de los formatos |
| Animación | Esqueleto, skinning lineal, clips TRS y attachments | Sin IK general, retargeting ni grafo de animación |
| Colisión | BVH, raycast, barridos esfera/AABB y deslizamiento | No equivale a un solver rígido general |
| Física avanzada | Jolt opcional detrás de handles propios | Hay que activarlo al compilar y en ejecución |
| Lógica | Entidades gestionadas, entrada, eventos, señales y paso fijo | Las reglas pertenecen al juego |
| Efectos | VFX/partículas genéricos y pool de proyectiles | Sin simulación GPU, bloom ni blend aditivo |
| UI | RmlUi retenida opcional | Painter CPU; no es navegador |
| Guardado | Contenedor binario tipado, versionado y validado | No serializa automáticamente objetos C++ arbitrarios |
| Lenguajes | C++, Lua 5.4 y Nova 0.2 | Los puentes entre funciones son explícitos |

No están implementados audio integrado, streaming real, descomposición convexa
automática, editor completo, networking, hot reload complejo, swapchain Vulkan
Android ni composición automática de UI retenida en GPU.

## Compilar y ejecutar

Se necesitan compiladores C/C++17, CMake y un generador como Make/Ninja/Visual
Studio. El mínimo de CMake es 3.20; los presets requieren **3.21**.
El build no descarga dependencias. No ejecutes el juego con `sudo`.

Sin dependencias gráficas del sistema, desde la raíz:

```sh
cmake --preset cpu
cmake --build --preset cpu --parallel 2
ctest --preset cpu
```

Para construir solamente la biblioteca CPU con recursos precocinados:

```sh
cmake --preset runtime
cmake --build --preset runtime --parallel 2
```

Linux x86-64 con ventana: instala desarrollo SDL2 y Vulkan, cargador y controlador.

```sh
cmake --preset linux-x64 -DEMBER_STARTER=ON
cmake --build --preset linux-x64 --parallel 2
ctest --preset linux-x64
./build/linux-x64/templates/game/retrato_game --backend vulkan
```

Para retirar el límite de render impuesto por la aplicación:

```sh
./build/linux-x64/templates/game/retrato_game --backend vulkan --uncapped
```

La simulación conserva su paso fijo. Wayland, el compositor, controlador o modo
de presentación pueden seguir limitando lo mostrado. Más frames no aceleran
el juego. `--headless --frames 120` ejecuta simulación sin ventana y no mide GPU.

El preset `linux-x64-compat` añade OpenGL. `--backend auto` intenta Vulkan,
OpenGL compilado y CPU, en ese orden. Seleccionar explícitamente un backend
inexistente produce un error. Revisa los avisos de CMake para saber qué se compiló.
`desktop` añade más integraciones, incluidas dependencias de UI.

## Módulos e integración

Los headers/namespace conservan `ember` por continuidad de fuentes.

| Target | Responsabilidad |
|---|---|
| `retrato::engine` | Runtime, escena, recursos, animación, colisión y render |
| `retrato::app` | Ventana, entrada, pausa, reloj y presentación |
| `retrato::vfx` | Simulación de partículas y visuales por lotes |
| `retrato::ui` | UI retenida; integración desde fuentes |

Activa solamente lo necesario mediante `EMBER_VULKAN`, `EMBER_OPENGL`,
`EMBER_APP`, `EMBER_LUA`, `EMBER_NOVA`, `EMBER_IMPORTERS`, `EMBER_VFX`,
`EMBER_RMLUI` y `EMBER_JOLT`. `EMBER_TOOLS`, `EMBER_EXAMPLES` y `EMBER_TESTS`
controlan herramientas, aplicaciones de validación y pruebas.

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyGame LANGUAGES C CXX)
set(EMBER_APP ON CACHE BOOL "" FORCE)
set(EMBER_VIEWER OFF CACHE BOOL "" FORCE)
set(EMBER_TOOLS OFF CACHE BOOL "" FORCE)
set(EMBER_EXAMPLES OFF CACHE BOOL "" FORCE)
set(EMBER_TESTS OFF CACHE BOOL "" FORCE)
set(EMBER_LUA OFF CACHE BOOL "" FORCE)
set(EMBER_NOVA OFF CACHE BOOL "" FORCE)
set(EMBER_RMLUI OFF CACHE BOOL "" FORCE)
add_subdirectory(external/Retrato-Engine engine-build)
retrato_add_game(my_game SOURCES main.cpp)
```

Coloca las fuentes en esa ruta o cámbiala por la ubicación real. `main.cpp`
implementa `int retrato_game_main(int, char**)` y utiliza `ember::app::run` con
callbacks de inicio/actualización. El README inglés contiene el código completo.
No añadas otro `main()` a ese target. Una aplicación con ventana propia puede
enlazar solo el núcleo y controlar Runtime/SceneRenderer/Backend directamente.

Un SDK instalado se usa con `find_package(Retrato 0.7 CONFIG REQUIRED)` y
`CMAKE_PREFIX_PATH` apuntando a su ubicación real. Comprueba SO/arquitectura:
un SDK Linux x86 no es un SDK Windows o ARM. `templates/game` admite
`RETRATO_SOURCE_DIR`, `GAME_USE_NOVA` y `GAME_USE_LUA`. La UI aún no se exporta
como componente del SDK.

## Arquitectura y ownership

Asset es un recurso reutilizable; Object es una instancia colocada; Frame es la
representación preparada que consume el renderer. Runtime coordina escena,
cámara, eventos, animación, attachments y física opcional. Host ofrece servicios
de gameplay mediante handles opacos y comparte una sesión al copiarse.

`ObjectRef` combina slot e identidad. La destrucción gestionada invalida handles
y permite reutilizar slots; un handle viejo no revive al crear otro objeto.
Conserva handles en lugar de pointers permanentes a elementos de vectores.

`scene.objects.push_back` no usa la lista libre. `active=false` no libera un
slot gestionado; usa `visible=false` para ocultar. La capacidad y los recorridos
conservan el pico histórico: no hay compactación automática. Los recursos que
una instancia todavía posee sobreviven a la descarga de su caché.

La sesión se modifica desde un hilo; raster CPU y Jolt pueden usar trabajadores
internos. No cambies la escena mientras se prepara/dibuja. Conserva conexiones
RAII mientras necesites callbacks. Una excepción arbitraria no revierte cambios.

## Render y configuración

La ruta es `Scene + Camera + Config -> prepare -> Frame -> draw`.
`Backend::present` presenta una superficie propia o devuelve false para que el
adaptador muestre `readback()`. Los píxeles RGBA se devuelven con origen superior
izquierdo y memoria prestada del backend. El gameplay no recibe tipos de GPU/SDL.

Vulkan y OpenGL presentan directamente en GPU en la aplicación SDL. CPU utiliza
una textura. Capturar GPU sigue siendo explícito. `DrawVertex` ocupa 100 bytes y
`ShaderSettings` 96, con contratos de offsets/tamaños verificados en compilación.
El paquete se valida antes de consumirlo. La API propia cubre el pipeline del
motor, no todos los comandos arbitrarios de Vulkan/OpenGL.

El archivo predeterminado es `config/engine.toml`; un producto puede usar su
propio `content/engine.toml`. La aplicación elige la ruta: no se buscan otras
versiones del motor instaladas.

```toml
[graphics]
backend = "auto"
profile = "pixel"
filter = "normal"
resolution_quality = 35
lighting_quality = 35
shadow_distance = 32
shadow_quality = 20
toon_bands = 4
local_lights_per_object = 2
shadowed_lights = 1
pixel_upscale = true
ps1_depth_test = false

[collision_cooking]
quality = 70
static_shape = "mesh_bvh"
dynamic_shape = "convex_compound" # reservado

[physics]
backend = "none"
plane = "3d"
solver_quality = 40
fixed_hz = 60
continuous_collision = "selected"

[runtime]
worker_threads = -1
streaming = false # reservado; true tampoco activa streaming
```

| Control | Efecto |
|---|---|
| resolution_quality 1–100 | Ancho 160–960 por pasos de 16; altura 9/16. 35 = 432×243 |
| shadow_quality 1–100 | 1–33:128²; 34–66:256²; 67–100:512² |
| lighting_quality 1–100 | Cuadrícula de sombra pixel: 2–32 celdas/unidad |
| shadow_distance 4–128 | Región de sombra solar alrededor del objetivo de cámara |
| toon_bands 2–16 | Bandas direccionales del perfil pixel |
| local_lights_per_object 0–4 | Presupuesto de luces seleccionadas por objeto |
| shadowed_lights 0–1 | Sombra solar apagada/activa |
| pixel_upscale | Aplicación genérica: nearest si true; lineal si false |
| ps1_depth_test | Z opcional conservando los otros efectos PS1 |
| collision_cooking.quality 1–100 | Organización de hojas BVH; conserva geometría |
| physics.fixed_hz 15–240 | Frecuencia de paso fijo |
| worker_threads | 0: sin workers; -1: automático con hasta 3 adicionales; 1–64: explícito |

El parser es un subconjunto estricto de TOML; rechaza claves desconocidas,
duplicadas o fuera de rango. Las claves omitidas conservan sus valores por defecto.
Imagen, mapa de sombras y densidad artística de sombras son controles separados.

PS1 usa snap de vértices, UV afín, 5 bits, dithering y orden del pintor.
Los triángulos grandes/intersectados pueden superponerse incorrectamente.
`ps1_depth_test=true` activa oclusión Z; false conserva el modo clásico.
Normal usa perspectiva/profundidad; N64 añade filtrado de tres muestras y niebla;
pixel usa bandas toon y cuadrícula mundial de sombra. CRT añade scanlines y
máscara RGB, sin bloom, persistencia, curvatura o emulación analógica completa.

Transformaciones, animación, luces y construcción de sombra solar se calculan
en CPU. El backend elegido rasteriza. No hay PBR completo, ray tracing, sombras
de luces locales, mezcla transparente ordenada general ni instancing GPU.

## Recursos y animación

OBJ/MTL admite geometría y materiales simples, sin animación esquelética. FBX
ASCII/binario conserva jerarquía, texturas, skinning y clips horneados TRS.
glTF/GLB admite escena, materiales base, textura/color de vértice, unlit,
transformación UV y animación STEP/LINEAR/CUBICSPLINE. `.embasset` contiene el
recurso precocinado con jerarquía, materiales, imágenes y clips.

```sh
./build/cpu/ember_import character.glb --out character.embasset
./build/cpu/ember_demo --backend cpu --model character.embasset \
  --clip Walk --time 0.3 --out character.ppm
```

Usa un nombre de clip real. El PPM es una imagen, no una ventana interactiva.
`EMBER_IMPORTERS=OFF` permite distribuir solo el lector precocinado. Renombrar
un FBX no lo convierte. Los formatos propios son little-endian y versionados,
con validación de tamaños/índices/finitud.

Presupuestos por recurso: 100 mil vértices expandidos, 100 mil triángulos,
2048 nodos, texturas hasta 2048² y 64 MiB de píxeles en total. No son metas de
rendimiento. Se admiten PNG/JPEG/BMP/TGA de modelos. Skinning lineal con cuatro
influencias; hay que hornear constraints/IK antes de exportar. No se integran
morphs, telas, IK general, Draco, meshopt, Basis ni materiales PBR avanzados.
Transparencia BLEND se aproxima por recorte alpha. Los colliders cocinados no
siguen deformación de skinning ni ediciones posteriores.

## Colisión y física

El camino ligero proporciona CollisionMesh compacto, BVH/raycast, barridos
continuos esfera/AABB y deslizamiento. Los colliders no retienen materiales/UV.
Las cajas de Host son axis-aligned, O(n), con máscaras y sin heredar yaw/attachments.
Los barridos son conservadores en esquinas. La calidad BVH cambia coste y
organización, no precisión de superficie.

Jolt es opcional: `EMBER_JOLT=ON`, paquete/fuentes locales y
`physics.backend="jolt"`, más creación explícita de cuerpos. No forma parte de
animación/render ni de las consultas arcade. C++ expone box/sphere/capsule,
cuerpos estáticos/cinemáticos/dinámicos, fuerzas/impulsos/velocidad/material y CCD.
`plane=xy/xz/yz` limita grados de libertad para 2.5D; el render conserva 3D.

No se exponen aún callbacks generales de contacto/trigger, raycast de mundo Jolt,
joints ni cocinado de mallas/compuestos convexos. No se promete determinismo
bit a bit de Jolt entre plataformas. El README inglés detalla sus presupuestos.

## C++, Lua y Nova

C++ puede implementar todo. Host ofrece entidades/assets, transform, cámara,
luces, entrada, tiempo, señales, consultas y física opcional. C++ puede conservar
un `shared_ptr<Runtime>` y compartirlo con los otros lenguajes.

Lua 5.4 recibe callbacks de inicio, actualización, cues, teclado, mouse y gamepad.
`Host::load_script` posee el script y su conexión. Un `Script` directo necesita
forwarding de eventos y respetar la vida del Runtime. Su update recibe
`(time, code)`, no el delta de Nova. Lua abre bibliotecas estándar: solo scripts
confiables, no sandbox para mods.

Nova 0.2 genera C++17 nativo mediante lexer/parser/AST/semántica/codegen:

```text
var speed: float = 2.0
func update(delta: float) {
    transform.position.x += speed * delta
}
```

```sh
./build/cpu/nova/nova --emit-cpp gameplay.nova -o gameplay.generated.cpp
```

Admite int/float/bool/string/Vec2/Vec3 y handles; funciones tipadas, scopes,
if/else/while/return, imports locales, arrays fijos, operadores y asignaciones
compuestas. Usa llaves, no indentación significativa. No hay clases generales,
generics, GC, LLVM ni JIT.

`nova_add_module` integra generación y dependencias. C++ instancia Program sobre
Host y Entity; hay que conservarlo mientras se usan sus callbacks. `self` y
`transform` corresponden a la entidad controlada. Los builtins cubren entidades,
recursos, transform, entrada, tiempo/delta, señales, guardado, cámara/luces,
clips, queries arcade y física limitada. El protocolo de módulos es 2.
La compilación cruzada usa `NOVA_HOST_COMPILER` ejecutable en el anfitrión.

Los errores conservan archivo/línea/columna y mappings `#line`; se comprueban
aritmética y presupuestos de ejecución. Fallos nativos/toolchain todavía pueden
mostrar C++. Los escalares float usan double y los vectores float32.
Los tres lenguajes coexisten, pero las llamadas entre funciones requieren
bindings C++ explícitos o señales. No hay ABI dinámica ni hot reload complejo.

## Partículas, proyectiles, UI y guardado

VFX es un target opcional con pools acotados, emisión, recetas editables, fuerzas,
planos arbitrarios, drag, colisión continua, rebote/fricción, asentamiento y
mallas temporales. Simulación y visuales están separados. El juego define género,
reglas y momento de emisión. Los lotes heredan iluminación/perfiles. No hay
simulación GPU, bloom, soft particles ni bindings VFX universales Lua/Nova.

`projectiles.hpp` es un pool de datos acotado con IDs monotónicos, velocidad,
radio, vida y payload. La consulta prestada devuelve el impacto más cercano;
daño/equipos/visuales pertenecen al juego. Incluye el último tramo parcial de
vida y rechaza mutaciones reentrantes del pool durante su consulta.

RmlUi proporciona RML/RCSS retenido con painter CPU. No incluye navegador,
JavaScript ni stack de red de navegador. No es HTML/CSS completo. El overlay
reutiliza píxeles hasta invalidarse; cambios/entrada/animación pueden consumir
trabajo. Un overlay vivo, imágenes BMP y callbacks de click/texto/markup.
La aplicación no lo mezcla automáticamente sobre presentación GPU directa.

SaveData guarda int64/double/bool/string/Vec2/Vec3 en binario versionado validado.
La aplicación decide qué persistir. `platform::paths` separa contenido de
saves/cache privados del usuario; los nombres relativos rechazan `..`.
Android materializa recursos APK a caché para lectores de archivo existentes.
No se necesita un directorio de trabajo concreto ni permisos de administrador.

Los paquetes de fuentes excluyen builds, cachés, archivos privados y PDFs;
conservan modelos `.obj`. Los productos distribuyen ejecutable/contenido y las
bibliotecas/licencias redistribuibles que correspondan, no el compilador Nova.
Un binario Linux compilado contra una libc nueva no garantiza sistemas antiguos.

## Pruebas de la versión 0.7.0

Pruebas locales Linux x86-64 con GCC 14, CMake 3.28, SDL2, Lua 5.4 y display X11
virtual. OpenGL usó Mesa por software; Vulkan lavapipe/Mesa 25.0.7.
No se afirma que Actions ya haya ejecutado estas pruebas.

| Comprobación | Resultado |
|---|---|
| Release de escritorio | 37/37 CTest |
| Runtime mínimo sin módulos externos | 6/6 |
| ASan + UBSan + float-cast-overflow | 25/25; sin backends GPU ni importadores en esa matriz |
| Comparaciones OpenGL/CPU | 5/5 seleccionadas |
| Comparaciones Vulkan/CPU | 5/5 seleccionadas |
| Presentación GPU | Dibujo, 8 presentaciones/capturas y resize de ventana/framebuffer, ambos backends |
| SDK reubicado | Consumidor C++/Nova/Lua; Unicode/espacios, actualización de recursos y rechazo de destino incompatible |
| Regresión visual CPU | Normal, PS1 clásico, N64 y pixel idénticos byte a byte a 0.6.1 en la escena comprobada |

LeakSanitizer no pudo validarse (`detect_leaks=0`). No se usaron capas de
validación Vulkan. RmlUi/Jolt no se ejecutaron en esta matriz. Las pruebas con
software no acreditan rendimiento de GPU física. Las comparaciones de bordes
admiten tolerancias; la identidad byte a byte se limita a la regresión CPU indicada.

### Prueba histórica: Sony VAIO VPCYB35AL

- AMD E-450, 2 núcleos/2 hilos a 1.65 GHz.
- Radeon HD 6320 integrada con memoria compartida; x86-64.
- Zorin OS 17.3 Lite, base Ubuntu 22.04, GLIBC 2.35.
- Pantalla 1366×768; resolución interna de render no registrada.
- Juego metroidvania low-poly de una versión anterior de Ember/Retrato.
- Resultado observado por el mantenedor: aproximadamente **15 FPS estables**.

Esto confirma ejecución histórica, no mide 0.7.0 ni acredita 60 FPS o una meta
jugable. No se registraron revisión exacta, backend/ajustes, RAM, distribución de
frametimes ni duración. Hace falta repetir una prueba controlada para afirmar
mejoras de la versión pública. La papa arrancó; todavía no ganó la carrera.

En nuevas mediciones registra revisión, Release/Debug, hardware, driver/SO,
backend, resolución interna, calidad/workers, escena, calentamiento y duración.
Separa simulación, preparación, draw, readback y presentación.

## Plataformas

Linux x86-64 fue comprobado localmente. Windows x64/ARM64, Linux ARM64 y Android
ARM64/x86-64 tienen adaptadores/configuración, pero no se validaron aquí.
Los archivos de CI no prueban que esos jobs ya hayan pasado.

Android usa minSdk 24; Vulkan depende del dispositivo/controlador. OpenGL es 3.3
de escritorio, no GLES. La presentación Android conserva copia/readback a
ANativeWindow. Los destinos de 32 bits no están certificados. Hay un núcleo
común y adaptadores por SO; el código se recompila para cada destino.
No se garantiza ABI estable entre versiones ni rendimiento universal.

## Desarrollo asistido por IA

Nombre ceremonial:

> ChatGPT Sonnet 6.1 Obliterated full uncensored Qwen 3 Deepseek R2 Kimi K4.5 GLM 7.2 Ultra Obliterated

**Es una broma: no identifica un modelo real, benchmark o patrocinio.**

El proyecto tuvo asistencia sustancial de ChatGPT/Codex, con prototipos/revisiones
atribuidos por el mantenedor a Claude Sonnet, Muse Spark y DeepSeek. Las versiones
exactas y la procedencia de cada línea no se registraron de forma consistente.
La evidencia técnica es el código, pruebas y límites documentados; no la marca
de una IA ni un certificado inventado de procedencia.

## Privacidad y licencias

La publicación incluye fuentes, recursos necesarios, pruebas y licencias. Excluye
builds, logs de desarrollo, notas internas, rutas personales absolutas, dumps de
entorno, PDFs, credenciales y archivos de telemetría del equipo. El motor no
tiene cliente propio de analítica ni reporte automático por red. Lua y las
aplicaciones pueden hacer operaciones que el juego decida; no son sandbox.

La limpieza se hace sobre una copia aislada de publicación. Se conservan avisos
legales y contactos de atribución de terceros. Los metadatos públicos de cuenta
y commits GitHub se gestionan aparte; limpiar fuentes no anonimiza una cuenta.

Código propio MIT: [LICENSE](LICENSE). Dependencias y parches:
[THIRD_PARTY.md](THIRD_PARTY.md). El [README inglés](README.md) reúne la referencia
técnica principal; [docs/](docs) contiene referencias suplementarias Markdown.
No se requiere PDF.
