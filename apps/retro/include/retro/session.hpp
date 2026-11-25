#pragma once
/**
 * @file session.hpp
 * @brief Session d'émulation : une ROM chargée dans la machine de son système, les
 *        captures d'écran et les états sauvegardés sur disque. Sans fenêtre ni son.
 */
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <core/result.hpp>
#include <emu/emu.hpp>

namespace retro {

struct SessionOptions {
	std::filesystem::path rom;
	std::string system;					 ///< vide : déduit de l'extension de la ROM
	std::string model;					 ///< option « model » du pilote (vide : auto)
	std::filesystem::path saveDirectory; ///< sauvegardes sur pile (vide : à côté de la ROM)
	std::filesystem::path stateDirectory; ///< états (~/.local/share/apps-lib/retro/states, posé par main)
	std::filesystem::path biosDirectory;
};

class Session {
public:
	/// Charge la ROM et démarre la machine ; l'erreur décrit ce qui manque.
	[[nodiscard]] static Result<std::unique_ptr<Session>, std::string> Open(const emu::SystemRegistry &registry, const SessionOptions &options);

	[[nodiscard]] emu::Machine &Machine() noexcept { return *m_machine; }
	[[nodiscard]] const emu::SystemInfo &System() const noexcept { return *m_system; }
	[[nodiscard]] const std::filesystem::path &RomPath() const noexcept { return m_options.rom; }
	/// Écran principal (toute machine en a un).
	[[nodiscard]] emu::Screen &Screen() noexcept { return *m_machine->Screens().front(); }

	/// Presse ou relâche un bouton sur le premier port qui le déclare ; faux s'il est inconnu.
	bool SetButton(std::string_view id, bool pressed);

	/// Écrit l'image courante de l'écran principal en PNG.
	[[nodiscard]] bool SaveScreenshot(const std::filesystem::path &path);
	/// Fichier de l'emplacement d'état `slot` : `<états>/<rom>.s<slot>`.
	[[nodiscard]] std::filesystem::path StatePath(int slot) const;
	/// Enregistre l'état et sa vignette `<état>.png`.
	[[nodiscard]] bool SaveState(int slot);
	[[nodiscard]] bool LoadState(int slot);
	/// Vrai si l'image n'est pas d'une seule couleur (la ROM affiche quelque chose).
	[[nodiscard]] bool ScreenHasContent();

private:
	Session() = default;
	SessionOptions m_options;
	const emu::SystemInfo *m_system = nullptr;
	std::unique_ptr<emu::Machine> m_machine;
};

} // namespace retro
