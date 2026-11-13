# API pública C++ y Lua

## C++

Enlaza la biblioteca `ember` desde CMake. La escena, cámara y eventos son independientes del backend. Ejemplo mínimo:

```cpp
#include <ember/runtime.hpp>

int main() {
    ember::Config cfg = ember::Config::load("config/engine.toml");
    cfg.backend = "cpu";
    ember::Runtime game(cfg);
    const auto actor = game.add_object({
        ember::box({2, 2, 2}, {0.8f, 0.3f, 0.1f}), {0, 1, 0}
    });
    game.on_event = [actor](ember::Runtime& g, const ember::Event& e) {
        if (e.type == ember::Event::Type::FixedUpdate)
            g.object(actor).yaw = float(e.time);
    };
    auto gpu = ember::make_backend(cfg.backend, cfg.worker_threads);
    ember::SceneRenderer render;
    game.start();
    game.tick(1.0 / cfg.fixed_hz);
    render.prepare(game.scene, game.camera, game.config, game.frame);
    gpu->draw(game.frame);
    ember::write_ppm("output.ppm", game.frame.width, game.frame.height, gpu->readback());
}
```

El visor conecta SDL a `Runtime::dispatch`; una aplicación propia puede usar otra capa de plataforma. La UI es un módulo opcional. Los callbacks son síncronos; el usuario serializa cambios de escena y llamadas al backend. Si uno falla, Ember intenta los listeners pares y señales ya encoladas, completa animación/attachments del paso actual y después propaga la primera excepción. No existe rollback de cambios arbitrarios hechos por el callback.

Para objetos que nacen y mueren durante el juego usa `Runtime::add_object` / `destroy_object`,
o `game::Host::create` / `destroy` (Lua/Nova ya usan esta ruta). Los slots destruidos
se reutilizan mediante una lista libre, normalmente en O(1), con una identidad nueva.
Un millón de creaciones con un pico de 201 vivos mantiene 201 slots en la regresión.
La lista y la capacidad de la escena conservan el pico histórico; no son un compactador.
`clear_scene()` elimina los registros. Tick/render todavía recorren todos los slots
reservados: una escena que alguna vez tuvo muchísimos objetos puede seguir costando más.

`scene.objects.push_back` es acceso de bajo nivel de **solo crecimiento**: no consulta
la lista libre. Añadir así cada bala/enemigo hará crecer memoria y recorridos aunque
luego se destruya. Poner `active=false` directamente tampoco registra un hueco en
la API; para ocultar sin destruir usa `visible=false`. Borrar/reordenar/reemplazar
el vector expira handles por índice/identidad y exige limpiar registros externos;
ningún puntero/referencia C++ a un elemento sobrevive garantizadamente a una inserción.

## Lua 5.4

El script se carga una vez. Las funciones ausentes se omiten:

```lua
function start() end
function update(time, unused) end
function key_down(time, keycode) end
function key_up(time, keycode) end
function mouse_move(time, x, y, dx, dy) end
function mouse_down(time, button, x, y) end
function mouse_up(time, button, x, y) end
function mouse_wheel(time, x, y) end
function gamepad_button_down(time, device, button) end
function gamepad_button_up(time, device, button) end
function gamepad_axis(time, device, axis, value) end
function cue(time, id) end
```

| Función | Parámetros / retorno |
|---|---|
| `engine.clear()` | Vacía objetos, luces y colliders |
| `engine.box(x,y,z,sx,sy,sz,r,g,b)` | Crea caja; devuelve ID de objeto |
| `engine.mesh(path,x,y,z)` | Carga OBJ/FBX/glTF/GLB/embasset; devuelve ID |
| `engine.position(id,x,y,z)` | Mueve objeto |
| `engine.rotation(id,yaw)` | Rotación en radianes alrededor de Y |
| `engine.camera(x,y,z,tx,ty,tz)` | Posición y punto de mira |
| `engine.light(x,y,z,r,g,b,radius)` | Añade luz local |
| `engine.destroy(id)` | Destruye el objeto, sus attachments/colliders y expira el handle |
| `engine.cue(time,id)` | Programa marcador futuro |
| `engine.collider(object)` | Cocina una BVH local una sola vez; devuelve ID de collider |
| `engine.raycast(collider,ox,oy,oz,dx,dy,dz,max_distance)` | Devuelve distancia, posición y normal (7 números), o nil |
| `engine.save_set_*` / `save_get_*` | Estado tipado int/float/bool/string/Vec3 |
| `engine.save_has`, `save_clear`, `save_write/read` | Consulta, limpia y persiste `.embsave` |

Los colliders reutilizan la malla cocinada cuando se cambia posición/rotación del objeto. No reflejan ediciones de sus vértices posteriores al cocinado. Los IDs Lua son opacos y monotónicos durante la vida de `Script`; internamente resuelven `ObjectRef`/`ColliderRef`, no índices desnudos. `destroy` y `clear` invalidan los anteriores y una mutación estructural desde C++ provoca un error de referencia expirada antes de tocar un reemplazo. `Script` no posee el Runtime y rechaza eventos si este ya murió. No hay escalas no uniformes.

Para código C++ de bajo nivel, `Runtime::object_ref(index)` captura la identidad actual y `Runtime::object(ref)` la comprueba. Reallocar el vector conserva IDs; borrar, sustituir o reordenar expira las referencias cuyo índice/identidad ya no coinciden. `Runtime::attach`, `add_collider` y `raycast` aceptan referencias comprobadas. No se deben fabricar referencias ni mutar directamente los registros internos.

Los eventos de UI se conectan desde C++; Lua dispone de teclado, mouse y gamepad. Un puente genérico de eventos DOM a Lua queda pendiente. Las operaciones pesadas se ejecutan en C++, no recorriendo píxeles desde Lua.

Lua abre sus bibliotecas estándar. Es adecuado para juegos propios y scripts de confianza, no un entorno seguro para ejecutar mods desconocidos. Errores del script se reportan y se propagan al llamador en lugar de seguir con un estado incompleto.

## Otros lenguajes

`include/ember/c_api.h` declara un ABI C versión 1, sin clases C++ ni excepciones atravesando la frontera. Puede enlazarse desde Rust, C#, Python o cualquier FFI compatible con el ABI de la plataforma. **Eso no constituye un port completo**: hay que crear bindings, completar la superficie pública y respetar la propiedad de memoria.

- `ember_create(config, backend)` / `ember_destroy(handle)`.
- `ember_add_box`, `ember_set_position`, `ember_set_camera`.
- `ember_render` / `ember_pixels`.
- `ember_last_error` devuelve un texto por hilo.

Los píxeles pertenecen al backend y pueden invalidarse en la próxima llamada de render/readback o al destruir el motor. Cada `uint32_t` expresa R en los bits 0–7, G en 8–15 y B en 16–23. No conservar punteros tras destruir el propietario.

## Colisiones nativas

```cpp
ember::CollisionMesh collision;
collision.cook(ember::load_obj("assets/cube.obj"), 70);
collision.save("cube.embcol");
auto ready = ember::CollisionMesh::load("cube.embcol");
ember::Hit hit;
bool found = ready.raycast({0, 0, -4}, {0, 0, 1}, 10, hit);
```

`EMBCOL02` guarda posiciones compactas, índice del triángulo fuente y nodos BVH en little-endian con floats IEEE-754 de 32 bits. El cargador conserva compatibilidad de lectura con `EMBCOL01`, limita el payload a 256 MiB y verifica tamaños, finitud, índices y profundidad. No es una promesa de formato estable entre futuras versiones ni un parser endurecido para cualquier archivo hostil. El importador OBJ está pensado para assets locales propios y caras convexas o trianguladas.

## Modelos y animación (0.3.1)

La guía completa de `Asset`, `Animator`, `Runtime::add_model`, `Runtime::attach`, funciones Lua y ampliaciones del C ABI está en [IMPORT_AND_ANIMATION.md](IMPORT_AND_ANIMATION.md). `load_obj()` de `collision.hpp` permanece como lector mínimo de geometría para herramientas de colisión; para modelos visuales con materiales usa `import_asset()` o `Runtime::add_model()`.
# Frontera adicional para Nova y nuevas aplicaciones C++

La API de `ember/game.hpp` es independiente del ABI C y del binding Lua descritos en este documento. `game::Host` conserva un Runtime propio o recibe un shared_ptr<Runtime> existente; sus copias comparten sesión. `create/entity/load_asset/spawn` devuelven handles opacos comprobados. `transform/set_transform/position/set_position` operan por valor. `on_update/on_signal` devuelven una `Connection` move-only que debe conservarse para seguir suscrito. `emit` encola una señal int64; `tick/time/delta` acceden al paso fijo. `render` solo se ofrece al anfitrión C++.

`game::Transform` contiene posición local y yaw en radianes; no es el TRS de huesos de `asset.hpp`. `alive/destroy` controlan entidades individuales; destruir expira el handle y permite reutilizar el nombre sin reciclar la identidad. `on_input` recibe `InputEvent`, mientras las consultas de teclado/mouse/gamepad permiten polling desde Nova. Host y listeners son monohilo. No capturar recursos propios formando ciclos permanentes: conservar y desconectar conexiones al terminar. El código generado lo hace automáticamente.

La física avanzada, si fue compilada y activada, se maneja con `add_rigid_body`, `has_rigid_body`, `remove_rigid_body`, `rigid_body_state`, `set_velocity/velocity`, `add_force` y `add_impulse`. `BodyDesc` permite box/sphere/capsule, static/kinematic/dynamic, masa, fricción, restitución, damping, gravedad, sensor, CCD y rotación. La entidad sigue siendo el dueño lógico: `destroy` elimina también su cuerpo. [PHYSICS.md](PHYSICS.md) documenta CMake y 2.5D.

Ejemplo nativo de propiedad y actualización:

```cpp
ember::Config config;
ember::game::Host host(config);
auto player = host.create("player");
auto connection = host.on_update([&](float dt) {
    auto t = host.transform(player);
    t.position.x += 2.f * dt;
    host.set_transform(player, t);
});
host.tick(1.0 / 60.0);
connection.disconnect();
```

El guardado no serializa Scene ni punteros. `ember::SaveData` conserva únicamente claves y valores explícitos (`int64`, `double`, `bool`, `string`, `Vec2`, `Vec3`) y escribe `.embsave` mediante `write`; `SaveData::read` valida y reconstruye el mapa. Esto mantiene los archivos independientes de layouts internos.

Incluye `ember/config.hpp` y `ember/game.hpp`. La variable host debe seguir viva durante ese callback que la referencia por `&`. Para Nova, consulta [NOVA_SPEC.md](NOVA_SPEC.md). Las funciones anteriores siguen disponibles. El ABI C histórico todavía usa índices y no expone Jolt; Lua y Nova usan handles comprobados con sus propios registros.


## Compartir mundo

```cpp
auto runtime = std::make_shared<ember::Runtime>(config);
ember::game::Host host(runtime);
auto player = host.create("player");
auto update = host.on_update([&](float dt) {
    auto p = host.position(player);
    p.x += 2 * dt;
    host.set_position(player, p);
});
auto events = runtime->observe([](ember::Runtime&, const ember::Event&) {});
host.tick(1.0 / 60);
```

Conserva conexiones; temporales se desconectan inmediatamente. No captures Host
propietario en sus propios listeners creando ciclos. on_event legado se reemplaza
fuera de su llamada activa; observe permite auto-desconexión segura. Las altas en
un despacho esperan al siguiente, las bajas pendientes evitan invocación y las
closures mutables conservan estado. No hay rollback de mutaciones de usuario.

set_visible solo afecta render/sombras; set_box no modifica colliders explícitos.
unload_asset expira handle/caché, conservando instancias. set_camera, set_light,
play_animation y queries sweep/slide pasan por Host sin layouts de render. Los
colliders de cajas con máscaras son axis-aligned, O(n), sin yaw/attachments/BVH/Jolt.
key_pressed/released conservan flancos por paso fijo; reset_input libera al perder foco.
Después de borrados nativos externos usa prune_destroyed para soltar metadatos.

Lua Script no se suscribe solo: conserva una observe que entregue Script::event,
y desconéctala antes de destruir Script. update Lua recibe (time, code), code=0
en paso fijo; no el delta Nova. nova_mixed demuestra C++, Lua y Nova moviendo tres
actores en una escena. Invocar funciones de un lenguaje desde otro requiere un
binding C++ explícito; Program ofrece entradas call_nombre tipadas.


## Aplicación portable y scripts de la sesión

El nuevo módulo `retrato::app` es opcional. `ember::app::run(Options, Callbacks)`
posee ventana, paso fijo y adaptación de entrada; el gameplay usa Host.
C++ puede pasar un shared_ptr<Runtime> como tercer argumento para conservar
acceso completo a su escena/VFX/servicios; la configuración de ese mundo manda.
Los callbacks son start, update, touch, suspend y running. start recibe las rutas
resueltas; running permite solicitar salida sin acoplarse al sistema.
La [plantilla](../templates/game/README.md) implementa retrato_game_main;
retrato_add_game elige main de escritorio o android_main de NativeActivity.

`Host::load_script(path_utf8)` carga un Lua opcional en la misma sesión, ejecuta
start y conserva su conexión a eventos. Host lo posee hasta cerrar la sesión;
la conexión se desconecta antes de destruir el script. Se carga fuera del tick,
normalmente desde start de la app. Si falta Lua en el build se informa un error.
No hay recarga en caliente ni un puntero a Runtime en Nova para hacer esto.

`platform::paths(org, app, content_override)` resuelve recursos y almacenamiento
privado. `platform::resource(paths, relative)` valida el nombre relativo y en
Android materializa un asset del APK por bloques para las APIs de archivo.
Usa `.u8string()` al pasar un filesystem::path a APIs públicas que reciben string.
Las búsquedas require/dofile de módulos Lua de terceros siguen las reglas de Lua;
la carga inicial de Script/Host y los formatos propios usan rutas UTF-8.
