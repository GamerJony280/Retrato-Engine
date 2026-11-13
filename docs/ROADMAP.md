# Roadmap

## 0.7: publicación inicial de desarrollo

- Contratos explícitos de Frame y oclusión coherente entre backends.
- Presentación GPU directa de escritorio conservando captura.
- SDK CMake y servicios de scripting compartidos.
- Documentación de capacidades, ownership y límites verificables.

## Validación de distribución

- Ejecutar CI pública de Linux, Windows y ARM en el commit publicado.
- Compilar APK y verificar entrada, pausa, assets y guardado en Android.
- Medir GPU física y equipos de bajos recursos con escenas representativas.
- Revisar instalación y redistribución de dependencias por plataforma.

## Siguientes funciones

- Audio opcional detrás de API propia, con dependencia justificada.
- Swapchain Vulkan Android y composición GPU de UI.
- Streaming y descarte espacial con presupuestos medidos.
- Herramientas de recursos y ergonomía del compilador Nova según casos reproducibles.

No se implementan aún editor completo, networking, GC/JIT, generics, ABI dinámica,
reflection general ni hot reload complejo. Cada hito debe cerrar con pruebas y
contratos documentados; no se fija soporte por la sola existencia de archivos.
