# Decisiones de arquitectura

## Núcleo compartido y adaptadores

Decision: conservar C++17 común y separar SO/arquitectura en adaptadores/toolchains.
Reason: evitar divergencia de APIs y correcciones entre copias del motor.
Alternatives: motores separados por destino; framework de plataforma completo.
Consequences: cada destino requiere recompilación y validación propia.

## API de render y presentación

Decision: conservar Frame/Backend y añadir presentación opcional sin tipos de GPU públicos.
Reason: evitar readback obligatorio por frame y mantener gameplay independiente del backend.
Alternatives: exponer Vulkan directamente; reemplazar el renderer con un framework.
Consequences: Vulkan/GL directos requieren la aplicación nativa; offscreen y captura siguen disponibles.

## Compatibilidad PS1

Decision: mantener painter order por defecto y ofrecer profundidad optativa.
Reason: el orden del pintor es parte del aspecto clásico pero falla en intersecciones.
Alternatives: activar Z silenciosamente para todo PS1; eliminar el perfil.
Consequences: la aplicación elige estética/oclusión sin modificar el layout GLSL.

## Frontera de scripting

Decision: Nova genera C++ sobre Host y handles comprobados.
Reason: compartir servicios y vidas útiles sin exponer internals ni añadir VM.
Alternatives: pointers directos al runtime; lenguaje independiente del motor.
Consequences: nuevos servicios requieren bindings explícitos y un compilador C++ anfitrión.

## Dependencias opcionales

Decision: mantener importadores vendorizados pequeños, SDL/Lua del sistema y Jolt optativo.
Reason: formatos y solver contienen problemas especializados, mientras el núcleo sigue modular.
Alternatives: reinventar decodificadores/solver o exigir todas las integraciones.
Consequences: construir una aplicación completa exige seleccionar y distribuir sus dependencias.
