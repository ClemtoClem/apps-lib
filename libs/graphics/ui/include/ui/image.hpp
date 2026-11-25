#pragma once
/**
 * @file image.hpp
 * @brief `ui::Image` — image importée depuis un fichier (SVG, PNG, JPG, BMP, …),
 *        décodée une fois en `sdl3::Surface` (pixels RGBA) et dessinée par les
 *        widgets via le `ui::Ui` (qui crée la texture GPU à la demande).
 *
 * Utilisable comme icône de bouton (`.Icon(img)`), comme contenu d'un
 * `ImageView`, ou comme curseur de souris personnalisé (`.Cursor(img)`).
 */
#include <memory>

#include <sdl3/render/render.hpp>
#include <sdl3/render/svg.hpp>

namespace ui {

class Image {
public:
	Image() = default;

	/// Charge n'importe quel format d'image ; les `.svg` sont analysés via
	/// `dataio` (XML) puis rastérisés par SDL3_image.
	[[nodiscard]] static Result<Image, String> Load(const String &path);

	[[nodiscard]] explicit operator bool() const noexcept { return m_surface != nullptr; }
	[[nodiscard]] int Width() const noexcept { return m_surface ? m_surface->GetWidth() : 0; }
	[[nodiscard]] int Height() const noexcept { return m_surface ? m_surface->GetHeight() : 0; }
	/// Surface décodée (non propriétaire) — pour le rendu et les curseurs.
	[[nodiscard]] sdl3::Surface *Surface() const noexcept { return m_surface.get(); }

private:
	explicit Image(std::shared_ptr<sdl3::Surface> s) : m_surface(std::move(s)) {}
	std::shared_ptr<sdl3::Surface> m_surface;
};

} // namespace ui
