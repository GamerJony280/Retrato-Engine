# Configuración

`Config::load` lee cuatro secciones de un subconjunto estricto de TOML: claves
conocidas, strings con comillas, enteros, floats y bools. Rechaza claves duplicadas,
valores fuera de rango y entradas desconocidas. No es un parser TOML general.
`Config::validate` aplica límites también a estructuras creadas desde C++.

## Gráficos

| Clave | Valores | Efecto |
|---|---|---|
| `backend` | auto/vulkan/opengl/cpu | Selección compilada y disponible |
| `profile` | normal/ps1/n64/pixel | Estilo de raster y color |
| `filter` | normal/crt | CRT independiente del perfil |
| `resolution_quality` | 1–100 | Ancho 160–960 por pasos de 16, altura 9/16 |
| `lighting_quality` | 1–100 | Cuadrícula mundial de sombra pixel: 2–32 celdas/unidad, solo perfil pixel |
| `shadow_distance` | 4–128 | Ancho de la región de sombra solar alrededor del objetivo de cámara |
| `shadow_quality` | 1–100 | 1–33:128²; 34–66:256²; 67–100:512² |
| `toon_bands` | 2–16 | Bandas direccionales del perfil pixel |
| `local_lights_per_object` | 0–4 | Selección por objeto de luces relevantes según influencia |
| `shadowed_lights` | 0–1 | Sombra solar apagada/activa |
| `pixel_upscale` | bool | Aplicación genérica: nearest si true, lineal si false |
| `ps1_depth_test` | bool | false: pintor clásico; true: profundidad, conservando estilo PS1 |

El ancho es `160 + 16 * floor((quality-1)*800/(99*16))`: 35 produce 432×243.
La resolución de imagen, densidad artística de sombra y tamaño del mapa son
controles independientes. Los visores de herramientas pueden conservar sus
propias políticas de escalado; la tabla de presentación corresponde a `ember_app`.

## Cocinado de colisión

| Clave | Valores y estado |
|---|---|
| `quality` | 1–100; cambia hojas BVH entre 2–16 triángulos, conservando geometría |
| `static_shape` | mesh_bvh |
| `dynamic_shape` | convex_compound: reservado, no descomposición implementada |

El tamaño de hoja cambia el coste de consulta y organización, no la precisión
de la superficie. Los barridos esfera/AABB son conservadores en esquinas.

## Física

| Clave | Valores y estado |
|---|---|
| `backend` | none/jolt; Jolt exige EMBER_JOLT=ON y creación de cuerpos |
| `plane` | 3d/xy/xz/yz para grados de libertad de cuerpos rígidos |
| `gravity_x/y/z` | Gravedad del mundo Jolt; por defecto 0,-9.81,0 |
| `solver_quality` | 1–100; iteraciones acotadas de Jolt, sin efecto con none |
| `fixed_hz` | 15–240; frecuencia de actualización |
| `continuous_collision` | selected/off; política por cuerpo avanzado, independiente de consultas arcade |
| `max_bodies` | 64–1000000 |
| `max_body_pairs` | 64–4000000 |
| `max_contacts` | 64–1000000 |
| `temporary_mb` | 1–1024; memoria temporal del solver |

## Runtime

| Clave | Valores y estado |
|---|---|
| `worker_threads` | 0: monohilo; -1: automático con hasta 3 adicionales; 1–64: explícito |
| `streaming` | Reservado; no activa carga/descarga espacial |

Un archivo de configuración no añade módulos al ejecutable. Los valores
reservados no deben interpretarse como funciones activas.
