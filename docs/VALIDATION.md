# Validación de la distribución 0.7.0

Entorno local: Linux x86-64, GCC 14, CMake 3.28, SDL2, Lua 5.4 y display X11
virtual. OpenGL usa Mesa por software y Vulkan usa lavapipe/Mesa 25.0.7.
Estas pruebas verifican comportamiento y contratos, no FPS de una GPU física.

| Comprobación | Resultado y alcance |
|---|---|
| Release de escritorio | 37/37 CTest; CPU, importadores, Lua, Nova, VFX y aplicación |
| Runtime mínimo | 6/6; sin GPU, SDL, importadores, Lua, Nova, VFX ni herramientas |
| ASan + UBSan + float-cast-overflow | 25/25; núcleo, Lua, Nova, VFX y aplicación CPU; sin importadores ni backends GPU |
| Comparación OpenGL/CPU | 5/5 pruebas seleccionadas, incluido contrato de profundidad |
| Comparación Vulkan/CPU | 5/5 pruebas seleccionadas, incluido contrato de profundidad |
| Presentación OpenGL y Vulkan | Dibujo, 8 presentaciones/capturas, cambio de ventana y framebuffer |
| SDK reubicado | Consumidor independiente con C++, Nova y Lua; rutas Unicode/espacios, actualización de contenido y rechazo de SDK incompatible |
| Regresión visual | normal, PS1 clásico, N64 y pixel: imágenes CPU idénticas byte a byte a 0.6.1 en la escena de regresión |
| Integración SDL | Targets importados directos, alias creados en hijo y targets heredados; visibilidad en consumidor |

LeakSanitizer no se validó: el entorno rechaza su inspección de procesos; ASan
se ejecutó con `detect_leaks=0`. No se utilizaron capas de validación Vulkan.
No se ejecutaron RmlUi ni Jolt en esta matriz. Su código opcional se conserva,
pero no se atribuyen aquí resultados de otras entregas.

El renderer de 0.5.1 y 0.6.1 tiene fuentes idénticas en preparación, shading,
raster CPU y shaders GLSL. La comparación no determina qué configuración o
cambios existen en instalaciones externas. El nuevo control PS1 está cubierto
por pruebas de parser, preparación y oclusión compartida entre backends.

Los buffers de recursos vacíos son válidos: GPU usa almacenamiento mínimo
interno sin introducir lecturas fuera del paquete. Las pruebas de contrato
incluyen esa ruta, profundidad explícita y rechazo de enums inválidos.
La cola de cues se comprueba con inserción durante callback, orden de tiempos
iguales y 1000 ciclos sin retener cues consumidos.

## Reproducción

```sh
cmake --preset cpu
cmake --build --preset cpu --parallel 2
ctest --preset cpu
```

En una compilación con aplicación y ambos backends disponibles:

```sh
./build/desktop/ember_presentation_tests opengl
./build/desktop/ember_presentation_tests vulkan
EMBER_TEST_BACKEND=opengl ctest --test-dir build/desktop -R 'render_contract|ember_asset_tests|ember_tests|ember_vfx' --output-on-failure
EMBER_TEST_BACKEND=vulkan ctest --test-dir build/desktop -R 'render_contract|ember_asset_tests|ember_tests|ember_vfx' --output-on-failure
cmake --install build/desktop --prefix /ruta/real/al/sdk
python3 tools/verify_sdk.py /ruta/real/al/sdk --nova
```

Las comparaciones usan tolerancias donde difieren rasterización y cobertura de
bordes. No prueban igualdad bit a bit entre arquitecturas ni drivers arbitrarios.
La identidad visual byte a byte se limita a la regresión CPU indicada.

Windows, Linux ARM y Android no se compilaron ni ejecutaron en esta máquina.
Los workflows incluidos deben ejecutarse en el commit publicado y complementarse
con pruebas de dispositivo antes de etiquetar soporte estable por destino.
