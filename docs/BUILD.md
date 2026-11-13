# Compilación e integración

## Selección de módulos

| Opción | Dependencia y efecto |
|---|---|
| `EMBER_VULKAN` | Cabeceras Vulkan; cargador y controlador al ejecutar. SPIR-V incluido y comprobado por hash |
| `EMBER_OPENGL` | OpenGL 3.3 de escritorio y SDL2; no GLES |
| `EMBER_APP` | Adaptador SDL2 en escritorio o NativeActivity en Android |
| `EMBER_LUA` | Desarrollo Lua 5.4; se omite con aviso si falta |
| `EMBER_IMPORTERS` | Lectores vendorizados; OFF deja `.embasset` |
| `EMBER_VFX` | Biblioteca independiente, sin dependencias nuevas |
| `EMBER_RMLUI` | Fuentes RmlUi incluidas, SDL2 y FreeType |
| `EMBER_JOLT` | Jolt instalado o fuentes locales mediante `EMBER_JOLT_SOURCE` |
| `EMBER_NOVA` | Compilador de desarrollo; no runtime de interpretación |
| `EMBER_TOOLS`, `EMBER_EXAMPLES`, `EMBER_TESTS` | Herramientas, aplicaciones de validación y pruebas, respectivamente |

`runtime`: biblioteca CPU mínima. `cpu`: herramientas, importadores y Nova sin
bibliotecas gráficas del sistema. `linux-x64`/`windows-x64`: capa de aplicación y
Vulkan prioritario. Los perfiles `-compat` añaden OpenGL. `desktop` habilita UI.
Las opciones gráficas pueden omitirse con aviso cuando faltan dependencias;
comprueba el resumen de configuración antes de asumir que se compiló un backend.
Una selección explícita no disponible produce error en ejecución.

## Linux

```sh
cmake --preset cpu
cmake --build --preset cpu --parallel 2
ctest --preset cpu
```

Para escritorio instala SDL2, desarrollo Vulkan y, si se solicita, OpenGL, Lua
5.4 y FreeType desde el gestor de paquetes del sistema. Los nombres cambian
según la distribución. `glslangValidator` solo es obligatorio si el GLSL o los
binarios SPIR-V no coinciden con los hashes distribuidos.

```sh
cmake --preset desktop -DEMBER_APP=ON
cmake --build --preset desktop --parallel 2
ctest --preset desktop
```

## Windows y Android

En la consola de herramientas Visual Studio 2022, usa `BUILD.cmd windows-x64`.
SDL2 y las cabeceras Vulkan deben corresponder al toolchain seleccionado.
El helper no instala dependencias. ARM64 requiere compilador y SDK ARM64.

Android utiliza `platform/android`, NDK, JDK y Gradle; los valores fijados viven
en sus archivos Gradle y presets. Proporciona `NOVA_HOST_COMPILER` si se genera
Nova durante una compilación cruzada. La existencia de estos archivos no acredita
una compilación o ejecución en dispositivo; consulta [plataformas](PLATFORMS.md).

## SDK instalado

```sh
cmake --install build/linux-x64 --prefix /ruta/real/al/sdk
```

Un consumidor independiente usa `find_package(Retrato 0.7 CONFIG REQUIRED)` y
`CMAKE_PREFIX_PATH` apuntando al SDK. Incluye targets, headers, versión, metadatos
de SO/arquitectura, funciones de entrada y documentación. Nova se instala si
está habilitado. El compilador verifica que el SDK corresponda al destino.
La redistribución de bibliotecas del sistema es responsabilidad del producto.
`retrato::ui` todavía se integra desde fuentes; no se exporta en el SDK.

`templates/game` proporciona el contrato de entrada y estructura de contenido.
`tools/verify_sdk.py` verifica un consumidor después de mover el SDK a una ruta
con espacios y Unicode, y comprueba el rechazo de destinos incompatibles.

Los binarios Linux dependen del baseline de libc y del toolchain utilizados.
Compila en el sistema mínimo de distribución o utiliza un entorno compatible.
No se fuerza `-march=native`, AVX ni extensiones opcionales de ARM.
