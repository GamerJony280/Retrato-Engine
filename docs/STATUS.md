# Estado de desarrollo

Retrato 0.7.0 es una publicación inicial de desarrollo. Puede servir como base
para juegos low-poly con C++, Lua opcional y Nova transpilado a C++.
No establece una ABI binaria permanente ni una garantía de ausencia de errores.

| Área | Implementado | Limitaciones |
|---|---|---|
| Render | CPU, Vulkan, OpenGL, perfiles y CRT | Preparación/sombras en CPU; materiales simplificados |
| Presentación | Directa GPU en aplicación SDL | Android usa lectura de píxeles; UI no se compone automáticamente |
| Recursos | OBJ/MTL, FBX, glTF/GLB, formato precocinado | No todos los features de los formatos; ver tabla de importación |
| Animación | Esqueleto y clips TRS, attachments | Sin editor, grafo de blend o retargeting general |
| Física | Consultas BVH, arcade; Jolt opcional | Jolt no vendorizado; compuestos cocinados pendientes |
| Scripting | C++, Lua 5.4, Nova 0.2 | Puentes explícitos; Lua no es sandbox |
| UI | RmlUi retenido y painter CPU | Un overlay activo; no navegador ni CSS completo |
| VFX | Pools, emisión, colisión y lotes | Sin simulación GPU, blend aditivo ni bloom |
| Persistencia | Assets/colisión/guardado binarios versionados | No serialización automática universal de escena |
| Plataforma | Adaptadores Linux/Windows/Android | Validación de esta entrega: Linux x86-64 |

Pendientes funcionales: audio integrado, streaming real, controlador Android
con swapchain, composición GPU de UI y mediciones de hardware destino.
`runtime.streaming` y `collision_cooking.dynamic_shape` son reservas de esquema.
No reducen memoria ni activan física por estar escritos en el TOML.

Los resultados exactos de pruebas y sus exclusiones están en [VALIDATION.md](VALIDATION.md).
