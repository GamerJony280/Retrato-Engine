# C++, Lua y Nova

C++ es suficiente para todo el gameplay. `game::Host` expone entidades y recursos
mediante handles opacos, transform, cámara, luces, consultas, tiempo, entrada,
actualizaciones y señales. Runtime es la API nativa de menor nivel. Nova y Lua
no reciben estructuras del renderer ni pointers C++ arbitrarios.

Nova 0.2 transpila fuentes `.nova` a C++17. El compilador procesa lexer, parser,
AST, análisis semántico y generación, y conserva posiciones de origen. No hay
VM, GC, LLVM ni JIT. La sintaxis, tipos, operadores, imports, arrays fijos y
bindings admitidos se especifican en [NOVA_SPEC.md](NOVA_SPEC.md).

```sh
nova --emit-cpp gameplay.nova -o gameplay.generated.cpp
```

`nova_add_module(logic gameplay.nova)` integra generación en CMake. C++ crea el
Program generado sobre un Host y Entity. Conserva ese Program durante su uso;
las suscripciones se desconectan al destruirlo. La compilación cruzada usa un
Nova anfitrión mediante `NOVA_HOST_COMPILER`.

Host puede compartir un `shared_ptr<Runtime>` con C++ y cargar Lua en ese mundo.
Los tres lenguajes pueden coexistir; no se enlazan funciones arbitrarias entre
ellos automáticamente. Usa señales simples o bindings nativos tipados.
Lua abre bibliotecas estándar y requiere scripts de confianza; no ofrece sandbox.
Errores de toolchain o de bindings nativos aún pueden mostrar diagnósticos C++.

Hot reload no está implementado. Alternativas futuras: módulos generados en
biblioteca compartida, reinicio con estado de escena conservado, o intérprete
de desarrollo. No se congela una ABI dinámica antes de definir persistencia y
vidas útiles de módulos/suscripciones.
