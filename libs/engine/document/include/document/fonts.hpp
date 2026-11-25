#pragma once
/**
 * @file fonts.hpp
 * @brief Polices disponibles pour la mise en page : les polices standard PDF (Helvetica, Times,
 *        Courier) et les polices installées (inventaire `pdf::ScanFonts`), chargées à la
 *        demande et partagées entre les fils de mise en page.
 *
 * La propriété `FontFamily` d'un texte est cherchée parmi les familles installées (casse
 * ignorée) ; le style le plus proche (graisse, italique) est choisi. À défaut, la famille est
 * rapprochée d'une police standard. Les mesures viennent des métriques de la police réelle :
 * l'écran (même fichier) et le PDF (police incorporée) donnent la même mise en page.
 *
 * Les dossiers à inventorier sont fournis par l'application (ceux de la plateforme) : le moteur
 * ne dépend d'aucune configuration du système.
 */
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <pdf/fonts.hpp>
#include <pdf/truetype.hpp>

namespace doc {

class FontLibrary {
public:
	FontLibrary() = default;
	explicit FontLibrary(std::vector<pdf::FontDescription> fonts);
	/// Inventaire des polices des dossiers (récursif).
	[[nodiscard]] static std::shared_ptr<FontLibrary> Scan(std::span<const std::filesystem::path> directories);

	/// Familles proposées à l'utilisateur : les trois familles standard, puis les familles
	/// installées (ordre alphabétique, sans doublon).
	[[nodiscard]] std::vector<std::string> Families() const;
	[[nodiscard]] size_t InstalledCount() const noexcept { return m_fonts.size(); }
	/// Police d'un texte. Sûr depuis plusieurs fils (chargements protégés par un verrou).
	[[nodiscard]] pdf::FontRef Resolve(std::string_view family, bool bold, bool italic) const;
	/// Fichier de la face choisie pour (famille, gras, italique) ; vide pour une police standard.
	[[nodiscard]] std::filesystem::path PathOf(std::string_view family, bool bold, bool italic) const;
	/// Identité (change si l'inventaire change) : invalide les mesures en cache.
	[[nodiscard]] uint64_t Identity() const noexcept { return m_identity; }

private:
	[[nodiscard]] const pdf::FontDescription *Best(std::string_view family, bool bold, bool italic) const;

	std::vector<pdf::FontDescription> m_fonts;
	std::map<std::string, std::vector<size_t>> m_byFamily; ///< famille en minuscules → indices
	uint64_t m_identity = 0;
	mutable std::mutex m_mutex;
	mutable std::map<std::filesystem::path, std::shared_ptr<const pdf::TrueTypeFont>> m_loaded;
	mutable std::map<std::filesystem::path, bool> m_failed;
};

} // namespace doc
