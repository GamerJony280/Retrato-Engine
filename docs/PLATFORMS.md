# Plataformas y arquitecturas

El núcleo es C++17 común. Los adaptadores viven en `platform/common`,
`platform/linux`, `platform/windows` y `platform/android`. `cmake/arch` declara
el baseline de arquitectura y `cmake/toolchains` selecciona compilación cruzada.
No se mantienen copias completas del motor por plataforma.

| Destino | Infraestructura | Validación de esta entrega |
|---|---|---|
| Linux x86-64 | Paths, entrada SDL, Vulkan/GL/CPU, SDK | Local; detalles en VALIDATION |
| Linux ARM64 | Núcleo y toolchain | Pendiente en hardware/toolchain destino |
| Windows x64/ARM64 | Paths Unicode, entrada nativa, SDL, cargador Vulkan | Pendiente; workflow disponible |
| Android ARM64/x86-64 | NativeActivity, assets APK/cache y paths privados | Pendiente de build y dispositivo |
| ARMv7/x86 de 32 bits | Detección/archivos de arquitectura | Sin certificación ni APK distribuido |

Un binario no es universal entre x86/ARM o Windows/Linux. El mismo código se
recompila con el compilador y dependencias del destino. SIMD adicional no es
obligatorio. El SDK registra SO y arquitectura y rechaza combinaciones erróneas.
No garantiza compatibilidad entre compiladores, runtimes C++ o versiones de libc.

Android fija minSdk 24 en su proyecto; Vulkan sigue dependiendo del dispositivo
y controlador. OpenGL del motor es 3.3 de escritorio: no es GLES. La aplicación
Android aún copia el resultado a ANativeWindow; no hay presentación Vulkan WSI
nativa ni validación de rendimiento móvil. No se etiqueta Android como soporte
completo únicamente por disponer de un proyecto Gradle.

La CI contiene jobs por destino, pero su definición no equivale a haberlos
compilado o ejecutado. Publicar soporte de una plataforma requiere ejecutar sus
jobs, arrancar una aplicación, probar entrada/pausa/recursos y medir un dispositivo.
