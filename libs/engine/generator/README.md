# engine.generator

Génération procédurale de géométrie (maillages, formes 2D, chemins 3D), réécrite en C++23 à partir
de la bibliothèque *generator* de Markus Ilmola. Toutes les mathématiques viennent de
`foundation.math` (`math::FVector2/3`, `FQuaternion`, `FMatrix4`, `math::Bezier*`) : plus de gml.

## Concepts

| Famille | Sommets | Primitives | Exemples |
|---------|---------|------------|----------|
| Mesh    | `MeshVertex` (position, normale, uv) | `Triangle` | `BoxMesh`, `SphereMesh`, `TorusMesh`, `TeapotMesh`… |
| Shape   | `ShapeVertex` (position 2D, tangente) | `Edge` | `CircleShape`, `RectangleShape`, `GridShape`… |
| Path    | `PathVertex` (position 3D, tangente, normale) | `Edge` | `HelixPath`, `KnotPath`, `LinePath`… |

Chaque générateur expose `Vertices()` et `Triangles()` (ou `Edges()`), qui renvoient des
itérateurs paresseux (`Generate()`, `Next()`, `Done()`) ; `Count(gen)` compte les éléments.

## Modificateurs

Fabriques `Make*` composables : `MakeTranslateMesh`, `MakeRotateMesh`, `MakeScaleMesh`,
`MakeTransformMesh`, `MakeFlipMesh`, `MakeAxisFlipMesh`, `MakeMirrorMesh`, `MakeAxisSwapMesh`,
`MakeSpherifyMesh`, `MakeRepeatMesh`, `MakeMergeMesh`, `MakeUvFlipMesh`, `MakeUvSwapMesh`
(et leurs équivalents `*Shape` / `*Path`), `MakeExtrudeMesh`, `MakeShapeToPath`, les classes
`SubdivideMesh` / `SubdivideShape` / `SubdividePath`, `LatheMesh`, `BezierMesh`, `BezierShape`
et l'effacement de type `AnyMesh` / `AnyShape` / `AnyPath`.

```cpp
#include <generator/generator.hpp>

auto mesh = generator::MakeTranslateMesh(generator::SphereMesh{1.0f, 32, 16},
                                         math::FVector3{0.0f, 2.0f, 0.0f});
for (const generator::MeshVertex &v : mesh.Vertices()) { /* v.position, v.normal, v.texCoord */ }
for (const generator::Triangle &t : mesh.Triangles()) { /* t.vertices[0..2] */ }
```

## Export

- `ObjWriter` : écrit un maillage au format Wavefront `.obj` ;
- `SvgWriter` : rendu filaire / plein en SVG (projection perspective ou orthographique), pratique pour le débogage.

## Notes

- Flottants `float` partout ; les générateurs épuisés déclenchent une assertion (pas d'exceptions).
- Tests : `cpp-maker test engine.generator` (cohérence des normales et indices, sphères, modificateurs, Bézier, OBJ/SVG).
