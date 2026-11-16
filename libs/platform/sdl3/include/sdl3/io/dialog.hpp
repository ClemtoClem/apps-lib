#pragma once
/**
 * @file dialog.hpp
 * @brief Boîtes de dialogue natives de sélection de fichiers / dossiers (asynchrones, SDL_dialog.h).
 */
#include <SDL3/SDL.h>
#include <functional>
#include <vector>

#include <core/core.hpp>
#include "sdl3/core/error.hpp"
#include "sdl3/render/render.hpp" // Window
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// Dialog — sélection native de fichiers / dossiers (asynchrone)
// ============================================================================

/// Filtre de fichiers d'une boîte de dialogue.
struct DialogFilter {
	String name;	///< libellé affiché (ex: "Images")
	String pattern; ///< motifs séparés par ';' (ex: "png;jpg;jpeg") ou "*" pour tout accepter
};

/// Résultat transmis au callback : liste de chemins choisis, ou vide (annulation), ou erreur.
struct DialogResult {
	std::vector<String> files;	 ///< chemins sélectionnés (vide si annulation)
	Option<String> error = NONE; ///< message d'erreur éventuel

	/// Vrai si l'utilisateur a annulé.
	[[nodiscard]] bool Cancelled() const noexcept { return error.IsNone() && files.empty(); }
	/// Vrai si une sélection a été faite.
	[[nodiscard]] bool Ok() const noexcept { return error.IsNone() && !files.empty(); }
	/// Premier chemin sélectionné (non défini si vide).
	[[nodiscard]] const String &First() const noexcept { return files.front(); }
};

namespace dialog {

/// Callback invoqué à la fin de la boîte de dialogue : résultat + index du filtre choisi.
using Callback = std::function<void(const DialogResult &, int filterIndex)>;

namespace detail {

/// Contexte utilisateur transmis au trampoline SDL.
struct DialogCtx {
	Callback fn;
};

/// Trampoline C → C++.
void SDLCALL Trampoline(void *userdata, const char *const *filelist, int filter);

/// Convertit les filtres en structure SDL.
[[nodiscard]] std::vector<SDL_DialogFileFilter> ToSdlFilters(const std::vector<DialogFilter> &filters);

void ShowOpenFileImpl(Callback cb, SDL_Window *window, const std::vector<DialogFilter> &filters,
					  const String &defaultLocation, bool allowMany);

void ShowSaveFileImpl(Callback cb, SDL_Window *window, const std::vector<DialogFilter> &filters,
					  const String &defaultLocation);

void ShowOpenFolderImpl(Callback cb, SDL_Window *window, const String &defaultLocation, bool allowMany);

} // namespace detail

// --- Sans fenêtre parente ---

/// Ouvre une boîte « ouvrir des fichiers » (sans fenêtre parente).
void ShowOpenFile(Callback cb, const std::vector<DialogFilter> &filters = {}, const String &defaultLocation = "",
				  bool allowMany = false);

/// Ouvre une boîte « enregistrer un fichier » (sans fenêtre parente).
void ShowSaveFile(Callback cb, const std::vector<DialogFilter> &filters = {},
				  const String &defaultLocation = "");

/// Ouvre une boîte « ouvrir un dossier » (sans fenêtre parente).
void ShowOpenFolder(Callback cb, const String &defaultLocation = "", bool allowMany = false);

// --- Modal pour une fenêtre donnée ---

/// Ouvre une boîte « ouvrir des fichiers » modale pour `window`.
void ShowOpenFile(Callback cb, Window &window, const std::vector<DialogFilter> &filters = {},
				  const String &defaultLocation = "", bool allowMany = false);

/// Ouvre une boîte « enregistrer un fichier » modale pour `window`.
void ShowSaveFile(Callback cb, Window &window, const std::vector<DialogFilter> &filters = {},
				  const String &defaultLocation = "");

/// Ouvre une boîte « ouvrir un dossier » modale pour `window`.
void ShowOpenFolder(Callback cb, Window &window, const String &defaultLocation = "", bool allowMany = false);

} // namespace dialog
} // namespace sdl3
