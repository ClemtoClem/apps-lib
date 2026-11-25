#pragma once
/**
 * @file wifi.hpp
 * @brief Réseaux Wi-Fi et cellule IBSS (« ad hoc ») sous Linux, par NetworkManager (`nmcli`).
 *
 * SDL_net ne parle qu'IP : il ne sait ni lister les réseaux radio ni changer le mode d'une
 * carte. Ce module lance donc `nmcli` (`sdl3::Process::Run`, sortie non traduite) et analyse
 * sa sortie « terse » (`-t -e yes` : champs séparés par `:`, `:` et `\` des valeurs échappés).
 *
 * Cellule IBSS : toutes les machines utilisent le même SSID sur le même canal ; la première
 * crée la cellule, les suivantes la rejoignent (même commande). IPv4 « lien local »
 * (169.254.x.x, sans serveur) ou adresse fixe par machine. Une carte n'ayant qu'une radio,
 * passer en ad hoc coupe la connexion Wi-Fi en cours.
 *
 * Droits : NetworkManager autorise en général l'utilisateur de la session (polkit) ; sinon
 * `IsPermissionError` reconnaît le refus et `ManualInstructions` donne les commandes à lancer
 * en administrateur.
 *
 * Les fonctions qui lancent `nmcli` prennent un `CommandRunner` (processus réel par défaut,
 * remplaçable dans les tests) et BLOQUENT (un balayage dure quelques secondes) : une
 * interface les appelle depuis un fil de travail.
 */
#include <cstdint>
#include <functional>
#include <vector>

#include <core/core.hpp>

namespace net::wifi {

/// Sortie d'une commande : stdout et stderr mêlés, code de sortie.
struct CommandResult {
	int exitCode = -1;
	String output;

	[[nodiscard]] bool Ok() const noexcept { return exitCode == 0; }
};

/// Lance `argv` ; Err si le lancement lui-même échoue (programme absent…).
using CommandRunner = std::function<Result<CommandResult, String>(const std::vector<String> &argv)>;

/// Processus réel (`sdl3::Process::Run`), en `LC_ALL=C` : valeurs analysables.
[[nodiscard]] Result<CommandResult, String> RunCommand(const std::vector<String> &argv);

struct WifiDevice {
	String name;	   ///< wlp0s20f3, wlan0…
	String state;	   ///< connected, disconnected, unavailable…
	String connection; ///< profil actif (vide : aucun)
};

/// Ce que la carte sait faire (WIFI-PROPERTIES de `nmcli device show`).
struct WifiCapabilities {
	bool adhoc = false;	  ///< mode IBSS
	bool ibssRsn = false; ///< IBSS chiffré WPA2
	bool band2GHz = false;
	bool band5GHz = false;
};

enum class WifiMode : uint8_t { Infra, Adhoc, Mesh, Other };

struct WifiNetwork {
	String ssid; ///< vide : réseau masqué
	String bssid;
	WifiMode mode = WifiMode::Other;
	int channel = 0;
	int frequencyMHz = 0;
	int signal = 0; ///< 0..100
	String rate;
	String security; ///< vide : ouvert
	bool inUse = false;

	[[nodiscard]] bool IsAdhoc() const noexcept { return mode == WifiMode::Adhoc; }
	[[nodiscard]] bool IsOpen() const noexcept { return security.IsEmpty(); }
};

[[nodiscard]] const char *WifiModeName(WifiMode mode) noexcept;

enum class Ipv4Method : uint8_t {
	LinkLocal, ///< 169.254.x.x automatique (rien à saisir, plus lent à converger)
	Manual,	   ///< adresse fixe, unique par machine (ex. 10.42.0.N/24)
};

struct AdhocConfig {
	String device; ///< interface (vide : celle que choisit NetworkManager)
	String ssid = "adhoc-chat";
	int channel = 6; ///< 1..14 (2,4 GHz), le même pour tous
	Ipv4Method ipv4 = Ipv4Method::LinkLocal;
	String address;		   ///< Manual : « 10.42.0.N/24 »
	String password;	   ///< vide : cellule ouverte ; sinon WPA2 IBSS-RSN (8 à 63 caractères)
	String connectionName; ///< profil NetworkManager (vide : « adhoc-<ssid> »)

	[[nodiscard]] String ProfileName() const;
	/// NONE si cohérent, la raison sinon.
	[[nodiscard]] Option<String> Validate() const;
};

// ── Analyse (pure) ───────────────────────────────────────────────────────

/// Découpe une ligne terse en champs, échappements retirés.
[[nodiscard]] std::vector<String> SplitTerseLine(std::string_view line);
/// `nmcli -t -e yes -f DEVICE,TYPE,STATE,CONNECTION device status` : interfaces wifi seules.
[[nodiscard]] std::vector<WifiDevice> ParseDevices(std::string_view output);
/// `nmcli -t -e yes -f IN-USE,BSSID,SSID,MODE,CHAN,FREQ,RATE,SIGNAL,SECURITY device wifi list` :
/// ad hoc d'abord, puis par signal décroissant.
[[nodiscard]] std::vector<WifiNetwork> ParseNetworks(std::string_view output);
/// `nmcli -t -f WIFI-PROPERTIES device show <dev>`.
[[nodiscard]] WifiCapabilities ParseCapabilities(std::string_view output);
/// Lignes `IP4.ADDRESS[n]:a.b.c.d/m`.
[[nodiscard]] std::vector<String> ParseIpv4Addresses(std::string_view output);
[[nodiscard]] int ChannelToFrequencyMHz(int channel) noexcept;
/// La sortie signale-t-elle un refus de droits (polkit, root) ?
[[nodiscard]] bool IsPermissionError(std::string_view output);

// ── Commandes ────────────────────────────────────────────────────────────

/// Création puis activation du profil ad hoc.
[[nodiscard]] std::vector<std::vector<String>> AdhocCommands(const AdhocConfig &config);
/// Une commande en ligne de shell (affichage) ; mot de passe masqué si `hideSecrets`.
[[nodiscard]] String CommandLine(const std::vector<String> &argv, bool hideSecrets = true);
/// Commandes à lancer en administrateur pour la même cellule (nmcli via sudo, ou iw + ip).
[[nodiscard]] String ManualInstructions(const AdhocConfig &config);

// ── Exécution (bloquante) ────────────────────────────────────────────────

/// nmcli installé et NetworkManager joignable ?
[[nodiscard]] bool IsAvailable(const CommandRunner &run = RunCommand);
[[nodiscard]] Result<std::vector<WifiDevice>, String> ListDevices(const CommandRunner &run = RunCommand);
[[nodiscard]] Result<WifiCapabilities, String> Capabilities(const String &device, const CommandRunner &run = RunCommand);
[[nodiscard]] Result<std::vector<String>, String> Ipv4Addresses(const String &device, const CommandRunner &run = RunCommand);
/// Réseaux visibles ; `rescan` relance un balayage radio (quelques secondes).
[[nodiscard]] Result<std::vector<WifiNetwork>, String> Scan(const String &device, bool rescan, const CommandRunner &run = RunCommand);

/// Bilan d'une configuration : journal « $ commande » puis sortie, dans l'ordre.
struct ConfigureReport {
	bool ok = false;
	bool permissionDenied = false;
	std::vector<String> log;
	String error; ///< dernière erreur (vide si ok)
};

/// Crée ou rejoint la cellule (supprime un profil de même nom, le recrée, l'active).
[[nodiscard]] ConfigureReport JoinAdhoc(const AdhocConfig &config, const CommandRunner &run = RunCommand);
/// Désactive et supprime le profil ; NetworkManager reprend la connexion habituelle.
[[nodiscard]] ConfigureReport LeaveAdhoc(const AdhocConfig &config, const CommandRunner &run = RunCommand);

/// Pare-feu actif : il bloque en général la découverte UDP et les connexions TCP des pairs.
struct FirewallInfo {
	String name;				  ///< « ufw », « firewalld »
	std::vector<String> commands; ///< à lancer en administrateur pour ouvrir les ports
};
/// ufw / firewalld actif (`systemctl is-active`, sans droits) ; NONE sinon.
[[nodiscard]] Option<FirewallInfo> DetectFirewall(uint16_t udpPort, uint16_t tcpFirst, uint16_t tcpLast,
												  const CommandRunner &run = RunCommand);

} // namespace net::wifi
