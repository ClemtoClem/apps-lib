#pragma once
/**
 * @file image_ops.hpp
 * @brief Moteur de composition et de manipulation d'images RGBA32 pour les scripts JSON.
 *
 * Ce module permet de charger, modifier et superposer des textures à la volée via une 
 * syntaxe de composition sous forme de chaîne de caractères.
 *
 * =======================================================================================
 * GRAMMAIRE DE COMPOSITION
 * =======================================================================================
 * La syntaxe de base utilise le circonflexe '^' pour chaîner les opérations :
 *     "image_base.png^op1(args)^op2(args)"
 *
 * On peut superposer des images (overlay) simplement en les chaînant :
 *     "fond.png^calque.png" (calque.png sera dessiné par-dessus fond.png)
 *
 * Les parenthèses permettent d'isoler et de composer des sous-expressions :
 *     "fond.png^(calque.png^tint(255,0,0))^opacity(0.8)"
 *
 * =======================================================================================
 * LISTE DES OPÉRATIONS DISPONIBLES
 * =======================================================================================
 * 
 * --- Transformations Spatiales & Géométriques ---
 * - resize(w, h)           : Redimensionne l'image aux dimensions exactes w x h.
 * - crop(x, y, w, h)       : Rogne une région de l'image.
 * - flip(h, v)             : Miroir horizontal (h>0) et/ou vertical (v>0). Ex: flip(1,0).
 * - rotate(angle)          : Rotation de l'image par pas de 90 degrés (90, 180, 270).
 * - shift(dx, dy)          : Décale les pixels de l'image de dx et dy (pratique pour des ombres).
 * 
 * --- Ajustements Colorimétriques ---
 * - tint(r, g, b)          : Multiplie les couleurs de l'image par la teinte RGB donnée (0-255).
 * - colorize(r, g, b, p)   : Mélange la couleur cible avec l'image selon un ratio p (0.0 à 1.0).
 * - grayscale()            : Convertit l'image en niveaux de gris.
 * - darken(f) / lighting(f): Multiplie la luminosité par le facteur f (ex: 0.5 assombrit, 1.5 éclaircit).
 * - opacity(f)             : Multiplie le canal alpha par le facteur f (0.0 à 1.0).
 * - invert()               : Inverse les couleurs RGB (effet négatif).
 * - contrast(f)            : Ajuste le contraste (1.0 = normal, <1.0 = diminué, >1.0 = augmenté).
 * - saturate(f)            : Ajuste la saturation (0.0 = gris, 1.0 = normal, >1.0 = sur-saturé).
 * - sepia()                : Applique un filtre sépia vintage.
 * - threshold(level)       : Binarise l'image en noir/blanc selon un seuil de luminance (0-255).
 * - chromakey(r, g, b, tol): Rend transparente une couleur cible selon une tolérance euclidienne.
 * 
 * --- Filtres & Effets ---
 * - blur(radius)           : Applique un flou de boîte avec le rayon donné.
 * - pixelate(size)         : Réduit la résolution perçue en créant des blocs de taille `size`.
 * - outline(r, g, b, size) : Dessine un contour de l'épaisseur donnée autour des pixels opaques.
 * - noise(amount)          : Ajoute du bruit texturé aléatoire (amount de 0.0 à 1.0).
 * 
 * --- Animation & Découpage ---
 * - frame(index, count)    : Extrait la trame `index` (0-basé) d'une bande verticale contenant `count` trames.
 * =======================================================================================
 */

#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>

#include "sdl3/render/render.hpp"

namespace sdl3 {

/// Résout un terme de composition en image pleine résolution (ex. « core:sand »).
/// Retourne une erreur si le terme n'est pas reconnu (le composeur retombe alors sur
/// un chargement de fichier relatif à `baseDir`).
using SurfaceResolver = std::function<Result<Surface, StringView>(std::string_view)>;

/// Charge une image et la convertit en RGBA32.
[[nodiscard]] Result<Surface, StringView> LoadImageRgba(const std::filesystem::path &path);
/// Première vignette carrée (bandes d'animation verticales), réduite/agrandie à `size` (filtre boîte).
[[nodiscard]] Result<Surface, StringView> SquareThumbnail(const Surface &src, int size);
/// Teinte multiplicative (canal alpha inchangé).
void Tint(Surface &img, uint8_t r, uint8_t g, uint8_t b);
/// Superposition « over » de `top` (même taille), alpha pris en compte.
void Overlay(Surface &dst, const Surface &top);
void ScaleAlpha(Surface &img, float factor);
void ScaleBrightness(Surface &img, float factor);
/// Teinte par mélange : `pixel = pixel·(1-part) + cible·part` (canal alpha inchangé).
void Colorize(Surface &img, uint8_t r, uint8_t g, uint8_t b, float part);
/// Désaturation (luminance Rec. 709), alpha inchangé.
void Grayscale(Surface &img);

/// Transformations géométriques
[[nodiscard]] Result<Surface, StringView> FlipImage(const Surface &src, bool horizontal, bool vertical);
[[nodiscard]] Result<Surface, StringView> Rotate90(const Surface &src, int angle);

/// Ajustements colorimétriques (in-place)
void Invert(Surface &img);
void AdjustContrast(Surface &img, float factor);
void AdjustSaturation(Surface &img, float factor);
void Sepia(Surface &img);

/// Filtres et effets
[[nodiscard]] Result<Surface, StringView> Blur(const Surface &src, int radius);
void Pixelate(Surface &img, int blockSize);
[[nodiscard]] Result<Surface, StringView> Outline(const Surface &src, uint8_t r, uint8_t g, uint8_t b, int thickness);

void AddNoise(Surface &img, float amount);
void Threshold(Surface &img, uint8_t level);
void ChromaKey(Surface &img, uint8_t r, uint8_t g, uint8_t b, float tolerance);

/// Transformations spatiales
[[nodiscard]] Result<Surface, StringView> Shift(const Surface &src, int dx, int dy);

/// Évalue une expression de composition relative à `baseDir` et la réduit à `size`.
/// Grammaire : terme ( '^' (terme | op) )* ; op ∈ {tint, colorize, grayscale, darken,
/// opacity, lighting, frame, resize, crop}(nombres…). `resolve` (optionnel) résout les
/// noms qualifiés (« core:sand ») dans les compositions de blocs.
[[nodiscard]] Result<Surface, StringView> ComposeSurface(std::string_view expr, const std::filesystem::path &baseDir, int size,
														 const SurfaceResolver &resolve = {});

} // namespace sdl3
