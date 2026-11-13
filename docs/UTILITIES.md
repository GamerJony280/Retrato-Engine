# Utilidades de juego (headers, sin núcleo nuevo)

Pequeños bloques header-only sobre la API pública, para no reescribirlos
en cada juego. No cambian ningún comportamiento del motor.

## `ember/character.hpp` — cuerpo arcade

Gravedad, slide en dos fases (plano horizontal X/Z y luego vertical), normales, coyote time
y buffer de salto sobre `slide_sphere_boxes`. Sin masa ni fuerzas: el doble
salto y la política quedan del lado del juego.

```cpp
ember::CharacterBody body;
body.queue_jump(0.12f);          // flanco de subida
body.cut_jump(5);                // soltar recorta el salto
bool jumped = body.update(solids, dt, 30, 11.5f, 0.10f);
if (needs_double) body.air_jump(10.5f); // el juego cuenta los usos
```

NOTA: un contacto de reposo exacto (centro justo a `radius` de la cara)
reporta normal nula por diseño del barrido; en juego el skin lo evita,
en tests conviene spawnear ligeramente arriba.

## `ember/input.hpp` — mapa de acciones

Keycodes cualesquiera (SDL u otros) a acciones `int`, con flancos de un
frame. Varias teclas pueden sostener la misma acción, el key-repeat no crea
flancos falsos y los índices negativos se rechazan. Llamar `next_frame()`
después del paso de simulación que consume la entrada. Si una iteración solo
consulta eventos o renderiza, conservar los flancos hasta el siguiente paso.
`release_all()` libera teclas al perder foco; evita acciones pegadas cuando
el sistema no entrega el key-up. Una pulsación y liberación entre pasos
conserva ambos flancos aunque `held()` ya sea falso.

```cpp
ember::ActionMap in;
in.bind(0, SDLK_SPACE).bind(1, SDLK_f);
in.key_down(k); in.key_up(k);    // desde SDL_PollEvent
bool jump = in.pressed(0);       // flanco; held(0) = mantenido
in.next_frame();
```

## `ember/loop.hpp` — pacing sin dependencias

Responde por iteración cuántos pasos fijos correr, si renderizar y cuánto
dormir. Topes espejo de `Runtime::tick` (8 pasos, 0.25 s); el backlog de
render se suelta en vez de espiral. Determinista con deltas explícitos.
La tolerancia de redondeo no deja un acumulador negativo. El sueño se limita
por el próximo paso fijo y el próximo render, lo que permite simular a 240 Hz
y renderizar a 30 Hz sin reducir la cadencia de simulación a la de imagen.

```cpp
ember::LoopTimer loop;
auto t = loop.update(dt);
for (int i = 0; i < t.steps; i++) runtime.tick(1.0 / 60);
if (t.render) { /* prepare/draw/present */ }
if (t.sleep_ms > 0) SDL_Delay(Uint32(t.sleep_ms));
```

## `ember/mesh_builder.hpp` — mallas procedurales

Junto a `box()`: `mesh::quad`, `mesh::disc` (sector/anillo en XY con
gradiente centro→borde, para slashes y ondas) y `mesh::grid` (suelos).
Doble cara opcional con winding invertido real; colores por vértice.

```cpp
ember::Mesh slash = ember::mesh::disc(0.6f, 1.3f, -0.6f, 2.0f, 12,
                                      {0.9f, 0.97f, 1.0f}, {0.25f, 0.55f, 1.0f});
```

## `ember/particles.hpp` — pool solo-datos

Capacidad fija, integración, edad y compactado; barrido continuo de esfera
contra AABBs con rebote opcional. El render queda del juego a propósito (cajas,
puntos o mallas): la pool mueve números, no dibuja. `ParticleStep` permite
desactivar colisiones, limitar contactos, ajustar drag/fricción/restitución
y retirar partículas asentadas o caídas fuera del mundo. Se resuelve cada
rebote usando el tiempo restante, en lugar de deslizar primero y rebotar solo
contra la última normal.

```cpp
ember::ParticlePool pool(256);
pool.spawn(pos, vel, color, 0.6f);
pool.update(dt, solids); // devuelve supervivientes
```

El target opcional `ember_vfx` reutiliza este pool y añade ráfagas,
estelas, render por batch y mallas temporales. Recetas,
ownership y límites en [VFX.md](VFX.md).

## `ember/follow.hpp` — cámara con shake

Suavizado exponencial hacia un foco, límites duros y trauma que decae.
`camera(offset, time)` construye la `Camera` con shake determinista.

```cpp
ember::FollowCam cam;
cam.bounds_lo = {-1, 0.8f, -1}; cam.bounds_hi = {84, 6, 1};
cam.snap(start);
cam.update(focus, dt);
cam.add_shake(0.35f); // al recibir daño, p. ej.
runtime.camera = cam.camera({0, 1.6f, -9.5f}, float(runtime.time()));
```

## `ember/lights.hpp` — pool de luces

Slots reutilizables sobre `Scene::lights` (nunca borra, solo aparca
lejos con radio 0, así los índices no se invalidan). Liberar dos veces no
duplica un slot. Cada pool pertenece a una sola escena. El renderer consume
las más cercanas según `local_lights_per_object`; el juego mantiene bajo
el conteo estático y destella por el pool.

```cpp
ember::LightPool fx;
int id = fx.acquire(scene);
fx.set(scene, id, pos, color, 7);
fx.park(scene, id); // o release() para devolver el slot
```

## RmlUi para juegos — target opcional `ember_ui`

`ember::UiOverlay` es una API pública pequeña sin tipos SDL. El programa
reenvía mouse, registra callbacks por id y mezcla `pixels()` sobre su frame:

```cpp
ember::UiOverlay ui("assets/hud.rml", "assets/ui.ttf", width, height);
ui.set_click_handler([](const std::string &id) { /* acción del juego */ });
ui.set_text("health", "HP 4/5"); // escapa markup; set_markup() es explícito
```

Se enlaza con `ember_ui` cuando `EMBER_RMLUI=ON`. La capa copia la geometría
compilada; no conserva spans hacia memoria de RmlUi. RmlUi usa interfaces
globales, por lo que este MVP rechaza una segunda `UiOverlay` simultánea en
vez de permitir corrupción silenciosa.

## Tests

`tests/util_tests.cpp` (`ember_util_tests` en ctest): aterrizaje, muros,
techo, buffer/coyote/corte/doble, flancos de input, pasos/cadencia del
loop, conteos y cotas de mallas, tope/edad/rebote de partículas,
convergencia/límites/shake de cámara, reuso seguro de luces, parámetros
inválidos, movimiento X/Z y tunneling de partículas. `ember_ui_tests`
cubre ownership de geometría, texto escapado, caché, visibilidad y clicks.
