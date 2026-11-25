// Tests de net::wifi : analyse des sorties de nmcli, commandes générées et configuration
// avec un exécutant simulé (aucun nmcli lancé), puis sdl3::Process::Run réel (sh).
#include <core/core.hpp>
#include <cpp-maker/test.hpp>
#include <net/wifi.hpp>
#include <sdl3/core/sdl_context.hpp>

#include <algorithm>
#include <map>

using namespace net::wifi;

namespace {

/// Exécutant simulé : réponses par préfixe de commande, journal des appels.
struct FakeRunner {
	std::map<String, CommandResult> replies; // clé : arguments joints par des espaces
	std::vector<String> calls;

	CommandRunner Runner() {
		return [this](const std::vector<String> &argv) -> Result<CommandResult, String> {
			String line;
			for (const String &a : argv)
				line.Append(line.IsEmpty() ? "" : " ").Append(a);
			calls.push_back(line);
			for (const auto &[prefix, reply] : replies)
				if (line.StartsWith(prefix))
					return Ok(reply);
			return Ok(CommandResult{0, {}});
		};
	}
};

bool HasArgPair(const std::vector<String> &argv, std::string_view key, std::string_view value) {
	for (size_t i = 0; i + 1 < argv.size(); ++i)
		if (argv[i] == key && argv[i + 1] == value)
			return true;
	return false;
}

} // namespace

TEST(Wifi, TerseLinesAndDevices) {
	const auto f = SplitTerseLine(R"(*:AA\:BB\:CC:Mon\\Réseau:Ad-Hoc)");
	REQUIRE(f.size() == 4);
	CHECK(f[1] == "AA:BB:CC");
	CHECK(f[2] == "Mon\\Réseau");
	const auto devices = ParseDevices("wlp2s0:wifi:connected:Maison\nenp3s0:ethernet:connected:Filaire\nlo:loopback:unmanaged:\n");
	REQUIRE(devices.size() == 1);
	CHECK(devices[0].name == "wlp2s0");
	CHECK(devices[0].connection == "Maison");
}

TEST(Wifi, NetworksSortedAdhocFirst) {
	const auto nets = ParseNetworks("*:11\\:22\\:33\\:44\\:55\\:66:Maison:Infra:6:2437 MHz:130 Mbit/s:82:WPA2\n"
									" :AA\\:BB\\:CC\\:DD\\:EE\\:FF:adhoc-chat:Ad-Hoc:11::54 Mbit/s:40:--\n"
									" :01\\:02\\:03\\:04\\:05\\:06:Voisin:Infra:1:2412 MHz:65 Mbit/s:90:WPA1 WPA2\n");
	REQUIRE(nets.size() == 3);
	CHECK(nets[0].IsAdhoc());
	CHECK(nets[0].IsOpen());
	CHECK_EQ(nets[0].frequencyMHz, 2462); // déduite du canal 11
	CHECK(nets[1].ssid == "Voisin");	   // puis par signal décroissant
	CHECK(nets[2].inUse);
	CHECK(nets[2].bssid == "11:22:33:44:55:66");
}

TEST(Wifi, CapabilitiesAddressesPermissions) {
	const WifiCapabilities caps = ParseCapabilities("WIFI-PROPERTIES.ADHOC:yes\nWIFI-PROPERTIES.IBSS-RSN:no\nWIFI-PROPERTIES.2GHZ:yes\nWIFI-PROPERTIES.5GHZ:yes\n");
	CHECK(caps.adhoc && !caps.ibssRsn && caps.band2GHz && caps.band5GHz);
	const auto ips = ParseIpv4Addresses("IP4.ADDRESS[1]:192.168.0.32/24\nIP4.ADDRESS[2]:169.254.10.3/16\nIP4.GATEWAY:192.168.0.1\n");
	REQUIRE(ips.size() == 2);
	CHECK(ips[1] == "169.254.10.3/16");
	CHECK(IsPermissionError("Error: Not authorized to control networking."));
	CHECK(!IsPermissionError("Connection successfully activated"));
	CHECK_EQ(ChannelToFrequencyMHz(1), 2412);
	CHECK_EQ(ChannelToFrequencyMHz(14), 2484);
	CHECK_EQ(ChannelToFrequencyMHz(36), 5180);
}

TEST(Wifi, ConfigValidationAndCommands) {
	AdhocConfig cfg;
	CHECK(!cfg.Validate());
	CHECK(cfg.ProfileName() == "adhoc-adhoc-chat");
	cfg.channel = 20;
	CHECK(cfg.Validate().IsSome());
	cfg.channel = 3;
	cfg.password = "court";
	CHECK(cfg.Validate().IsSome());
	cfg.password = "motdepasse";
	cfg.ipv4 = Ipv4Method::Manual;
	cfg.address = "10.42.0.300/24";
	CHECK(cfg.Validate().IsSome());
	cfg.address = "10.42.0.2/24";
	CHECK(!cfg.Validate());
	cfg.device = "wlan1";
	const auto cmds = AdhocCommands(cfg);
	REQUIRE(cmds.size() == 2);
	CHECK(HasArgPair(cmds[0], "ifname", "wlan1"));
	CHECK(HasArgPair(cmds[0], "mode", "adhoc"));
	CHECK(HasArgPair(cmds[0], "wifi.channel", "3"));
	CHECK(HasArgPair(cmds[0], "ipv4.addresses", "10.42.0.2/24"));
	CHECK(HasArgPair(cmds[0], "wifi-sec.psk", "motdepasse"));
	const String shown = CommandLine(cmds[0]);
	CHECK(!shown.Contains("motdepasse")); // mot de passe masqué
	CHECK(shown.Contains("********"));
	CHECK(CommandLine({"echo", "deux mots", "l'apostrophe"}) == "echo 'deux mots' 'l'\\''apostrophe'");
	CHECK(ManualInstructions(cfg).Contains("sudo iw dev wlan1 ibss join adhoc-chat 2422"));
}

TEST(Wifi, JoinAndLeaveWithFakeRunner) {
	FakeRunner fake;
	AdhocConfig cfg;
	cfg.device = "wlan0";
	ConfigureReport ok = JoinAdhoc(cfg, fake.Runner());
	CHECK(ok.ok);
	REQUIRE(fake.calls.size() == 3); // delete (ignoré s'il échoue), add, up
	CHECK(fake.calls[0].StartsWith("nmcli connection delete"));
	CHECK(fake.calls[2].Contains("connection up id adhoc-adhoc-chat"));

	FakeRunner denied;
	denied.replies["nmcli connection add"] = {4, "Error: Not authorized to control networking."};
	const ConfigureReport refused = JoinAdhoc(cfg, denied.Runner());
	CHECK(!refused.ok);
	CHECK(refused.permissionDenied);
	CHECK(refused.error.Contains("Not authorized"));
	CHECK(std::ranges::any_of(refused.log, [](const String &l) { return l.StartsWith("$ nmcli connection add"); }));

	FakeRunner leave;
	CHECK(LeaveAdhoc(cfg, leave.Runner()).ok);
	CHECK(leave.calls.back() == "nmcli device connect wlan0");

	FakeRunner scan;
	scan.replies["nmcli -t -e yes -f IN-USE"] = {0, " :AA\\:BB\\:CC\\:DD\\:EE\\:FF:cellule:Ad-Hoc:6:2437 MHz:54 Mbit/s:70:--\n"};
	auto nets = Scan("wlan0", false, scan.Runner());
	REQUIRE(nets.IsOk());
	REQUIRE(nets.Value().size() == 1);
	CHECK(scan.calls[0].EndsWith("--rescan no ifname wlan0"));

	FakeRunner firewall;
	firewall.replies["systemctl is-active ufw"] = {0, "active\n"};
	const auto fw = DetectFirewall(48620, 48621, 48630, firewall.Runner());
	REQUIRE(fw.IsSome());
	CHECK(fw->name == "ufw");
	CHECK(fw->commands[1] == "sudo ufw allow 48621:48630/tcp");
}

TEST(Wifi, RealProcessWithEnvironment) {
	// sdl3::Process::Run : environnement modifié (LC_ALL=C), stderr mêlé à stdout, code de sortie.
	auto res = RunCommand({"sh", "-c", "echo \"$LC_ALL\"; echo erreur >&2; exit 3"});
	REQUIRE(res.IsOk());
	CHECK_EQ(res.Value().exitCode, 3);
	CHECK(res.Value().output.Contains("C\n"));
	CHECK(res.Value().output.Contains("erreur"));
	CHECK(RunCommand({"programme-introuvable-xyz"}).IsError() || RunCommand({"programme-introuvable-xyz"}).Value().exitCode != 0);
}
