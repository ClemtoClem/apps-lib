// Voxelia — HUD du jeu et menus d'inventaire (survie, créatif avec recherche).
//
// Tout est placé dynamiquement (ui/layout.hpp) à partir de la taille de la fenêtre :
// échelle d'interface `FitScale` (1 à 1280×720), barre d'outils ancrée en bas, statut
// empilé au-dessus, indications sous le viseur, boîtes de dialogue centrées et bornées.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

#include <core/log.hpp>
#include <diagnostics/trace.hpp>
#include <ui/layout.hpp>

#include "app.hpp"

namespace voxelia {

namespace {

constexpr int kAtlasCell = 32;

/// Projection isométrique d'un point du bloc (−0,5..0,5) dans une icône 32×32.
struct IsoPoint {
	float x, y;
};
IsoPoint Iso(float x, float y, float z) noexcept { return {16.f + (x - z) * 15.f, 16.f + (x + z) * 7.5f - y * 16.f}; }

/// Rastérise un parallélogramme O + u·A + v·B (u, v ∈ [0, 1]) avec `shade(u, v)` → RGBA.
template <typename Fn> void FillFace(uint8_t *dst, int pitch, IsoPoint o, IsoPoint a, IsoPoint b, Fn &&sample) {
	const float det = a.x * b.y - a.y * b.x;
	if (std::fabs(det) < 1e-4f)
		return;
	const float minX = std::min({o.x, o.x + a.x, o.x + b.x, o.x + a.x + b.x}), maxX = std::max({o.x, o.x + a.x, o.x + b.x, o.x + a.x + b.x});
	const float minY = std::min({o.y, o.y + a.y, o.y + b.y, o.y + a.y + b.y}), maxY = std::max({o.y, o.y + a.y, o.y + b.y, o.y + a.y + b.y});
	for (int py = std::max(0, int(std::floor(minY))); py < std::min(kAtlasCell, int(std::ceil(maxY)) + 1); ++py)
		for (int px = std::max(0, int(std::floor(minX))); px < std::min(kAtlasCell, int(std::ceil(maxX)) + 1); ++px) {
			const float dx = float(px) + 0.5f - o.x, dy = float(py) + 0.5f - o.y;
			const float u = (dx * b.y - dy * b.x) / det, v = (a.x * dy - a.y * dx) / det;
			if (u < 0.f || u > 1.f || v < 0.f || v > 1.f)
				continue;
			uint8_t rgba[4];
			sample(u, v, rgba);
			if (rgba[3] < 40)
				continue;
			uint8_t *d = dst + size_t(py) * size_t(pitch) + size_t(px) * 4;
			std::memcpy(d, rgba, 4);
		}
}

const uint8_t *LayerPixels(const Content &ct, uint16_t layer) {
	return ct.textureLayers.data() + size_t(layer) * voxel::kTextureSize * voxel::kTextureSize * 4;
}

void SampleLayer(const uint8_t *px, float u, float v, float shade, uint8_t out[4]) {
	const int x = std::clamp(int(u * float(voxel::kTextureSize)), 0, voxel::kTextureSize - 1);
	const int y = std::clamp(int(v * float(voxel::kTextureSize)), 0, voxel::kTextureSize - 1);
	const uint8_t *p = px + (size_t(y) * voxel::kTextureSize + size_t(x)) * 4;
	out[0] = uint8_t(float(p[0]) * shade);
	out[1] = uint8_t(float(p[1]) * shade);
	out[2] = uint8_t(float(p[2]) * shade);
	out[3] = p[3];
}

/// Icône 32×32 d'un bloc : image d'objet déclarée, plante en croix (à plat), sinon pavé
/// isométrique aux dimensions de sa forme (dalle, lit, bouton…).
void RenderBlockIcon(const Content &ct, const voxel::BlockDef &def, uint8_t *dst, int pitch) {
	if (!def.icon.empty())
		if (auto src = ct.mods.ResolveTexture(def.icon); src && !src->path.empty())
			if (auto img = sdl3::LoadImageRgba(src->path)) {
				const sdl3::Surface t = sdl3::SquareThumbnail(img.Value(), kAtlasCell).UnwrapOr(sdl3::Surface{});
				for (int y = 0; y < kAtlasCell; ++y)
					std::memcpy(dst + size_t(y) * size_t(pitch), &t.Pixels()[size_t(y) * kAtlasCell * 4], kAtlasCell * 4);
				return;
			}
	const uint8_t *side = LayerPixels(ct, def.layers[size_t(voxel::Face::PosZ)]);
	if (def.cross || (def.material == voxel::Material::Cutout && def.boxes.empty() && def.mesh.empty() && !def.HasTag("leaves") &&
					  def.name.find("leaves") == std::string::npos)) {
		for (int y = 0; y < kAtlasCell; ++y)
			for (int x = 0; x < kAtlasCell; ++x) {
				uint8_t c[4];
				SampleLayer(side, (float(x) + 0.5f) / kAtlasCell, (float(y) + 0.5f) / kAtlasCell, 1.f, c);
				if (c[3] >= 40)
					std::memcpy(dst + size_t(y) * size_t(pitch) + size_t(x) * 4, c, 4);
			}
		return;
	}
	// Emprise de la forme (boîtes) ; cube plein sinon.
	float x0 = -0.5f, y0 = -0.5f, z0 = -0.5f, x1 = 0.5f, y1 = 0.5f, z1 = 0.5f;
	if (!def.boxes.empty()) {
		x0 = y0 = z0 = 1.f;
		x1 = y1 = z1 = -1.f;
		for (const voxel::BlockBox &b : def.boxes) {
			x0 = std::min(x0, b[0]), y0 = std::min(y0, b[1]), z0 = std::min(z0, b[2]);
			x1 = std::max(x1, b[3]), y1 = std::max(y1, b[4]), z1 = std::max(z1, b[5]);
		}
		// Formes très fines (boutons) : centrées verticalement pour rester lisibles.
		if (y1 - y0 < 0.3f) {
			const float c = (y1 + y0) * 0.5f;
			y0 -= c;
			y1 -= c;
		}
	}
	const uint8_t *top = LayerPixels(ct, def.layers[size_t(voxel::Face::PosY)]);
	const uint8_t *right = LayerPixels(ct, def.layers[size_t(voxel::Face::PosX)]);
	auto sub = [](IsoPoint p, IsoPoint q) { return IsoPoint{p.x - q.x, p.y - q.y}; };
	{ // dessus
		const IsoPoint o = Iso(x0, y1, z0);
		FillFace(dst, pitch, o, sub(Iso(x1, y1, z0), o), sub(Iso(x0, y1, z1), o), [&](float u, float v, uint8_t c[4]) {
			SampleLayer(top, x0 + 0.5f + u * (x1 - x0), z0 + 0.5f + v * (z1 - z0), 1.f, c);
		});
	}
	{ // face +Z (gauche de l'icône)
		const IsoPoint o = Iso(x0, y1, z1);
		FillFace(dst, pitch, o, sub(Iso(x1, y1, z1), o), sub(Iso(x0, y0, z1), o), [&](float u, float v, uint8_t c[4]) {
			SampleLayer(side, x0 + 0.5f + u * (x1 - x0), 0.5f - y1 + v * (y1 - y0), 0.8f, c);
		});
	}
	{ // face +X (droite de l'icône)
		const IsoPoint o = Iso(x1, y1, z1);
		FillFace(dst, pitch, o, sub(Iso(x1, y1, z0), o), sub(Iso(x1, y0, z1), o), [&](float u, float v, uint8_t c[4]) {
			SampleLayer(right, 0.5f - z1 + u * (z1 - z0), 0.5f - y1 + v * (y1 - y0), 0.62f, c);
		});
	}
}

std::string Lower(std::string_view s) {
	std::string out(s);
	for (char &ch : out)
		ch = char(std::tolower(static_cast<unsigned char>(ch)));
	return out;
}

/// Catégories de l'inventaire créatif.
constexpr const char *kCreativeTabs[] = {"Tous", "Construction", "Nature", "Mobilier", "Mécanismes", "Liquides", "Objets", "Armures"};

int CategoryOf(const Content &ct, ItemId id) {
	if (!IsBlockItem(id)) {
		const ItemDef *d = ct.Item(id);
		return d && !d->armorSlot.empty() ? 7 : 6;
	}
	const voxel::BlockDef &def = ct.blocks.Get(voxel::BlockId(id));
	auto tag = [&](std::initializer_list<const char *> tags) {
		for (const char *t : tags)
			if (def.HasTag(t))
				return true;
		return false;
	};
	if (def.IsLiquid())
		return 5;
	if (tag({"mechanism", "button", "lever", "light", "lamp", "elevator", "rail"}) || !def.interact.call.empty() ||
		def.name.find("lamp") != std::string::npos)
		return 4;
	if (tag({"furniture", "door", "bed", "fence", "stairs", "slab", "container", "seat"}) || def.HasShape())
		return 3;
	if (tag({"natural", "plant", "flower", "leaves", "log", "ore", "soil", "sand", "crop", "tree"}) || def.cross ||
		def.material == voxel::Material::Cutout)
		return 2;
	return 1;
}

} // namespace

// ── Atlas d'icônes ──────────────────────────────────────────────────────────

void Game::EnsureIconAtlas(sdl3::Canvas &c) {
	const size_t blocks = m_content.blocks.Count(), items = m_content.items.size();
	if (m_iconAtlas && blocks == m_iconAtlasBlocks && items == m_iconAtlasItems)
		return;
	DIAG_ZONE("Game::EnsureIconAtlas");
	const size_t total = blocks + items;
	const int rows = int((total + kIconColumns - 1) / kIconColumns);
	const int w = kIconColumns * kIconSize, h = std::max(1, rows) * kIconSize;
	std::vector<uint8_t> px(size_t(w) * size_t(h) * 4, 0);
	const int pitch = w * 4;
	for (size_t i = 1; i < total; ++i) {
		uint8_t *cell = px.data() + (size_t(i / kIconColumns) * kIconSize * size_t(w) + size_t(i % kIconColumns) * kIconSize) * 4;
		if (i < blocks) {
			const voxel::BlockDef &def = m_content.blocks.Get(voxel::BlockId(i));
			if (def.IsVisible())
				RenderBlockIcon(m_content, def, cell, pitch);
		} else if (const ItemDef *d = m_content.Item(ItemId(kItemBase + (i - blocks))); d && d->icon.size() == 16 * 16 * 4) {
			for (int y = 0; y < kIconSize; ++y)
				for (int x = 0; x < kIconSize; ++x)
					std::memcpy(cell + size_t(y) * size_t(pitch) + size_t(x) * 4, &d->icon[(size_t(y / 2) * 16 + size_t(x / 2)) * 4], 4);
		}
	}
	m_iconAtlas.reset();
	if (auto t = sdl3::Texture::Create(c.Renderer(), w, h, sdl3::PixelFormat::RGBA32)) {
		m_iconAtlas.emplace(std::move(t.Value()));
		m_iconAtlas->SetScaleMode(sdl3::ScaleMode::NEAREST);
		m_iconAtlas->SetBlendMode(sdl3::BlendMode::BLEND);
		m_iconAtlas->Update(px.data(), pitch);
	}
	m_iconAtlasBlocks = blocks;
	m_iconAtlasItems = items;
}

void Game::DrawItemIcon(sdl3::Canvas &c, const sdl3::FRect &r, ItemId item, uint8_t alpha) {
	if (item == 0 || !m_iconAtlas)
		return;
	const size_t index = IsBlockItem(item) ? size_t(item) : m_iconAtlasBlocks + size_t(item - kItemBase);
	const sdl3::FRect src{float(index % kIconColumns * kIconSize), float(index / kIconColumns * kIconSize), float(kIconSize), float(kIconSize)};
	c.ImageRegion(*m_iconAtlas, float(kIconColumns * kIconSize), float(m_iconAtlas->GetHeight()), src, r, false, sdl3::Rgba(255, 255, 255, alpha));
}

// ── HUD ─────────────────────────────────────────────────────────────────────

void Game::DrawHud(sdl3::Canvas &c, float dt) {
	DIAG_ZONE("Game::DrawHud");
	EnsureIconAtlas(c);
	const ui::Theme &th = m_ui.Style();
	const float s = ui::FitScale(c.Width(), c.Height(), 1280.f, 720.f, 0.75f, 2.2f);
	const sdl3::FRect screen{0, 0, c.Width(), c.Height()};
	const bool survival = !Creative();

	// Sommeil (la nuit) : l'écran s'assombrit.
	if (m_bed && (m_timeOfDay < 0.23f || m_timeOfDay > 0.77f)) {
		const float k = std::clamp(m_sleepTime / 3.f, 0.f, 0.88f);
		c.FillRect(screen, sdl3::Rgba(4, 6, 14, uint8_t(255.f * k)));
		if (k > 0.5f)
			c.TextCentered(ui::Place(screen, 400 * s, 40 * s, ui::Anchor::Center, 0, -30 * s), "Z z z…", sdl3::Rgba(200, 210, 240, uint8_t(255.f * k)),
						   26 * s, sdl3::CanvasFont::Bold);
	}

	if (m_inventoryOpen) {
		DrawInventoryMenu(c);
		return;
	}

	// ── Viseur et indication d'interaction ───────────────────────────────
	const float cx = c.Width() * 0.5f, cy = c.Height() * 0.5f;
	const bool interactive = !m_actionHint.empty() || m_targetEntity;
	const sdl3::Color cross = interactive ? th.accent : sdl3::Rgba(255, 255, 255, 220);
	const float arm = 9.f * s, thick = std::max(2.f, 2.f * s);
	if (m_cameraMode == CameraMode::FirstPerson || m_cameraMode == CameraMode::ThirdBack) { // viseur (vues qui visent)
		c.FillRect({cx - arm, cy - thick * 0.5f, arm * 2, thick}, cross);
		c.FillRect({cx - thick * 0.5f, cy - arm, thick, arm * 2}, cross);
	}
	{
		ui::AnchorStack below(ui::Place(screen, 1, 1, ui::Anchor::Center, 0, 0), ui::Anchor::Bottom, 14.f * s);
		std::string target;
		if (m_targetEntity) {
			const auto *k = m_ecs.Get<voxel::Kind>(m_targetEntity->first);
			target = k ? k->id : "entité";
			if (const auto *h = m_ecs.Get<voxel::Health>(m_targetEntity->first))
				target += std::format("  ·  PV {:.0f}/{:.0f}", h->current, h->max);
		} else if (m_target) {
			target = m_content.blocks.Get(m_target->id).displayName;
		}
		if (!target.empty()) {
			const float fs = 13.f * s;
			const float w = c.MeasureText(target, fs) + 16.f * s;
			const sdl3::FRect r = below.Push(w, c.LineHeight(fs) + 6.f * s);
			c.FillRoundRect(r, 4.f * s, sdl3::Rgba(0, 0, 0, 120));
			c.TextCentered(r, target, sdl3::Rgba(255, 255, 255, 220), fs);
		}
		if (!m_actionHint.empty()) {
			const float fs = 12.f * s;
			const float w = c.MeasureText(m_actionHint, fs) + 18.f * s;
			const sdl3::FRect r = below.Push(w, c.LineHeight(fs) + 6.f * s);
			c.FillRoundRect(r, 4.f * s, sdl3::WithAlpha(th.accent, 70));
			c.TextCentered(r, m_actionHint, th.text, fs);
		}
	}

	// ── Barre d'outils (ancrée en bas, centrée) ───────────────────────────
	const float slot = 44.f * s, gap = 4.f * s, pad = 5.f * s;
	const sdl3::FRect bar = ui::Place(screen, float(kHotbarSlots) * slot + float(kHotbarSlots - 1) * gap + pad * 2, slot + pad * 2, ui::Anchor::Bottom,
									0, 10.f * s);
	c.FillRoundRect(bar, 8.f * s, sdl3::Rgba(10, 12, 18, 190));
	const ui::GridLayout cells = ui::Grid(bar.Inset(pad), int(kHotbarSlots), slot, gap, int(kHotbarSlots), ui::Align::Start);
	for (size_t i = 0; i < kHotbarSlots && i < m_slots.size(); ++i) {
		const bool selected = int(i) == m_hotbarIndex;
		sdl3::FRect r = cells.Cell(int(i));
		if (selected)
			r.y -= 3.f * s;
		c.FillRoundRect(r, 5.f * s, selected ? sdl3::Rgba(40, 46, 60, 235) : sdl3::Rgba(22, 25, 33, 220));
		DrawItemIcon(c, r.Inset(6.f * s), m_slots[i].id);
		if (survival && m_slots[i].count > 1)
			c.Text(r.Right() - c.MeasureText(std::to_string(m_slots[i].count), 12.f * s) - 4.f * s, r.Bottom() - c.LineHeight(12.f * s) - 1.f * s,
				   std::to_string(m_slots[i].count), th.text, 12.f * s, sdl3::CanvasFont::Bold);
		c.Text(r.x + 4.f * s, r.y + 2.f * s, std::to_string(i + 1), sdl3::WithAlpha(th.textDim, 170), 10.f * s);
		c.StrokeRoundRect(r, 5.f * s, selected ? th.accent : sdl3::Rgba(64, 70, 86), selected ? 2.5f * s : 1.f);
	}
	// Main gauche : case séparée à gauche de la barre.
	if (!m_offhand.Empty()) {
		const sdl3::FRect oh = ui::Place({0, bar.y, bar.x - 12.f * s, bar.h}, slot + pad * 2, slot + pad * 2, ui::Anchor::Right);
		c.FillRoundRect(oh, 8.f * s, sdl3::Rgba(10, 12, 18, 190));
		DrawItemIcon(c, oh.Inset(pad + 6.f * s), m_offhand.id);
		if (survival && m_offhand.count > 1)
			c.Text(oh.Right() - 18.f * s, oh.Bottom() - 18.f * s, std::to_string(m_offhand.count), th.text, 12.f * s, sdl3::CanvasFont::Bold);
		c.Text(oh.x + 4.f * s, oh.y + 2.f * s, "G", th.textDim, 10.f * s);
	}

	// ── Statut empilé au-dessus de la barre ───────────────────────────────
	ui::AnchorStack above(bar, ui::Anchor::Top, 6.f * s);
	const float icon = 9.f * s, step = 11.f * s;
	if (survival) {
		const sdl3::FRect row = above.Push(bar.w, icon + 2.f * s, ui::Align::Center);
		// Cœurs (gauche).
		const auto *h = m_ecs.Get<voxel::Health>(m_player);
		const float hp = h ? h->current : 0.f, hpMax = h ? h->max : 20.f;
		const int hearts = int(std::max(1.f, hpMax * 0.5f));
		for (int i = 0; i < hearts; ++i) {
			const float x = row.x + float(i) * step + icon * 0.5f, y = row.y + icon * 0.5f;
			const float fill = std::clamp(hp - float(i) * 2.f, 0.f, 2.f) * 0.5f;
			c.FillCircle(x, y, icon * 0.55f, sdl3::Rgba(24, 24, 28, 230));
			if (fill >= 0.75f)
				c.FillCircle(x, y, icon * 0.45f, sdl3::Rgba(232, 48, 48, 240));
			else if (fill > 0.f)
				c.FillCircle(x - icon * 0.18f, y, icon * 0.3f, sdl3::Rgba(232, 48, 48, 220));
		}
		// Faim (droite, de droite à gauche).
		for (int i = 0; i < 10; ++i) {
			const float x = row.Right() - float(i + 1) * step;
			const bool full = m_hunger >= float(i + 1) * 2.f, half = !full && m_hunger >= float(i) * 2.f + 1.f;
			c.FillRoundRect({x, row.y, icon, icon}, 2.f * s, sdl3::Rgba(24, 24, 28, 220));
			if (full || half)
				c.FillRoundRect({x + 1.f, row.y + 1.f, half ? icon * 0.5f : icon - 2.f, icon - 2.f}, 2.f * s, sdl3::Rgba(204, 128, 52, 240));
		}
	}
	// Armure (gauche) et air (droite) sur la rangée suivante, seulement si utiles.
	const int armor = ArmorPoints();
	const bool underwater = m_air < 10.f - 0.01f;
	if (armor > 0 || underwater) {
		const sdl3::FRect row = above.Push(bar.w, icon + 2.f * s, ui::Align::Center);
		for (int i = 0; i < std::min(armor, 20) / 2 + armor % 2; ++i) {
			const float x = row.x + float(i) * step;
			const bool half = armor % 2 == 1 && i == armor / 2;
			c.FillRoundRect({x, row.y, half ? icon * 0.5f : icon, icon}, 2.f * s, sdl3::Rgba(176, 186, 204, 235));
		}
		if (underwater) {
			const int bubbles = int(std::ceil(std::clamp(m_air, 0.f, 10.f)));
			for (int i = 0; i < bubbles; ++i)
				c.FillCircle(row.Right() - float(i) * step - icon * 0.5f, row.y + icon * 0.5f, icon * 0.45f, sdl3::Rgba(96, 176, 244, 235));
		}
	}
	// Nom de l'objet en main (s'efface après 2,5 s).
	const ItemStack &held = Held(true);
	if (m_hotbarIndex != m_lastShownSlot || held.id != m_lastShownItem) {
		m_lastShownSlot = m_hotbarIndex;
		m_lastShownItem = held.id;
		m_itemNameTimer = 2.5f;
	}
	m_itemNameTimer = std::max(0.f, m_itemNameTimer - dt);
	if (m_itemNameTimer > 0.f && !held.Empty()) {
		const std::string name = m_content.ItemLabel(held.id);
		const float fs = 15.f * s;
		const sdl3::FRect r = above.Push(c.MeasureText(name, fs) + 20.f * s, c.LineHeight(fs) + 6.f * s);
		const uint8_t a = uint8_t(255.f * std::clamp(m_itemNameTimer / 0.6f, 0.f, 1.f));
		c.FillRoundRect(r, 4.f * s, sdl3::Rgba(0, 0, 0, uint8_t(a / 2)));
		c.TextCentered(r, name, sdl3::WithAlpha(th.text, a), fs);
	}

	// ── État du joueur (haut à gauche, sous les infos de débogage) ─────────
	{
		std::string state;
		switch (m_avatarState) {
		case voxel::AvatarState::Crouch: state = "Accroupi"; break;
		case voxel::AvatarState::Sit: state = "Assis"; break;
		case voxel::AvatarState::Sleep: state = "Couché"; break;
		case voxel::AvatarState::Swim: state = std::format("Nage : {}  (K : changer)", voxel::SwimStyleLabel(m_swimStyle)); break;
		case voxel::AvatarState::FreeFall: state = "Chute libre !"; break;
		default: break;
		}
		if (m_gesture != voxel::AvatarGesture::None)
			state = voxel::AvatarGestureLabel(m_gesture);
		if (m_cameraMode != CameraMode::FirstPerson) {
			if (!state.empty())
				state += "  ·  ";
			state += m_cameraMode == CameraMode::ThirdBack ? "Vue de dos (F6)" : m_cameraMode == CameraMode::ThirdFront ? "Vue de face (F6)" : "Vue de côté (F6)";
		}
		if (!state.empty()) {
			const float fs = 13.f * s;
			const sdl3::FRect r = ui::Place(screen, c.MeasureText(state, fs) + 20.f * s, c.LineHeight(fs) + 8.f * s, ui::Anchor::Top, 0, 10.f * s);
			c.FillRoundRect(r, 5.f * s, sdl3::Rgba(0, 0, 0, 140));
			c.TextCentered(r, state, th.text, fs);
		}
	}
	// Aide des commandes (haut à droite) pendant les premières secondes.
	if (m_animTime < 14.f) {
		static constexpr const char *kHelp[] = {"Clic gauche : casser / frapper", "Clic droit : utiliser / poser", "Maj : s'accroupir",
												"V / B / N : coucou", "K : style de nage", "F6 : caméra", "X : changer de main", "Tab : inventaire"};
		const float fs = 12.f * s, lh = c.LineHeight(fs) + 2.f * s;
		float w = 0.f;
		for (const char *line : kHelp)
			w = std::max(w, c.MeasureText(line, fs));
		const uint8_t a = uint8_t(255.f * std::clamp((14.f - m_animTime) / 2.f, 0.f, 1.f));
		const sdl3::FRect box = ui::Place(screen, w + 20.f * s, lh * float(std::size(kHelp)) + 12.f * s, ui::Anchor::TopRight, 10.f * s, 10.f * s);
		c.FillRoundRect(box, 6.f * s, sdl3::Rgba(0, 0, 0, uint8_t(a / 2)));
		float y = box.y + 6.f * s;
		for (const char *line : kHelp) {
			c.Text(box.x + 10.f * s, y, line, sdl3::WithAlpha(th.text, a), fs);
			y += lh;
		}
	}

	if (!m_settings.showDebug)
		return;
	const voxel::WorldStats ws = m_world->Stats();
	const voxel::RenderStats &rs = m_renderer.Stats();
	const auto *t = m_ecs.Get<voxel::Transform>(m_player);
	const math::IVec3 chunk = voxel::ChunkOfPosition(t->position);
	const voxel::ChunkKey key = voxel::MakeChunkKey(chunk);
	std::vector<std::string> lines = {
		std::format("{:.0f} ips  ({:.2f} ms/image)  jeu {:.2f} ms", 1.f / std::max(dt, 1e-4f), dt * 1000.f, m_lastUpdateMs),
		std::format("pos {:.1f} {:.1f} {:.1f}  {}  état {}", t->position.x, t->position.y, t->position.z, m_flying ? "[vol]" : "[marche]",
					voxel::AvatarStateLabel(m_avatarState)),
		std::format("chunk {} {} {}  morton {:#x}  région {:#x}", chunk.x, chunk.y, chunk.z, key, m_world->Storage().Regions().RegionOf(key)),
		std::format("chunks : {} chargés ({} matérialisés, {:.1f} Mo)  en attente {}  maillages {}", ws.loadedChunks, ws.materializedChunks,
					double(ws.memoryBytes) / 1048576.0, ws.pendingLoads, ws.pendingMeshes),
		std::format("base : {} lus  {} générés  {} sauvés  {} déchargés", ws.chunksLoadedFromDisk, ws.chunksGenerated, ws.chunksSaved, ws.chunksUnloaded),
		std::format("entités : {}  transferts de grille {}  dans ce chunk {}", ws.entities, ws.gridMoves,
					m_world->GetChunk(key) ? m_world->GetChunk(key)->entities.size() : 0),
		std::format("rendu : {} chunks / {}  {} appels  {:.1f} M sommets  GPU {:.1f} Mo", rs.chunksDrawn, rs.chunksResident, rs.drawCalls,
					double(rs.verticesDrawn) / 1e6, double(rs.gpuBytes) / 1048576.0),
		std::format("scripts : {} hooks  {} interactions  {} systèmes  {} commandes  heure {:.2f}", m_scripts ? m_scripts->HookCount() : 0,
					m_scripts ? m_scripts->InteractionCount() : 0, m_scripts ? m_scripts->SystemCount() : 0, m_scripts ? m_scripts->CommandCount() : 0,
					m_timeOfDay),
	};
	for (const auto &st : m_schedule.Stats())
		lines.push_back(std::format("  système {:<20} {:.3f} ms", st.name, st.avgMs));
	const float fs = 13.f;
	sdl3::FRect panel = ui::Place(screen, 600.f, float(lines.size()) * 18.f + 10.f, ui::Anchor::TopLeft, 8.f, 6.f);
	c.FillRect(panel, sdl3::Rgba(0, 0, 0, 150));
	float ly = panel.y + 4.f;
	for (const auto &l : lines) {
		c.Text(panel.x + 6.f, ly, l, sdl3::Rgba(230, 236, 245), fs);
		ly += 18.f;
	}
}

// ════════════════════════════════════════════════════════════════════════════
// Inventaire : cases, info-bulles, survie (armure, fabrication, four), créatif
// ════════════════════════════════════════════════════════════════════════════

void Game::DrawTooltip(sdl3::Canvas &c, float x, float y, ItemId item, float s) {
	if (item == 0)
		return;
	const ui::Theme &th = m_ui.Style();
	std::vector<std::pair<std::string, sdl3::Color>> lines = {{m_content.ItemLabel(item), th.text}, {m_content.ItemName(item), th.textDim}};
	if (const ItemDef *d = m_content.Item(item)) {
		if (!d->armorSlot.empty())
			lines.push_back({std::format("Armure : +{}  (clic droit en main : enfiler)", d->armorPoints), th.info});
		if (!d->tags.empty()) {
			std::string tags;
			for (const auto &t : d->tags)
				tags += (tags.empty() ? "#" : "  #") + t;
			lines.push_back({tags, th.textDim});
		}
	} else if (IsBlockItem(item)) {
		const voxel::BlockDef &def = m_content.blocks.Get(voxel::BlockId(item));
		if (!def.interact.Empty())
			lines.push_back({def.interact.action == "sit"	  ? "Clic droit : s'asseoir"
							 : def.interact.action == "sleep" ? "Clic droit : dormir"
							 : !def.interact.call.empty()	  ? std::format("Clic droit : {}()", def.interact.call)
															  : "Clic droit : basculer",
							 th.info});
		if (def.light > 0)
			lines.push_back({std::format("Lumière {}", def.light), th.warning});
	}
	const float fs = 13.f * s, lh = c.LineHeight(fs) + 2.f * s;
	float w = 0.f;
	for (const auto &[t, col] : lines)
		w = std::max(w, c.MeasureText(t, fs));
	const sdl3::FRect screen{0, 0, c.Width(), c.Height()};
	const sdl3::FRect r = ui::KeepInside({x + 16.f * s, y + 12.f * s, w + 16.f * s, lh * float(lines.size()) + 10.f * s}, screen);
	c.FillRoundRect(r, 5.f * s, sdl3::Rgba(14, 10, 26, 245));
	c.StrokeRoundRect(r, 5.f * s, sdl3::Rgba(90, 60, 160), 1.5f);
	float ly = r.y + 5.f * s;
	for (const auto &[t, col] : lines) {
		c.Text(r.x + 8.f * s, ly, t, col, fs);
		ly += lh;
	}
}

void Game::DrawSlot(sdl3::Canvas &c, const sdl3::FRect &r, ItemStack &slot, std::string_view id, float s, bool creativeSource,
					std::optional<ArmorSlot> armor) {
	ui::Context &u = m_ui;
	const ui::Theme &th = u.Style();
	const auto in = u.Interact(u.MakeId(id), r);
	c.FillRoundRect(r, 4.f * s, in.hovered ? sdl3::Rgba(58, 64, 82, 240) : sdl3::Rgba(26, 29, 38, 240));
	c.StrokeRoundRect(r, 4.f * s, in.hovered ? th.accent : sdl3::Rgba(60, 66, 82), in.hovered ? 1.5f : 1.f);
	if (slot.Empty() && armor) { // silhouette de l'emplacement d'armure
		static constexpr const char *kArmorLabel[] = {"Tête", "Torse", "Jambes", "Pieds"};
		c.TextCentered(r, kArmorLabel[size_t(*armor)], sdl3::WithAlpha(th.textDim, 150), 10.f * s);
	}
	DrawItemIcon(c, r.Inset(5.f * s), slot.id);
	if (!creativeSource && slot.count > 1) {
		const std::string n = std::to_string(slot.count);
		c.Text(r.Right() - c.MeasureText(n, 12.f * s) - 3.f * s, r.Bottom() - c.LineHeight(12.f * s), n, th.text, 12.f * s, sdl3::CanvasFont::Bold);
	}
	if (in.hovered && !slot.Empty())
		m_hoverItem = slot.id;
	if (!in.leftClicked && !in.rightClicked)
		return;
	const bool shift = u.Shift();
	if (creativeSource) { // réserve infinie du mode créatif
		if (!m_cursor.Empty() && in.leftClicked) {
			m_cursor = {}; // reposer la pile dans la réserve : elle disparaît
			return;
		}
		if (shift) { // directement dans la case active de la barre
			m_slots[size_t(m_hotbarIndex) % kHotbarSlots] = {slot.id, m_content.MaxStack(slot.id)};
			return;
		}
		m_cursor = {slot.id, in.rightClicked ? 1 : m_content.MaxStack(slot.id)};
		return;
	}
	if (armor) { // n'accepte que la pièce de cet emplacement
		if (!m_cursor.Empty()) {
			const ItemDef *d = m_content.Item(m_cursor.id);
			if (!d || ParseArmorSlot(d->armorSlot) != armor)
				return;
			const ItemStack piece{m_cursor.id, 1};
			if (--m_cursor.count <= 0)
				m_cursor = {};
			if (!slot.Empty())
				Give(slot.id, slot.count);
			slot = piece;
			PlayGesture(voxel::AvatarGesture::EquipArmor);
			PlaySound("core:equip", m_camera.position, 0.8f);
		} else if (!slot.Empty()) {
			m_cursor = std::exchange(slot, ItemStack{});
		}
		return;
	}
	if (shift && in.leftClicked && !slot.Empty()) { // transfert rapide barre ↔ inventaire
		const bool inHotbar = &slot >= m_slots.data() && &slot < m_slots.data() + kHotbarSlots;
		const size_t from = inHotbar ? kHotbarSlots : 0, to = inHotbar ? m_slots.size() : kHotbarSlots;
		const int maxStack = m_content.MaxStack(slot.id);
		for (size_t i = from; i < to && slot.count > 0; ++i)
			if (m_slots[i].id == slot.id && m_slots[i].count < maxStack) {
				const int n = std::min(slot.count, maxStack - m_slots[i].count);
				m_slots[i].count += n;
				slot.count -= n;
			}
		for (size_t i = from; i < to && slot.count > 0; ++i)
			if (m_slots[i].Empty()) {
				m_slots[i] = slot;
				slot.count = 0;
			}
		if (slot.count <= 0)
			slot = {};
		return;
	}
	const int maxStack = m_content.MaxStack(m_cursor.Empty() ? slot.id : m_cursor.id);
	if (in.leftClicked) {
		if (m_cursor.Empty())
			m_cursor = std::exchange(slot, ItemStack{});
		else if (slot.Empty() || slot.id != m_cursor.id)
			std::swap(slot, m_cursor);
		else { // même objet : on complète la pile
			const int n = std::min(m_cursor.count, maxStack - slot.count);
			slot.count += n;
			m_cursor.count -= n;
			if (m_cursor.count <= 0)
				m_cursor = {};
		}
	} else { // clic droit : prendre la moitié / poser un seul objet
		if (m_cursor.Empty() && !slot.Empty()) {
			const int half = (slot.count + 1) / 2;
			m_cursor = {slot.id, half};
			slot.count -= half;
			if (slot.count <= 0)
				slot = {};
		} else if (!m_cursor.Empty() && (slot.Empty() || (slot.id == m_cursor.id && slot.count < maxStack))) {
			if (slot.Empty())
				slot = {m_cursor.id, 0};
			++slot.count;
			if (--m_cursor.count <= 0)
				m_cursor = {};
		}
	}
}

std::map<std::string, int> Game::RecipeNeeds(const Recipe &rec) const {
	std::map<std::string, int> needs;
	for (const std::string &line : rec.pattern)
		for (char ch : line)
			if (ch != ' ')
				if (auto it = rec.key.find(ch); it != rec.key.end())
					++needs[it->second];
	for (const std::string &ing : rec.ingredients)
		++needs[ing];
	return needs;
}

int Game::CountMatching(std::string_view ingredient) const {
	if (!ingredient.starts_with('#'))
		return CountOf(m_content.FindItem(ingredient));
	const std::string_view tag = ingredient.substr(1);
	int n = 0;
	for (const ItemStack &st : m_slots) {
		if (st.Empty())
			continue;
		const bool match = IsBlockItem(st.id) ? m_content.blocks.Get(voxel::BlockId(st.id)).HasTag(tag)
											  : (m_content.Item(st.id) && m_content.Item(st.id)->HasTag(tag));
		if (match)
			n += st.count;
	}
	return n;
}

bool Game::CanCraft(const Recipe &rec) const {
	for (const auto &[name, n] : RecipeNeeds(rec))
		if (CountMatching(name) < n)
			return false;
	return true;
}

void Game::Craft(const Recipe &rec) {
	if (!Creative()) {
		if (!CanCraft(rec))
			return;
		for (const auto &[name, n] : RecipeNeeds(rec)) {
			int left = n;
			if (!name.starts_with('#')) {
				Take(m_content.FindItem(name), left);
				continue;
			}
			const std::string_view tag = std::string_view(name).substr(1);
			for (ItemStack &st : m_slots) {
				if (left <= 0)
					break;
				if (st.Empty())
					continue;
				const bool match = IsBlockItem(st.id) ? m_content.blocks.Get(voxel::BlockId(st.id)).HasTag(tag)
													  : (m_content.Item(st.id) && m_content.Item(st.id)->HasTag(tag));
				if (!match)
					continue;
				const int k = std::min(left, st.count);
				st.count -= k;
				left -= k;
				if (st.count <= 0)
					st = {};
			}
		}
	}
	const ItemId result = m_content.FindItem(rec.result);
	const int rest = Give(result, rec.count);
	if (rest > 0 && m_cursor.Empty())
		m_cursor = {result, rest};
	PlaySound("core:ui_click", m_camera.position, 0.6f);
}

void Game::DrawPaperDoll(sdl3::Canvas &c, const sdl3::FRect &r) {
	// Aperçu de face de la skin (régions avant de la texture 64×64) avec l'armure portée.
	const auto *skin = m_ecs.Get<voxel::Skin>(m_player);
	const std::string key = skin ? skin->id : std::string();
	if (key != m_skinPreviewKey) {
		m_skinPreviewKey = key;
		m_skinPreview.reset();
		if (auto path = m_content.SkinTexturePath(key))
			if (auto img = sdl3::LoadImageRgba(*path)) {
				const sdl3::Surface &image = img.Value();
				if (image.GetWidth() == 64)
					if (auto t = sdl3::Texture::Create(c.Renderer(), image.GetWidth(), image.GetHeight(), sdl3::PixelFormat::RGBA32)) {
						m_skinPreview.emplace(std::move(t.Value()));
						m_skinPreview->SetScaleMode(sdl3::ScaleMode::NEAREST);
						m_skinPreview->SetBlendMode(sdl3::BlendMode::BLEND);
						m_skinPreview->Update(image.Pixels(), image.GetWidth() * 4);
					}
			}
	}
	const float px = std::floor(std::min(r.w / 16.f, r.h / 32.f)); // 16×32 texels de face
	const sdl3::FRect doll = ui::Place(r, 16.f * px, 32.f * px, ui::Anchor::Center);
	auto part = [&](float sx, float sy, float sw, float sh, float dx, float dy, std::optional<ArmorSlot> slot) {
		const sdl3::FRect dst{doll.x + dx * px, doll.y + dy * px, sw * px, sh * px};
		if (m_skinPreview)
			c.ImageRegion(*m_skinPreview, 64.f, float(m_skinPreview->GetHeight()), {sx, sy, sw, sh}, dst);
		else
			c.FillRect(dst, sdl3::Rgba(120, 130, 150));
		if (slot && !m_armor[size_t(*slot)].Empty())
			if (const ItemDef *d = m_content.Item(m_armor[size_t(*slot)].id))
				c.FillRect(dst.Inset(-px * 0.4f), sdl3::WithAlpha(d->color, 200));
	};
	const bool tall = !m_skinPreview || m_skinPreview->GetHeight() >= 64;
	part(8, 8, 8, 8, 4, 0, ArmorSlot::Head);   // tête
	part(20, 20, 8, 12, 4, 8, ArmorSlot::Chest); // torse
	part(44, 20, 4, 12, 0, 8, ArmorSlot::Chest); // bras droit (à gauche de l'image)
	part(tall ? 36.f : 44.f, tall ? 52.f : 20.f, 4, 12, 12, 8, ArmorSlot::Chest);
	part(4, 20, 4, 12, 4, 20, ArmorSlot::Legs);
	part(tall ? 20.f : 4.f, tall ? 52.f : 20.f, 4, 12, 8, 20, ArmorSlot::Legs);
	if (!m_armor[size_t(ArmorSlot::Feet)].Empty())
		if (const ItemDef *d = m_content.Item(m_armor[size_t(ArmorSlot::Feet)].id))
			c.FillRect({doll.x + 4.f * px - px * 0.4f, doll.y + 28.f * px, 8.f * px + px * 0.8f, 4.f * px + px * 0.4f}, sdl3::WithAlpha(d->color, 220));
}

void Game::DrawInventoryMenu(sdl3::Canvas &c) {
	EnsureIconAtlas(c);
	ui::Context &u = m_ui;
	const ui::Theme &th = u.Style();
	const float s = ui::FitScale(c.Width(), c.Height(), 1280.f, 720.f, 0.7f, 2.f);
	const sdl3::FRect screen{0, 0, c.Width(), c.Height()};
	c.FillRect(screen, sdl3::Rgba(0, 0, 0, 120));
	m_hoverItem = 0;

	const sdl3::FRect dlg = ui::Place(screen, std::min(c.Width() - 24.f, 900.f * s), std::min(c.Height() - 24.f, 600.f * s), ui::Anchor::Center);
	c.FillRoundRect(dlg, 10.f * s, sdl3::Rgba(17, 19, 27, 250));
	c.StrokeRoundRect(dlg, 10.f * s, sdl3::Rgba(72, 78, 98), 1.5f);
	sdl3::FRect body = dlg.Inset(14.f * s);
	sdl3::FRect head = ui::CutTop(body, 30.f * s, 10.f * s);
	const sdl3::FRect close = ui::CutRight(head, 30.f * s, 8.f * s);
	if (u.ButtonAt(close, "×##closeInv", true)) {
		ToggleInventory();
		return;
	}
	c.Text(head.x, head.y + (head.h - c.LineHeight(18.f * s)) * 0.5f, Creative() ? "Inventaire créatif" : "Inventaire", th.text, 18.f * s,
		   sdl3::CanvasFont::Bold);
	c.Text(head.x + c.MeasureText(Creative() ? "Inventaire créatif" : "Inventaire", 18.f * s, sdl3::CanvasFont::Bold) + 14.f * s,
		   head.y + (head.h - c.LineHeight(12.f * s)) * 0.5f,
		   Creative() ? "clic : prendre une pile · clic droit : un · Maj+clic : vers la barre" : "clic : prendre/poser · clic droit : moitié/un · Maj+clic : transfert",
		   th.textDim, 12.f * s);

	if (Creative())
		DrawCreativeInventory(c, body, s);
	else
		DrawSurvivalInventory(c, body, s);

	// Pile tenue par le curseur et info-bulle par-dessus tout.
	const float mx = u.MouseX(), my = u.MouseY();
	if (!m_cursor.Empty()) {
		const float sz = 36.f * s;
		DrawItemIcon(c, {mx - sz * 0.5f, my - sz * 0.5f, sz, sz}, m_cursor.id);
		if (m_cursor.count > 1 && !Creative())
			c.Text(mx + sz * 0.2f, my + sz * 0.1f, std::to_string(m_cursor.count), th.text, 12.f * s, sdl3::CanvasFont::Bold);
	} else if (m_hoverItem) {
		DrawTooltip(c, mx, my, m_hoverItem, s);
	}
}

void Game::DrawSurvivalInventory(sdl3::Canvas &c, const sdl3::FRect &area, float s) {
	ui::Context &u = m_ui;
	const ui::Theme &th = u.Style();
	const float slot = std::floor(std::min(40.f * s, (area.w * 0.62f - 8.f * 4.f * s) / 9.f)), gap = 4.f * s;
	sdl3::FRect body = area;
	// Bas : inventaire principal (3 × 9) puis barre d'outils (1 × 9), séparés.
	const float gridW = 9.f * slot + 8.f * gap;
	sdl3::FRect bottom = ui::CutBottom(body, 4.f * slot + 4.f * gap + 14.f * s + 18.f * s, 12.f * s);
	const sdl3::FRect invLabel = ui::CutTop(bottom, 18.f * s);
	c.Text(invLabel.x, invLabel.y, "Sac", th.textDim, 13.f * s);
	const sdl3::FRect gridArea = ui::Place(bottom, gridW, bottom.h, ui::Anchor::Left);
	const ui::GridLayout main = ui::Grid({gridArea.x, gridArea.y, gridW, 3.f * slot + 2.f * gap}, 27, slot, gap, 9, ui::Align::Start);
	for (int i = 0; i < 27; ++i)
		DrawSlot(c, main.Cell(i), m_slots[kHotbarSlots + size_t(i)], std::format("inv{}", i), s);
	const ui::GridLayout hot = ui::Grid({gridArea.x, main.bounds.Bottom() + 10.f * s, gridW, slot}, 9, slot, gap, 9, ui::Align::Start);
	for (int i = 0; i < 9; ++i) {
		DrawSlot(c, hot.Cell(i), m_slots[size_t(i)], std::format("hot{}", i), s);
		if (i == m_hotbarIndex)
			c.StrokeRoundRect(hot.Cell(i).Inset(-2.f * s), 5.f * s, th.accent, 2.f);
	}
	// Four (à droite de la barre, sous la fabrication).
	{
		sdl3::FRect furnace{gridArea.Right() + 18.f * s, bottom.y, bottom.Right() - gridArea.Right() - 18.f * s, bottom.h};
		c.Text(furnace.x, furnace.y - 18.f * s, "Four", th.textDim, 13.f * s);
		static constexpr std::pair<const char *, const char *> kSmelting[] = {
			{"core:sand", "core:glass"}, {"core:cobble", "core:stone"}, {"core:iron_ore", "core:iron_block"}, {"core:gold_ore", "core:gold_block"},
			{"core:oak_log", "core:coal_block"}};
		u.BeginScroll("##furnace", furnace);
		for (const auto &[inName, outName] : kSmelting) {
			const ItemId src = m_content.FindItem(inName), dst = m_content.FindItem(outName);
			if (!src || !dst)
				continue;
			const sdl3::FRect row = u.Next(slot * 0.8f);
			const bool have = CountOf(src) > 0;
			const auto in = u.Interact(u.MakeId(std::format("smelt{}", outName)), row);
			c.FillRoundRect(row, 4.f * s, in.hovered && have ? th.widgetHover : th.widget);
			DrawItemIcon(c, {row.x + 4.f * s, row.y + 2.f * s, row.h - 4.f * s, row.h - 4.f * s}, src, have ? 255 : 110);
			c.Text(row.x + row.h + 6.f * s, row.y + (row.h - c.LineHeight(13.f * s)) * 0.5f, "→", th.textDim, 13.f * s);
			DrawItemIcon(c, {row.x + row.h + 24.f * s, row.y + 2.f * s, row.h - 4.f * s, row.h - 4.f * s}, dst, have ? 255 : 110);
			c.Text(row.x + row.h * 2.f + 30.f * s, row.y + (row.h - c.LineHeight(12.f * s)) * 0.5f,
				   c.Ellipsize(std::format("{} ({})", m_content.ItemLabel(dst), CountOf(src)), row.w - row.h * 2.f - 34.f * s, 12.f * s),
				   have ? th.text : th.textDim, 12.f * s);
			if (in.leftClicked && have) {
				Take(src, 1);
				if (Give(dst, 1) > 0)
					Give(src, 1);
			}
		}
		u.EndScroll();
	}

	// Haut gauche : personnage, armure, main gauche.
	sdl3::FRect left = ui::CutLeft(body, std::min(260.f * s, body.w * 0.34f), 16.f * s);
	c.FillRoundRect(left, 8.f * s, sdl3::Rgba(11, 13, 19, 230));
	sdl3::FRect leftIn = left.Inset(10.f * s);
	sdl3::FRect armorCol = ui::CutLeft(leftIn, slot, 10.f * s);
	for (int i = 0; i < 4; ++i) {
		const sdl3::FRect r{armorCol.x, armorCol.y + float(i) * (slot + gap), slot, slot};
		DrawSlot(c, r, m_armor[size_t(i)], std::format("armor{}", i), s, false, ArmorSlot(i));
	}
	const sdl3::FRect off{armorCol.x, armorCol.y + 4.f * (slot + gap) + 10.f * s, slot, slot};
	DrawSlot(c, off, m_offhand, "offhand", s);
	c.Text(off.Right() + 6.f * s, off.y + (slot - c.LineHeight(11.f * s)) * 0.5f, "Main gauche (X)", th.textDim, 11.f * s);
	sdl3::FRect dollArea = leftIn;
	const sdl3::FRect stats = ui::CutBottom(dollArea, 20.f * s, 4.f * s);
	DrawPaperDoll(c, dollArea);
	const auto *h = m_ecs.Get<voxel::Health>(m_player);
	c.TextCentered(stats, std::format("Vie {:.0f}/{:.0f}   Armure {}   Faim {:.0f}", h ? h->current : 0.f, h ? h->max : 20.f, ArmorPoints(), m_hunger),
				   th.textDim, 12.f * s);

	// Haut droite : fabrication (recettes réalisables d'abord).
	c.Text(body.x, body.y, "Fabrication", th.textDim, 13.f * s);
	const sdl3::FRect list{body.x, body.y + 20.f * s, body.w, body.h - 20.f * s};
	std::vector<const Recipe *> recipes;
	for (const Recipe &rec : m_content.recipes)
		if (m_content.FindItem(rec.result))
			recipes.push_back(&rec);
	std::ranges::stable_partition(recipes, [&](const Recipe *r) { return CanCraft(*r); });
	u.BeginScroll("##craft", list);
	int shown = 0;
	for (const Recipe *rec : recipes) {
		if (++shown > 120)
			break;
		const bool ok = CanCraft(*rec);
		const ItemId result = m_content.FindItem(rec->result);
		const sdl3::FRect row = u.Next(34.f * s);
		const auto in = u.Interact(u.MakeId("craft" + rec->id), row);
		c.FillRoundRect(row, 4.f * s, in.hovered && ok ? th.widgetHover : (ok ? th.widget : sdl3::Rgba(24, 27, 35, 200)));
		DrawItemIcon(c, {row.x + 4.f * s, row.y + 3.f * s, 28.f * s, 28.f * s}, result, ok ? 255 : 120);
		const std::string label = std::format("{}×{}", m_content.ItemLabel(result), rec->count);
		c.Text(row.x + 40.f * s, row.y + 3.f * s, c.Ellipsize(label, row.w * 0.45f, 13.f * s), ok ? th.text : th.textDim, 13.f * s);
		// Ingrédients (icônes + quantité possédée / requise).
		float ix = row.x + 40.f * s;
		for (const auto &[name, n] : RecipeNeeds(*rec)) {
			const ItemId ing = name.starts_with('#') ? 0 : m_content.FindItem(name);
			const int have = CountMatching(name);
			const sdl3::Color col = have >= n ? th.success : th.danger;
			if (ing)
				DrawItemIcon(c, {ix, row.y + 18.f * s, 13.f * s, 13.f * s}, ing);
			else
				c.Text(ix, row.y + 18.f * s, name, th.textDim, 10.f * s);
			ix += (ing ? 15.f * s : c.MeasureText(name, 10.f * s) + 3.f * s);
			const std::string cnt = std::format("{}/{}", have, n);
			c.Text(ix, row.y + 18.f * s, cnt, col, 10.f * s);
			ix += c.MeasureText(cnt, 10.f * s) + 8.f * s;
		}
		if (in.hovered)
			m_hoverItem = result;
		if (in.leftClicked && ok)
			Craft(*rec);
	}
	if (recipes.empty())
		u.Label("Aucune recette chargée.", th.textDim);
	u.EndScroll();
}

void Game::DrawCreativeInventory(sdl3::Canvas &c, const sdl3::FRect &area, float s) {
	ui::Context &u = m_ui;
	const ui::Theme &th = u.Style();
	sdl3::FRect body = area;
	// Onglets de catégories + recherche.
	sdl3::FRect tabsRow = ui::CutTop(body, 28.f * s, 8.f * s);
	const sdl3::FRect search = ui::CutRight(tabsRow, std::min(260.f * s, tabsRow.w * 0.32f), 10.f * s);
	std::vector<ui::FlexItem> tabItems;
	for (const char *t : kCreativeTabs)
		tabItems.push_back({c.MeasureText(t, 13.f * s) + 18.f * s, 1.f, 34.f * s, 160.f * s}); // rétrécit si l'écran est étroit
	const auto tabs = ui::Row(tabsRow, tabItems, 4.f * s);
	for (size_t i = 0; i < tabs.size(); ++i)
		if (u.ButtonAt(tabs[i], kCreativeTabs[i], true, m_creativeTab == int(i)))
			m_creativeTab = int(i);
	u.TextInputAt(search, "##creativeSearch", m_creativeSearch, nullptr, "Rechercher un bloc ou un objet…");

	// Bas : barre d'outils (cible) et corbeille.
	const float slot = std::floor(std::min(40.f * s, (body.w - 60.f * s) / 10.f)), gap = 4.f * s;
	sdl3::FRect bottom = ui::CutBottom(body, slot + 22.f * s, 12.f * s);
	c.Text(bottom.x, bottom.y, "Barre d'outils", th.textDim, 12.f * s);
	const ui::GridLayout hot = ui::Grid({bottom.x, bottom.y + 18.f * s, bottom.w, slot}, 9, slot, gap, 9, ui::Align::Start);
	for (int i = 0; i < 9; ++i) {
		DrawSlot(c, hot.Cell(i), m_slots[size_t(i)], std::format("chot{}", i), s);
		if (i == m_hotbarIndex)
			c.StrokeRoundRect(hot.Cell(i).Inset(-2.f * s), 5.f * s, th.accent, 2.f);
	}
	{
		const sdl3::FRect trash{hot.bounds.Right() + 18.f * s, hot.bounds.y, slot, slot};
		const auto in = u.Interact(u.MakeId("trash"), trash);
		c.FillRoundRect(trash, 4.f * s, in.hovered ? sdl3::WithAlpha(th.danger, 160) : sdl3::Rgba(40, 24, 28, 230));
		c.TextCentered(trash, "Suppr.", th.text, 10.f * s);
		if (in.leftClicked)
			m_cursor = {};
	}

	// Grille filtrée (défilement).
	const std::string needle = Lower(m_creativeSearch);
	std::vector<ItemId> list;
	auto keep = [&](ItemId id) {
		if (m_creativeTab != 0 && CategoryOf(m_content, id) != m_creativeTab)
			return;
		if (!needle.empty()) {
			const std::string hay = Lower(m_content.ItemLabel(id)) + " " + Lower(m_content.ItemName(id));
			if (hay.find(needle) == std::string::npos)
				return;
		}
		list.push_back(id);
	};
	for (size_t id = 1; id < m_content.blocks.Count(); ++id) {
		const voxel::BlockDef &def = m_content.blocks.Get(voxel::BlockId(id));
		if (def.IsVisible() && def.item && !def.isPart)
			keep(ItemId(id));
	}
	for (size_t i = 0; i < m_content.items.size(); ++i)
		keep(ItemId(kItemBase + i));
	c.Text(body.x, body.y, std::format("{} résultat{}", list.size(), list.size() > 1 ? "s" : ""), th.textDim, 12.f * s);
	const sdl3::FRect gridRect{body.x, body.y + 18.f * s, body.w, body.h - 18.f * s};
	u.BeginScroll("##creativeGrid", gridRect);
	const int cols = std::max(1, int((gridRect.w - 14.f * s + gap) / (slot + gap)));
	for (size_t start = 0; start < list.size(); start += size_t(cols)) {
		const sdl3::FRect row = u.Next(slot);
		for (int k = 0; k < cols && start + size_t(k) < list.size(); ++k) {
			ItemStack src{list[start + size_t(k)], 1};
			DrawSlot(c, {row.x + float(k) * (slot + gap), row.y, slot, slot}, src, std::format("src{}", src.id), s, true);
		}
	}
	if (list.empty())
		u.Label("Aucun résultat.", th.textDim);
	u.EndScroll();
}

} // namespace voxelia
