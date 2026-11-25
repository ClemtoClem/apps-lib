#pragma once
/**
 * @file app.hpp
 * @brief Application fenêtrée « retro » : fenêtre sans décoration du système (barre de
 *        titre dessinée par `ui::WindowFrame`), deux vues :
 *
 *  - **Jeu** : bibliothèque de ROM (recherche, favoris, récents), écran de la machine avec
 *    ses commandes (lecture, pause, vitesse, volume, plein écran) et états sauvegardés
 *    (vignette, date, renommer, supprimer) ;
 *  - **Débogage** : registres et drapeaux du processeur, désassemblage avec points d'arrêt,
 *    tuiles de la mémoire vidéo, vidage de la mémoire, console, performances.
 *
 * Clavier : flèches (croix), X (A), W ou Z (B), Entrée (Start), Maj droite (Select), A/S (L/R),
 * Q/D (X/Y de la Nintendo DS) ; souris : stylet sur l'écran tactile.
 * F5 : nouvel état, F9 : chargement rapide, F2 : capture, F12 : débogage, P : pause,
 * F11 : pas à pas, F10 : pas principal, Tab (maintenu) : avance rapide.
 */
#include <deque>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <sdl3/render/canvas.hpp>
#include <sdl3/input/audio.hpp>
#include <sdl3/core/events_manager.hpp>
#include <sdl3/render/render.hpp>
#include <sdl3/core/sdl_context.hpp>
#include <ui/ui.hpp>
#include <ui/window_frame.hpp>
#include <ui/retained.hpp>

#include "session.hpp"

namespace retro {

struct AppOptions {
	SessionOptions session;				  ///< ROM lancée au démarrage (chemin vide : aucune)
	std::vector<std::filesystem::path> libraries; ///< dossiers de ROM de la bibliothèque
	std::filesystem::path dataDirectory;   ///< données (~/.local/share/apps-lib/retro, posé par main)
	bool mute = false;
	bool debugView = false;				  ///< ouvrir sur la vue de débogage
	bool paused = false;
	int frames = -1;					  ///< images d'interface avant de quitter (tests)
	std::filesystem::path screenshot;	  ///< capture de la fenêtre écrite à la fermeture
};

/// ROM de la bibliothèque.
struct RomEntry {
	std::filesystem::path path;
	std::string title;	///< nom du fichier sans extension
	std::string system; ///< description du système
	bool favorite = false;
	int recent = -1; ///< rang dans les ROM jouées récemment (−1 : jamais)
	std::filesystem::path thumbnail; ///< vignette d'un état enregistré (vide : aucune)
};

/// État sauvegardé de la ROM courante.
struct StateEntry {
	std::filesystem::path file;
	int slot = 0;
	std::string label; ///< nom donné par l'utilisateur (vide : aucun)
	std::string date;
	std::filesystem::file_time_type time;
};

/// Mappage d'un bouton de console à une touche physique (scancode).
struct KeyBinding {
	SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
	std::string button; ///< nom du bouton émulé ("up", "down", "a"…)
};

class App {
public:
	explicit App(AppOptions options);
	~App();
	App(const App &) = delete;
	App &operator=(const App &) = delete;

	/// Boucle principale ; retourne le code de sortie du programme.
	[[nodiscard]] int Run();

private:
	enum class View : uint8_t { Game, Debug };
	/// Disposition des deux écrans de la Nintendo DS (image émulée : écrans empilés).
	enum class DualLayout : uint8_t { Stacked, SideBySide };
	enum class LogLevel : uint8_t { Info, Warn, Error };

	// ── Logique ─────────────────────────────────────────────────────────
	bool InitWindow();
	/// Lance l'analyse des dossiers de ROM en arrière-plan.
	void ScanLibrary();
	/// Reprend le résultat de l'analyse quand il est prêt (à chaque image).
	void PollLibrary();
	/// Marques (favoris, récents), vignettes et ordre de la bibliothèque.
	void SortLibrary();
	[[nodiscard]] static std::vector<RomEntry> FindRoms(const emu::SystemRegistry &registry, const std::vector<std::filesystem::path> &directories);
	/// Recherche la vignette (capture d'un état) de chaque ROM ; après un enregistrement d'état.
	void RefreshThumbnails();
	void LoadLibraryMarks();
	void SaveLibraryMarks() const;
	bool LoadRom(const std::filesystem::path &path);
	void RefreshStates();
	void SaveNewState();
	void QuickLoad();
	void LoadStateFile(const StateEntry &state);
	void DeleteState(size_t index);
	void Log(LogLevel level, std::string text);
	void HandleEvents();
	void Emulate(float dt);
	void StepInto();
	void StepOver();
	void SetPaused(bool paused);
	void ToggleFullscreen();
	/// Ouvre le modal de configuration des touches.
	void OpenKeyConfig();
	/// (Re)construit l'arborescence du modal de configuration des touches.
	void BuildKeyConfigModal();
	/// Affiche le modal (overlay) au premier plan, dans la zone donnée.
	void DrawKeyConfigModal(const sdl3::FRect &area);
	/// Vrai si la machine a deux écrans empilés dans une seule image (Nintendo DS).
	[[nodiscard]] bool IsDualScreen() const noexcept;
	/// Bascule entre écrans l'un au-dessus de l'autre et côte à côte (mémorisé).
	void ToggleDualLayout();
	[[nodiscard]] sdl3::Texture *Thumbnail(const std::filesystem::path &png);
	[[nodiscard]] bool SaveWindowScreenshot(const std::filesystem::path &path);

	// ── Dessin ──────────────────────────────────────────────────────────
	void Draw(float dt);
	void DrawGameView(const sdl3::FRect &area);
	void DrawLibrary(const sdl3::FRect &area);
	void DrawPlayer(const sdl3::FRect &area);
	void DrawStates(const sdl3::FRect &area);
	void DrawDebugView(const sdl3::FRect &area);
	void DrawCpuPanel(const sdl3::FRect &area);
	void DrawDisassembly(const sdl3::FRect &area);
	void DrawVideoPanel(const sdl3::FRect &area);
	void DrawMemoryPanel(const sdl3::FRect &area);
	void DrawConsole(const sdl3::FRect &area);
	void DrawPerformance(const sdl3::FRect &area);
	void DrawStatusBar(const sdl3::FRect &area);
	/// Cadre d'un panneau titré ; retourne la zone de contenu.
	[[nodiscard]] sdl3::FRect Panel(const sdl3::FRect &area, std::string_view title);
	/// Écran de la machine ajusté (agrandissement entier si possible) dans `area`.
	/// `interactive` : la souris sert de stylet sur l'écran tactile de la machine.
	void DrawScreen(const sdl3::FRect &area, bool interactive = false);
	/// Taille de l'image affichée (en pixels de la machine), selon la disposition des écrans.
	[[nodiscard]] std::pair<float, float> ScreenViewSize() const noexcept;
	void UpdateTileTexture();

	AppOptions m_options;
	emu::SystemRegistry m_registry;
	std::optional<sdl3::SdlContext> m_sdl;
	std::optional<sdl3::Window> m_window;
	std::optional<sdl3::Renderer> m_renderer;
	sdl3::Canvas m_canvas;
	std::unique_ptr<ui::Context> m_ui;
	std::unique_ptr<ui::Ui> m_retained; ///< interface retenue (builder + CSS + affichage auto)
	std::unique_ptr<ui::Ui> m_controls; ///< commandes du lecteur (curseur de volume, vitesses)
	std::unique_ptr<ui::Ui> m_modal;	///< modal de configuration des touches
	std::unique_ptr<ui::WindowChrome> m_chrome;
	sdl3::EventsManager m_events;
	float m_scale = 1.f;
	std::optional<sdl3::AudioStream> m_audio;
	std::optional<sdl3::Gamepad> m_gamepad; // rendu à SDL dans le destructeur
	std::string m_gamepadName;

	std::unique_ptr<Session> m_session;
	std::optional<sdl3::Texture> m_screenTexture;
	int m_screenW = 0, m_screenH = 0;
	std::vector<int16_t> m_samples;

	View m_view = View::Game;
	DualLayout m_dualLayout = DualLayout::Stacked;
	bool m_running = true, m_paused = false, m_fast = false, m_fullscreen = false;
	float m_speed = 1.f, m_volume = 0.8f;
	double m_frameDebt = 0;
	int m_uiFrames = 0;

	// Bibliothèque.
	std::vector<RomEntry> m_library;
	std::future<std::vector<RomEntry>> m_scan; ///< analyse en cours
	std::string m_search;
	std::set<std::string> m_favorites;
	std::vector<std::string> m_recent; ///< du plus récent au plus ancien

	// États sauvegardés.
	std::vector<StateEntry> m_states;
	int m_renaming = -1;
	std::string m_renameText;
	std::map<std::string, std::pair<std::filesystem::file_time_type, std::optional<sdl3::Texture>>> m_thumbnails;

	// Débogage.
	std::set<uint32_t> m_breakpoints;
	uint32_t m_disasmAnchor = 0;
	uint32_t m_memoryAddress = 0xC000;
	std::string m_memoryAddressText = "C000";
	std::vector<uint8_t> m_memoryPrevious;
	std::deque<std::pair<LogLevel, std::string>> m_log;
	std::optional<sdl3::Texture> m_tileTexture;
	int m_tileBank = 0;
	uint64_t m_unmappedReads = 0;

	// Configuration des touches.
	std::vector<KeyBinding> m_keys;   ///< mappage bouton → scancode (modifiable)
	bool m_keysOpen = false;		  ///< modal de configuration ouvert
	bool m_modalDirty = false;		  ///< arborescence du modal à reconstruire
	std::string m_rebinding;		  ///< bouton en cours de réassignation (vide = aucun)

	// Performances.
	std::vector<float> m_frameMs, m_emulationMs;
	float m_fps = 0, m_fpsTimer = 0;
	int m_fpsFrames = 0;
	float m_lastEmulationMs = 0;
};

} // namespace retro
