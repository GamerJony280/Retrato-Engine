# Física de Ember: ligera, avanzada y 2.5D

Ember conserva dos caminos deliberadamente distintos:

| Camino | Coste | Uso |
|---|---:|---|
| `CollisionMesh`, raycast, sweep y `slide_sphere_boxes` | mínimo | interacción arcade, proyectiles y controladores sencillos |
| `PhysicsWorld` con Jolt | opcional | gravedad, masa, fuerzas, impulsos, fricción, rebote, apilado, cuerpos estáticos/cinemáticos/dinámicos y CCD |

Jolt no forma parte del renderer, la animación ni el build mínimo. La API pública de Ember usa PIMPL y handles `BodyRef`; ninguna cabecera de Jolt se filtra al juego.

## Dos interruptores, no uno

Primero el desarrollador decide si incluye el backend en el binario:

```bash
git clone --depth 1 --branch v5.6.0 https://github.com/jrouwe/JoltPhysics.git ../JoltPhysics
cmake -S . -B build/jolt \
  -DEMBER_JOLT=ON \
  -DEMBER_JOLT_SOURCE="$PWD/../JoltPhysics" \
  -DEMBER_OPENGL=OFF -DEMBER_VULKAN=OFF
cmake --build build/jolt -j2
```

Ember no descarga Jolt automáticamente. También se admite un paquete instalado que exporte `Jolt` o `Jolt::Jolt`, omitiendo `EMBER_JOLT_SOURCE`.

Después cada proyecto decide si crea el mundo avanzado:

```toml
[physics]
backend = "jolt" # none conserva el camino mínimo
plane = "xy"     # 3d, xy, xz o yz
solver_quality = 40
fixed_hz = 60
continuous_collision = "selected"
max_bodies = 4096
max_body_pairs = 4096
max_contacts = 2048
temporary_mb = 4
```

Un ejecutable compilado con Jolt todavía puede usar `backend = "none"`. Un ejecutable sin Jolt rechaza `backend = "jolt"` con un error explícito.

`EMBER_JOLT_LEGACY_CPU=ON` es el valor predeterminado al compilar la fuente local: desactiva SSE4/AVX y conserva SSE2 como baseline x86. Puede ponerse en `OFF` para un build dirigido a CPUs modernas. Un paquete Jolt precompilado conserva el baseline con el que fue construido; Ember no puede rebajarlo después.

## 2.5D

El plano limita grados de libertad dentro del solver:

| Valor | Traslación | Rotación si `allow_rotation=true` |
|---|---|---|
| `3d` | X, Y, Z | X, Y, Z |
| `xy` | X, Y | Z |
| `xz` | X, Z | Y |
| `yz` | Y, Z | X |

No se corrige la profundidad después de simular: el eje está realmente bloqueado. Para plataformas 2.5D normalmente se usa `xy`; `xz` es útil cuando Y es altura y la cámara observa el plano XZ. La representación histórica `Object` solo dibuja yaw (rotación Y). `PhysicsWorld::state()` conserva el cuaternión completo, pero el enlace automático Runtime→Object solo copia posición y yaw. Por eso los helpers Nova desactivan rotación por defecto; C++ y Lua pueden activarla cuando la representación visual lo soporte.

## C++ con `game::Host`

```cpp
ember::Config config;
config.physics_backend = "jolt";
config.physics_plane = "xy";
ember::game::Host game(config);

auto floor = game.create("floor");
game.set_position(floor, {0, -0.5f, 0});
ember::BodyDesc fixed;
fixed.shape = ember::BodyShape::box({5, 0.5f, 1});
game.add_rigid_body(floor, fixed);

auto player = game.create("player");
game.set_position(player, {0, 3, 0});
ember::BodyDesc dynamic;
dynamic.shape = ember::BodyShape::capsule(0.5f, 0.35f);
dynamic.motion = ember::BodyMotion::Dynamic;
dynamic.allow_rotation = false;
dynamic.continuous = true;
game.add_rigid_body(player, dynamic);
game.add_impulse(player, {3, 0, 0});
```

`Runtime` avanza Jolt exactamente una vez por paso fijo, después de los callbacks de juego y antes de actualizar animación/attachments. Los cuerpos cinemáticos leen el transform del objeto; los dinámicos escriben su pose simulada. `destroy_object`/`Host::destroy` eliminan primero el cuerpo y expiran su handle.

## Lua

```lua
local floor = engine.box(0, -0.5, 0, 10, 1, 2, 0.4, 0.4, 0.4)
engine.physics_box(floor, 5, 0.5, 1, "static")

local ball = engine.box(0, 3, 0, 1, 1, 1, 1, 0.3, 0.2)
-- objeto, radio, movimiento, masa, fricción, rebote, CCD, rotación
engine.physics_sphere(ball, 0.5, "dynamic", 1, 0.5, 0.1, true, false)
engine.physics_impulse(ball, 3, 0, 0)
```

También existen `physics_capsule`, `physics_has`, `physics_remove`, `physics_velocity`, `physics_set_velocity` y `physics_force`.

## Nova

```nova
func start() {
    physics_box(self, Vec3(0.5, 0.5, 0.5), true)
    physics_set_velocity(self, Vec3(2, 0, 0))
    physics_impulse(self, Vec3(0, 4, 0))
}
```

El MVP expone `physics_box`, `physics_sphere`, `physics_has`, `physics_remove`, `physics_velocity`, `physics_set_velocity`, `physics_force` y `physics_impulse`. La descripción completa (masa, material, sensor, cápsula y rotación) permanece en C++/Lua hasta ampliar los tipos del lenguaje.

## Límites actuales

- El adaptador Ember admite box, sphere y capsule. Jolt ofrece más formas, joints y character controllers, pero todavía no están en la frontera estable.
- Aún no hay callbacks de contacto/trigger, raycast del mundo Jolt ni cooking convexo/mesh hacia Jolt.
- `dynamic_shape = "convex_compound"` sigue reservado: no afirma que una malla visual se descomponga automáticamente.
- `solver_quality` se mapea de forma acotada a 2–10 iteraciones de velocidad y 1–3 de posición. Más calidad no cambia la geometría.
- `continuous_collision = "selected"` deja que cada `BodyDesc::continuous` decida; `off` fuerza CCD discreto para todos los cuerpos creados mediante Runtime/Host/Lua/Nova.
- `optimize_physics_broadphase()` es para lotes estáticos terminados; no debe llamarse cada frame.
- El rendimiento debe medirse en el dispositivo destino. El camino arcade evita el solver cuando no se necesitan cuerpos rígidos.
