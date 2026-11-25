# Voxelia mods

Each mod is a **directory** under `games/voxelia/mods/`. Its entry point is the
`init.script` file; any additional files in `scripts/*.script` are loaded after
it, in name order. Script files use the `.script` extension.

```
games/voxelia/mods/
  welcome/
    init.script            # entry point (hooks, commands, settings)
    scripts/build.script   # shared functions, loaded after init.script
    README.md
  core/                    # core gameplay (blocks, creatures, dimensions, items)
  backrooms/
  edit/
  elevators/
```

The Script interpreter (module `foundation.script`) runs every mod file in a shared
global environment, in load order (`core` first, then by `priority` and name).

The full host API (hooks, systems, commands, `world.*`, `entity.*`, `game.*`,
`content.*`, `sql.query`) and the list of game events are documented in
[`docs/voxelia.md`](../../../docs/voxelia.md#api-script). Functions of the original
Voxelia API that this engine does not implement (structures, formulas, sounds, forms,
inventory) exist but return `nil`; their first call is reported in the console.

Example (`welcome/init.script`):

```script
voxel.on("world_load", fn() {
    let p = pos()
    let x = math.floor(p.x)
    let z = math.floor(p.z)
    let y = surface_height(x, z) + 1
    fill(x - 5, y, z - 5, x + 5, y + 1, z + 5, "core:stone")
    sphere(x, y + 3, z, 2, "core:glowstone")
    notify("Welcome plaza ready")
})
```

In the game console (`T`), Script snippets and `/commands` (including those registered
by mods) are executed directly.
