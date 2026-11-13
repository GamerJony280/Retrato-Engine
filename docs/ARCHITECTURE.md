# Arquitectura y ownership

| Componente | Posee | Coordina o referencia |
|---|---|---|
| `Asset` | Mallas, jerarquía, materiales y clips importados | Recurso reutilizable |
| `Object` | Geometría de instancia, transform, Animator opcional | Materiales/imágenes con ownership compartido |
| `Scene` | Objetos y luces colocados | No posee dispositivos de render |
| `Runtime` | Escena, cámara, eventos, registros de colisión y física opcional | Animación, attachments y pasos fijos |
| `game::Host` | Sesión compartida, nombres, handles y servicios de gameplay | Frontera estable para scripting |
| `SceneRenderer` | Buffers temporales, caché de texturas y sombra | Toma prestada Scene durante prepare |
| `Backend` | Recursos del dispositivo y captura | No conserva punteros a Scene |
| `ember_app` | Ventana, entrada, pausa y pacing | Host y Backend |

Runtime coordina servicios relacionados con una sesión. El renderer, física,
importación y aplicación conservan implementaciones independientes. No se añade
un ECS ni un contenedor universal de servicios. La frontera de scripting es
Host, sin pointers arbitrarios, layouts del renderer ni handles de GPU.

## Vidas útiles

Las imágenes y materiales se comparten mediante `shared_ptr<const ...>`. Los
assets almacenados en Runtime usan caché débil; las instancias/animadores retienen
los recursos necesarios. Host mantiene handles de assets y permite descargar
su caché sin invalidar instancias ya creadas. Las mallas de instancia se copian
cuando se requiere geometría mutable o animada; no hay instancing GPU general.

`ObjectRef` combina índice e identidad. Copiar un Object crea otra identidad;
moverlo conserva la identidad. `add_object` reutiliza slots publicados por
`destroy_object`; los handles viejos no representan el reemplazo. `ColliderRef`
y `BodyRef` se comprueban antes de acceder. Destruir objetos retira sus vínculos.
Una referencia C++ prestada a un elemento de un vector puede invalidarse al
insertar o reordenar: conserva handles, no referencias permanentes a elementos.

Mutar directamente `Scene::objects` es acceso de bajo nivel. `push_back` no usa
la lista libre; `active=false` no publica un slot reutilizable. Para ocultar usa
`visible=false`. El coste de recorridos y capacidad conserva el máximo histórico
de slots; no hay compactación automática de escena.

Host comparte Runtime al copiarse. `Script` toma prestado Runtime y comprueba
su token de vida. Las conexiones de eventos son RAII: conserva la conexión
mientras necesites recibir eventos. Los callbacks son síncronos; no existe
rollback de mutaciones arbitrarias si uno lanza una excepción.

## Colisión y representación

CollisionMesh copia posiciones/índices compactos y construye su BVH. No necesita
materiales, UV ni texturas para consultar. Cocinar usa la geometría disponible
en ese momento; no sigue deformación de skinning ni edición posterior de vértices.
Las consultas arcade de Host usan cajas independientes, axis-aligned y O(n).
No heredan yaw ni attachments. Esta separación permite incorporar otras formas
sin convertir los datos de render en el estado del solver.

## Concurrencia

Una sesión de simulación se modifica desde un hilo. Raster CPU y Jolt pueden
usar trabajadores internos. No se permite mutar Scene mientras prepare/draw usa
sus datos. No se promete determinismo de Jolt entre arquitecturas. El driver
nativo vive más que Backend; el contexto/dispositivo se destruye antes de ventana.
