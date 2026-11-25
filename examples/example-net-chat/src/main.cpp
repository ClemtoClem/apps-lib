// example-net-chat — messagerie de réseau local, sans serveur.
//
// Les pairs se découvrent par diffusion UDP (ou s'ajoutent à la main par adresse), puis
// échangent messages et fichiers par TCP (`net::chat::ChatNode`, construit sur le wrapper
// sdl3 de SDL_net, sans fil d'exécution : tout avance dans `Poll` à chaque image).
// Colonne de gauche : la conversation. Colonne de droite : les pairs (sélection = destinataire
// des fichiers), l'ajout manuel, les transferts avec progression, débit et annulation.
// Déposer un fichier sur la fenêtre l'envoie au pair sélectionné (ou à tous).
// Colonne Wi-Fi (bouton de la barre de titre) : interfaces et capacités, balayage des
// réseaux, création ou adhésion à une cellule ad hoc et départ (`net::wifi`, nmcli lancé par
// sdl3::Process dans des tâches de fond), journal des commandes, instructions administrateur
// en cas de refus de droits, pare-feu détecté.
//
//   build/host/debug/bin/example-net-chat [--nick NOM] [--peer HÔTE[:PORT]]… [--tcp-port N]
//       [--discovery-port N] [--download-dir DOSSIER] [--no-broadcast] [--say TEXTE]
//       [--no-wifi] [--device IFACE]
//
// Essai sur une machine : lancer deux fois ; la seconde instance prend le port TCP suivant
// et les deux se découvrent (ou se joignent avec --peer 127.0.0.1:48621).
#include <algorithm>
#include <array>
#include <functional>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <app/app.hpp>
#include <net/chat.hpp>
#include <net/wifi.hpp>
#include <sdl3/io/net.hpp>

namespace {

namespace chat = net::chat;
namespace wifi = net::wifi;

/// État Wi-Fi relevé par une tâche de fond (nmcli bloque plusieurs secondes).
struct WifiSnapshot {
	bool available = false;
	std::vector<wifi::WifiDevice> devices;
	String device;
	wifi::WifiCapabilities caps;
	std::vector<String> addresses;
	std::vector<wifi::WifiNetwork> networks;
	Option<wifi::FirewallInfo> firewall;
	std::vector<String> log;		 ///< journal de l'opération (création, départ, erreurs)
	Option<wifi::ConfigureReport> report;
};

WifiSnapshot ReadWifi(String device, bool rescan, uint16_t udp, uint16_t tcp) {
	WifiSnapshot s;
	s.available = wifi::IsAvailable();
	if (!s.available)
		return s;
	if (auto devices = wifi::ListDevices())
		s.devices = std::move(devices.Value());
	else
		s.log.push_back(devices.Error());
	if (device.IsEmpty() && !s.devices.empty())
		device = s.devices.front().name;
	s.device = device;
	if (!device.IsEmpty()) {
		if (auto caps = wifi::Capabilities(device))
			s.caps = caps.Value();
		if (auto ips = wifi::Ipv4Addresses(device))
			s.addresses = std::move(ips.Value());
	}
	if (auto nets = wifi::Scan(device, rescan))
		s.networks = std::move(nets.Value());
	else
		s.log.push_back("Balayage : " + nets.Error());
	s.firewall = wifi::DetectFirewall(udp, tcp, uint16_t(tcp + 9));
	return s;
}

/// Coupe `text` en lignes d'au plus `width` (mots entiers quand c'est possible).
std::vector<std::string> Wrap(sdl3::Canvas &c, std::string_view text, float width, float size) {
	std::vector<std::string> lines;
	std::string line;
	size_t start = 0;
	while (start <= text.size()) {
		const size_t space = text.find(' ', start);
		const std::string_view word = text.substr(start, space == std::string_view::npos ? std::string_view::npos : space - start);
		const std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
		if (!line.empty() && c.MeasureText(candidate, size) > width) {
			lines.push_back(line);
			line = std::string(word);
		} else
			line = candidate;
		if (space == std::string_view::npos)
			break;
		start = space + 1;
	}
	lines.push_back(line);
	return lines;
}

std::string Size(uint64_t bytes) {
	if (bytes >= 1024 * 1024)
		return std::format("{:.1f} Mo", double(bytes) / 1048576.0);
	if (bytes >= 1024)
		return std::format("{:.0f} Ko", double(bytes) / 1024.0);
	return std::format("{} o", bytes);
}

class ChatApp final : public app::Application {
public:
	ChatApp(chat::NodeConfig config, std::vector<std::string> peers, std::string say, bool wifi, std::string device)
		: m_config(std::move(config)), m_manualPeers(std::move(peers)), m_say(std::move(say)), m_wifiEnabled(wifi), m_showWifi(wifi) {
		m_adhoc.device = String(device.c_str());
	}

protected:
	bool OnInit() override {
		auto net = sdl3::NetContext::Create();
		if (net.IsError())
			return false;
		m_net.emplace(std::move(net.Value()));
		auto node = chat::ChatNode::Start(m_config);
		if (node.IsError()) {
			m_error = node.Error().c_str();
			return true; // l'interface affiche l'erreur
		}
		m_node = std::move(node.Value());
		m_nick = m_node->Nickname().c_str();
		for (const std::string &peer : m_manualPeers)
			AddPeer(peer);
		if (m_wifiEnabled)
			StartWifiTask(false);
		return true;
	}

	void OnUpdate(double) override {
		PollWifiTask();
		if (!m_node)
			return;
		m_node->Poll(NowMs());
		// --say : message envoyé dès qu'un pair est connecté (essais automatiques).
		if (!m_say.empty() && std::ranges::any_of(m_node->Peers(), [](const chat::PeerInfo &p) { return p.connected; })) {
			m_node->SendChat(String(m_say.c_str()));
			m_say.clear();
		}
		for (const std::string &file : Events().DroppedFiles())
			SendFile(file);
	}

	void OnRender(const sdl3::FRect &area, double) override {
		sdl3::Canvas &c = Canvas();
		const ui::Theme &th = Theme();
		if (!m_node) {
			c.TextCentered(area, "Réseau indisponible : " + m_error, th.danger, 16);
			return;
		}
		const float wifiW = m_showWifi ? 340.f : 0.f;
		const sdl3::FRect side{area.Right() - 380.f, area.y, 380.f, area.h};
		if (m_showWifi)
			DrawWifi({area.x, area.y, wifiW, area.h});
		DrawConversation({area.x + wifiW, area.y, area.w - side.w - wifiW, area.h});
		DrawSide(side);
	}

	void TitleButtons(std::vector<ui::FrameButton> &left, std::vector<ui::FrameButton> &) override {
		if (m_wifiEnabled)
			left.push_back({.id = "wifi", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::PUBLIC), .label = "Wi-Fi", .tooltip = "Cellule ad hoc et réseaux visibles", .active = m_showWifi});
	}

	void OnTitleButton(std::string_view id) override {
		if (id == "wifi")
			m_showWifi = !m_showWifi;
	}

	void OnShutdown() override {
		if (m_wifiTask.valid())
			m_wifiTask.wait(); // une commande nmcli en cours se termine d'elle-même
		m_node.reset(); // annonce BYE, ferme les sockets avant NET_Quit
		m_net.reset();
	}

private:
	static uint64_t NowMs() {
		return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	void AddPeer(std::string_view text) {
		std::string host(text);
		uint16_t port = chat::DEFAULT_TCP_PORT;
		if (const size_t colon = host.rfind(':'); colon != std::string::npos) {
			port = uint16_t(std::strtoul(host.c_str() + colon + 1, nullptr, 10));
			host.resize(colon);
		}
		if (const Option<String> error = m_node->AddManualPeer(String(host.c_str()), port); error.IsSome())
			Ui().Notify("Pair refusé : " + std::string(error.Value().c_str()), Theme().danger);
	}

	void SendFile(const std::string &path) {
		std::vector<uint64_t> targets;
		for (const chat::PeerInfo &p : m_node->Peers())
			if (p.connected && p.id != 0 && (m_selected == 0 || p.id == m_selected))
				targets.push_back(p.id);
		if (targets.empty()) {
			Ui().Notify("Aucun pair connecté pour recevoir le fichier", Theme().warning);
			return;
		}
		for (const uint64_t id : targets)
			if (auto sent = m_node->SendFile(id, String(path.c_str())); sent.IsError())
				Ui().Notify("Envoi impossible : " + std::string(sent.Error().c_str()), Theme().danger);
	}

	void DrawConversation(const sdl3::FRect &area) {
		sdl3::Canvas &c = Canvas();
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();
		// En-tête : pseudo modifiable, adresses de la machine.
		const sdl3::FRect head{area.x + 16.f, area.y + 12.f, area.w - 32.f, 34.f};
		c.Text(head.x, head.y + 8, "Pseudo", th.textDim, 13);
		bool renamed = false;
		u.TextInputAt({head.x + 60.f, head.y, 200.f, head.h}, "##nick", m_nick, &renamed, "pseudo");
		if (renamed && !m_nick.empty())
			m_node->SetNickname(String(m_nick.c_str()));
		std::string addresses;
		for (const String &a : m_node->LocalAddresses())
			addresses += (addresses.empty() ? "" : ", ") + std::string(a.c_str());
		const std::string where = std::format("TCP {} · UDP {} · {}", m_node->TcpPort(), m_node->DiscoveryPort(), addresses.empty() ? "aucune adresse" : addresses);
		c.Text(head.x + 280.f, head.y + 9, c.Ellipsize(where, head.w - 280.f, 12), th.textDim, 12);

		// Fil de discussion : dessiné du plus récent vers le haut, collé en bas sauf défilement.
		const sdl3::FRect input{area.x + 16.f, area.Bottom() - 50.f, area.w - 32.f, 36.f};
		const sdl3::FRect log{area.x + 16.f, head.Bottom() + 12.f, area.w - 32.f, input.y - head.Bottom() - 24.f};
		c.FillRoundRect(log, 10.f, th.widget);
		if (log.Contains(u.MouseX(), u.MouseY()) && u.WheelY() != 0.f)
			m_scroll = std::max(0.f, m_scroll + u.WheelY() * 40.f);
		c.PushClip(log.Inset(4));
		float y = log.Bottom() - 10.f + m_scroll;
		const float fs = 14.f, lh = c.LineHeight(fs), maxW = log.w * 0.62f;
		const std::vector<chat::ChatMessage> &messages = m_node->Messages();
		for (auto it = messages.rbegin(); it != messages.rend() && y > log.y; ++it) {
			const chat::ChatMessage &m = *it;
			if (m.system) {
				const std::string text(m.text.c_str());
				y -= lh + 8.f;
				c.Text(log.x + (log.w - c.MeasureText(text, 12)) * 0.5f, y, text, th.textDim, 12);
				continue;
			}
			const std::vector<std::string> lines = Wrap(c, m.text.c_str(), maxW - 24.f, fs);
			float w = 0.f;
			for (const std::string &l : lines)
				w = std::max(w, c.MeasureText(l, fs));
			const float h = lh * float(lines.size()) + 28.f;
			y -= h + 8.f;
			const sdl3::FRect bubble{m.outgoing ? log.Right() - w - 36.f : log.x + 12.f, y, w + 24.f, h};
			c.FillRoundRect(bubble, 10.f, m.outgoing ? sdl3::WithAlpha(th.accent, 220) : th.widgetHover);
			const sdl3::Color text = m.outgoing ? th.accentText : th.text;
			c.Text(bubble.x + 12.f, bubble.y + 6.f, m.outgoing ? std::string("Vous") : std::string(m.author.c_str()), sdl3::WithAlpha(text, 170), 11,
				   sdl3::CanvasFont::Bold);
			for (size_t i = 0; i < lines.size(); ++i)
				c.Text(bubble.x + 12.f, bubble.y + 22.f + lh * float(i), lines[i], text, fs);
		}
		c.PopClip();
		if (messages.empty())
			c.TextCentered(log, "Aucun message. Les pairs du réseau local apparaissent à droite.", th.textDim, 14);

		// Saisie.
		bool submitted = false;
		u.TextInputAt({input.x, input.y, input.w - 110.f, input.h}, "##message", m_draft, &submitted, "Écrire un message…");
		const bool send = u.PrimaryButton({input.Right() - 100.f, input.y, 100.f, input.h}, "Envoyer", !m_draft.empty());
		if ((submitted || send) && !m_draft.empty()) {
			m_node->SendChat(String(m_draft.c_str()));
			m_draft.clear();
			m_scroll = 0.f;
			u.SetFocus("##message");
		}
	}

	void DrawSide(const sdl3::FRect &side) {
		sdl3::Canvas &c = Canvas();
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();
		c.FillRect(side, th.title);
		c.Line(side.x, side.y, side.x, side.Bottom(), th.panelBorder);
		u.BeginColumn(side.Inset(14));
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::PERSON), "Pairs");
		const std::vector<chat::PeerInfo> peers = m_node->Peers();
		if (peers.empty())
			u.Wrapped("Personne pour l'instant. Lancez une autre instance, ou ajoutez une adresse.", th.textDim, 12);
		// « Tous » puis chaque pair : la sélection désigne le destinataire des fichiers.
		const auto peerRow = [&](uint64_t id, std::string_view title, std::string_view detail, sdl3::Color dot) {
			const sdl3::FRect row = u.Next(42);
			u.PushId(id);
			const auto it = u.Interact(u.MakeId("peer"), row);
			u.PopId();
			if (it.leftClicked)
				m_selected = id;
			c.FillRoundRect(row, 8.f, m_selected == id ? th.widgetActive : (it.hovered ? th.widgetHover : th.widget));
			c.FillCircle(row.x + 14.f, row.y + 21.f, 5.f, dot);
			c.Text(row.x + 28.f, row.y + 5.f, c.Ellipsize(title, row.w - 36.f, 14), th.text, 14, sdl3::CanvasFont::Bold);
			c.Text(row.x + 28.f, row.y + 23.f, c.Ellipsize(detail, row.w - 36.f, 12), th.textDim, 12);
		};
		peerRow(0, "Tous les pairs", std::format("{} pair(s) connu(s)", peers.size()), th.accent);
		for (const chat::PeerInfo &p : peers)
			peerRow(p.id, p.nickname.IsEmpty() ? std::string("?") : std::string(p.nickname.c_str()),
					std::format("{}:{} · {}", p.address.c_str(), p.tcpPort, p.status.c_str()), p.connected ? th.success : th.warning);
		const sdl3::FRect manual = u.Next(32);
		bool add = false;
		u.TextInputAt({manual.x, manual.y, manual.w - 150.f, manual.h}, "##peer", m_peerText, &add, "hôte:port");
		if ((u.ButtonAt({manual.Right() - 144.f, manual.y, 70.f, manual.h}, "Ajouter", !m_peerText.empty()) || add) && !m_peerText.empty()) {
			AddPeer(m_peerText);
			m_peerText.clear();
		}
		if (u.ButtonAt({manual.Right() - 70.f, manual.y, 70.f, manual.h}, "Sonder"))
			m_node->Probe();

		u.Space(8);
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::INSERT_DRIVE_FILE), "Fichiers");
		const sdl3::FRect file = u.Next(32);
		u.TextInputAt({file.x, file.y, file.w - 80.f, file.h}, "##file", m_filePath, nullptr, "chemin du fichier (ou déposez-le)");
		if (u.ButtonAt({file.Right() - 74.f, file.y, 74.f, file.h}, "Envoyer", !m_filePath.empty())) {
			SendFile(m_filePath);
			m_filePath.clear();
		}
		u.Wrapped(std::format("Reçus dans : {}", m_node->DownloadDir().c_str()), th.textDim, 11);
		const uint64_t now = NowMs();
		for (const chat::TransferInfo &t : m_node->Transfers()) {
			const sdl3::FRect row = u.Next(56);
			c.FillRoundRect(row, 8.f, th.widget);
			c.Icon({row.x + 6.f, row.y + 6.f, 22.f, 22.f}, t.outgoing ? ui::icons::Codepoint(ui::icons::MaterialIcons::ARROW_FORWARD) : ui::icons::Codepoint(ui::icons::MaterialIcons::ARROW_BACK), th.textDim, 16);
			c.Text(row.x + 32.f, row.y + 6.f, c.Ellipsize(std::format("{} — {}", t.fileName.c_str(), t.peerName.c_str()), row.w - 70.f, 13), th.text, 13);
			const sdl3::FRect bar{row.x + 32.f, row.y + 28.f, row.w - 70.f, 8.f};
			const sdl3::Color color = t.state == chat::TransferState::DONE ? th.success
								   : t.state == chat::TransferState::RUNNING ? th.accent
																			  : th.danger;
			c.FillRoundRect(bar, 4.f, th.widgetHover);
			c.FillRoundRect({bar.x, bar.y, bar.w * t.Progress(), bar.h}, 4.f, color);
			const std::string info = t.state == chat::TransferState::RUNNING
										 ? std::format("{} / {} · {}/s", Size(t.done), Size(t.size), Size(uint64_t(t.Rate(now))))
										 : std::format("{} · {}", chat::TransferStateName(t.state), t.error.IsEmpty() ? Size(t.size) : std::string(t.error.c_str()));
			c.Text(bar.x, bar.Bottom() + 3.f, c.Ellipsize(info, bar.w, 11), th.textDim, 11);
			if (t.state == chat::TransferState::RUNNING && u.IconButton({row.Right() - 32.f, row.y + 14.f, 26.f, 26.f}, ui::icons::Codepoint(ui::icons::MaterialIcons::CLOSE), "Annuler"))
				m_node->CancelTransfer(t.id);
		}
		if (!m_node->Transfers().empty() && u.Button("Effacer les transferts terminés"))
			m_node->ClearFinishedTransfers();
		u.EndColumn();
	}

	// ── Wi-Fi ────────────────────────────────────────────────────────────
	/// Relevé en tâche de fond (balayage radio si `rescan`), précédé d'une configuration.
	void StartWifiTask(bool rescan, std::function<wifi::ConfigureReport()> configure = {}) {
		if (m_wifiTask.valid())
			return;
		const uint16_t udp = m_node ? m_node->DiscoveryPort() : chat::DEFAULT_DISCOVERY_PORT;
		const uint16_t tcp = m_node ? m_node->TcpPort() : chat::DEFAULT_TCP_PORT;
		m_wifiTask = std::async(std::launch::async, [device = m_adhoc.device, rescan, udp, tcp, configure = std::move(configure)] {
			Option<wifi::ConfigureReport> report;
			if (configure)
				report = Some(configure());
			WifiSnapshot s = ReadWifi(device, rescan, udp, tcp);
			s.report = std::move(report);
			return s;
		});
	}

	void PollWifiTask() {
		if (!m_wifiTask.valid() || m_wifiTask.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			return;
		WifiSnapshot s = m_wifiTask.get();
		for (String &line : s.log)
			m_wifiLog.push_back(std::move(line));
		if (s.report) {
			for (const String &line : s.report->log)
				m_wifiLog.push_back(line);
			m_permissionDenied = s.report->permissionDenied;
			if (s.report->ok)
				Ui().Notify("Cellule ad hoc configurée", Theme().success);
			else
				Ui().Notify("Wi-Fi : " + std::string(s.report->error.c_str()), Theme().danger, 5.f);
			if (s.report->ok && m_node)
				m_node->Probe(); // nouvelles adresses : se faire connaître aussitôt
		}
		if (m_wifiLog.size() > 200)
			m_wifiLog.erase(m_wifiLog.begin(), m_wifiLog.end() - 200);
		m_wifi = std::move(s);
		if (m_adhoc.device.IsEmpty())
			m_adhoc.device = m_wifi.device;
	}

	void DrawWifi(const sdl3::FRect &area) {
		sdl3::Canvas &c = Canvas();
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();
		c.FillRect(area, th.title);
		c.Line(area.Right(), area.y, area.Right(), area.Bottom(), th.panelBorder);
		const bool busy = m_wifiTask.valid();
		u.BeginScroll("wifi", area.Inset(14), {.axes = ui::ScrollAxes::Vertical});
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::PUBLIC), "Wi-Fi");
		if (busy) {
			const sdl3::FRect row = u.Next(22);
			u.Spinner({row.x, row.y, 22.f, 22.f}, 18.f);
			c.Text(row.x + 30.f, row.y + 3.f, "nmcli en cours…", th.textDim, 13);
		} else if (!m_wifi.available) {
			u.Wrapped("NetworkManager (nmcli) est introuvable : le chat fonctionne sur le réseau actuel.", th.warning, 12);
		}
		if (m_wifi.available) {
			std::vector<std::string> names;
			for (const wifi::WifiDevice &d : m_wifi.devices)
				names.push_back(std::format("{} · {}", d.name.c_str(), d.state.c_str()));
			std::vector<std::string_view> views(names.begin(), names.end());
			int index = 0;
			for (size_t i = 0; i < m_wifi.devices.size(); ++i)
				if (m_wifi.devices[i].name == m_adhoc.device)
					index = int(i);
			if (!views.empty() && u.Choice(u.Next(30), "device", index, views)) {
				m_adhoc.device = m_wifi.devices[size_t(index)].name;
				StartWifiTask(false);
			}
			const sdl3::FRect caps = u.Next(22);
			float x = caps.x;
			const auto badge = [&](std::string_view text, bool on) {
				const float w = c.MeasureText(text, 11) + 14.f;
				u.Badge({x, caps.y, w, 20.f}, text, on ? th.success : th.textDim);
				x += w + 6.f;
			};
			badge("Ad hoc", m_wifi.caps.adhoc);
			badge("WPA2 IBSS", m_wifi.caps.ibssRsn);
			badge("2,4 GHz", m_wifi.caps.band2GHz);
			badge("5 GHz", m_wifi.caps.band5GHz);
			std::string ips;
			for (const String &a : m_wifi.addresses)
				ips += (ips.empty() ? "" : ", ") + std::string(a.c_str());
			u.Label(ips.empty() ? std::string("Aucune adresse IPv4") : "IPv4 : " + ips, th.textDim, 12);
		}

		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::SEARCH), "Réseaux visibles");
		if (u.Button(busy ? "Balayage…" : "Balayer", !busy && m_wifi.available))
			StartWifiTask(true);
		size_t shown = 0;
		for (const wifi::WifiNetwork &n : m_wifi.networks) {
			if (++shown > 12)
				break;
			const sdl3::FRect row = u.Next(36);
			u.PushId(uint64_t(shown));
			const auto it = u.Interact(u.MakeId("net"), row);
			u.PopId();
			if (it.leftClicked && !n.ssid.IsEmpty()) { // recopie SSID et canal dans la cellule
				m_ssid = n.ssid.c_str();
				if (n.channel >= 1 && n.channel <= 14)
					m_adhoc.channel = n.channel;
			}
			c.FillRoundRect(row, 6.f, it.hovered ? th.widgetHover : th.widget);
			const sdl3::FRect bars{row.x + 8.f, row.y + 9.f, 18.f, 18.f};
			for (int b = 0; b < 4; ++b) { // force du signal
				const float h = 4.f + 4.f * float(b);
				c.FillRect({bars.x + float(b) * 5.f, bars.Bottom() - h, 3.f, h}, n.signal > b * 25 ? th.accent : th.widgetHover);
			}
			const std::string name = n.ssid.IsEmpty() ? std::string("(masqué)") : std::string(n.ssid.c_str());
			c.Text(row.x + 34.f, row.y + 3.f, c.Ellipsize(name, row.w - 110.f, 13), n.inUse ? th.accent : th.text, 13, n.inUse ? sdl3::CanvasFont::Bold : sdl3::CanvasFont::Regular);
			c.Text(row.x + 34.f, row.y + 19.f, std::format("{} · canal {} · {}", wifi::WifiModeName(n.mode), n.channel, n.IsOpen() ? "ouvert" : n.security.c_str()), th.textDim, 11);
			if (n.IsAdhoc())
				u.Badge({row.Right() - 62.f, row.y + 8.f, 54.f, 20.f}, "ad hoc", th.success);
		}

		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::SETTINGS), "Cellule ad hoc");
		u.TextInput("##ssid", m_ssid, nullptr, "SSID commun");
		u.SliderInt("Canal", m_adhoc.channel, 1, 13);
		static constexpr std::array<std::string_view, 2> kIpv4 = {"IPv4 lien local", "IPv4 manuelle"};
		int method = int(m_adhoc.ipv4);
		if (u.Choice(u.Next(30), "ipv4", method, kIpv4))
			m_adhoc.ipv4 = wifi::Ipv4Method(method);
		if (m_adhoc.ipv4 == wifi::Ipv4Method::Manual)
			u.TextInput("##address", m_address, nullptr, "10.42.0.N/24");
		u.TextInput("##password", m_password, nullptr, "mot de passe (vide : ouverte)");
		m_adhoc.ssid = String(m_ssid.c_str());
		m_adhoc.address = String(m_address.c_str());
		m_adhoc.password = String(m_password.c_str());
		const Option<String> problem = m_adhoc.Validate();
		if (problem.IsSome())
			u.Wrapped(problem.Value().c_str(), th.warning, 11);
		u.Row({1.f, 1.f}, 30.f);
		if (u.Button("Créer / rejoindre", !busy && m_wifi.available && problem.IsNone()))
			StartWifiTask(false, [cfg = m_adhoc] { return wifi::JoinAdhoc(cfg); });
		if (u.Button("Quitter", !busy && m_wifi.available))
			StartWifiTask(false, [cfg = m_adhoc] { return wifi::LeaveAdhoc(cfg); });
		u.Wrapped("Même SSID et même canal sur chaque machine ; la carte quitte le Wi-Fi en cours.", th.textDim, 11);

		if (m_permissionDenied) {
			u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::LOCK), "Droits insuffisants");
			u.Wrapped(wifi::ManualInstructions(m_adhoc).c_str(), th.textDim, 11);
		}
		if (m_wifi.firewall) {
			u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::WARNING), std::format("Pare-feu actif : {}", m_wifi.firewall->name.c_str()));
			for (const String &cmd : m_wifi.firewall->commands)
				u.Wrapped(cmd.c_str(), th.textDim, 11);
		}
		if (!m_wifiLog.empty()) {
			u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::TERMINAL), "Journal");
			const size_t first = m_wifiLog.size() > 14 ? m_wifiLog.size() - 14 : 0;
			for (size_t i = first; i < m_wifiLog.size(); ++i)
				u.Wrapped(m_wifiLog[i].c_str(), m_wifiLog[i].StartsWith("$ ") ? th.accent : th.textDim, 11);
		}
		u.EndScroll();
	}

	chat::NodeConfig m_config;
	std::vector<std::string> m_manualPeers;
	std::string m_say;
	std::optional<sdl3::NetContext> m_net;
	std::unique_ptr<chat::ChatNode> m_node;
	std::string m_error, m_nick, m_draft, m_peerText, m_filePath;
	uint64_t m_selected = 0;
	float m_scroll = 0.f;
	bool m_wifiEnabled = true, m_showWifi = true, m_permissionDenied = false;
	wifi::AdhocConfig m_adhoc;
	std::string m_ssid = "adhoc-chat", m_address, m_password;
	WifiSnapshot m_wifi;
	std::vector<String> m_wifiLog;
	std::future<WifiSnapshot> m_wifiTask;
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Messagerie locale";
	config.width = 1180;
	config.height = 760;
	config.customFrame = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::PUBLIC);
	chat::NodeConfig node;
	const char *user = std::getenv("USER");
	node.nickname = user && *user ? String(user) : String("anonyme");
	node.downloadDir = String((std::filesystem::temp_directory_path() / "apps-lib-chat").string().c_str());
	std::vector<std::string> peers;
	std::string say, device;
	bool wifiPanel = true;
	const std::vector<std::string> args = config.ParseArgs(argc, argv);
	for (size_t i = 0; i < args.size(); ++i) {
		const std::string &a = args[i];
		const auto value = [&] { return i + 1 < args.size() ? args[++i] : std::string(); };
		if (a == "--nick")
			node.nickname = String(value().c_str());
		else if (a == "--peer")
			peers.push_back(value());
		else if (a == "--tcp-port")
			node.tcpPort = uint16_t(std::strtoul(value().c_str(), nullptr, 10));
		else if (a == "--discovery-port")
			node.discoveryPort = uint16_t(std::strtoul(value().c_str(), nullptr, 10));
		else if (a == "--download-dir")
			node.downloadDir = String(value().c_str());
		else if (a == "--no-broadcast")
			node.broadcast = false;
		else if (a == "--say")
			say = value();
		else if (a == "--no-wifi")
			wifiPanel = false;
		else if (a == "--device")
			device = value();
	}
	config.width = wifiPanel ? 1480 : 1180;
	return ChatApp(std::move(node), std::move(peers), std::move(say), wifiPanel, std::move(device)).Run(config);
}
