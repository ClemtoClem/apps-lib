#include "net/wifi.hpp"

#include <algorithm>
#include <cctype>

#include <sdl3/io/process.hpp>

namespace net::wifi {

namespace {

int ToInt(const String &s) { return int(s.Trim().TryParseInt().UnwrapOr(0)); }

/// « 2462 MHz » → 2462.
int LeadingNumber(const String &s) {
	int value = 0;
	for (char c : s.Trim()) {
		if (c < '0' || c > '9')
			break;
		value = value * 10 + (c - '0');
	}
	return value;
}

WifiMode ParseMode(const String &s) {
	const String m = s.ToLower();
	if (m.StartsWith("infra"))
		return WifiMode::Infra;
	if (m.StartsWith("ad-hoc") || m.StartsWith("adhoc") || m == "ibss")
		return WifiMode::Adhoc;
	if (m.StartsWith("mesh"))
		return WifiMode::Mesh;
	return WifiMode::Other;
}

String Quote(const String &arg) {
	const bool plain = !arg.IsEmpty() && std::ranges::all_of(arg, [](char c) {
		return std::isalnum(static_cast<unsigned char>(c)) || std::string_view("-_./:=,").find(c) != std::string_view::npos;
	});
	return plain ? arg : String(String("'") + arg.Replace("'", "'\\''") + "'");
}

void Log(ConfigureReport &report, const std::vector<String> &argv, const CommandResult *result) {
	report.log.push_back("$ " + CommandLine(argv));
	if (result && !result->output.Trim().IsEmpty())
		report.log.push_back(result->output.Trim());
}

/// Une étape de configuration ; faux (rapport rempli) si elle échoue.
bool Step(ConfigureReport &report, const std::vector<String> &argv, const CommandRunner &run) {
	auto res = run(argv);
	if (!res) {
		Log(report, argv, nullptr);
		report.error = res.Error();
		report.log.push_back(report.error);
		return false;
	}
	Log(report, argv, &res.Value());
	if (!res.Value().Ok()) {
		report.permissionDenied = report.permissionDenied || IsPermissionError(res.Value().output);
		report.error = res.Value().output.Trim();
		if (report.error.IsEmpty())
			report.error = String::Format("code de sortie {}", res.Value().exitCode);
		return false;
	}
	return true;
}

/// Lance une commande ; Err avec sa sortie si le code de sortie n'est pas nul.
Result<String, String> RunChecked(const std::vector<String> &argv, const CommandRunner &run) {
	auto res = run(argv);
	if (!res)
		return Err(res.Error());
	if (!res.Value().Ok()) {
		const String msg = res.Value().output.Trim();
		return Err(msg.IsEmpty() ? String::Format("{} : code de sortie {}", argv[0], res.Value().exitCode) : msg);
	}
	return Ok(std::move(res.Value().output));
}

} // namespace

Result<CommandResult, String> RunCommand(const std::vector<String> &argv) {
	auto out = sdl3::Process::Run({.args = argv, .environment = {{"LC_ALL", "C"}, {"LANG", "C"}}});
	if (!out)
		return Err(String::Format("{} : {}", argv.empty() ? String("?") : argv[0], String(out.Error())));
	return Ok(CommandResult{out.Value().exitCode, String(reinterpret_cast<const char *>(out.Value().data.data()), out.Value().data.size())});
}

const char *WifiModeName(WifiMode mode) noexcept {
	switch (mode) {
	case WifiMode::Infra: return "Infra";
	case WifiMode::Adhoc: return "Ad hoc";
	case WifiMode::Mesh: return "Mesh";
	default: return "?";
	}
}

String AdhocConfig::ProfileName() const { return connectionName.IsEmpty() ? String("adhoc-" + ssid) : connectionName; }

Option<String> AdhocConfig::Validate() const {
	if (ssid.IsEmpty())
		return Some(String("SSID vide"));
	if (ssid.size() > 32)
		return Some(String("SSID trop long (32 octets au plus)"));
	if (channel < 1 || channel > 14)
		return Some(String("canal 2,4 GHz attendu (1 à 14)"));
	if (!password.IsEmpty() && (password.size() < 8 || password.size() > 63))
		return Some(String("mot de passe WPA2 : 8 à 63 caractères"));
	if (ipv4 == Ipv4Method::Manual) {
		const auto parts = address.Split('/');
		const int prefix = parts.size() == 2 ? ToInt(parts[1]) : 0;
		const auto bytes = parts.empty() ? std::vector<String>{} : parts[0].Split('.');
		if (parts.size() != 2 || bytes.size() != 4 || prefix < 1 || prefix > 30)
			return Some(String("adresse attendue sous la forme a.b.c.d/m (ex. 10.42.0.2/24)"));
		for (const String &b : bytes) {
			const auto v = b.TryParseInt();
			if (!v || *v < 0 || *v > 255)
				return Some(String("adresse IPv4 invalide : ") + parts[0]);
		}
	}
	return NONE;
}

std::vector<String> SplitTerseLine(std::string_view line) {
	std::vector<String> fields(1);
	for (size_t i = 0; i < line.size(); ++i) {
		const char c = line[i];
		if (c == '\\' && i + 1 < line.size())
			fields.back().Append(line[++i]);
		else if (c == ':')
			fields.emplace_back();
		else if (c != '\r')
			fields.back().Append(c);
	}
	return fields;
}

std::vector<WifiDevice> ParseDevices(std::string_view output) {
	std::vector<WifiDevice> out;
	for (const String &line : String(output).Lines())
		if (auto f = SplitTerseLine(line); f.size() >= 4 && f[1] == "wifi")
			out.push_back({f[0], f[2], f[3]});
	return out;
}

std::vector<WifiNetwork> ParseNetworks(std::string_view output) {
	std::vector<WifiNetwork> out;
	for (const String &line : String(output).Lines()) {
		const auto f = SplitTerseLine(line);
		if (f.size() < 9)
			continue;
		WifiNetwork n;
		n.inUse = f[0].Trim() == "*";
		n.bssid = f[1];
		n.ssid = f[2];
		n.mode = ParseMode(f[3]);
		n.channel = ToInt(f[4]);
		n.frequencyMHz = LeadingNumber(f[5]);
		n.rate = f[6];
		n.signal = ToInt(f[7]);
		n.security = f[8].Trim() == "--" ? String() : f[8].Trim();
		if (n.frequencyMHz == 0)
			n.frequencyMHz = ChannelToFrequencyMHz(n.channel);
		out.push_back(std::move(n));
	}
	std::ranges::stable_sort(out, [](const WifiNetwork &a, const WifiNetwork &b) {
		return a.IsAdhoc() != b.IsAdhoc() ? a.IsAdhoc() : a.signal > b.signal;
	});
	return out;
}

WifiCapabilities ParseCapabilities(std::string_view output) {
	WifiCapabilities caps;
	for (const String &line : String(output).Lines()) {
		const auto f = SplitTerseLine(line);
		if (f.size() < 2)
			continue;
		const bool yes = f[1].Trim().ToLower() == "yes";
		if (f[0] == "WIFI-PROPERTIES.ADHOC")
			caps.adhoc = yes;
		else if (f[0] == "WIFI-PROPERTIES.IBSS-RSN")
			caps.ibssRsn = yes;
		else if (f[0] == "WIFI-PROPERTIES.2GHZ")
			caps.band2GHz = yes;
		else if (f[0] == "WIFI-PROPERTIES.5GHZ")
			caps.band5GHz = yes;
	}
	return caps;
}

std::vector<String> ParseIpv4Addresses(std::string_view output) {
	std::vector<String> out;
	for (const String &line : String(output).Lines())
		if (auto f = SplitTerseLine(line); f.size() >= 2 && f[0].StartsWith("IP4.ADDRESS") && !f[1].Trim().IsEmpty())
			out.push_back(f[1].Trim());
	return out;
}

int ChannelToFrequencyMHz(int channel) noexcept {
	if (channel == 14)
		return 2484;
	if (channel >= 1 && channel <= 13)
		return 2407 + 5 * channel;
	if (channel >= 32 && channel <= 177)
		return 5000 + 5 * channel;
	return 0;
}

bool IsPermissionError(std::string_view output) {
	const String s = String(output).ToLower();
	for (std::string_view key : {"not authorized", "not authorised", "insufficient privileges", "permission denied", "operation not permitted", "polkit"})
		if (s.Contains(key))
			return true;
	return false;
}

std::vector<std::vector<String>> AdhocCommands(const AdhocConfig &config) {
	std::vector<String> add = {"nmcli", "connection", "add", "type", "wifi"};
	if (!config.device.IsEmpty())
		add.insert(add.end(), {"ifname", config.device});
	add.insert(add.end(), {"con-name", config.ProfileName(), "ssid", config.ssid, "mode", "adhoc", "wifi.band", "bg", "wifi.channel",
						   String::Format("{}", config.channel), "connection.autoconnect", "no"});
	if (config.ipv4 == Ipv4Method::Manual)
		add.insert(add.end(), {"ipv4.method", "manual", "ipv4.addresses", config.address});
	else
		add.insert(add.end(), {"ipv4.method", "link-local"});
	add.insert(add.end(), {"ipv6.method", "link-local"});
	if (!config.password.IsEmpty())
		add.insert(add.end(), {"wifi-sec.key-mgmt", "wpa-psk", "wifi-sec.proto", "rsn", "wifi-sec.pairwise", "ccmp", "wifi-sec.group", "ccmp",
							   "wifi-sec.psk", config.password});
	return {add, {"nmcli", "--wait", "30", "connection", "up", "id", config.ProfileName()}};
}

String CommandLine(const std::vector<String> &argv, bool hideSecrets) {
	String line;
	for (size_t i = 0; i < argv.size(); ++i) {
		if (i)
			line.Append(' ');
		const bool secret = hideSecrets && i > 0 && argv[i - 1] == "wifi-sec.psk";
		line.Append(secret ? String("********") : Quote(argv[i]));
	}
	return line;
}

String ManualInstructions(const AdhocConfig &config) {
	AdhocConfig cfg = config;
	if (cfg.device.IsEmpty())
		cfg.device = "wlan0";
	const String dev = Quote(cfg.device);
	String text = "Avec NetworkManager, en administrateur :\n";
	for (const auto &cmd : AdhocCommands(cfg))
		text.Append("  sudo " + CommandLine(cmd) + "\n");
	text.Append("\nSans NetworkManager (iw + ip, cellule ouverte) :\n");
	text.Append("  sudo nmcli device set " + dev + " managed no   # si NetworkManager tourne\n");
	text.Append("  sudo ip link set " + dev + " down\n");
	text.Append("  sudo iw dev " + dev + " set type ibss\n");
	text.Append("  sudo ip link set " + dev + " up\n");
	text.Append(String::Format("  sudo iw dev {} ibss join {} {}\n", dev, Quote(cfg.ssid), ChannelToFrequencyMHz(cfg.channel)));
	text.Append("  sudo ip addr add " + (cfg.ipv4 == Ipv4Method::Manual ? cfg.address : String("169.254.X.Y/16")) + " dev " + dev + "\n");
	text.Append("\nChaque machine : même SSID, même canal, adresse IPv4 différente dans le même sous-réseau.");
	return text;
}

bool IsAvailable(const CommandRunner &run) {
	auto res = run({"nmcli", "-t", "-f", "RUNNING", "general"});
	return res && res.Value().Ok() && res.Value().output.Trim() == "running";
}

Result<std::vector<WifiDevice>, String> ListDevices(const CommandRunner &run) {
	auto out = RunChecked({"nmcli", "-t", "-e", "yes", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device", "status"}, run);
	if (!out)
		return Err(out.Error());
	return Ok(ParseDevices(out.Value()));
}

Result<WifiCapabilities, String> Capabilities(const String &device, const CommandRunner &run) {
	auto out = RunChecked({"nmcli", "-t", "-f", "WIFI-PROPERTIES", "device", "show", device}, run);
	if (!out)
		return Err(out.Error());
	return Ok(ParseCapabilities(out.Value()));
}

Result<std::vector<String>, String> Ipv4Addresses(const String &device, const CommandRunner &run) {
	auto out = RunChecked({"nmcli", "-t", "-f", "IP4.ADDRESS", "device", "show", device}, run);
	if (!out)
		return Err(out.Error());
	return Ok(ParseIpv4Addresses(out.Value()));
}

Result<std::vector<WifiNetwork>, String> Scan(const String &device, bool rescan, const CommandRunner &run) {
	std::vector<String> argv = {"nmcli", "-t", "-e", "yes", "-f", "IN-USE,BSSID,SSID,MODE,CHAN,FREQ,RATE,SIGNAL,SECURITY", "device", "wifi", "list",
								"--rescan", rescan ? "yes" : "no"};
	if (!device.IsEmpty())
		argv.insert(argv.end(), {"ifname", device});
	auto out = RunChecked(argv, run);
	if (!out)
		return Err(out.Error());
	return Ok(ParseNetworks(out.Value()));
}

ConfigureReport JoinAdhoc(const AdhocConfig &config, const CommandRunner &run) {
	ConfigureReport report;
	if (auto problem = config.Validate()) {
		report.error = *problem;
		return report;
	}
	// Profil du même nom (essai précédent) : remplacé ; son absence n'est pas une erreur.
	const std::vector<String> del = {"nmcli", "connection", "delete", "id", config.ProfileName()};
	if (auto res = run(del); res && res.Value().Ok())
		Log(report, del, &res.Value());
	for (const auto &cmd : AdhocCommands(config))
		if (!Step(report, cmd, run))
			return report;
	report.ok = true;
	return report;
}

ConfigureReport LeaveAdhoc(const AdhocConfig &config, const CommandRunner &run) {
	ConfigureReport report;
	const std::vector<String> down = {"nmcli", "connection", "down", "id", config.ProfileName()};
	if (auto res = run(down))
		Log(report, down, &res.Value());
	if (!Step(report, {"nmcli", "connection", "delete", "id", config.ProfileName()}, run))
		return report;
	if (!config.device.IsEmpty()) { // l'interface revient à sa connexion habituelle
		const std::vector<String> reconnect = {"nmcli", "device", "connect", config.device};
		if (auto back = run(reconnect))
			Log(report, reconnect, &back.Value());
	}
	report.ok = true;
	return report;
}

Option<FirewallInfo> DetectFirewall(uint16_t udpPort, uint16_t tcpFirst, uint16_t tcpLast, const CommandRunner &run) {
	const auto active = [&](const char *unit) {
		auto res = run({"systemctl", "is-active", unit});
		return res && res.Value().output.Trim() == "active";
	};
	if (active("ufw"))
		return Some(FirewallInfo{"ufw", {String::Format("sudo ufw allow {}/udp", udpPort), String::Format("sudo ufw allow {}:{}/tcp", tcpFirst, tcpLast)}});
	if (active("firewalld"))
		return Some(FirewallInfo{"firewalld", {String::Format("sudo firewall-cmd --add-port={}/udp --add-port={}-{}/tcp", udpPort, tcpFirst, tcpLast)}});
	return NONE;
}

} // namespace net::wifi
