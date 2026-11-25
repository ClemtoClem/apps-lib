// Voxelia — écrans de menu (inspirés de l'interface de Voxelia dans SDL3_Wrapper) :
// panneau latéral sur le monde animé, dialogues à en-tête, sections vertes, lignes de
// paramètres avec bouton « valeur par défaut ».
//
// L'interface est **conservée** (inspirée de Flutter) : `BuildInterface` construit
// l'arborescence à l'initialisation via le **factory builder** (`ui::Column`, `ui::Dialog`,
// `ui::Section`, `ui::Setting*`, `ui::MenuButton`…) et la boucle principale l'affiche
// automatiquement (`WidgetTree::Begin / Render / End`). La fonction de construction est
// ré-évaluée à chaque image (comme le `build` de Flutter), si bien que les listes
// dynamiques (mondes, mods, skins) restent à jour. Les écrans au dessin personnalisé
// (menu principal, aperçus 3D, grilles de cartes) passent par `ui::Custom`.
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cwctype>
#include <format>

#include <sdl3/sdl3.hpp>

#include <core/hash.hpp>
#include <core/string.hpp>

#include "app.hpp"

namespace voxelia {

using Rect = sdl3::FRect;
using sdl3::Rgba;
namespace icons = ui::icons;

namespace {

constexpr std::string_view kGameModes[] = {"Survie", "Créatif"};
constexpr std::string_view kGenerators[] = {"Monde normal (biomes)", "Monde plat", "Amplifié", "Archipel"};
constexpr std::string_view kDayLengths[] = {"5 min", "10 min", "20 min", "40 min"};
constexpr int kDayLengthValues[] = {5, 10, 20, 40};
constexpr std::string_view kStartTimes[] = {"Aube", "Midi", "Crépuscule", "Nuit"};
constexpr float kStartTimeValues[] = {0.27f, 0.5f, 0.75f, 0.0f};
constexpr std::string_view kDifficulties[] = {"Normale", "Difficile"};

constexpr std::string_view kTips[] = {
	"F3 affiche les statistiques : chunks, base de données, grille d'entités, rendu.",
	"/spawn core:sheep 100000 : l'ECS simule cent mille créatures sans broncher.",
	"/chunks 6 interroge l'index de Morton de la base, sans charger les chunks.",
	"F10 ouvre Voxelia Studio dans une seconde fenêtre : éditez et exécutez les scripts des mods.",
	"Chaque paramètre modifié affiche une flèche : un clic remet sa valeur par défaut.",
	"/sql SELECT * FROM events ORDER BY tick DESC LIMIT 5 : le journal du monde en SQL.",
	"Les paramètres des mods sont déclarés dans leur mod.json et s'appliquent à tous les mondes.",
	"F2 enregistre une capture d'écran, F5 sauvegarde le monde.",
};

struct SettingsCategory {
	char32_t icon;
	std::string_view name;
};
constexpr SettingsCategory kSettingsCategories[] = {{icons::Codepoint(icons::MaterialIcons::PALETTE), "Graphismes"}, {icons::Codepoint(icons::MaterialIcons::KEYBOARD), "Commandes"}, {icons::Codepoint(icons::MaterialIcons::VOLUME_UP), "Audio"},
													{icons::Codepoint(icons::MaterialIcons::DASHBOARD), "Interface"}, {icons::Codepoint(icons::MaterialIcons::SPEED), "Performances"}};

int IndexOf(const int *values, int n, int v) {
	for (int i = 0; i < n; ++i)
		if (values[i] == v)
			return i;
	return 1;
}

int StartTimeIndex(float t) {
	for (int i = 0; i < 4; ++i)
		if (std::fabs(kStartTimeValues[i] - t) < 0.01f)
			return i;
	return 0;
}

sdl3::Color TileColor(std::string_view id) {
	const uint64_t h = core::HashString(id);
	static constexpr sdl3::Color palette[] = {Rgba(176, 64, 184), Rgba(64, 196, 150), Rgba(64, 176, 180), Rgba(116, 110, 60),
											 Rgba(206, 120, 52), Rgba(70, 120, 210), Rgba(196, 70, 90), Rgba(110, 160, 60)};
	return palette[h % std::size(palette)];
}

/// Vignette d'un mod : initiale dans un carré coloré.
void ModTile(sdl3::Canvas &c, const Rect &r, const voxel::ModInfo &m) {
	c.FillRoundRect(r, 6, TileColor(m.id));
	std::string initial;
	size_t i = 0;
	unicode::AppendUtf8(initial, std::towupper(wint_t(unicode::DecodeUtf8(m.name.empty() ? m.id : m.name, i))));
	c.TextCentered(r, initial, Rgba(240, 232, 140), r.h * 0.52f, sdl3::CanvasFont::Bold);
}

/// Assombrit le fond et dessine un dialogue centré ; retourne sa zone.
Rect Dialog(App &app, float maxW, float maxH) {
	sdl3::Canvas &c = app.Canvas();
	c.FillRect({0, 0, c.Width(), c.Height()}, Rgba(6, 8, 12, app.background ? 110 : 0));
	const float w = std::min(maxW, c.Width() - 48), h = std::min(maxH, c.Height() - 64);
	const Rect r{(c.Width() - w) * 0.5f, std::max(20.f, (c.Height() - 24 - h) * 0.5f), w, h};
	c.FillRoundRect(r, 12, Rgba(18, 21, 28, 240));
	c.StrokeRoundRect(r, 12, Rgba(52, 58, 72));
	return r;
}

/// Bande d'actions du bas d'un dialogue (boutons « Annuler / Enregistrer »).
Rect DialogFooter(const Rect &dlg) { return {dlg.x + 24, dlg.Bottom() - 64, dlg.w - 48, 44}; }

/// Texte d'aide à gauche de la bande d'actions.
void DialogFooterHint(App &app, const Rect &dlg, const char *text) {
	sdl3::Canvas &c = app.Canvas();
	const Rect f = DialogFooter(dlg);
	const float w = std::max(0.f, f.w - 560.f);
	if (w > 8.f)
		c.Text(f.x + 4, f.y + (f.h - c.LineHeight(12)) * 0.5f, c.Ellipsize(text, w, 12), app.Ui().Style().textDim, 12);
}

/// Texture d'un skin (chargée une fois, échantillonnage au plus proche : pixels nets).
sdl3::Texture *SkinTexture(App &app, const SkinDef &skin) {
	if (const auto it = app.skinTextures.find(skin.texture); it != app.skinTextures.end())
		return &it->second;
	const std::optional<fs::path> path = app.content.SkinTexturePath(skin.texture);
	if (!path)
		return nullptr;
	auto t = sdl3::Texture::CreateFromFile(app.Canvas().Renderer(), String(path->string()));
	if (!t)
		return nullptr;
	t.Value().SetScaleMode(sdl3::ScaleMode::NEAREST);
	t.Value().SetBlendMode(sdl3::BlendMode::BLEND);
	auto [ins, _] = app.skinTextures.emplace(skin.texture, std::move(t.Value()));
	return &ins->second;
}

/// Personnage vu de face d'après un skin (gabarit 64×32 ou 64×64, haute définition
/// acceptée) : tête, torse, bras, jambes, puis calques (veste, chapeau). Le
/// personnage (16×32 unités) est centré dans `area` en gardant ses proportions.
void DrawSkinFront(App &app, const SkinDef &skin, const Rect &area) {
	sdl3::Texture *tex = SkinTexture(app, skin);
	if (!tex)
		return;
	sdl3::Canvas &c = app.Canvas();
	const float tw = float(tex->GetWidth()), th = float(tex->GetHeight());
	if (tw <= 0.f || th <= 0.f)
		return;
	const float k = std::min(area.w / 16.f, area.h / 32.f);
	const float ox = std::round(area.x + (area.w - 16.f * k) * 0.5f), oy = std::round(area.y + (area.h - 32.f * k) * 0.5f);
	const float t = tw / 64.f; // facteur des skins haute définition
	const bool tall = th >= tw; // 64×64 : membres gauches et calques distincts
	auto part = [&](float sx, float sy, float sw, float sh, float dx, float dy, bool flip = false) {
		c.ImageRegion(*tex, tw, th, {sx * t, sy * t, sw * t, sh * t}, {ox + dx * k, oy + dy * k, sw * k, sh * k}, flip);
	};
	part(8, 8, 8, 8, 4, 0);	   // tête
	part(20, 20, 8, 12, 4, 8); // torse
	part(44, 20, 4, 12, 0, 8); // bras droit
	part(4, 20, 4, 12, 4, 20); // jambe droite
	if (tall) {
		part(36, 52, 4, 12, 12, 8); // bras gauche
		part(20, 52, 4, 12, 8, 20); // jambe gauche
		part(20, 36, 8, 12, 4, 8);	// veste
		part(44, 36, 4, 12, 0, 8);	// manche droite
		part(52, 52, 4, 12, 12, 8); // manche gauche
		part(4, 36, 4, 12, 4, 20);	// pantalon droit
		part(4, 52, 4, 12, 8, 20);	// pantalon gauche
	} else {
		part(44, 20, 4, 12, 12, 8, true); // membres gauches = miroir des droits
		part(4, 20, 4, 12, 8, 20, true);
	}
	part(40, 8, 8, 8, 4, 0); // chapeau (calque)
}

/// Personnage en 3D d'après un skin : six pavés (tête, torse, bras, jambes) et leurs
/// calques (chapeau, veste, manches, pantalon), texturés selon le gabarit 64×64
/// (ou 64×32, membres gauches en miroir), tournés, projetés en perspective sur le
/// CPU, faces arrière éliminées, triées de l'arrière vers l'avant (algorithme du
/// peintre) et ombrées selon leur orientation. Glisser à la souris fait tourner le
/// modèle (inertie) ; au repos, il pivote lentement.
void DrawSkin3D(App &app, const SkinDef &skin, const Rect &area, float dt, bool interactive) {
	sdl3::Canvas &c = app.Canvas();
	ui::Context &u = app.Ui();
	App::SkinView &v = app.skinView;
	// ── Interaction ──────────────────────────────────────────────────────
	if (interactive) {
		const auto it = u.Interact(u.MakeId("##skin3d"), area);
		if (it.held) {
			if (!v.dragging) {
				v.dragging = true;
				v.lastX = u.MouseX();
				v.lastY = u.MouseY();
			}
			const float dx = u.MouseX() - v.lastX, dy = u.MouseY() - v.lastY;
			v.yaw += dx * 0.012f;
			v.pitch = std::clamp(v.pitch + dy * 0.008f, -0.7f, 0.7f);
			v.velocity = dt > 0 ? std::clamp(dx * 0.012f / dt, -12.f, 12.f) : 0.f;
			v.lastX = u.MouseX();
			v.lastY = u.MouseY();
			v.idle = 0.f;
		} else {
			v.dragging = false;
		}
		if (it.hovered && !it.held)
			u.Tooltip("Glisser pour faire tourner le personnage");
	}
	if (!v.dragging) {
		v.yaw += v.velocity * dt; // inertie
		v.velocity *= std::exp(-3.5f * dt);
		v.idle += dt;
		if (v.idle > 2.5f && std::fabs(v.velocity) < 0.3f) { // au repos : rotation lente, regard qui revient à l'horizontale
			v.yaw += 0.45f * dt;
			v.pitch += (0.08f - v.pitch) * std::min(1.f, 1.5f * dt);
		}
	}
	sdl3::Texture *tex = SkinTexture(app, skin);
	if (!tex)
		return;
	const float tw = float(tex->GetWidth()), thh = float(tex->GetHeight());
	if (tw <= 0.f || thh <= 0.f)
		return;
	const bool tall = thh >= tw;
	const float vScale = (tw / 64.f) / thh; // texel (gabarit 64) → v normalisé

	// ── Géométrie (unités = pixels du gabarit, Y vers le haut, face avant vers +Z) ──
	struct Face {
		float p[4][3]; // coins : haut-gauche, haut-droit, bas-droit, bas-gauche (sens de la texture)
		float n[3];
		float uv[4]; // u, v, largeur, hauteur (texels du gabarit)
		bool overlay;
	};
	std::vector<Face> faces;
	faces.reserve(72);
	auto box = [&](float x0, float y0, float z0, float w, float h, float d, float tu, float tv, float inflate, bool overlay, bool mirror) {
		const float x1 = x0 + w + inflate, y1 = y0 + h + inflate, z1 = z0 + d + inflate;
		x0 -= inflate;
		y0 -= inflate;
		z0 -= inflate;
		const Face f[6] = {
			{{{x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z1}}, {0, 0, 1}, {tu + d, tv + d, w, h}, overlay},				// avant
			{{{x1, y1, z0}, {x0, y1, z0}, {x0, y0, z0}, {x1, y0, z0}}, {0, 0, -1}, {tu + 2 * d + w, tv + d, w, h}, overlay},	// arrière
			{{{x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {x0, y0, z0}}, {-1, 0, 0}, {tu, tv + d, d, h}, overlay},				// côté droit
			{{{x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}, {x1, y0, z1}}, {1, 0, 0}, {tu + d + w, tv + d, d, h}, overlay},		// côté gauche
			{{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}, {0, 1, 0}, {tu + d, tv, w, d}, overlay},				// dessus
			{{{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}}, {0, -1, 0}, {tu + d + w, tv, w, d}, overlay},			// dessous
		};
		for (Face face : f) {
			if (mirror) { // membre gauche d'un skin 64×32 : membre droit réfléchi en X
				for (auto &pt : face.p)
					pt[0] = -pt[0];
				face.n[0] = -face.n[0];
			}
			faces.push_back(face);
		}
	};
	box(-4, 24, -4, 8, 8, 8, 0, 0, 0, false, false);	// tête
	box(-4, 12, -2, 8, 12, 4, 16, 16, 0, false, false); // torse
	box(-8, 12, -2, 4, 12, 4, 40, 16, 0, false, false); // bras droit
	box(-4, 0, -2, 4, 12, 4, 0, 16, 0, false, false);	// jambe droite
	if (tall) {
		box(4, 12, -2, 4, 12, 4, 32, 48, 0, false, false); // bras gauche
		box(0, 0, -2, 4, 12, 4, 16, 48, 0, false, false);  // jambe gauche
		box(-4, 12, -2, 8, 12, 4, 16, 32, 0.25f, true, false); // veste
		box(-8, 12, -2, 4, 12, 4, 40, 32, 0.25f, true, false); // manche droite
		box(4, 12, -2, 4, 12, 4, 48, 48, 0.25f, true, false);  // manche gauche
		box(-4, 0, -2, 4, 12, 4, 0, 32, 0.25f, true, false);   // pantalon droit
		box(0, 0, -2, 4, 12, 4, 0, 48, 0.25f, true, false);	   // pantalon gauche
	} else {
		box(-8, 12, -2, 4, 12, 4, 40, 16, 0, false, true); // bras gauche (miroir)
		box(-4, 0, -2, 4, 12, 4, 0, 16, 0, false, true);   // jambe gauche (miroir)
	}
	box(-4, 24, -4, 8, 8, 8, 32, 0, 0.5f, true, false); // chapeau

	// ── Transformation : lacet puis tangage autour du centre, perspective douce ──
	const float cy = std::cos(v.yaw), sy = std::sin(v.yaw), cp = std::cos(v.pitch), sp = std::sin(v.pitch);
	auto rotate = [&](const float in[3], float out[3]) {
		const float x = in[0], y = in[1] - 16.f, z = in[2];
		const float x1 = cy * x + sy * z, z1 = -sy * x + cy * z;
		out[0] = x1;
		out[1] = cp * y - sp * z1;
		out[2] = sp * y + cp * z1;
	};
	auto rotateDir = [&](const float in[3], float out[3]) {
		const float x1 = cy * in[0] + sy * in[2], z1 = -sy * in[0] + cy * in[2];
		out[0] = x1;
		out[1] = cp * in[1] - sp * z1;
		out[2] = sp * in[1] + cp * z1;
	};
	constexpr float kCamera = 70.f; // distance de la caméra (unités du gabarit)
	const float scale = std::min(area.h / 38.f, area.w / 26.f);
	const float ox = area.x + area.w * 0.5f, oy = area.y + area.h * 0.5f + scale * 1.5f;

	struct Projected {
		float xy[8];
		float uv[8];
		float depth;
		sdl3::Color tint;
	};
	std::vector<Projected> visible;
	visible.reserve(faces.size());
	const float light[3] = {0.35f, 0.55f, 0.76f};
	for (const Face &f : faces) {
		float n[3], q[4][3];
		rotateDir(f.n, n);
		for (int i = 0; i < 4; ++i)
			rotate(f.p[i], q[i]);
		const float centre[3] = {(q[0][0] + q[2][0]) * 0.5f, (q[0][1] + q[2][1]) * 0.5f, (q[0][2] + q[2][2]) * 0.5f};
		const float toCam[3] = {-centre[0], -centre[1], kCamera - centre[2]};
		if (n[0] * toCam[0] + n[1] * toCam[1] + n[2] * toCam[2] <= 0.f)
			continue; // face tournée vers l'arrière
		Projected pr{};
		for (int i = 0; i < 4; ++i) {
			const float persp = kCamera / (kCamera - q[i][2]);
			pr.xy[2 * i] = ox + q[i][0] * persp * scale;
			pr.xy[2 * i + 1] = oy - q[i][1] * persp * scale;
		}
		constexpr float e = 0.02f; // évite de déborder sur les texels voisins
		const float u0 = (f.uv[0] + e) / 64.f, u1 = (f.uv[0] + f.uv[2] - e) / 64.f;
		const float v0 = (f.uv[1] + e) * vScale, v1 = (f.uv[1] + f.uv[3] - e) * vScale;
		const float uvs[8] = {u0, v0, u1, v0, u1, v1, u0, v1};
		std::copy(uvs, uvs + 8, pr.uv);
		pr.depth = centre[2] + (f.overlay ? 0.01f : 0.f);
		const float lambert = std::max(0.f, n[0] * light[0] + n[1] * light[1] + n[2] * light[2]);
		const auto shade = static_cast<uint8_t>(std::clamp(255.f * (0.55f + 0.45f * lambert), 0.f, 255.f));
		pr.tint = Rgba(shade, shade, shade);
		visible.push_back(pr);
	}
	std::ranges::sort(visible, {}, &Projected::depth);

	// Ombre au sol puis faces, de la plus lointaine à la plus proche.
	const float shadowW = 12.f * scale, shadowY = oy + 16.f * scale;
	c.FillRoundRect({ox - shadowW * 0.5f, shadowY - 1.5f * scale, shadowW, 3.f * scale}, 1.5f * scale, Rgba(0, 0, 0, 50));
	c.FillRoundRect({ox - shadowW * 0.35f, shadowY - 1.f * scale, shadowW * 0.7f, 2.f * scale}, 1.f * scale, Rgba(0, 0, 0, 40));
	for (const Projected &pr : visible)
		c.TexturedQuad(*tex, pr.xy, pr.uv, pr.tint);
}

void StatusBar(App &app) {
	sdl3::Canvas &c = app.Canvas();
	const Rect r{0, c.Height() - 24, c.Width(), 24};
	c.FillRect(r, Rgba(6, 7, 10, 235));
	std::string text = std::format("Menu principal · Voxelia {} · F10 : Voxelia Studio · {} mod(s) · SDL3", kVersion, app.content.mods.Mods().size());
	if (app.screen == Screen::Paused && app.game)
		text = std::format("Pause · « {} » · F10 : Voxelia Studio", app.game->WorldInfo().name);
	c.Text(10, r.y + (r.h - c.LineHeight(13)) * 0.5f, text, app.Ui().Style().textDim, 13);
	if (const std::string track = app.audio.CurrentTrack(); !track.empty()) {
		const std::string label = std::format("♪ {}", track);
		const float w = c.MeasureText(label, 13);
		c.Text(r.Right() - w - 12, r.y + (r.h - c.LineHeight(13)) * 0.5f, label, app.Ui().Style().textDim, 13);
	}
}

// ══════════════════════════════════════════════════════════════════════════
// Menu principal (dessin personnalisé : panneau latéral, personnage 3D, musique)
// ══════════════════════════════════════════════════════════════════════════
void MainMenu(App &app, float dt) {
	sdl3::Canvas &c = app.Canvas();
	ui::Context &u = app.Ui();
	const ui::Theme &th = u.Style();
	if (!app.background)
		c.FillGradientV({0, 0, c.Width(), c.Height()}, Rgba(28, 46, 78), Rgba(10, 12, 18));
	// Panneau latéral translucide.
	const float pw = std::min(460.f, c.Width() * 0.42f);
	c.FillGradientH({0, 0, pw, c.Height()}, Rgba(10, 12, 16, 225), Rgba(14, 17, 23, 205));
	c.FillRect({pw, 0, 1, c.Height()}, Rgba(60, 68, 84, 160));
	const float x = 44, w = pw - 88;
	c.Text(x + 3, 63, "VOXELIA", Rgba(20, 50, 20, 200), 56, sdl3::CanvasFont::Bold);
	c.Text(x, 60, "VOXELIA", Rgba(160, 214, 92), 56, sdl3::CanvasFont::Bold);
	c.FillRoundRect({x, 138, 26, 4}, 2, th.accent);
	c.Text(x + 36, 130, "Construire, explorer, survivre", Rgba(196, 206, 220), 15);

	const auto worlds = app.ListWorlds();
	const auto last = std::ranges::find(worlds, app.settings.lastWorld, &std::pair<std::string, WorldSettings>::first);
	u.BeginColumn({x, 186, w, c.Height() - 300});
	if (last != worlds.end()) {
		const WorldSettings &ws = last->second;
		if (u.MenuButton("play", icons::Codepoint(icons::MaterialIcons::PLAY_ARROW), "Jouer",
						 std::format("« {} » — {}, {}", ws.name, kGameModes[std::clamp(ws.gameMode, 0, 1)], ws.lastPlayed.empty() ? "jamais joué" : ws.lastPlayed),
						 true))
			app.StartGame(last->first, std::nullopt);
	} else if (u.MenuButton("play", icons::Codepoint(icons::MaterialIcons::PLAY_ARROW), "Jouer", "Créer un premier monde", true)) {
		app.newWorld = WorldSettings{};
		app.newWorld.seed = core::Mix64(sdl3::GetTicksMS()) % 1000000000;
		app.newSeedText = std::to_string(app.newWorld.seed);
		app.screen = Screen::NewWorld;
	}
	u.Space(4);
	if (u.MenuButton("new", icons::Codepoint(icons::MaterialIcons::ADD), "Nouveau monde", "Choisir le mode, la génération et les créatures")) {
		app.newWorld = WorldSettings{};
		app.newWorld.seed = core::Mix64(sdl3::GetTicksMS() * 7919) % 1000000000;
		app.newSeedText = std::to_string(app.newWorld.seed);
		app.screen = Screen::NewWorld;
	}
	if (u.MenuButton("worlds", icons::Codepoint(icons::MaterialIcons::PUBLIC), "Mondes", std::format("{} monde(s) enregistré(s)", worlds.size())))
		app.screen = Screen::Worlds;
	size_t enabled = 0;
	for (const auto &m : app.content.mods.Mods())
		enabled += !app.settings.disabledMods.contains(m.id);
	if (u.MenuButton("mods", icons::Codepoint(icons::MaterialIcons::EXTENSION), "Mods", std::format("{} installé(s), {} activé(s) — paramètres", app.content.mods.Mods().size(), enabled)))
		app.screen = Screen::Mods;
	if (u.MenuButton("settings", icons::Codepoint(icons::MaterialIcons::SETTINGS), "Paramètres", "Graphismes, commandes, audio, interface, performances")) {
		app.draft = app.settings;
		app.settingsReturn = Screen::Main;
		app.screen = Screen::Settings;
	}
	if (u.MenuButton("skin", icons::Codepoint(icons::MaterialIcons::PERSON), "Skin du personnage", "Choisir votre apparence")) {
		app.selectedSkin = app.settings.playerSkin;
		app.skinQuery.clear();
		app.skinPage = 0;
		app.skinReturn = Screen::Main;
		app.screen = Screen::Skins;
	}
	if (u.MenuButton("studio", icons::Codepoint(icons::MaterialIcons::VIEW_IN_AR), "Ouvrir le studio", "IDE des mods dans une autre fenêtre · F10"))
		app.OpenStudio();
	u.EndColumn();
	if (u.ButtonAt({x, c.Height() - 104, w, 38}, "Quitter le jeu"))
		app.RequestQuit();
	c.Text(x, c.Height() - 54, std::format("Voxelia {} · moteur voxel ECS · {} bloc(s) · {}", kVersion, app.content.blocks.Count(),
										   app.Device().Driver() ? app.Device().Driver() : "SDL_GPU"),
		   th.textDim, 12);

	// Lecteur de musique (coin supérieur droit).
	if (app.audio.Ready()) {
		const std::string track = app.audio.CurrentTrack();
		const Rect card{c.Width() - 290, 20, 270, 52};
		c.FillRoundRect(card, 10, Rgba(18, 21, 28, 225));
		c.StrokeRoundRect(card, 10, Rgba(52, 58, 72));
		u.Icon({card.x + 10, card.y, 30, card.h}, icons::Codepoint(icons::MaterialIcons::MUSIC_NOTE), th.accent, 24);
		c.Text(card.x + 48, card.y + 8, "Musique", th.text, 15, sdl3::CanvasFont::Bold);
		c.Text(card.x + 48, card.y + 28, c.Ellipsize(track.empty() ? "silence" : track, 160, 12), th.textDim, 12);
		if (u.IconButton({card.Right() - 42, card.y + 10, 32, 32}, icons::Codepoint(icons::MaterialIcons::SKIP_NEXT), "Piste suivante"))
			app.audio.Next();
	}
	// Personnage du joueur en 3D (côté droit) : glisser pour le faire tourner.
	{
		const SkinDef *skin = app.content.FindSkin(app.settings.playerSkin);
		if (!skin && !app.content.skins.empty())
			skin = &app.content.skins.front();
		const float colW = std::min(300.f, c.Width() - pw - 80);
		if (skin && colW > 180) {
			const float top = 92, bottomLimit = c.Height() - 24 - (app.settings.showTips ? 112.f : 16.f);
			const Rect card{c.Width() - colW - 20, bottomLimit - 70, colW, 62};
			const Rect figure{card.x, top, colW, std::max(120.f, card.y - top - 10)};
			DrawSkin3D(app, *skin, figure, dt, true);
			c.FillRoundRect(card, 10, Rgba(18, 21, 28, 225));
			c.StrokeRoundRect(card, 10, Rgba(52, 58, 72));
			u.Icon({card.x + 10, card.y, 30, card.h}, icons::Codepoint(icons::MaterialIcons::PERSON), th.accent, 24);
			c.Text(card.x + 48, card.y + 10, "Joueur", th.text, 15, sdl3::CanvasFont::Bold);
			c.Text(card.x + 48, card.y + 32, c.Ellipsize(skin->id.substr(skin->id.find(':') + 1), colW - 150, 12), th.textDim, 12);
			if (u.ButtonAt({card.Right() - 104, card.y + 13, 94, 36}, "Changer")) {
				app.selectedSkin = app.settings.playerSkin;
				app.skinQuery.clear();
				app.skinPage = 0;
				app.skinReturn = Screen::Main;
				app.screen = Screen::Skins;
			}
		}
	}
	// Astuce tournante (coin inférieur droit).
	if (app.settings.showTips) {
		app.tipTimer += dt;
		if (app.tipTimer > 9.f) {
			app.tipTimer = 0;
			app.tipIndex = (app.tipIndex + 1) % std::size(kTips);
		}
		const float tw = std::min(420.f, c.Width() - pw - 60);
		if (tw > 220) {
			const Rect card{c.Width() - tw - 24, c.Height() - 24 - 96, tw, 76};
			c.FillRoundRect(card, 10, Rgba(18, 21, 28, 225));
			c.StrokeRoundRect(card, 10, Rgba(52, 58, 72));
			u.Icon({card.x + 10, card.y + 8, 28, 28}, icons::Codepoint(icons::MaterialIcons::LIGHTBULB), th.warning, 22);
			c.Text(card.x + 46, card.y + 10, "Astuce", th.warning, 14, sdl3::CanvasFont::Bold);
			u.BeginColumn({card.x + 46, card.y + 30, card.w - 60, 44});
			u.Wrapped(kTips[app.tipIndex], th.text, 13);
			u.EndColumn();
		}
	}
}

// ══════════════════════════════════════════════════════════════════════════
// Sections de réglages (factory builder) — réutilisées par plusieurs dialogues
// ══════════════════════════════════════════════════════════════════════════
void DrawWorldRules(ui::Context &u, WorldSettings &w) {
	const WorldSettings def;
	u.Section(icons::Codepoint(icons::MaterialIcons::BOLT), "Mode de jeu et règles");
	u.SettingChoice("Mode de jeu", "Survie : gravité et marche. Créatif : vol et blocs illimités.", w.gameMode, kGameModes, def.gameMode);
	u.SettingToggle("Commandes et triche", "Autorise /tp, /time, /spawn, /fly (modifiable plus tard).", w.cheats, def.cheats);
}

void DrawWorldTime(ui::Context &u, WorldSettings &w) {
	const WorldSettings def;
	u.Section(icons::Codepoint(icons::MaterialIcons::WB_SUNNY), "Temps");
	u.SettingToggle("Cycle jour / nuit", "Le soleil avance ; désactivé, l'heure reste figée.", w.dayCycle, def.dayCycle);
	int day = IndexOf(kDayLengthValues, 4, w.dayLengthMinutes);
	if (u.SettingChoice("Durée d'une journée", "Durée réelle d'un cycle jour / nuit.", day, kDayLengths, 1))
		w.dayLengthMinutes = kDayLengthValues[day];
}

/// Ligne « graine » personnalisée : champ + bouton de graine aléatoire.
void DrawSeedRow(App &app, ui::Context &u) {
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	const Rect r = u.Next(48);
	c.FillRoundRect(r, th.radius, th.row);
	const float fw = std::min(r.w * 0.46f, 380.f);
	const float labelW = r.w - fw - 64;
	u.TextIn({r.x + 10, r.y + 6, labelW, c.LineHeight(th.fontSize)}, "Graine", {th.text, th.fontSize, sdl3::CanvasFont::Regular, u.settingOverflow});
	u.TextIn({r.x + 10, r.y + 6 + c.LineHeight(th.fontSize), labelW, c.LineHeight(12)}, "Un même nombre redonne le même monde.",
			 {th.textDim, 12, sdl3::CanvasFont::Regular, u.settingOverflow});
	u.TextInputAt({r.Right() - 44 - fw, r.y + 10, fw, 28}, "seed", app.newSeedText);
	if (u.IconButton({r.Right() - 36, r.y + 9, 30, 30}, icons::Codepoint(icons::MaterialIcons::REFRESH), "Nouvelle graine aléatoire"))
		app.newSeedText = std::to_string(core::Mix64(sdl3::GetTicksMS() ^ core::HashString(app.newSeedText)) % 1000000000);
}

/// Ligne d'information (libellé / valeur) pour les paramètres figés d'un monde.
void DrawInfoRow(ui::Context &u, std::string_view label, std::string value) {
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	const Rect r = u.Next(30);
	c.FillRoundRect(r, th.radius, th.row);
	c.Text(r.x + 10, r.y + (r.h - c.LineHeight(th.fontSize)) * 0.5f, label, th.textDim, th.fontSize);
	c.Text(r.x + r.w * 0.45f, r.y + (r.h - c.LineHeight(th.fontSize)) * 0.5f, value, th.text, th.fontSize);
}

/// Ligne « touche / action » des commandes.
void DrawKeyRow(ui::Context &u, std::string_view key, std::string_view desc) {
	u.Row({1.f, 1.6f}, 24);
	u.Label(key, u.Style().accent, 13);
	u.Label(desc, {}, 13);
}

// ══════════════════════════════════════════════════════════════════════════
// Nouveau monde et paramètres de monde (factory builder)
// ══════════════════════════════════════════════════════════════════════════
void DrawNewWorldMenu(App &app, ui::Context &u) {
	static const WorldSettings def;
	const Rect dlg = Dialog(app, 1180, 680);
	WorldSettings &w = app.newWorld;
	if (u.PageHeader({dlg.x + 24, dlg.y + 20, dlg.w - 48, 60}, "Créer un nouveau monde",
					 "Toutes les options se modifient plus tard, sauf la génération du terrain."))
		app.screen = Screen::Main;
	const float top = dlg.y + 92, bottom = dlg.Bottom() - 76;
	const float colW = (dlg.w - 48 - 24) * 0.5f;
	u.BeginScroll("newworld-left", {dlg.x + 24, top, colW, bottom - top});
	u.Section(icons::Codepoint(icons::MaterialIcons::EDIT), "Général");
	u.SettingText("Nom du monde", "Nom affiché dans la liste des mondes.", w.name, def.name, "Nouveau monde");
	u.SettingText("Description", "Facultative.", w.description, "", "Une vallée paisible, un défi de survie…");
	DrawWorldRules(u, w);
	u.Section(icons::Codepoint(icons::MaterialIcons::TERRAIN), "Génération du terrain");
	u.SettingChoice("Type de génération", "Biomes et relief, monde plat, relief amplifié ou archipel.", w.generator, kGenerators, def.generator);
	DrawSeedRow(app, u);
	u.SettingFloat("Relief", "Amplitude des collines et des montagnes.", w.relief, 0.2f, 2.5f, def.relief, "×{:.2f}");
	u.SettingToggle("Arbres", "Forêts, taïgas et cactus.", w.trees, def.trees);
	u.EndScroll();
	u.BeginScroll("newworld-right", {dlg.x + 24 + colW + 24, top, colW, bottom - top});
	u.Section(icons::Codepoint(icons::MaterialIcons::PETS), "Créatures et événements");
	u.SettingToggle("Apparition des créatures", "Moutons, renards et chameaux dans les chunks générés.", w.creatures, def.creatures);
	DrawWorldTime(u, w);
	int st = StartTimeIndex(w.startTime);
	if (u.SettingChoice("Heure de départ", "Moment de la journée à la création.", st, kStartTimes, 0))
		w.startTime = kStartTimeValues[st];
	u.Section(icons::Codepoint(icons::MaterialIcons::INFO), "Résumé");
	u.Wrapped(std::format("{} · {} · relief ×{:.2f} · {} · {}", kGameModes[size_t(w.gameMode)], kGenerators[size_t(w.generator)], w.relief,
						  w.creatures ? "créatures" : "sans créatures", w.cheats ? "triche autorisée" : "sans triche"),
			  {}, 13);
	u.Wrapped(std::format("Dossier : {}", (app.WorldsDir() / WorldFolderName(w.name)).string()), {}, 12);
	u.EndScroll();
	DialogFooterHint(app, dlg, "Astuce : Échap revient en arrière sans rien créer.");
	const Rect f = DialogFooter(dlg);
	if (u.ButtonAt({f.Right() - 330, f.y, 110, f.h}, "Retour"))
		app.screen = Screen::Main;
	if (u.PrimaryButton({f.Right() - 200, f.y, 200, f.h}, "Créer le monde", !w.name.empty())) {
		uint64_t seed = 0;
		if (std::from_chars(app.newSeedText.data(), app.newSeedText.data() + app.newSeedText.size(), seed).ec != std::errc{})
			seed = core::HashString(app.newSeedText) % 1000000000;
		w.seed = seed;
		w.created = NowText();
		std::string folder = WorldFolderName(w.name);
		for (int n = 2; fs::exists(app.WorldsDir() / folder); ++n)
			folder = std::format("{}-{}", WorldFolderName(w.name), n);
		app.StartGame(folder, w);
	}
}

void DrawWorldSettingsMenu(App &app, ui::Context &u) {
	static const WorldSettings def;
	const Rect dlg = Dialog(app, 900, 660);
	WorldSettings &w = app.editWorld;
	const bool live = app.game && app.game->WorldDir().filename() == app.editWorldFolder;
	const Screen back = live ? Screen::Paused : Screen::Worlds;
	if (u.PageHeader({dlg.x + 24, dlg.y + 20, dlg.w - 48, 60}, std::format("Paramètres du monde — {}", w.name),
					 live ? "Partie en cours : les changements s'appliquent immédiatement." : std::format("Monde enregistré dans {}.", app.WorldsDir().string())))
		app.screen = back;
	u.BeginScroll("worldsettings", {dlg.x + 24, dlg.y + 92, dlg.w - 48, dlg.h - 92 - 76});
	u.Section(icons::Codepoint(icons::MaterialIcons::EDIT), "Général");
	u.SettingText("Nom du monde", "", w.name, def.name, "");
	u.SettingText("Description", "", w.description, "", "");
	DrawWorldRules(u, w);
	DrawWorldTime(u, w);
	u.Section(icons::Codepoint(icons::MaterialIcons::TERRAIN), "Génération (figée à la création)");
	DrawInfoRow(u, "Type", std::string(kGenerators[size_t(std::clamp(w.generator, 0, 3))]));
	DrawInfoRow(u, "Graine", std::to_string(w.seed));
	DrawInfoRow(u, "Relief", std::format("×{:.2f}", w.relief));
	DrawInfoRow(u, "Créatures / arbres", std::format("{} / {}", w.creatures ? "oui" : "non", w.trees ? "oui" : "non"));
	u.Section(icons::Codepoint(icons::MaterialIcons::STORAGE), "Données");
	DrawInfoRow(u, "Créé le", w.created.empty() ? "?" : w.created);
	DrawInfoRow(u, "Dernière partie", w.lastPlayed.empty() ? "jamais" : w.lastPlayed);
	DrawInfoRow(u, "Temps de jeu", std::format("{} h {:02} min", int(w.playSeconds / 3600), int(std::fmod(w.playSeconds / 60, 60))));
	DrawInfoRow(u, "Dossier", (app.WorldsDir() / app.editWorldFolder).string());
	u.EndScroll();
	DialogFooterHint(app, dlg, "La graine et le type de génération ne changent plus après la création.");
	const Rect f = DialogFooter(dlg);
	if (u.ButtonAt({f.Right() - 330, f.y, 110, f.h}, "Annuler"))
		app.screen = back;
	if (u.PrimaryButton({f.Right() - 200, f.y, 200, f.h}, "Enregistrer", !w.name.empty())) {
		if (live) {
			app.game->ApplyWorldSettings(w);
			app.game->WorldInfo().Save(app.game->WorldDir());
		} else {
			w.Save(app.WorldsDir() / app.editWorldFolder);
		}
		app.Ui().Notify("Paramètres du monde enregistrés", app.Ui().Style().success);
		app.screen = back;
	}
}

// ══════════════════════════════════════════════════════════════════════════
// Mondes (dessin personnalisé : liste de cartes)
// ══════════════════════════════════════════════════════════════════════════
void WorldsMenu(App &app) {
	sdl3::Canvas &c = app.Canvas();
	ui::Context &u = app.Ui();
	const ui::Theme &th = u.Style();
	const Rect dlg = Dialog(app, 980, 660);
	const auto worlds = app.ListWorlds();
	if (u.PageHeader({dlg.x + 24, dlg.y + 20, dlg.w - 48, 60}, "Mondes", std::format("{} monde(s) enregistré(s) · {}", worlds.size(), app.WorldsDir().string())))
		app.screen = Screen::Main;
	if (u.PrimaryButton({dlg.Right() - 224, dlg.y + 26, 200, 38}, "Nouveau monde")) {
		app.newWorld = WorldSettings{};
		app.newWorld.seed = core::Mix64(sdl3::GetTicksMS()) % 1000000000;
		app.newSeedText = std::to_string(app.newWorld.seed);
		app.screen = Screen::NewWorld;
	}
	u.BeginScroll("worlds", {dlg.x + 24, dlg.y + 96, dlg.w - 48, dlg.h - 96 - 24});
	if (worlds.empty())
		u.Label("Aucun monde : créez-en un.", th.textDim);
	for (const auto &[folder, ws] : worlds) {
		u.PushId(folder);
		const Rect r = u.Next(82);
		const bool hover = u.MouseIn(r);
		u.Card(r, hover, app.selectedWorld == folder);
		const Rect tile{r.x + 14, r.y + 14, 54, 54};
		c.FillRoundRect(tile, 8, Rgba(40, 70, 44));
		u.Icon(tile, ws.generator == 3 ? icons::Codepoint(icons::MaterialIcons::PUBLIC) : icons::Codepoint(icons::MaterialIcons::TERRAIN), th.accent, 30);
		c.Text(r.x + 84, r.y + 12, ws.name, th.text, 17, sdl3::CanvasFont::Bold);
		c.Text(r.x + 84, r.y + 36,
			   c.Ellipsize(std::format("{} · {} · graine {}{}", kGameModes[size_t(std::clamp(ws.gameMode, 0, 1))], kGenerators[size_t(std::clamp(ws.generator, 0, 3))],
									   ws.seed, ws.description.empty() ? "" : " · " + ws.description),
						   r.w - 420, 13),
			   th.textDim, 13);
		c.Text(r.x + 84, r.y + 56, std::format("Dernière partie : {}", ws.lastPlayed.empty() ? "jamais" : ws.lastPlayed), th.textDim, 12);
		if (app.confirmDelete && app.selectedWorld == folder) {
			c.Text(r.Right() - 400, r.y + 30, "Supprimer définitivement ?", th.danger, 14);
			if (u.DangerButton({r.Right() - 210, r.y + 22, 100, 36}, "Supprimer")) {
				std::error_code ec;
				fs::remove_all(app.WorldsDir() / folder, ec);
				u.Notify(std::format("Monde « {} » supprimé", ws.name), th.warning);
				app.confirmDelete = false;
				app.selectedWorld.clear();
			}
			if (u.ButtonAt({r.Right() - 100, r.y + 22, 86, 36}, "Annuler"))
				app.confirmDelete = false;
		} else {
			if (u.PrimaryButton({r.Right() - 220, r.y + 22, 110, 36}, "Jouer"))
				app.StartGame(folder, std::nullopt);
			if (u.IconButton({r.Right() - 100, r.y + 25, 32, 32}, icons::Codepoint(icons::MaterialIcons::EDIT), "Paramètres du monde")) {
				app.editWorld = ws;
				app.editWorldFolder = folder;
				app.screen = Screen::WorldSettings;
			}
			if (u.IconButton({r.Right() - 56, r.y + 25, 32, 32}, icons::Codepoint(icons::MaterialIcons::DELETE), "Supprimer le monde", true, th.danger)) {
				app.selectedWorld = folder;
				app.confirmDelete = true;
			}
		}
		u.PopId();
	}
	u.EndScroll();
}

// ══════════════════════════════════════════════════════════════════════════
// Mods : catalogue, détails, configuration
// ══════════════════════════════════════════════════════════════════════════
void ToggleMod(App &app, const voxel::ModInfo &m, bool enabled) {
	if (enabled)
		app.settings.disabledMods.erase(m.id);
	else
		app.settings.disabledMods.insert(m.id);
	app.contentDirty = true;
	app.settings.Save(app.settingsFile);
}

void ModsMenu(App &app) {
	sdl3::Canvas &c = app.Canvas();
	ui::Context &u = app.Ui();
	const ui::Theme &th = u.Style();
	const Rect dlg = Dialog(app, 1240, 680);
	auto &mods = app.content.mods.Mods();
	size_t enabled = 0, configurable = 0;
	for (const auto &m : mods) {
		enabled += !app.settings.disabledMods.contains(m.id);
		configurable += !m.settings.empty();
	}
	if (u.PageHeader({dlg.x + 24, dlg.y + 20, dlg.w - 48, 60}, "Mods",
					 std::format("{} installé(s) · {} activé(s) · {} configurable(s)", mods.size(), enabled, configurable)))
		app.screen = Screen::Main;
	// Catégories.
	std::vector<std::string> categories = {"Toutes", "Activés", "Avec paramètres"};
	for (const auto &m : mods)
		if (!m.category.empty() && std::ranges::find(categories, m.category) == categories.end())
			categories.push_back(m.category);
	const Rect side{dlg.x + 24, dlg.y + 96, 230, dlg.h - 120};
	c.Text(side.x, side.y - 4, "CATÉGORIES", th.textDim, 12, sdl3::CanvasFont::Bold);
	u.BeginColumn({side.x, side.y + 18, side.w, side.h - 18});
	for (size_t i = 0; i < categories.size(); ++i) {
		size_t count = 0;
		for (const auto &m : mods)
			count += i == 0 || (i == 1 && !app.settings.disabledMods.contains(m.id)) || (i == 2 && !m.settings.empty()) || (i > 2 && m.category == categories[i]);
		const std::string label = std::format("{}##cat{}", categories[i], i);
		const Rect r = u.Next(30);
		if (u.ButtonAt(r, label, true, app.modCategory == int(i)))
			app.modCategory = int(i);
		c.Text(r.Right() - 26, r.y + (r.h - c.LineHeight(12)) * 0.5f, std::to_string(count), app.modCategory == int(i) ? th.accentText : th.textDim, 12);
	}
	u.EndColumn();
	// Recherche + grille de cartes.
	const Rect area{side.Right() + 24, dlg.y + 96, dlg.Right() - side.Right() - 48, dlg.h - 120};
	u.Icon({area.x, area.y, 28, 32}, icons::Codepoint(icons::MaterialIcons::SEARCH), th.textDim, 20);
	u.TextInputAt({area.x + 34, area.y, area.w - 34, 32}, "modsearch", app.modFilter, nullptr, "Rechercher (nom, identifiant, mot-clé)…");
	const std::string filter = String(app.modFilter).ToLower();
	u.BeginScroll("modgrid", {area.x, area.y + 44, area.w, area.h - 44});
	const float cardW = (area.w - 10 - 16) * 0.5f, cardH = 176;
	int col = 0;
	Rect line{};
	for (const voxel::ModInfo &m : mods) {
		const bool on = !app.settings.disabledMods.contains(m.id);
		const size_t cat = size_t(app.modCategory);
		if ((cat == 1 && !on) || (cat == 2 && m.settings.empty()) || (cat > 2 && cat < categories.size() && m.category != categories[cat]))
			continue;
		if (!filter.empty()) {
			std::string hay = String(m.id + " " + m.name + " " + m.description + " " + m.category).ToLower();
			for (const auto &t : m.tags)
				hay += " " + t;
			if (hay.find(filter) == std::string::npos)
				continue;
		}
		if (col == 0)
			line = u.Next(cardH);
		const Rect r{line.x + float(col) * (cardW + 16), line.y, cardW, cardH};
		col = (col + 1) % 2;
		u.PushId(m.id);
		const bool hover = u.MouseIn(r);
		u.Card(r, hover, false);
		ModTile(c, {r.x + 16, r.y + 16, 50, 50}, m);
		c.Text(r.x + 80, r.y + 16, c.Ellipsize(m.name, r.w - 150, 17, sdl3::CanvasFont::Bold), th.text, 17, sdl3::CanvasFont::Bold);
		c.Text(r.x + 80, r.y + 40, std::format("par {} · v{}", m.author.empty() ? "inconnu" : m.author, m.version), th.textDim, 12);
		bool toggle = on;
		if (m.id != "core" && u.Toggle({r.Right() - 64, r.y + 18, 48, 26}, "enabled", toggle))
			ToggleMod(app, m, toggle);
		c.Text(r.x + 16, r.y + 76, c.Ellipsize(m.description.empty() ? "Aucune description." : m.description, r.w - 32, 13), th.text, 13);
		float bx = r.x + 16;
		auto badge = [&](std::string_view text, sdl3::Color col2) {
			const float bw = c.MeasureText(text, 12, sdl3::CanvasFont::Bold) + 20;
			u.Badge({bx, r.y + 102, bw, 22}, text, col2);
			bx += bw + 6;
		};
		if (!m.category.empty())
			badge(m.category, th.info);
		badge(m.id == "core" ? "Intégré" : (on ? "Activé" : "Désactivé"), m.id == "core" ? th.textDim : (on ? th.success : th.warning));
		if (!m.settings.empty())
			badge(std::format("{} paramètre(s)", m.settings.size()), th.accent);
		if (!m.dependencies.empty()) {
			std::string deps;
			for (const auto &d : m.dependencies)
				deps += (deps.empty() ? "" : ", ") + d;
			c.Text(r.x + 16, r.y + 134, c.Ellipsize("Requiert : " + deps, r.w - 260, 12), th.textDim, 12);
		}
		if (u.ButtonAt({r.Right() - 236, r.y + cardH - 48, 100, 34}, "Détails")) {
			app.selectedMod = m.id;
			app.screen = Screen::ModDetail;
		}
		if (u.ButtonAt({r.Right() - 128, r.y + cardH - 48, 112, 34}, "Configurer", !m.settings.empty())) {
			app.selectedMod = m.id;
			app.modDraft = app.modSettings;
			app.modReturn = Screen::Mods;
			app.screen = Screen::ModSettings;
		}
		u.PopId();
	}
	u.EndScroll();
	if (app.contentDirty)
		c.Text(area.x, dlg.Bottom() - 22, "Les mods activés ou désactivés s'appliquent au prochain lancement d'un monde.", th.warning, 12);
}

void ModDetailMenu(App &app) {
	sdl3::Canvas &c = app.Canvas();
	ui::Context &u = app.Ui();
	const ui::Theme &th = u.Style();
	const voxel::ModInfo *m = app.content.mods.Find(app.selectedMod);
	if (!m) {
		app.screen = Screen::Mods;
		return;
	}
	const Rect dlg = Dialog(app, 1100, 660);
	if (u.IconButton({dlg.x + 24, dlg.y + 40, 36, 36}, icons::Codepoint(icons::MaterialIcons::ARROW_BACK), "Retour"))
		app.screen = Screen::Mods;
	ModTile(c, {dlg.x + 76, dlg.y + 20, 84, 84}, *m);
	c.Text(dlg.x + 176, dlg.y + 20, m->name, th.text, 26, sdl3::CanvasFont::Bold);
	c.Text(dlg.x + 178, dlg.y + 56, std::format("par {} · identifiant « {} »", m->author.empty() ? "inconnu" : m->author, m->id), th.textDim, 13);
	const bool on = !app.settings.disabledMods.contains(m->id);
	float bx = dlg.x + 178;
	for (const auto &[text, col] : {std::pair<std::string, sdl3::Color>{m->category.empty() ? "mod" : m->category, th.info},
									{std::format("{} · v{}", on ? "Activé" : "Désactivé", m->version), on ? th.success : th.warning}}) {
		const float bw = c.MeasureText(text, 12, sdl3::CanvasFont::Bold) + 20;
		u.Badge({bx, dlg.y + 80, bw, 22}, text, col);
		bx += bw + 6;
	}
	if (u.ButtonAt({dlg.Right() - 164, dlg.y + 24, 140, 36}, "Configurer", !m->settings.empty())) {
		app.modDraft = app.modSettings;
		app.modReturn = Screen::ModDetail;
		app.screen = Screen::ModSettings;
	}
	if (m->id != "core") {
		if (on ? u.DangerButton({dlg.Right() - 164, dlg.y + 68, 140, 36}, "Désactiver") : u.PrimaryButton({dlg.Right() - 164, dlg.y + 68, 140, 36}, "Activer"))
			ToggleMod(app, *m, !on);
	}
	const float top = dlg.y + 124;
	const float leftW = (dlg.w - 48) * 0.58f;
	u.BeginScroll("moddetail-left", {dlg.x + 24, top, leftW, dlg.Bottom() - top - 20});
	u.Section(icons::Codepoint(icons::MaterialIcons::INFO), "Description");
	u.Wrapped(m->description.empty() ? "Aucune description." : m->description);
	u.Section(icons::Codepoint(icons::MaterialIcons::DESCRIPTION), "Contenu");
	u.Label(std::format("{} bloc(s) · {} script(s){}", m->blockCount, m->scripts.size(), m->hasConfig ? " · config.json" : ""), th.textDim);
	for (const auto &s : m->scripts)
		u.Label("• " + fs::relative(s, m->directory).string(), th.text, 13);
	if (!m->tags.empty()) {
		std::string tags;
		for (const auto &t : m->tags)
			tags += (tags.empty() ? "" : ", ") + t;
		u.Section(icons::Codepoint(icons::MaterialIcons::EXTENSION), "Mots-clés");
		u.Wrapped(tags, th.textDim, 13);
	}
	u.EndScroll();
	u.BeginScroll("moddetail-right", {dlg.x + 24 + leftW + 24, top, dlg.w - 48 - leftW - 24, dlg.Bottom() - top - 20});
	u.Section(icons::Codepoint(icons::MaterialIcons::EXTENSION), "Dépendances");
	if (m->dependencies.empty())
		u.Label("Aucune", th.textDim);
	for (const auto &d : m->dependencies) {
		const voxel::ModInfo *dep = app.content.mods.Find(d);
		const bool depOn = dep && !app.settings.disabledMods.contains(d);
		u.Row({1, 1}, 24);
		u.Label(d);
		u.Label(!dep ? "manquante" : (depOn ? "installée" : "désactivée"), !dep ? th.danger : (depOn ? th.success : th.warning));
	}
	u.Section(icons::Codepoint(icons::MaterialIcons::TUNE), "Paramètres configurables");
	if (m->settings.empty())
		u.Label("Aucun", th.textDim);
	for (const auto &s : m->settings)
		u.Label("• " + s.label, th.text, 13);
	u.Section(icons::Codepoint(icons::MaterialIcons::BUILD), "Informations techniques");
	auto kv = [&](std::string_view k, const std::string &v) {
		u.Row({1, 1.4f}, 22);
		u.Label(k, th.textDim, 13);
		u.Label(v, th.text, 13);
	};
	kv("Identifiant", m->id);
	kv("Version", m->version);
	kv("Priorité", std::to_string(m->priority));
	kv("Dossier", m->directory.string());
	u.EndScroll();
}

// ══════════════════════════════════════════════════════════════════════════
// Configuration des mods (factory builder, contenu dynamique)
// ══════════════════════════════════════════════════════════════════════════
void DrawModSettingsMenu(App &app, ui::Context &u) {
	const voxel::ModInfo *m = app.content.mods.Find(app.selectedMod);
	if (!m) {
		app.screen = Screen::Mods;
		return;
	}
	const Rect dlg = Dialog(app, 940, 660);
	if (u.PageHeader({dlg.x + 24, dlg.y + 20, dlg.w - 48, 60}, std::format("Configuration — {}", m->name),
					 "Valeurs par défaut pour tous les mondes"))
		app.screen = app.modReturn;
	u.BeginScroll("modsettings", {dlg.x + 24, dlg.y + 92, dlg.w - 48, dlg.h - 92 - 76});
	std::vector<std::string> groups;
	for (const auto &s : m->settings)
		if (std::ranges::find(groups, s.group) == groups.end())
			groups.push_back(s.group);
	for (const auto &group : groups) {
		u.Section(icons::Codepoint(icons::MaterialIcons::TUNE), group.empty() ? "Général" : group);
		for (const voxel::ModSetting &s : m->settings) {
			if (s.group != group)
				continue;
			const dataio::NodePtr current = app.modDraft.Get(*m, s);
			auto commit = [&app, m, s](const dataio::NodePtr &next) {
				if (next->Equals(s.defaultValue))
					app.modDraft.Reset(m->id, s.id);
				else
					app.modDraft.Set(m->id, s.id, next);
			};
			if (s.type == "bool") {
				bool v = current->AsBool(), d = s.defaultValue->AsBool();
				if (u.SettingToggle(s.label, s.description, v, d))
					commit(dataio::Node::MakeBool(v));
			} else if (s.type == "int") {
				int v = int(current->AsInt()), d = int(s.defaultValue->AsInt());
				if (u.SettingInt(s.label, s.description, v, int(s.min), int(s.max), d, s.unit))
					commit(dataio::Node::MakeInt(int64_t(v)));
			} else if (s.type == "string") {
				std::string v(current->AsString());
				if (u.SettingText(s.label, s.description, v, std::string(s.defaultValue->AsString()), ""))
					commit(dataio::Node::MakeString(std::move(v)));
			} else {
				float v = current->AsFloat(), d = s.defaultValue->AsFloat();
				if (u.SettingFloat(s.label, s.description, v, float(s.min), float(s.max), d, "{:.2f}"))
					commit(dataio::Node::MakeFloat(std::round(double(v) * 100.0) / 100.0));
			}
		}
	}
	u.EndScroll();
	DialogFooterHint(app, dlg, "Les changements s'appliquent au prochain chargement du monde.");
	const Rect f = DialogFooter(dlg);
	if (u.ButtonAt({f.Right() - 520, f.y, 180, f.h}, "Valeurs par défaut"))
		app.modDraft.ResetMod(m->id);
	if (u.ButtonAt({f.Right() - 330, f.y, 110, f.h}, "Annuler"))
		app.screen = app.modReturn;
	if (u.PrimaryButton({f.Right() - 200, f.y, 200, f.h}, "Enregistrer")) {
		app.modSettings = app.modDraft;
		app.modSettings.Save(app.modSettingsFile);
		app.Ui().Notify(std::format("Paramètres de « {} » enregistrés", m->name), app.Ui().Style().success);
		app.screen = app.modReturn;
	}
}

// ══════════════════════════════════════════════════════════════════════════
// Paramètres globaux (factory builder)
// ══════════════════════════════════════════════════════════════════════════
void DrawSettingsSidebar(App &app, ui::Context &u, const Rect &side) {
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	u.BeginColumn(side);
	for (size_t i = 0; i < std::size(kSettingsCategories); ++i) {
		const Rect r = u.Next(38);
		if (u.ButtonAt(r, std::format("##cat{}", i), true, app.settingsCategory == int(i)))
			app.settingsCategory = int(i);
		const bool sel = app.settingsCategory == int(i);
		u.Icon({r.x + 8, r.y, 28, r.h}, kSettingsCategories[i].icon, sel ? th.accentText : th.accent, 20);
		c.Text(r.x + 44, r.y + (r.h - c.LineHeight(th.fontSize)) * 0.5f, kSettingsCategories[i].name, sel ? th.accentText : th.text, th.fontSize,
			   sel ? sdl3::CanvasFont::Bold : sdl3::CanvasFont::Regular);
	}
	u.EndColumn();
}

void DrawSettingsBody(App &app, ui::Context &u, Settings &s, const Settings &d) {
	switch (app.settingsCategory) {
	case 0:
		u.Section(icons::Codepoint(icons::MaterialIcons::VISIBILITY), "Affichage");
		u.SettingInt("Distance de vue", "Rayon horizontal des chunks chargés et affichés.", s.viewDistance, 2, 24, d.viewDistance, "chunks");
		u.SettingInt("Distance verticale", "Couches de chunks au-dessus et au-dessous du joueur.", s.verticalDistance, 1, 6, d.verticalDistance, "chunks");
		u.SettingFloat("Champ de vision", "", s.fov, 50.f, 110.f, d.fov, "{:.0f}°");
		u.SettingToggle("Synchronisation verticale", "Limite les images par seconde au rafraîchissement de l'écran.", s.vsync, d.vsync);
		u.Section(icons::Codepoint(icons::MaterialIcons::WB_SUNNY), "Ambiance");
		u.SettingFloat("Début du brouillard", "Fraction de la distance de vue où le brouillard commence.", s.fogStart, 0.1f, 0.95f, d.fogStart, "%");
		u.SettingFloat("Luminosité", "", s.brightness, 0.5f, 1.6f, d.brightness, "×{:.2f}");
		u.SettingFloat("Distance de détail des textures",
					   "Au-delà de N blocs, on saute un pixel de plus tous les N blocs (plus la valeur est grande, plus les textures restent nettes loin).",
					   s.textureLodInterval, 2.f, 64.f, d.textureLodInterval, "{:.0f} blocs");
		u.SettingFloat("Réflexions (rebonds)", "Intensité des reflets sur les matériaux réfléchissants (métaux, verre, glace).", s.reflectionBounces, 0.f, 6.f,
					   d.reflectionBounces, "{:.1f}");
		u.Section(icons::Codepoint(icons::MaterialIcons::PETS), "Entités");
		u.SettingToggle("Afficher les entités", "Créatures et objets mobiles.", s.showEntities, d.showEntities);
		u.SettingInt("Rayon d'affichage des entités", "En chunks autour du joueur.", s.entityRenderRadius, 0, 6, d.entityRenderRadius, "chunks");
		u.SettingInt("Maximum d'entités", "Nombre maximal de pnj, animaux et monstres qui peuvent apparaître autour du joueur.", s.maxEntities, 8, 256,
					 d.maxEntities, "entités");
		break;
	case 1:
		u.Section(icons::Codepoint(icons::MaterialIcons::MOUSE), "Souris");
		u.SettingFloat("Sensibilité", "", s.sensitivity, 0.5f, 8.f, d.sensitivity, "{:.1f}");
		u.SettingToggle("Inverser l'axe vertical", "", s.invertY, d.invertY);
		u.Section(icons::Codepoint(icons::MaterialIcons::SPORTS_ESPORTS), "Déplacement");
		u.SettingFloat("Vitesse de vol", "Blocs par seconde.", s.flySpeed, 4.f, 40.f, d.flySpeed, "{:.0f}");
		u.SettingFloat("Vitesse de vol rapide", "Avec Ctrl.", s.sprintFlySpeed, 10.f, 120.f, d.sprintFlySpeed, "{:.0f}");
		u.Section(icons::Codepoint(icons::MaterialIcons::KEYBOARD), "Touches");
		DrawKeyRow(u, "ZQSD / WASD", "se déplacer");
		DrawKeyRow(u, "Espace / Maj", "monter, sauter / descendre, courir");
		DrawKeyRow(u, "Clic gauche / droit / E", "casser / poser / utiliser");
		DrawKeyRow(u, "1–9, molette", "barre d'outils");
		DrawKeyRow(u, "F", "vol (créatif ou triche)");
		DrawKeyRow(u, "T, Entrée, /", "console");
		DrawKeyRow(u, "F2 · F3 · F5 · F10 · F11", "capture · statistiques · sauvegarde · studio · plein écran");
		break;
	case 2:
		u.Section(icons::Codepoint(icons::MaterialIcons::VOLUME_UP), "Volumes");
		if (u.SettingFloat("Volume général", "", s.masterVolume, 0.f, 1.f, d.masterVolume, "%"))
			app.audio.SetVolumes(s.masterVolume, s.musicVolume, s.effectsVolume);
		if (u.SettingFloat("Musique", "", s.musicVolume, 0.f, 1.f, d.musicVolume, "%"))
			app.audio.SetVolumes(s.masterVolume, s.musicVolume, s.effectsVolume);
		if (u.SettingFloat("Effets et interface", "", s.effectsVolume, 0.f, 1.f, d.effectsVolume, "%"))
			app.audio.SetVolumes(s.masterVolume, s.musicVolume, s.effectsVolume);
		u.Section(icons::Codepoint(icons::MaterialIcons::MUSIC_NOTE), "Musique");
		u.SettingToggle("Musique dans les menus", "Thème principal derrière les menus.", s.menuMusic, d.menuMusic);
		u.SettingToggle("Lecture aléatoire en jeu", "Sinon, les pistes s'enchaînent dans l'ordre.", s.shuffleMusic, d.shuffleMusic);
		u.Label(std::format("{} piste(s) dans games/voxelia/mods/core/sounds/musics · en cours : {}", app.audio.Tracks().size() + 1,
							app.audio.CurrentTrack().empty() ? "aucune" : app.audio.CurrentTrack()),
				{}, 13);
		if (u.ButtonAt(u.Next(32), "Piste suivante"))
			app.audio.Next();
		break;
	case 3:
		u.Section(icons::Codepoint(icons::MaterialIcons::DASHBOARD), "Interface");
		u.SettingFloat("Échelle de l'interface", "Multiplie l'échelle de l'écran.", s.uiScale, 0.75f, 1.75f, d.uiScale, "×{:.2f}");
		u.SettingToggle("Statistiques de débogage (F3)", "", s.showDebug, d.showDebug);
		u.SettingToggle("Astuces du menu", "", s.showTips, d.showTips);
		u.SettingToggle("Monde animé derrière les menus", "Un monde de démonstration tourne lentement en arrière-plan.", s.menuBackground, d.menuBackground);
		u.Section(icons::Codepoint(icons::MaterialIcons::WARNING), "Jouabilité");
		u.SettingChoice("Difficulté",
						"Difficile : les créatures ont plus de vie et infligent plus de dégâts (exposé aux scripts via game.difficulty).", s.difficulty,
						kDifficulties, 0);
		break;
	default:
		u.Section(icons::Codepoint(icons::MaterialIcons::MEMORY), "Simulation");
		u.SettingInt("Distance de simulation", "Chunks où les créatures bougent.", s.simulationDistance, 1, 12, d.simulationDistance, "chunks");
		u.SettingInt("Rayon de recherche d'entités", "Chunks voisins interrogés par la grille spatiale.", s.entityQueryRadius, 0, 6, d.entityQueryRadius,
					 "chunks");
		u.Section(icons::Codepoint(icons::MaterialIcons::STORAGE), "Chargement");
		u.SettingInt("Chargements simultanés", "Requêtes base / génération en vol sur le pool.", s.maxPendingLoads, 4, 256, d.maxPendingLoads, "");
		u.SettingInt("Maillages simultanés", "", s.maxPendingMeshes, 2, 128, d.maxPendingMeshes, "");
		u.SettingInt("Budget de téléversement GPU", "Par image.", s.uploadBudgetMB, 1, 128, d.uploadBudgetMB, "Mo");
		u.Label(std::format("Pool : {} fil(s) de travail", app.pool.WorkerCount()), {}, 13);
		break;
	}
}

void DrawSettingsMenu(App &app, ui::Context &u) {
	static const Settings d;
	const Rect dlg = Dialog(app, 1080, 680);
	Settings &s = app.draft;
	const Rect side{dlg.x + 24, dlg.y + 96, 220, dlg.h - 180};
	const Rect body{side.Right() + 24, dlg.y + 96, dlg.Right() - side.Right() - 48, dlg.h - 96 - 76};
	if (u.PageHeader({dlg.x + 24, dlg.y + 20, dlg.w - 48, 60}, "Paramètres", "Graphismes, commandes, audio, interface et performances"))
		app.screen = app.settingsReturn;
	DrawSettingsSidebar(app, u, side);
	u.BeginScroll(std::format("settings{}", app.settingsCategory), body);
	DrawSettingsBody(app, u, s, d);
	u.EndScroll();
	DialogFooterHint(app, dlg, "Chaque flèche remet un paramètre à sa valeur par défaut.");
	const Rect f = DialogFooter(dlg);
	if (u.ButtonAt({f.Right() - 520, f.y, 180, f.h}, "Valeurs par défaut")) {
		const std::string lastWorld = s.lastWorld;
		const auto disabled = s.disabledMods;
		s = Settings{};
		s.lastWorld = lastWorld;
		s.disabledMods = disabled;
		app.audio.SetVolumes(s.masterVolume, s.musicVolume, s.effectsVolume);
	}
	if (u.ButtonAt({f.Right() - 330, f.y, 110, f.h}, "Annuler")) {
		app.audio.SetVolumes(app.settings.masterVolume, app.settings.musicVolume, app.settings.effectsVolume);
		app.screen = app.settingsReturn;
	}
	if (u.PrimaryButton({f.Right() - 200, f.y, 200, f.h}, "Enregistrer")) {
		app.settings = app.draft;
		app.ApplySettings();
		app.settings.Save(app.settingsFile);
		app.Ui().Notify("Paramètres enregistrés", app.Ui().Style().success);
		app.screen = app.settingsReturn;
	}
}

// ══════════════════════════════════════════════════════════════════════════
// Skin du personnage (dessin personnalisé : aperçu 3D + grille)
// ══════════════════════════════════════════════════════════════════════════
void SkinMenu(App &app, float dt) {
	sdl3::Canvas &c = app.Canvas();
	ui::Context &u = app.Ui();
	const ui::Theme &th = u.Style();
	const Rect dlg = Dialog(app, 1180, 660);
	if (u.PageHeader({dlg.x + 24, dlg.y + 20, dlg.w - 48, 60}, "Skin du personnage",
					 std::format("{} skins disponibles (textures/skins des mods)", app.content.skins.size())))
		app.screen = app.skinReturn;

	// Sélection courante : le skin choisi dans le menu, sinon celui du joueur, sinon le premier.
	const SkinDef *selected = app.content.FindSkin(app.selectedSkin);
	if (!selected && !app.content.skins.empty()) {
		app.selectedSkin = app.content.skins.front().id;
		selected = &app.content.skins.front();
	}
	const float top = dlg.y + 82, bottom = dlg.Bottom() - 22;

	// ── Aperçu en grand (gauche) ─────────────────────────────────────────
	const Rect preview{dlg.x + 24, top, 280, bottom - top};
	c.FillRoundRect(preview, 10, Rgba(26, 30, 38, 240));
	c.StrokeRoundRect(preview, 10, th.panelBorder);
	if (selected) {
		const float figH = std::min(preview.h - 120, 320.f);
		const Rect fig{preview.x + 20, preview.y + 18, preview.w - 40, figH};
		DrawSkin3D(app, *selected, fig, dt, true);
		const std::string label = selected->id.substr(selected->id.find(':') + 1);
		c.TextCentered({preview.x, fig.Bottom() + 10, preview.w, 22}, c.Ellipsize(label, preview.w - 24, 14), th.text, 14);
		const bool current = app.settings.playerSkin == selected->id;
		const Rect use{preview.x + (preview.w - 152) * 0.5f, fig.Bottom() + 40, 152, 36};
		if (current) {
			u.Badge(use.Inset(10, 6), "Skin actuel", th.success);
		} else if (u.PrimaryButton(use, "Utiliser ce skin")) {
			app.settings.playerSkin = selected->id;
			app.settings.Save(app.settingsFile);
			if (app.game)
				app.game->SetPlayerSkin(selected->id);
			u.Notify(std::format("Skin appliqué : {}", label), th.success);
			app.screen = app.skinReturn;
		}
	}

	// ── Recherche, pagination (en haut) et grille (droite) ───────────────
	const Rect area{preview.Right() + 20, top, dlg.Right() - 24 - (preview.Right() + 20), bottom - top};
	std::vector<const SkinDef *> shown;
	const std::string q = String(app.skinQuery).ToLower();
	for (const SkinDef &sk : app.content.skins)
		if (q.empty() || String(sk.name).ToLower().find(q) != std::string::npos || String(sk.id).ToLower().find(q) != std::string::npos)
			shown.push_back(&sk);
	constexpr int kCols = 10, kRows = 3, kPerPage = kCols * kRows;
	const int pages = std::max(1, int((shown.size() + kPerPage - 1) / kPerPage));
	app.skinPage = std::clamp(app.skinPage, 0, pages - 1);

	const float pagerW = 170;
	u.Icon({area.x, area.y, 28, 34}, icons::Codepoint(icons::MaterialIcons::SEARCH), th.textDim, 20);
	const std::string before = app.skinQuery;
	u.TextInputAt({area.x + 32, area.y, area.w - 32 - pagerW - 12, 34}, "skinfilter", app.skinQuery, nullptr, "Numéro ou nom (prince, rogue…)");
	if (app.skinQuery != before)
		app.skinPage = 0;
	const float px = area.Right() - pagerW;
	if (u.IconButton({px, area.y + 1, 32, 32}, icons::Codepoint(icons::MaterialIcons::ARROW_BACK), "Page précédente", app.skinPage > 0))
		--app.skinPage;
	c.TextCentered({px + 34, area.y, pagerW - 68, 34}, std::format("Page {} / {}", app.skinPage + 1, pages), th.text, 14);
	if (u.IconButton({area.Right() - 32, area.y + 1, 32, 32}, icons::Codepoint(icons::MaterialIcons::ARROW_FORWARD), "Page suivante", app.skinPage + 1 < pages))
		++app.skinPage;
	// Molette sur la grille : page suivante / précédente.
	const Rect grid{area.x, area.y + 46, area.w, area.h - 46};
	if (u.MouseIn(grid) && u.WheelY() != 0)
		app.skinPage = std::clamp(app.skinPage - (u.WheelY() > 0 ? 1 : -1), 0, pages - 1);

	const float gap = 8;
	const float cellW = std::min(70.f, (grid.w - gap * (kCols - 1)) / kCols);
	const float cellH = std::min(cellW * 1.72f, (grid.h - gap * (kRows - 1)) / kRows);
	for (int i = 0; i < kPerPage; ++i) {
		const size_t idx = size_t(app.skinPage) * kPerPage + size_t(i);
		if (idx >= shown.size())
			break;
		const SkinDef &sk = *shown[idx];
		const Rect cell{grid.x + float(i % kCols) * (cellW + gap), grid.y + float(i / kCols) * (cellH + gap), cellW, cellH};
		const bool on = sk.id == app.selectedSkin;
		u.PushId(sk.id);
		const auto it = u.Interact(u.MakeId("skincell"), cell);
		u.PopId();
		c.FillRoundRect(cell, 8, on ? Rgba(46, 96, 52, 150) : (it.hovered ? Rgba(40, 46, 58, 240) : Rgba(28, 32, 41, 235)));
		c.StrokeRoundRect(cell, 8, on ? th.accent : (it.hovered ? Rgba(86, 96, 118) : th.panelBorder), on ? 2.f : 1.f);
		DrawSkinFront(app, sk, cell.Inset(5, 6));
		if (it.hovered)
			u.Tooltip(sk.id);
		if (it.leftClicked)
			app.selectedSkin = sk.id;
	}
	if (shown.empty())
		c.TextCentered(grid, "Aucun skin ne correspond à la recherche.", th.textDim, 15);
}

// ══════════════════════════════════════════════════════════════════════════
// Pause (dessin personnalisé : panneau centré + boutons de menu)
// ══════════════════════════════════════════════════════════════════════════
void PauseMenu(App &app) {
	sdl3::Canvas &c = app.Canvas();
	ui::Context &u = app.Ui();
	c.FillRect({0, 0, c.Width(), c.Height()}, Rgba(0, 0, 0, 130));
	const float w = 420, h = 500;
	const Rect r{(c.Width() - w) * 0.5f, (c.Height() - 24 - h) * 0.5f, w, h};
	c.FillRoundRect(r, 12, Rgba(18, 21, 28, 240));
	c.StrokeRoundRect(r, 12, Rgba(52, 58, 72));
	c.Text(r.x + 24, r.y + 18, "Pause", u.Style().text, 24, sdl3::CanvasFont::Bold);
	c.Text(r.x + 24, r.y + 52, c.Ellipsize(std::format("« {} »", app.game->WorldInfo().name), w - 48, 13), u.Style().textDim, 13);
	u.BeginColumn({r.x + 24, r.y + 84, w - 48, h - 100});
	if (u.MenuButton("resume", icons::Codepoint(icons::MaterialIcons::PLAY_ARROW), "Reprendre", "Échap", true)) {
		app.screen = Screen::Playing;
		app.CaptureMouse(true);
	}
	if (u.MenuButton("settings", icons::Codepoint(icons::MaterialIcons::SETTINGS), "Paramètres", "")) {
		app.draft = app.settings;
		app.settingsReturn = Screen::Paused;
		app.screen = Screen::Settings;
	}
	if (u.MenuButton("skin", icons::Codepoint(icons::MaterialIcons::PERSON), "Skin du personnage", "Choisir votre apparence")) {
		app.selectedSkin = app.settings.playerSkin;
		app.skinQuery.clear();
		app.skinPage = 0;
		app.skinReturn = Screen::Paused;
		app.screen = Screen::Skins;
	}
	if (u.MenuButton("world", icons::Codepoint(icons::MaterialIcons::PUBLIC), "Paramètres du monde", "Mode de jeu, triche, temps")) {
		app.editWorld = app.game->WorldInfo();
		app.editWorldFolder = app.game->WorldDir().filename().string();
		app.screen = Screen::WorldSettings;
	}
	if (u.MenuButton("studio", icons::Codepoint(icons::MaterialIcons::VIEW_IN_AR), "Ouvrir le studio", "F10"))
		app.OpenStudio();
	if (u.MenuButton("save", icons::Codepoint(icons::MaterialIcons::SAVE), "Sauvegarder", "F5")) {
		app.game->Save();
		u.Notify("Monde sauvegardé", u.Style().success);
	}
	if (u.MenuButton("quit", icons::Codepoint(icons::MaterialIcons::EXIT_TO_APP), "Quitter vers le menu", "Le monde est sauvegardé"))
		app.LeaveGame();
	u.EndColumn();
}

/// Dessine l'écran courant (menu, dialogue, console) avec le contexte immédiat.
void DrawScreen(App &app, ui::Context &u) {
	switch (app.screen) {
	case Screen::Main: MainMenu(app, u.Dt()); break;
	case Screen::NewWorld: DrawNewWorldMenu(app, u); break;
	case Screen::Worlds: WorldsMenu(app); break;
	case Screen::WorldSettings: DrawWorldSettingsMenu(app, u); break;
	case Screen::Mods: ModsMenu(app); break;
	case Screen::ModDetail: ModDetailMenu(app); break;
	case Screen::ModSettings: DrawModSettingsMenu(app, u); break;
	case Screen::Settings: DrawSettingsMenu(app, u); break;
	case Screen::Skins: SkinMenu(app, u.Dt()); break;
	case Screen::Paused: PauseMenu(app); break;
	case Screen::Playing:
		if (app.consoleOpen)
			DrawConsole(app);
		break;
	}
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// Construction de l'interface (factory builder) — appelée à l'initialisation
// ══════════════════════════════════════════════════════════════════════════
void BuildInterface(App &app) {
	app.interface = std::make_unique<ui::Ui>(app.Canvas(), app.Events());
	app.interface->SetImmediateContext(&app.Ui());
	app.interface->Root(ui::Column().Children({
		ui::Custom([&app](ui::Context &u) { DrawScreen(app, u); }).Grow(),
		ui::Custom([&app](ui::Context &) {
			if (app.screen != Screen::Playing)
				StatusBar(app);
		}).Height(26),
	}));
}

void DrawConsole(App &app) {
	sdl3::Canvas &c = app.Canvas();
	ui::Context &u = app.Ui();
	const float h = std::min(360.f, c.Height() * 0.45f);
	const Rect area{12, c.Height() - h - 96, std::min(920.f, c.Width() - 24), h};
	c.FillRoundRect(area, 8, Rgba(8, 10, 14, 220));
	auto &lines = app.game->ConsoleLines();
	float y = area.Bottom() - 52;
	c.PushClip(area);
	for (auto it = lines.rbegin(); it != lines.rend() && y > area.y; ++it) {
		const auto parts = String(*it).Split('\n');
		for (auto p = parts.rbegin(); p != parts.rend() && y > area.y; ++p) {
			c.Text(area.x + 10, y, *p, it->starts_with(">") ? u.Style().accentHover : u.Style().text, 14, sdl3::CanvasFont::Mono);
			y -= 18;
		}
	}
	c.PopClip();
	u.BeginColumn({area.x + 8, area.Bottom() - 38, area.w - 16, 30});
	bool submitted = false;
	u.TextInput("##console", app.consoleLine, &submitted, "commande (/help) ou code Script");
	u.EndColumn();
	if (submitted && !app.consoleLine.empty()) {
		app.game->ExecuteConsole(app.consoleLine);
		app.consoleLine.clear();
	}
}

} // namespace voxelia
