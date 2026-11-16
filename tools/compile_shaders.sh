#!/usr/bin/env bash
# Compile les shaders GLSL (glslc) en SPIR-V et les embarque dans <dossier>/src/shaders_spirv.inc :
# aucune dépendance de compilation de shaders à l'exécution (SDL_GPU consomme le SPIR-V).
#
#   tools/compile_shaders.sh                       # tous les dossiers ci-dessous
#   tools/compile_shaders.sh sdl3                  # ceux dont le chemin contient « sdl3 »
set -euo pipefail
shopt -s nullglob
root="$(cd "$(dirname "$0")/.." && pwd)"
# dossier (contenant shaders/ et src/) : espace de noms C++ [: fichier de sortie relatif]
targets=(
  "libs/engine/voxel:voxel::spirv"
  "libs/platform/sdl3:sdl3::spirv:src/render/shaders_spirv.inc"
  "examples/example-gpu-basics:shaders"
  "examples/example-ui-shader-effects:shaders"
)
filter="${1:-}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
for target in "${targets[@]}"; do
  dir="${target%%:*}"; rest="${target#*:}"; namespace="${rest%%:*}"; rel="${rest#*:}"
  [[ "$rel" == "$rest" ]] && rel="src/shaders_spirv.inc"
  [[ -n "$filter" && "$dir" != *"$filter"* ]] && continue
  src="$root/$dir/shaders"
  out="$root/$dir/$rel"
  {
    echo "// Généré par tools/compile_shaders.sh — ne pas modifier à la main."
    echo "#pragma once"
    echo "#include <cstdint>"
    echo "namespace $namespace {"
    for f in "$src"/*.vert "$src"/*.frag; do
      name="$(basename "$f" | tr '.' '_')"
      glslc -O --target-env=vulkan1.1 "$f" -o "$tmp/$name.spv"
      echo "alignas(4) inline constexpr unsigned char k_${name}[] = {"
      od -An -v -tx1 "$tmp/$name.spv" | sed 's/ \([0-9a-f][0-9a-f]\)/0x\1,/g'
      echo "};"
    done
    echo "} // namespace $namespace"
  } > "$out"
  echo "écrit : $out"
done
