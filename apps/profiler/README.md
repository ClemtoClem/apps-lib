# Profiler

`apps/profiler` capture et analyse les programmes instrumentés avec `diagnostics::trace`.

## Instrumenter un programme

```cpp
#include <diagnostics/trace.hpp>

int main() {
    DIAG_THREAD_NAME("main");
    while (running) {
        DIAG_ZONE("Frame");
        { DIAG_ZONE("Update"); Update(); }
        DIAG_COUNTER("entities", world.EntityCount());
        DIAG_FRAME();
    }
}
```

Désactivé, chaque macro coûte une lecture atomique. Le module `diagnostics` doit être lié.

## Commandes

| Commande | Rôle |
|---|---|
| `profiler record [--out F] [--duration S] -- programme args…` | Lance le programme avec `DIAG_TRACE_FILE` (et `DIAG_TRACE_DURATION`). Avec `--duration`, la capture est écrite au bout de S secondes et le programme est arrêté ; sinon, à sa sortie. Captures par défaut dans `data/profiler/captures/` |
| `profiler list` | Captures, la plus récente d'abord |
| `profiler report [F] [--from MS] [--to MS] [--thread NOM] [--target-fps N] [--top N]` | Images (moyenne, médiane, p95, p99, max, % hors budget, saccades), diagnostic classé, zones (appels, temps propre, total, moyenne, max, écart type, par image), fils (occupation), compteurs |
| `profiler compare AVANT APRES [--top N]` | Écarts sur les images et le temps propre par image des zones |
| `profiler export [F] --chrome SORTIE.json` | Format Trace Event (chrome://tracing, ui.perfetto.dev) |
| `profiler [view] [F] [--frames N --screenshot P.png]` | Visualiseur |

Sans fichier, les commandes prennent la capture la plus récente.

## Visualiseur

- Piste des images (vert < 16,7 ms, orange < 33,3 ms, rouge au-delà), puis une piste par
  fil avec les zones empilées par profondeur.
- Molette : zoom au curseur · clic droit glissé : défilement · Maj + clic gauche glissé :
  sélection d'une plage (toutes les statistiques sont recalculées dessus) · clic : zone
  sélectionnée · Origine : vue entière · glisser-déposer d'un `.vcap` : ouverture.
- Docks : Diagnostic (courbe des images), Zones, Fils, Compteurs (courbes), Sélection.
