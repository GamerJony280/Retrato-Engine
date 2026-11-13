# Partículas y VFX opcionales

El target `ember_vfx` añade efectos reutilizables sobre Ember. Su código no vive
en `Runtime` y el núcleo no lo enlaza. No añade bibliotecas externas, threads,
Jolt, un renderer ni un lenguaje de scripting.

## Activar y quitar

`EMBER_VFX=ON` es el valor predeterminado del proyecto. El preset mínimo
`runtime` lo desactiva. Un juego C++ que usa los efectos enlaza ambos targets:

```cmake
add_subdirectory(ember)
add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE ember ember_vfx)
```

Para excluirlo de una compilación:

```sh
cmake --preset cpu -DEMBER_VFX=OFF
cmake --build --preset cpu
ctest --preset cpu
```

Una aplicación que incluya `vfx.hpp` debe retirar sus llamadas o protegerlas
con `#ifdef EMBER_HAS_VFX` al quitar el target. El núcleo no incorpora stubs
obligatorios para una función opcional.

## Qué posee cada componente

| Componente | Responsabilidad y ownership |
| --- | --- |
| `ParticlePool` (`particles.hpp`) | Datos compactos, fuerzas vectoriales, drag y colisión; header-only compatible con la API anterior |
| `vfx::System` | Posee un pool y un generador aleatorio local; emisión orientable en 3D o planos elegidos; no referencia la escena |
| `vfx::Emitter` | Guarda solo la fracción de emisión; usa un System prestado en cada llamada |
| `vfx::Visuals` | Posee un ObjectRef y una malla reservada; Runtime posee el objeto de escena |
| `vfx::TimedMeshes` | Posee los ObjectRefs de efectos temporales; destruye únicamente esos IDs al expirar |

Los adaptadores de escena no son copiables ni movibles. Guardan el token
débil de Runtime: pueden destruirse después del runtime sin acceder a memoria
liberada; intentar usarlos entonces lanza un error. Borrar una escena invalida
sus IDs: `Visuals` recrea su objeto al próximo `sync` con partículas; `TimedMeshes`
descarta los IDs vencidos. Nunca destruyen un objeto ajeno que reutilizó el slot.
No registrar callbacks que capturen estos objetos después de destruirlos.

## Daño, escombros o polvo

```cpp
#include "ember/vfx_recipes.hpp"

ember::vfx::System particles(96, 7); // capacidad y semilla
ember::vfx::Visuals visual(runtime, 96);
auto damage = ember::vfx::recipes::planar_sparks({1, .2f, .2f});
damage.count = 12;
damage.particle.color = {1, .2f, .2f};
damage.speed = 3.5f;
particles.burst(hit_position, damage);

particles.step.collisions = true; // opcional, desactivado por defecto
// FixedUpdate: solids son AABBs del mundo, prestadas durante la llamada.
particles.update(delta, solids);
// Antes de SceneRenderer::prepare, una sola vez por render:
visual.sync(particles);
```

El núcleo no elige gravedad, plano ni colisiones: `Particle{}` y `ParticleStep{}`
son neutrales; `Burst{}` emite una esfera de direcciones en 3D. La receta anterior
elige un arco hacia arriba y gravedad para obtener chispas de daño, pero no es
un tipo obligatorio de partícula.

| Patrón | Dirección inicial |
| --- | --- |
| `Sphere` | Direcciones isotrópicas en 3D |
| `Ring` | Anillo completo o arco, orientado con `direction` (normal) y `up` |
| `Cone` | Cono alrededor de `direction`; `cone_angle` es el semiángulo en radianes, 0 da un chorro |
| `Box3D` | Componentes independientes entre `velocity_min/velocity_max` |

`RingXY` es un alias de compatibilidad de `Ring`: ya no fuerza el hemisferio
superior. Para conservar ese aspecto usar `recipes::planar_sparks`.
`speed_min_factor` elige velocidades entre `speed * factor` y `speed`;
`arc_min/arc_max/phase` seleccionan el arco. Si `up` es paralelo a la dirección,
se usa un eje auxiliar estable. Todos los patrones suman `particle.velocity`.
`SpawnVolume::Point/Box/Sphere` elige el volumen de nacimiento independientemente
del patrón de velocidad. Box usa offsets en ejes mundiales; Sphere usa radio y
muestreo uniforme del volumen. `particle.plane_normal` no nulo proyecta offsets,
velocidad y fuerzas a un plano por la posición de emisión. Sirve para XY, XZ o
un plano inclinado; cero conserva movimiento 3D libre.
La posición procede de `burst` y la vida de `life_min/life_max`; en `spawn(Particle)`
se usan directamente `position`, `velocity` y `life`.
La misma semilla y la misma secuencia producen el mismo resultado en el mismo
build; las funciones trigonométricas no prometen identidad bit a bit entre CPUs.

## Ajustes y coste

| Ajuste | Valor inicial / significado |
| --- | --- |
| Capacidad | 96; rango 0..65536; exceso descartado, nunca expansión automática |
| `step.collisions` | false; true activa cajas o una consulta externa |
| `step.radius` | 0.06; radio de colisión independiente del tamaño visible |
| `step.bounce` | 0.35; restitución 0..1, también en paredes |
| `step.friction` | 8; amortigua velocidad tangencial al contactar |
| `step.drag` | 0; resistencia en vuelo, separada de fricción |
| `step.max_contacts` | 4; límite 1..16 por partícula y paso |
| `step.settle_speed` | 0.05; elimina rebotes pequeños |
| `step.settle_life` | 0 conserva vida; 0.45 permite retirar escombros asentados pronto |
| `step.kill_below` | Mínimo float finito; sin corte Y por defecto, atajo heredado configurable |
| `particle.gravity` | 0; atajo heredado de aceleración en -Y |
| `particle.acceleration`, `step.acceleration` | Vectores cero; se suman por partícula y por sistema |
| `particle.plane_normal` | Cero permite 3D libre; otra normal restringe movimiento |
| `step.response` | Bounce; también Slide, Stop o Kill |
| `step.settle_normal` | Cero admite cualquier superficie; otra normal filtra el asentamiento |
| `step.bounds_enabled/bounds` | Desactivados; caja XYZ para retirar partículas fuera del área |
| `size/end_size` | Ancho completo 0.12 / 0.03; no escala el radio físico |
| `shrink_time` | Últimos 0.45 segundos; 0 mantiene tamaño inicial |
| `size_steps` | 4 cubetas; 0 permite encogimiento continuo |

Se reservan datos al construir y geometría al crear el adaptador. El render de
cubos cuesta 12 triángulos por partícula; `Shape::Billboard` usa 4 (dos caras).
No se asigna una malla ni un objeto por partícula y no se recrean las mallas
al encogerlas. Un batch vacío conserva su memoria y su objeto vacío para
reutilizarlos. Destruir el adaptador libera su objeto; reconstruirlo permite
cambiar capacidad o forma. `clear()` conserva la reserva, no libera RAM.
Copiar un pool/sistema duplica sus datos y reserva la capacidad completa;
no es una operación barata por frame. Moverlo transfiere la reserva y deja
el origen con capacidad cero, válido para destruir o reasignar.

Con capacidad 96, los datos reservados ocupan `96 * sizeof(Particle)` y la
malla `96 * 12 * sizeof(Triangle)` en modo Cube, más la plantilla de 12
triángulos y las cabeceras de contenedores. `sizeof` depende de ABI/arquitectura.
La preparación/renderización de Ember conserva sus propios buffers y costes;
una malla por batch no equivale a instancing GPU ni hace gratuito el render.

La simulación sin colisiones es lineal en partículas vivas. Con cajas, tiene
coste máximo proporcional a partículas × cajas × presupuesto de contactos;
un rechazo por AABB del trayecto evita muchos barridos. En mapas enormes,
el juego debe entregar solo las cajas de la región cercana al sistema. No hay
BVH propio ni escaneo de todos los colliders del runtime.

Los barridos continuos evitan saltarse suelos/paredes finos y consumen el tiempo
restante después de cada rebote. Al agotar contactos se descarta el movimiento
restante, manteniendo el límite de trabajo. Esfera contra AABB expandida es
conservadora en esquinas; no es un solver de cuerpos rígidos. Una partícula
que empieza dentro de una caja se detiene, sin resolver penetración. No colocar
emisores dentro de sólidos; separarlos al menos por radio más un pequeño margen.
No hay colisión partícula-partícula ni eventos de daño. Cajas es la única
geometría incorporada; otras geometrías requieren la consulta externa descrita abajo.
Valores no finitos/configuración inválida lanzan error. Desbordamiento numérico
durante integración retira la partícula antes de pasarla al renderer.

## Fuerzas, superficies y contextos

```cpp
ember::vfx::System spray(128);
spray.step.acceleration = {0, 0, -9.8f}; // gravedad elegida por el juego
ember::vfx::Burst jet;
jet.pattern = ember::vfx::Pattern::Cone;
jet.direction = {1, 0, 0};
jet.cone_angle = .15f;
jet.volume = ember::vfx::SpawnVolume::Sphere;
jet.volume_radius = .1f;
spray.burst(nozzle_position, jet);
```

Para lluvia, usar Box como volumen y Box3D con velocidad descendente. Para una
estela ingrávida, dejar fuerzas en cero. Para una explosión, Sphere; para una
onda plana, Ring. Estos ajustes no cambian la lógica del módulo. Humo con
transparencia suave sigue fuera del renderer actual.

`recipes::running_dust(surface_normal, motion, color)` reproduce el polvo de
movimiento como Burst editable: sale de la superficie elegida, hereda una
pequeña velocidad opuesta al movimiento y acelera hacia esa superficie.
`recipes::planar_sparks(color, speed, plane_normal, up)` reproduce las chispas
planas de daño. Ambas devuelven datos ordinarios: pueden cambiarse vida, tamaño,
velocidad, fuerzas, patrón o volumen antes de emitir. No habilitan colisiones,
no conocen jugador, sprint, daño, suelo del nivel ni cámara.

```cpp
auto dust = ember::vfx::recipes::running_dust(ground_normal, character_velocity);
dust.count = 3;
dust.particle.plane_normal = {0, 0, 1}; // solo si este juego limita movimiento a XY
particles.burst(feet_position, dust); // el juego decide frecuencia y condición
```

La API corta heredada `ParticlePool::spawn(pos, vel, color, life)` mantiene
su gravedad 20; `update(dt, boxes)` mantiene colisiones activas. Para opciones
neutrales usar `spawn(Particle)` y `update(dt, boxes, ParticleStep)`.

## Consultas de colisión propias

```cpp
bool sweep_world(ember::Vec3 from, ember::Vec3 delta, float radius,
                 ember::ParticleContact &hit, const void *context);
ember::ParticleCollider collider{sweep_world, &world};
particles.step.collisions = true;
particles.step.response = ember::ParticleResponse::Slide;
particles.update(delta, {}, collider);
```

La función devuelve el contacto más cercano del segmento (`fraction` en 0..1,
normal finita), o false. La normal se normaliza y proyecta al plano si lo hay.
Una normal cero detiene el movimiento porque no permite resolver un impacto.
El contexto se presta solo durante `update`; ni el sistema ni las partículas
lo guardan. Una consulta externa reemplaza las cajas de esa llamada y puede
usar un tilemap, plano, BVH o un backend físico. Debe cumplir el contrato de
barrido para evitar tunneling; Ember no transforma un raycast puntual en CCD.
El número de consultas conserva `max_contacts`, pero su coste interno depende
del código de la aplicación. No hay callbacks ocultos por frame ni dependencia
obligatoria de Jolt. Bounce refleja, Slide quita velocidad entrante, Stop deja
la partícula quieta hasta caducar y Kill la retira. No es un solver rígido.

## Estelas de emisión estable

```cpp
ember::vfx::Emitter trail;
trail.rate = 30; // partículas por segundo, no por frame
trail.effect.particle.gravity = 0;
trail.effect.particle.color = {.85f, .95f, 1};
trail.effect.speed = .5f;
trail.effect.life_min = trail.effect.life_max = .25f;
trail.emit(particles, sword_position, delta); // solo mientras el efecto esté activo
trail.reset(); // al iniciar una nueva estela
```

El emitter admite delta 0..1 s y rate 0..1000000. Su fracción conserva emisiones
entre llamadas. Si el pool está lleno, las emisiones se pierden y no producen
una ráfaga tardía. Las partículas nacen en la posición recibida; todavía no hay
interpolación espacial entre muestras, cintas conectadas ni spline de estela.
Pausar consiste en no llamar `emit/update`, sin hilo que siga trabajando.

## Slashes, anillos y ondas temporales

```cpp
#include "ember/mesh_builder.hpp"
ember::vfx::TimedMeshes effects(runtime, 8);
auto slash = effects.spawn(
    ember::mesh::disc(.6f, 1.3f, -.6f, 2, 12,
                      {.9f, .97f, 1}, {.25f, .55f, 1}),
    sword_position, .28f);
if (runtime.object_alive(slash))
    runtime.object(slash).position = sword_position;
effects.update(delta); // expiración; puede usarse con partículas o por separado
```

Capacidad llena o vida no positiva devuelve `ObjectRef{}`; comprobarlo antes
de acceder. Las mallas pueden ser hechas a mano o con `mesh::disc/quad`.
Aquí se admiten asignaciones al crear/reemplazar la malla: no son el camino
de partículas sin asignaciones por frame. El juego conserva la decisión de
animar fases, causar daño y combinar `LightPool` o `FollowCam`.

## Geometría elegida por el juego

```cpp
#include "ember/mesh_builder.hpp"
auto shard = ember::mesh::quad({-.5f,0,-.5f}, {.5f,0,-.5f},
                              {.5f,0,.5f}, {-.5f,0,.5f}, {1,1,1});
ember::vfx::Visuals custom(runtime, std::move(shard), 96);
custom.sync(particles);
```

La plantilla admite 1..1024 triángulos y conserva colores de vértice, UV y
referencias compartidas a materiales/imágenes. Su origen y orientación los
define el juego; cada partícula añade posición y tamaño uniforme. No hay aún
rotación individual ni selección de frame de textura. La plantilla pertenece
al adaptador; sus recursos compartidos siguen el ownership ordinario de Mesh.
El coste de geometría es capacidad × triángulos de plantilla, por lo que una
plantilla grande puede consumir mucha RAM. Usar pocos triángulos para polvo.

## Backend y límites actuales

Los efectos envían Mesh/Triangle por la ruta ordinaria de CPU/OpenGL/Vulkan.
No exponen handles de GPU y heredan toon, resolución, luces y sombras.
El batch comparte la selección de luces por objeto; si los efectos abarcan
regiones lejanas, separarlos en sistemas por zona para no compartir ese presupuesto.
Los efectos son opacos y reciben iluminación/sombras como cualquier malla.
No hay blend aditivo, soft particles, texturas animadas, bloom, simulación GPU
ni editor de efectos en este módulo. Lua/Nova aún no tienen bindings VFX:
se usan desde C++ y pueden activarse desde lógica del juego escrita en otros
lenguajes mediante una API propia de la aplicación.

## Seguridad de consultas

Las consultas prestadas no pueden spawn/clear/update/reemplazar/mover su propio
pool activo. Se rechaza esa reentrada; contexto/pool deben sobrevivir a la llamada.
Un callback que falla no revierte avance parcial. Las aplicaciones pueden añadir bindings tipados; no hay un catálogo universal VFX Lua/Nova.
