# Interfaz retenida opcional

`EMBER_RMLUI=ON` construye `retrato::ui` desde las fuentes locales RmlUi 6.0.
Requiere SDL2 y FreeType. No se incorpora Chromium, Gecko, JavaScript ni red.
RML/RCSS son un subconjunto de tecnologías de interfaz, no HTML/CSS de navegador.

`UiOverlay` posee contexto, documento y textura CPU; admite texto, markup,
visibilidad y callbacks de click. La aplicación remite posiciones y botones.
`pixels()` devuelve memoria prestada para composición. El painter reutiliza el
resultado cuando no hay invalidación; animaciones, entrada o cambios pueden
provocar trabajo adicional. No hay promesa de coste cero o memoria fija universal.

Solo se admite un overlay vivo por las interfaces globales de RmlUi. El backend
de imágenes de UI admite BMP; no comparte todos los decodificadores de modelos.
La aplicación genérica no mezcla este overlay sobre la presentación GPU directa.
La composición actual requiere integración propia y puede implicar readback.
`retrato::ui` no se exporta todavía como componente del SDK instalado.
