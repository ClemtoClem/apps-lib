#include <cpp-maker/test.hpp>

#include <sdl3/input/input.hpp>

#include <ui/catalog.hpp>
#include <ui/keymap.hpp>

namespace {
using sdl3::keyboard::Key;
}

TEST(KeyChord_ToStringFromString) {
	const ui::KeyChord ctrlZ{Key::Z, true};
	CHECK_EQ(ctrlZ.ToString(), "Ctrl+Z");
	const auto back = ui::KeyChord::FromString("Ctrl+Z");
	REQUIRE(back.has_value());
	CHECK(*back == ctrlZ);
	// F5 sans modificateur.
	const ui::KeyChord f5{Key::F5};
	CHECK_EQ(f5.ToString(), "F5");
	CHECK(*ui::KeyChord::FromString("f5") == f5);
	// Espace.
	const ui::KeyChord space{Key::SPACE};
	CHECK_EQ(space.ToString(), "Space");
	CHECK(*ui::KeyChord::FromString("space") == space);
	// Ctrl+Shift+S, insensible à la casse.
	const ui::KeyChord ctrlShiftS{Key::S, true, true};
	CHECK_EQ(ctrlShiftS.ToString(), "Ctrl+Shift+S");
	CHECK(*ui::KeyChord::FromString("ctrl+shift+s") == ctrlShiftS);
	// Invalide.
	CHECK_FALSE(ui::KeyChord::FromString("").has_value());
	CHECK_FALSE(ui::KeyChord::FromString("NoSuchKey").has_value());
	CHECK_FALSE(ui::KeyChord{}.Valid());
}

TEST(KeyMap_BindingsMatchConflict) {
	ui::KeyMap map;
	map.Add("nouveau", {Key::N, true});
	map.Add("ouvrir", {Key::O, true});
	map.Add("jouer", {Key::F5});
	REQUIRE(map.Find("nouveau") != nullptr);
	CHECK(map.Find("nouveau")->chord == (ui::KeyChord{Key::N, true}));
	CHECK(map.Find("inconnu") == nullptr);
	// Correspondance.
	CHECK_EQ(map.Match(Key::N, true, false, false), "nouveau");
	CHECK_EQ(map.Match(Key::F5, false, false, false), "jouer");
	CHECK(map.Match(Key::N, false, false, false).empty()); // sans Ctrl : pas de match
	CHECK(map.Match(Key::N, true, true, false).empty());   // Ctrl+Shift ≠ Ctrl
	// Conflit.
	CHECK(map.Conflict({Key::X, true}).empty()); // Ctrl+X libre
	CHECK_EQ(map.Conflict({Key::N, true}), "nouveau");
	CHECK(map.Conflict({Key::N, true}, "nouveau").empty());
	CHECK(map.Conflict({}).empty()); // combinaison vide : pas de conflit
	// Set introduit un conflit, puis Reset le résout.
	CHECK(map.Set("ouvrir", {Key::N, true}));
	CHECK_EQ(map.Conflict({Key::N, true}, "ouvrir"), "nouveau");
	map.Reset("ouvrir");
	CHECK(map.Find("ouvrir")->chord == (ui::KeyChord{Key::O, true}));
	map.ResetAll();
	CHECK(map.Find("nouveau")->chord == (ui::KeyChord{Key::N, true}));
}

TEST(KeyMap_SerializeDeserialize) {
	ui::KeyMap map;
	map.Add("nouveau", {Key::N, true});
	map.Add("jouer", {Key::F5});
	map.Set("jouer", {Key::F6});
	CHECK_EQ(map.Serialize(), "jouer = F6\n"); // seule la combinaison modifiée est écrite
	ui::KeyMap other;
	other.Add("nouveau", {Key::N, true});
	other.Add("jouer", {Key::F5});
	other.Deserialize(map.Serialize());
	CHECK(other.Find("jouer")->chord == (ui::KeyChord{Key::F6}));
	CHECK(other.Find("nouveau")->chord == (ui::KeyChord{Key::N, true})); // inchangé
	// Action inconnue ou ligne illisible : ignorées.
	other.Deserialize("inconnu = Ctrl+X\njouer = PasUneTouche\n");
	CHECK(other.Find("jouer")->chord == (ui::KeyChord{Key::F6}));
}

TEST(Catalog_Search) {
	ui::Catalog cat;
	cat.Add({"ligne", "Outils", "Ligne", 0, "Dessine une ligne", "Cliquer puis tirer."});
	cat.Add({"rect", "Outils", "Rectangle", 0, "Dessine un rectangle", "Taper largeur;hauteur."});
	cat.Add({"script", "Panneaux", "Éditeur de scripts", 0, "Édite le code", "Coloration syntaxique."});
	CHECK_EQ(cat.Size(), size_t(3));
	CHECK_EQ(cat.Search("").size(), size_t(3)); // requête vide : tout
	CHECK_EQ(cat.Search("ligne").size(), size_t(1));
	CHECK_EQ(cat.Search("RECTANGLE").size(), size_t(1)); // insensible à la casse
	CHECK_EQ(cat.Search("outils").size(), size_t(2));	   // par catégorie
	CHECK_EQ(cat.Search("coloration").size(), size_t(1)); // par description
	CHECK(cat.Search("zzz").empty());
}
