# Publicación de fuentes

La publicación inicial se etiqueta como versión de desarrollo 0.7.0. El alcance
validado consta en STATUS y VALIDATION; no equivale a soporte completo de todos
los destinos ni a una versión 1.0 con ABI estable.

## Verificación

```sh
cmake --preset cpu
cmake --build --preset cpu --parallel 2
ctest --preset cpu
python3 tools/package_source.py dist/Retrato-Engine-v0.7.0.zip
```

Comprueba además los módulos modificados, las licencias y los workflows del
commit que se publicará. El paquete incluye fuentes/recursos/licencias y un
manifiesto SHA-256; excluye builds, cachés, binarios del sistema y notas internas.
La extensión `.obj` se conserva porque también representa modelos 3D.

## Crear un repositorio

Crea en GitHub un repositorio vacío para evitar conflictos con archivos iniciales.
Desde la raíz descomprimida, sustituye la URL por la del repositorio real:

```sh
git init -b main
git add .
git status --short
git commit -m "Initial development release 0.7.0"
git remote add origin https://github.com/USUARIO/REPOSITORIO.git
git push -u origin main
```

Revisa `git status` antes del commit: no deben aparecer archivos personales,
SDKs ni builds. El contenido de la carpeta del motor debe quedar en la raíz del
repositorio, sin el ZIP dentro. Autentica con el método que GitHub indique; no
escribas tokens dentro de scripts o archivos del proyecto.

Después del push revisa Actions. Cuando pasen los jobs aplicables, crea el tag
y una Release con sus límites de plataforma y notas de cambios:

```sh
git tag -a v0.7.0 -m "Retrato Engine 0.7.0 development release"
git push origin v0.7.0
```

No publiques un SDK binario como si fuera portable a todos los destinos. Los
jugadores reciben el ejecutable y contenido de su juego, más dependencias y
licencias redistribuibles; no requieren Nova CLI ni compiladores.

Referencia: [procedimiento oficial de GitHub](https://docs.github.com/en/migrations/importing-source-code/using-the-command-line-to-import-source-code/adding-locally-hosted-code-to-github).
