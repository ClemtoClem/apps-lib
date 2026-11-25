#pragma once
// Voxelia Studio — phase 6 : outils de génération du monde.
//
//  - Générateur : préréglages, graine, forme du relief (bruits : continents, collines,
//    montagnes, détail, octaves), climat (échelle, biais de température et d'humidité),
//    grottes, fosses, arbres, créatures, règles de placement des structures des mods
//    (chance, espacement, biomes) ; « Créer un monde avec ces réglages ».
//  - Carte : vue de dessus calculée en arrière-plan (biomes, relief ombré, profondeur de
//    l'eau, structures), zoom et déplacement, lecture du point survolé, profil d'altitude.
//  - Banc d'essai : « Lancer la simulation » ouvre un monde neutre (plat, sans créatures,
//    jour fixe) recréé à chaque lancement et y pose un bloc, une structure, un mécanisme ou
//    exécute une commande (`/elevator build`…) devant le joueur, puis vérifie l'ensemble
//    (entités, joints actifs, chevauchements, erreurs de script).
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <jobs/jobs.hpp>
#include <sdl3/render/render.hpp>
#include <ui/ui.hpp>

#include "app.hpp"

namespace voxelia::studio {

class WorldgenWorkshop {
public:
	explicit WorldgenWorkshop(App &app);
	~WorldgenWorkshop();

	void DrawSettings(ui::Context &u, const sdl3::FRect &r);
	void DrawMap(ui::Context &u, const sdl3::FRect &r, sdl3::Renderer &ren, jobs::ThreadPool &worker);
	void DrawSandbox(ui::Context &u, const sdl3::FRect &r);
	/// À chaque image : action du banc d'essai une fois le monde chargé.
	void Update(Game *game);
	/// Le Studio doit passer à l'espace « Monde en jeu » (simulation lancée) ; position à cadrer.
	[[nodiscard]] std::optional<math::FVector3> TakeFocus();
	[[nodiscard]] std::string TakeStatus();

	struct Impl;

private:
	std::unique_ptr<Impl> m_impl;
};

} // namespace voxelia::studio
