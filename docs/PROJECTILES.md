# Proyectiles reutilizables

`ember/projectiles.hpp` es un módulo header-only de datos, independiente de
Runtime, render, partículas, SDL y un género de juego. Capacidad elegida al crear
el pool, IDs monotónicos, radio, velocidad, vida y payload propio del juego.
Reserva datos e impactos al construirlo; no asigna memoria por disparo/update.

```cpp
ember::ProjectilePool bolts(24);
ember::Projectile p;
p.position = {0, 1, 0};
p.velocity = {26, 0, 0};
p.life = 1.1f;
p.payload = 4; // índice de arma/equipo elegido por el juego
if (auto id = bolts.spawn(p)) { /* disparo aceptado */ }
bolts.update(delta, {mi_barrido, &mi_mundo});
for (const auto& impact : bolts.impacts()) { /* daño, sonido/VFX */ }
```

La consulta tiene firma `bool(Vec3 from, Vec3 delta, float radius,
ProjectileContact& out, const void* borrowed_context)`. Debe devolver el contacto
más cercano del segmento y `fraction` en [0,1]. El contexto nativo solo se presta
durante la llamada; no es un puntero que se exponga a Nova. Sin consulta los
proyectiles se mueven libremente. Mask, equipos, rebote/perforación y daño son
política del consumidor.

El último paso usa `min(delta, life)`: una bala que expira puede impactar durante
ese último tramo. El barrido no sustituye la geometría por substeps discretos.
El barrido esfera/AABB de Ember es conservador en esquinas; la consulta puede
usar otro sistema de colisión. Los impactos se conservan hasta el siguiente
`update` o `clear`. Los IDs no son índices y no se reciclan al vaciar.

Límites: capacidad 0..65536, exceso descartado con ID 0, sin crecimiento de pool,
sin renderer impuesto ni daño automático. Valores no finitos se rechazan; un
movimiento que desborda retira el proyectil. No se permite spawn/clear/update
reentrante durante una consulta. Consulta/contexto y pool deben sobrevivir a
la llamada; no pueden moverse/destruirse durante ella. Un error de usuario no
implica rollback de un update parcialmente avanzado.

`ember_game_services_tests` verifica pared delgada, último tramo de vida, capacidad,
impactos y rechazo de mutaciones reentrantes.
