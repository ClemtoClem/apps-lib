#pragma once
// Voxelia Studio — phase 2 : atelier de modélisation (inspiré de Blockbench).
//
//  - Catalogue : toutes les formes 3D déclarées par les mods (nodebox, croix, modèles json,
//    rigs, .obj, .b3d), lu en arrière-plan.
//  - Document : un modèle `voxel::Model` ouvert depuis sa définition JSON (ou son fichier de
//    maillage), avec sa texture, annulation / rétablissement, enregistrement qui ne remplace
//    que la définition modifiée dans son fichier (commentaires et mise en forme conservés).
//  - Vue du modèle : projection 3D sur le canvas (tri en profondeur), caméra orbitale,
//    grille, cotes, boîte englobante, boîtes de collision, gizmo déplacer / redimensionner /
//    tourner, sélection au clic, peinture directe sur les faces.
//  - Arborescence (parties et cubes, visibilité), propriétés de l'élément, éditeur UV,
//    peinture 2D en pixel art (crayon, gomme, pipette, remplissage, palette), timeline
//    d'animation à images clés (+ aperçu des mouvements procéduraux des rigs).
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdl3/render/image_ops.hpp>
#include <jobs/jobs.hpp>
#include <sdl3/render/render.hpp>
#include <ui/ui.hpp>
#include <voxel/model.hpp>

#include "app.hpp"

namespace voxelia::studio {

/// Forme 3D trouvée dans les fichiers d'un mod.
struct ModelEntry {
	std::string id;	   // « core:lamp »
	std::string label; // nom affiché
	std::string mod;
	std::string kind; // « Bloc », « Modèle »
	voxel::ModelFormat format = voxel::ModelFormat::Nodebox;
	fs::path file;			 // JSON de la définition
	size_t index = SIZE_MAX; // élément du tableau racine (SIZE_MAX : objet racine)
	fs::path mesh;			 // .obj / .b3d
	std::string texture;	 // texture déclarée (graphics.texture…)
	std::array<std::string, 6> textures; // textures par face (Face : +X, -X, +Y, -Y, +Z, -Z)
	std::string refModel;	 // bloc qui référence un modèle (shape.model)
};

struct ModelCatalog {
	std::vector<ModelEntry> entries;
	std::vector<std::pair<std::string, std::vector<std::string>>> skins; // modèle rig → skins des créatures
	size_t files = 0;
};

/// Parcourt les JSON des mods (thread de chargement : lecture de fichiers seulement).
[[nodiscard]] ModelCatalog ScanModels(const std::vector<std::pair<std::string, fs::path>> &mods);

using OpenFileFn = std::function<void(const fs::path &, const std::string &)>;

class ModelWorkshop {
public:
	explicit ModelWorkshop(App &app);
	~ModelWorkshop();

	/// À chaque image, avant les panneaux : catalogue (chargement asynchrone), lecture de
	/// l'animation, instantané d'annulation au début d'un geste.
	void Update(jobs::ThreadPool &worker, ui::Context &u, bool keyboardBusy, float dt);
	/// Des couches de texture de blocs ont été rechargées (aperçus à invalider).
	[[nodiscard]] bool TakeTexturesChanged();

	// Panneaux (onglets du dock).
	void DrawCatalog(ui::Context &u, const sdl3::FRect &r);
	void DrawView(ui::Context &u, const sdl3::FRect &r, sdl3::Renderer &ren);
	void DrawParts(ui::Context &u, const sdl3::FRect &r);
	void DrawElement(ui::Context &u, const sdl3::FRect &r, const OpenFileFn &openFile);
	void DrawAnimation(ui::Context &u, const sdl3::FRect &r);
	/// Peinture de la texture du document (ou d'une texture ouverte seule) ; faux si
	/// aucune image n'est ouverte (le Studio affiche alors son aperçu en lecture seule).
	bool DrawPaint(ui::Context &u, const sdl3::FRect &r, sdl3::Renderer &ren);
	/// Éditeur UV : texture et dépliage des boîtes de la partie sélectionnée.
	bool DrawUv(ui::Context &u, const sdl3::FRect &r, sdl3::Renderer &ren);

	/// Ouvre une texture de bloc seule pour la peindre (fichier PNG source).
	bool OpenImage(const fs::path &png, std::string textureName);
	/// Ouvre une entrée du catalogue (par identifiant) ; vrai si trouvée.
	bool Open(std::string_view id);
	/// Crée un nouveau modèle générique (rig) depuis un gabarit : 0 cube, 1 porte,
	/// 2 dalle, 3 torche. Enregistré dans `<core>/models/studio_rigs.json` puis ouvert.
	void NewModel(int preset);
	/// Importe une texture PNG et l'associe au modèle ouvert : active le dépliage
	/// « box UV », applique le cadrillage UV automatique et ouvre l'éditeur de peinture.
	bool ImportTexture(const fs::path &png);
	/// Crée une **variante de bloc** depuis le modèle ouvert : aplatit ses boîtes en
	/// nodebox, y associe une texture et la mécanique (`interact.toggle`), et l'écrit
	/// dans `<mod>/blocks/studio_blocks.json`. `blockName` (non qualifié) et
	/// `blockTexture` sont lus dans l'état du panneau.
	bool CreateBlock();
	[[nodiscard]] bool HasDocument() const noexcept;
	/// Relit le catalogue (fichiers des mods modifiés).
	void ReloadCatalog();
	[[nodiscard]] bool HasImage() const noexcept;
	/// Dernier clic dans un panneau de l'atelier (Ctrl+S / Ctrl+Z s'y appliquent).
	[[nodiscard]] bool Focused() const noexcept { return m_focused; }
	void SetFocused(bool f) noexcept { m_focused = f; }
	void Save();
	void Undo();
	void Redo();
	/// Message d'état (enregistrement, erreurs) à reprendre dans la barre d'état.
	[[nodiscard]] std::string TakeStatus();

	struct Impl;

private:
	App &m_app;
	std::unique_ptr<Impl> m_impl;
	bool m_focused = false;
};

} // namespace voxelia::studio
