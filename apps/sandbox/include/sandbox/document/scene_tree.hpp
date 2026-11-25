#pragma once
/**
 * @file scene_tree.hpp
 * @brief Import des scènes et objets de l'ancien sandbox (arbre de nœuds « scene.tree »,
 *        à la Godot) dans un document : un groupe par nœud, hiérarchie conservée.
 *
 *  - Node / Node3D / SpawnPoint / Camera : groupe vide (type gardé dans `Entity::type`) ;
 *  - MeshInstance : primitive (boîte, cylindre, cône, sphère, tore, icosaèdre) reconstruite en
 *    faces éditables, ou modèle `.model` (`shape: mesh`) ; couleur → matériau de la palette ;
 *  - RigidBody : corps solide ou dynamique (masse, frottement, rebond) ; sans lui, un maillage
 *    est traversable, comme dans l'ancien moteur ;
 *  - Light : lampe ponctuelle ou spot ; Script : `../scripts/<nom>.script` ;
 *  - ObjectInstance : l'arbre de `objects/<nom>.object` développé sous le nœud ;
 *  - scène : script de jeu (`gameplay_script_file`), environnement, caméra de jeu (vue « Départ »).
 */
#include <filesystem>

#include <core/core.hpp>
#include <dataio/json.hpp>

#include "sandbox/document/document.hpp"

namespace sandbox {

/// Le JSON est une scène ou un objet de l'ancien format (champ `tree`).
[[nodiscard]] bool IsSceneTree(const dataio::NodePtr &json) noexcept;

/// Document construit depuis une scène (ou un objet) `tree` lue dans `file` ; les objets, scripts
/// et modèles sont cherchés dans le projet (dossier de `file`, ou celui au-dessus de `scenes/`).
[[nodiscard]] Result<Document, String> ImportSceneTree(const dataio::NodePtr &json, const std::filesystem::path &file);

} // namespace sandbox
