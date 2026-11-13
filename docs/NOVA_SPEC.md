# Nova 0.2 - especificación implementada

Nova es un compilador pequeño escrito en C++17. Traduce un grafo local de archivos `.nova` a un módulo C++17 que enlaza con Ember. No interpreta código durante el juego. El compilador no forma parte del ejecutable del juego: este enlaza las cabeceras de soporte y las partes necesarias de Ember.

## Flujo real

`source -> lex -> parse -> AST -> analyze -> generate -> CMake/compiler C++ -> ejecutable`

`nova --emit-cpp archivo.nova -o modulo.cpp` valida todo el archivo antes de abrir la salida. Un error de Nova no sustituye la salida anterior. La herramienta genera código; CMake se encarga de invocar el compilador nativo y enlazar. No hay descargas ni ejecución de comandos construidos a partir del texto Nova.

Por defecto el resultado define `nova_generated::Program`. Para incluir varias fuentes generadas en un ejecutable, la aplicación define un `NOVA_PROGRAM_NAME` diferente antes de cada inclusión; esto evita colisiones de clase, además de los imports locales que fusionan un grafo en una clase. La compilación cruzada requiere `NOVA_HOST_COMPILER` apuntando al compilador ejecutable en el anfitrión; no se intenta ejecutar el compilador de la arquitectura destino. Los puertos de plataforma todavía requieren trabajo y validación propios.

```cpp
#define NOVA_PROGRAM_NAME PlayerProgram
#include "player.generated.cpp"
#undef NOVA_PROGRAM_NAME
#define NOVA_PROGRAM_NAME CameraProgram
#include "camera.generated.cpp"
#undef NOVA_PROGRAM_NAME
```

## Léxico y estructura

- Identificadores ASCII de letras, números y `_`, comenzando por letra o `_`.
- Comentarios `// hasta fin de línea` y `/* bloque */`, sin anidación de comentarios.
- Strings UTF-8 entre comillas dobles. Escapes: `\n`, `\r`, `\t`, `\"`, `\\`. No hay interpolación ni strings multilínea.
- Enteros decimales; decimales con fracción o exponente (`3.5`, `1e-2`). No hay hexadecimal.
- Sentencias simples terminadas por salto de línea, `;`, cierre de bloque o fin de archivo.
- Bloques con `{ }`. La indentación es visual. Se permiten saltos de línea tras un operador, alrededor de argumentos y antes del bloque. Para partir una expresión, deja el operador al final de la línea anterior.
- Máximo 1 MiB de fuente y anidamiento limitado a 128; el parser también limita cadenas largas de operadores. No es un formato para fuentes generadas gigantes.

Gramática abreviada:

```text
module    := (function | statement)*
function  := func name '(' parameters? ')' ('->' type)? block
statement := var name ':' type '=' expression
           | return expression?
           | if expression block (else (block | if-statement))?
           | while expression block
           | block | expression
block     := '{' statement* '}'
```

## Tipos

| Nova | Representación nativa | Regla |
|---|---|---|
| `int` | `std::int64_t` | Entero con signo; aritmética comprobada |
| `float` | `double` | Cálculo escalar; no implica doble precisión en el renderer |
| `bool` | `bool` | Sin conversión implícita desde números |
| `string` | `std::string` | Valor con ownership propio; puede asignar memoria |
| `Vec2`, `Vec3` | Valores matemáticos de Ember | Componentes float32; conversión comprobada al escribir |
| `Entity`, `Asset` | Handles opacos de `ember::game` | No son números ni punteros accesibles al script |

La única promoción numérica implícita es `int -> float`. No se convierte float a int. Una variable requiere tipo e inicializador. `Transform` no se declara como tipo del usuario: `transform` es el estado especial de la entidad controlada y ofrece `position: Vec3` y `yaw: float` en radianes. Esta restricción refleja el transform de instancia actual de Ember; no se promete escala o rotación completa para objetos de escena.

## Variables, funciones y scopes

```text
var health: int = 100
var speed: float = 2.0

func difference(a: int, b: int) -> int {
    if a > b { return a - b }
    else { return b - a }
}

func update(delta: float) {
    transform.position.x += speed * delta
}
```

Las funciones tienen parámetros por valor, variables locales y retorno opcional. Los bloques crean scopes. Un local puede ocultar un global; su inicializador se evalúa antes de declarar el local, por lo que `var health: int = health + 1` lee el global si existe. Duplicar un nombre dentro del mismo scope es un error. Los nombres de funciones, builtins, `self` y `transform` están reservados; `delta` puede ser un parámetro o variable y `delta()` sigue designando el builtin.

Las funciones se conocen antes de analizar sus cuerpos: hay llamadas adelantadas y recursión limitada. Una función con retorno debe retornar por todos los caminos que el análisis sencillo puede demostrar. No se infiere que `while true` siempre retorna: usa un `return` explícito después si necesitas satisfacer el análisis conservador.

Los globales y sentencias superiores se ejecutan en orden al construir `Program`, antes de `start()`. No se permite llamar funciones del usuario desde esa inicialización: evita dependencias ocultas con globales todavía no inicializados. Usa `start()` para esa lógica. Los builtins sí están disponibles allí. No hay closures, valores función ni sobrecarga.

## Operadores y evaluación

Precedencia, de menor a mayor:

| Nivel | Operadores |
|---|---|
| 1 | `= += -= *= /=`, asociativos a la derecha |
| 2 | `||` |
| 3 | `&&` |
| 4 | `== !=` |
| 5 | `< > <= >=` |
| 6 | `+ -` |
| 7 | `* / %` |
| 8 | Unarios `! - +` |
| 9 | Acceso `.`, llamadas y paréntesis |

Los demás binarios son asociativos a la izquierda. Operandos y argumentos se evalúan de izquierda a derecha mediante temporales en el C++ generado. `&&` y `||` mantienen cortocircuito. La asignación compuesta lee el valor anterior antes de evaluar su lado derecho. No se asigna a temporales ni al handle `self`.

- Números: aritmética y comparaciones. `/` entre enteros trunca hacia cero; `%` solo admite enteros.
- Bool: igualdad, desigualdad, operadores lógicos.
- String: concatenación `+`, igualdad y desigualdad.
- Vectores: `+` y `-` entre vectores del mismo tipo; `*` y `/` por escalar a la derecha. No hay producto vectorial implícito ni comparación vectorial.
- Componentes `.x/.y/.z` pueden leerse y escribirse sobre variables. No hay métodos `object.method()` en el MVP; el parser los reconoce, pero el análisis explica que debes usar funciones de la API.

Overflow entero, división por cero y resultados aritméticos no finitos producen errores controlados. La promoción de int64 a double puede redondear enteros grandes; los vectores tienen precisión float32. No se promete determinismo bit a bit entre arquitecturas.

## API Ember disponible desde Nova

Además de transform, entidades, entrada, señales y guardado, el catálogo incluye física opcional:

| Builtin | Resultado |
|---|---|
| `physics_box(entity, half_extents, dynamic)` | añade box estática o dinámica, sin rotación automática |
| `physics_sphere(entity, radius, dynamic)` | añade esfera estática o dinámica |
| `physics_has(entity)` / `physics_remove(entity)` | consulta o elimina el cuerpo |
| `physics_velocity(entity)` / `physics_set_velocity(entity, value)` | lee o escribe velocidad lineal |
| `physics_force(entity, value)` / `physics_impulse(entity, value)` | aplica fuerza o impulso |

Estas llamadas pasan por `game::Host`; no exponen Jolt. Requieren que el host se construya con `physics.backend = "jolt"`. Un build o proyecto sin física produce un error Nova localizado en ejecución, no accede a un puntero nulo. Masa, materiales, cápsula, sensores y rotación completa quedan por ahora en C++/Lua.

| Operación | Resultado / efecto |
|---|---|
| `create(name: string)` | `Entity`: caja unitaria, nombre único, origen |
| `entity(name: string)` | `Entity`: obtiene un objeto de la misma sesión |
| `load_asset(path: string)` | `Asset`: importación o `.embasset`, carga síncrona |
| `spawn(asset: Asset, name: string)` | `Entity`: instancia del recurso compartido |
| `alive(object: Entity)` / `destroy(object)` | Consulta o destruye una entidad; los handles viejos no reviven |
| `position(object: Entity)` | `Vec3`: copia de la posición local |
| `set_position(object: Entity, value: Vec3)` | Escribe la posición local |
| `key_down`, `mouse_*`, `gamepad_*` | Estado de entrada entregado por la aplicación anfitriona |
| `save_set_*`, `save_get_*`, `save_has` | Estado persistente tipado en la instancia Program |
| `save_write(path)`, `save_read(path)` | Escribe o reemplaza el estado desde un `.embsave` validado |
| `time()` | `float`: tiempo simulado acumulado de Ember |
| `delta()` | `float`: paso fijo configurado; no tiempo de render |
| `emit(code: int)` | Encola una señal entera para esta sesión |
| `print(value)` | Imprime un escalar o vector y salto de línea |
| `Vec2(x,y)`, `Vec3(x,y,z)` | Constructores de valores |

`self` es la entidad que entrega la aplicación nativa. `transform` refleja su posición y yaw. Se lee al entrar a un callback y se escribe al terminar correctamente si `self` sigue vivo. `position(self)` y `set_position(self,...)` usan ese mismo estado para no pisar escrituras. `destroy(self)` impide confirmar el transform y desconecta el callback al terminar; el resto de la función todavía puede leer su snapshot local. Una excepción no confirma el transform de self; otras operaciones ya realizadas sobre el mundo no se revierten. No es un sistema transaccional.

## Ciclo de vida y señales

Funciones opcionales con firmas comprobadas:

```text
func start() { print("ready") }
func update(delta: float) { transform.position.x += delta }
func on_signal(code: int) { print(code) }
```

`start` corre una vez por instancia de Program. `update` se registra en el paso fijo de Host. `on_signal` recibe todas las señales de esa sesión. Destruir Program desconecta ambas conexiones. Program no se copia ni se mueve; conserva una copia compartida de Host que mantiene vivo el runtime.

Las señales se despachan después de los callbacks de actualización de cada paso fijo. Las emitidas por receptores se difieren al siguiente paso. El orden es FIFO; los receptores se llaman en orden de registro. Máximo 4096 señales pendientes. Los callbacks registrados durante un despacho empiezan en el siguiente despacho; desconectar uno pendiente evita que se invoque. Si un receptor lanza una excepción, Host intenta los demás receptores y conserva la primera para propagarla al terminar el paso.

Se admiten varias instancias del mismo Program. Usa distintas entidades self; si varios scripts escriben la misma entidad, gana el último callback. Los globales que crean nombres fijos pueden colisionar al construir una segunda instancia: los nombres son únicos por Host. Cada Program posee su propio `SaveData`; compartir una partida entre módulos requiere que la aplicación coordine el archivo o añada una instancia compartida explícita.

## Errores y protección mínima

`archivo.nova:línea:columna: error: mensaje` para errores léxicos, sintácticos y semánticos. Se informa el primer error; no hay recuperación múltiple todavía.

El C++ contiene directivas `#line` y cada nodo del AST conserva archivo/línea/columna. Expresiones y condiciones envuelven errores de ejecución con su localización Nova. Las pruebas nativas comprueban la línea de división por cero y de un bucle que agota su presupuesto.

El compilador nativo se invoca a través de CMake. Si hay un defecto del generador o un error de toolchain, todavía puede aparecer un diagnóstico C++ (las líneas Nova se conservan donde corresponde). No existe aún un traductor completo de diagnósticos de GCC/Clang/MSVC; los errores internos del glue pueden indicar `<nova-host>`. Es una limitación explícita, no una promesa de ocultarlos todos.

Cada entrada de ciclo de vida tiene presupuesto de 100000 iteraciones/entradas de función y profundidad de llamadas 128. Esto detiene bucles infinitos y recursión comunes. No constituye aislamiento frente a código no confiable: carga de assets, crecimiento de strings y creación de objetos pueden consumir recursos. El host es monohilo y no permite `tick` recursivo.

## Fuera del MVP

No hay GC, LLVM, JIT, namespaces/paquetes, clases, herencia, arrays dinámicos/anidados, generics, reflexión, async, macros, coroutines, editor, debugger gráfico, LSP, ABI dinámica ni hot reload. Nova 0.2 sí expone imports, arrays fijos, clips, cámara, luces y queries de cajas. No expone raycast BVH, materiales generales, UI, audio, contactos Jolt ni renderer directamente.


## Extensiones implementadas en 0.2

### Imports

`import "state.nova"` carga rutas relativas al archivo que importa, dentro del
árbol de la carpeta del archivo principal. Se resuelven symlinks, se rechazan
rutas absolutas, escapes fuera del root y ciclos. Los imports se cargan una vez,
antes del startup del consumidor; sus nombres pertenecen al mismo scope global.
No son namespaces/paquetes. Máximo 32 archivos, 1 MiB por archivo y 4 MiB en total.
`compile_file` es la API con imports; `compile(source,file)` no lee el sistema de
archivos y rechaza imports. `--depfile` y `nova_add_module` hacen que CMake regenere
al editar archivos importados. Salida y depfile no pueden pisar fuentes del grafo.

### Colecciones

```text
var positions: Vec3[] = array_vec3(32, Vec3(0,0,0))
var enemies: Entity[] = array_entity(4, self)
positions[2].x += 1
print(len(positions))
```

Tipos `int[]`, `float[]`, `bool[]`, `string[]`, `Vec2[]`, `Vec3[]`, `Entity[]`,
`Asset[]`. Factories `array_int/float/bool/string/vec2/vec3/entity/asset(count,fill)`.
Tamaño fijo 0..8192, índice int comprobado, sin resize/push/pop, arrays anidados ni
literals de lista. Inicializar una colección desde otra crea una copia de valor.
Los parámetros array son referencias const, sin copia; no se pueden modificar.
No hay retorno de arrays de funciones del usuario, ni reemplazo de un array
entero por asignación. Así un lvalue indexado no se invalida durante su RHS.
Bool tiene storage real, no proxies `vector<bool>`. Los fallos de índice/size
conservan posición Nova. El código generado usa moves para inicializaciones.

### Servicios de juego

- `create_box(name,size,color)`, `set_box(entity,size,color)`, `set_visible(entity,bool)`.
- `camera(position,target)`, `light(index,position,color,radius)` (índice 0..63; radio 0 desactiva).
- `collider_box(entity,half,mask)`, `collider_remove(entity)`; cajas axis-aligned.
- `sweep(from,delta,radius,mask) -> Sweep`: hit/fraction/position/normal/entity.
- `slide(from,delta,radius,mask) -> Slide`: position/normal/remaining/contacts.
- `play_animation(entity,clip,loop)`, `unload_asset(handle)`.
- `key_pressed(code)`, `key_released(code)`, además de estados de entrada previos.
- `abs/sin/cos/sqrt/min/max/clamp` escalares; `length/normalize/dot` Vec3.
- `str` de escalar/string, `len` de colección. No hay conversión float->int.

Masks enteras 0..UINT32_MAX; queries lineales sobre cajas del Host, no el BVH de
triángulos ni la física Jolt. Yaw no rota estas cajas. `Sweep`/`Slide` son valores,
no referencias internas. Las cajas siguen posición local de entidades sin
attachments. No sustituyen los colliders Runtime de malla. Los deltas de entrada
se consumen por paso fijo: tap completo entre frames conserva ambos flancos.
`unload_asset` expira ese handle y quita la entrada fuerte de caché; instancias
vivas mantienen su recurso. Recargar puede dar otro handle.

### Extensiones C++ y coexistencia

```text
native func sparks(position: Vec3, count: int)
func score() -> int { return 100 }
```

La aplicación incluye su header con `NOVA_NATIVE_HEADER`; implementa
`nova_native::fn_sparks(ember::game::Host&, ember::Vec3, std::int64_t)`.
No pasan punteros arbitrarios del script. Los bindings nativos no son lifecycle,
no admiten arrays y requieren firma C++ correspondiente. Son C++ de confianza:
no se sandboxea una implementación nativa y sus errores de toolchain pueden
seguir siendo errores C++, mapeados por `#line` cuando corresponda.

Funciones normales tienen entradas públicas tipadas `Program::call_nombre(...)`.
Refrescan transform, reinician presupuesto, ejecutan y confirman si self sigue
vivo. Permiten a C++ llamar Nova. Se rechaza reentrada activa del mismo Program;
no destruyas/muevas Program dentro de su propio callback. Los arrays de parámetros
son const también desde C++. No hay reflection ni lookup dinámico por nombre.

`Host(shared_ptr<Runtime>)` permite que C++, Lua y Nova compartan escena/eventos.
Conserva una `Runtime::observe` conexión para entregar eventos a `Script`; el
Script Lua no se suscribe solo. `nova_mixed` prueba los tres a la vez. Un puente
Lua->C++->Nova requiere binding explícito en la aplicación; no se inventa una
ABI universal ni se exponen layouts de renderer. Los servicios VFX pueden integrarse mediante bindings tipados; no son builtins universales.

## Integración y toolchain

`nova_add_module(target, source)` genera C++ durante la compilación; sus imports
se registran como dependencias. El módulo se enlaza con el mismo Runtime/Host que
C++ y Lua. Los puentes nativos deben validar entradas y conservar suscripciones.

El protocolo de módulos del compilador es 2; la sintaxis implementada es 0.2.
La compilación cruzada requiere `NOVA_HOST_COMPILER` ejecutable en el anfitrión.
No se ejecuta un compilador ARM de destino durante la compilación x86.
El compilador Nova no es necesario en el ejecutable distribuido.

`--emit-cpp` conserva código determinista y directivas `#line`. Los nombres
ordinarios se generan como `nv_nombre`; los identificadores reservados de C++
usan un prefijo separado y codificación hexadecimal. Fallos del toolchain o de
bindings nativos aún pueden producir diagnósticos C++.
