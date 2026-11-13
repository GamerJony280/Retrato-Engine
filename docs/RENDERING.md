# Contrato de renderizado

`SceneRenderer::prepare` convierte escena/cámara/configuración en `Frame`.
`Backend::draw` consume ese paquete. `Backend::present` presenta cuando hay una
superficie nativa; devuelve false en backends offscreen y CPU. En ese caso la
capa de plataforma presenta `readback()`. La API pública no expone tipos SDL,
Vulkan ni OpenGL al código de gameplay o Nova.

`readback()` devuelve RGBA de 32 bits con origen superior izquierdo. La
referencia permanece propiedad del backend y puede invalidarse en otra lectura,
dibujo o destrucción. Las llamadas al backend y mutaciones de escena se serializan.
No hay una API de comandos Vulkan arbitrarios: el contrato implementado es el
pipeline de escena, presentación y captura del motor.

## Backend y presentación

| Ruta | Presentación | Captura |
|---|---|---|
| Vulkan + aplicación de escritorio | Swapchain, blit GPU, barras de aspecto | Buffer de descarga creado bajo demanda |
| OpenGL + aplicación de escritorio | Blit framebuffer y swap SDL | Lectura explícita y conversión de orientación |
| CPU + aplicación de escritorio | Textura SDL | Buffer CPU existente |
| Factorías offscreen | El llamador presenta la imagen | Disponible en los tres backends |
| Aplicación Android | Copia a ANativeWindow | Lectura del backend seleccionado |

Auto intenta Vulkan, OpenGL compilado y CPU. Una selección explícita falla con
diagnóstico si no está disponible. Vulkan requiere superficie UNORM RGBA/BGRA y
blit; puede usar colas separadas de gráficos/presentación. Se mantiene un envío
en vuelo y semáforos de finalización por imagen del swapchain. Minimizar omite
la presentación; redimensionar recrea recursos. No hay doble envío de píxeles
CPU en la ruta directa. `uncapped` solicita un modo sin sincronización cuando
está disponible; el compositor puede imponer límites adicionales.

La UI retenida sigue siendo un módulo CPU. La aplicación genérica no compone
UiOverlay automáticamente sobre la ruta GPU directa. Una aplicación puede usar
la composición de píxeles existente o proporcionar su propio adaptador.

## PS1 y coherencia

Los tres backends usan `frame_depth_test(Frame)`:

- `Automatic`: profundidad excepto en PS1.
- `Enabled`: profundidad activa, también en PS1.
- `Disabled`: orden de entrega de triángulos.

`graphics.ps1_depth_test=false` conserva el PS1 clásico: orden estable por
profundidad media, UV afín, snap de vértices, 5 bits y dithering. Intersecciones
y triángulos grandes pueden sobreponerse incorrectamente. Activar la opción
corrige oclusión con Z; no convierte el perfil en una emulación exacta de consola.
La cuantización pixel de iluminación es una característica del perfil pixel,
independiente de la resolución del framebuffer y del mapa de sombras.

## Formatos compartidos

`DrawVertex`: 100 bytes. `ShaderSettings`: 96 bytes. Tamaños, offsets, float IEEE
binary32 y enteros de 32 bits se comprueban con `static_assert`. `DepthMode` vive
en Frame y no altera el layout GLSL. Los atributos se declaran en una tabla común.
`validate_frame` comprueba dimensiones, finitud, índices, recursos y consistencia
de settings antes de consumirlos. Cada backend también valida límites de su GPU.

El espacio de clip usa profundidad 0..1 y Y hacia abajo en el paquete. OpenGL
convierte Y y profundidad en su vertex shader; la captura normaliza la orientación.
Cambiar GLSL requiere actualizar SPIR-V con `tools/compile_shaders.py`. El build
rechaza reutilizar binarios de shader obsoletos.

## Costes y límites

Preparación, skinning, selección de luces y sombras solares se calculan en CPU.
Rasterización se ejecuta en el backend seleccionado. No hay PBR completo,
ray tracing, sombras de luces locales ni mezcla transparente ordenada general;
la transparencia de materiales usa recorte alpha. N64 usa filtrado de tres muestras;
CRT aplica scanlines y máscara RGB. No incluye bloom ni persistencia temporal.

El raster CPU divide filas entre trabajadores persistentes. El fondo constante
utiliza un patrón periódico en lugar de sombreado completo por píxel. PS1 sin
profundidad no escribe ni limpia el depth buffer. El ordenado calcula una vez
la profundidad de cada triángulo antes de comparar.
