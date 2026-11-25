#include <cpp-maker/test.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <emu/emu.hpp>

namespace {

/// Processeur factice : chaque « instruction » coûte `cost` cycles et note le temps de la machine.
class FakeCpu : public emu::Device, public emu::Executable {
public:
	FakeCpu(emu::Machine &machine, std::string tag, emu::Device *owner, int cost) : Device(machine, std::move(tag), owner), m_cost(cost) {}
	void Start() override { GetMachine().GetScheduler().AddExecutable(*this); }
	void Reset() override { steps = 0; }
	void Execute() override {
		while (mIcount > 0) {
			++steps;
			mIcount -= m_cost;
			if (onStep)
				onStep();
		}
	}
	void State(emu::StateArchive &archive) override { archive.Io(steps); }
	uint64_t steps = 0;
	std::function<void()> onStep;

private:
	int m_cost;
};

} // namespace

TEST(Emu_StateArchiveRoundTripAndFailure) {
	emu::StateArchive save;
	uint32_t a = 0x12345678;
	uint8_t table[4] = {1, 2, 3, 4};
	std::vector<uint16_t> list = {10, 20, 30};
	std::string text = "état";
	save.Marker(0xABCD);
	save.Io(a);
	save.Io(table);
	save.IoVector(list);
	save.IoString(text);
	CHECK(save.Saving());

	emu::StateArchive load{std::span<const uint8_t>(save.Data())};
	uint32_t a2 = 0;
	uint8_t table2[4] = {};
	std::vector<uint16_t> list2;
	std::string text2;
	load.Marker(0xABCD);
	load.Io(a2);
	load.Io(table2);
	load.IoVector(list2);
	load.IoString(text2);
	CHECK(!load.Failed());
	CHECK_EQ(load.Remaining(), 0u);
	CHECK_EQ(a2, a);
	CHECK_EQ(table2[3], 4);
	CHECK(list2 == list);
	CHECK(text2 == text);

	// Lecture au-delà de la fin : échec, zéros, aucune lecture hors bornes.
	uint64_t beyond = 7;
	load.Io(beyond);
	CHECK(load.Failed());
	CHECK_EQ(beyond, 0u);
	// Marqueur différent : échec.
	emu::StateArchive wrong{std::span<const uint8_t>(save.Data())};
	wrong.Marker(0x1111);
	CHECK(wrong.Failed());
	// Longueur de vecteur aberrante : refusée sans allocation géante.
	const std::array<uint8_t, 8> huge = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F};
	emu::StateArchive bad{std::span<const uint8_t>(huge)};
	std::vector<uint32_t> v;
	bad.IoVector(v);
	CHECK(bad.Failed());
	CHECK(v.empty());
}

TEST(Emu_AddressSpaceDirectHandlersMirrorsBanks) {
	emu::AddressSpace bus("test", 16, 8);
	std::vector<uint8_t> ram(0x2000, 0), rom(0x8000);
	for (size_t i = 0; i < rom.size(); ++i)
		rom[i] = uint8_t(i >> 8);
	bus.MapRom(0x0000, 0x7FFF, rom);
	bus.MapRam(0xC000, 0xDFFF, ram);
	// Non projeté : valeur de bus ouvert, compteurs de diagnostic.
	CHECK_EQ(bus.Read8(0xA000), 0xFF);
	CHECK_EQ(bus.UnmappedReads(), 1u);
	bus.SetUnmappedValue(0x00);
	CHECK_EQ(bus.Read8(0xA000), 0x00);
	// ROM : lecture directe, écriture ignorée.
	CHECK_EQ(bus.Read8(0x1234), 0x12);
	bus.Write8(0x1234, 0x99);
	CHECK_EQ(bus.Read8(0x1234), 0x12);
	CHECK_EQ(bus.UnmappedWrites(), 1u);
	// RAM et accès 16/32 bits petit-boutistes.
	bus.Write32(0xC010, 0xDEADBEEF);
	CHECK_EQ(ram[0x10], 0xEF);
	CHECK_EQ(bus.Read16(0xC012), 0xDEAD);
	CHECK_EQ(bus.Read32(0xC010), 0xDEADBEEFu);
	// Miroir : une mémoire plus petite que la plage est répétée (écho de la RAM).
	bus.MapRam(0xE000, 0xFDFF, ram);
	CHECK_EQ(bus.Read8(0xE010), 0xEF);
	bus.Write8(0xE020, 0x42);
	CHECK_EQ(ram[0x20], 0x42);
	// Gestionnaire d'écriture sur une plage de ROM (contrôleur de banques) : la lecture reste directe.
	int bank = 1;
	bus.MapWrite(0x2000, 0x3FFF, [&](uint32_t, uint8_t value) {
		bank = value & 1;
		bus.MapRom(0x4000, 0x7FFF, std::span<const uint8_t>(rom).subspan(bank ? 0x4000 : 0x0000, 0x4000));
	});
	CHECK_EQ(bus.Read8(0x4100), 0x41);
	bus.Write8(0x2000, 0); // banque 0 projetée en 0x4000
	CHECK_EQ(bus.Read8(0x4100), 0x01);
	CHECK_EQ(bus.Read8(0x2100), 0x21); // la ROM basse est toujours lisible
	// Plages non alignées dans une même page : registres d'E/S puis petite RAM, puis un registre seul.
	std::array<uint8_t, 0x7F> hram{};
	uint8_t lastPort = 0, lastValue = 0;
	bus.MapHandlers(0xFF00, 0xFF7F, [](uint32_t address) { return uint8_t(address & 0xFF); },
					[&](uint32_t address, uint8_t value) {
						lastPort = uint8_t(address);
						lastValue = value;
					});
	bus.MapRam(0xFF80, 0xFFFE, hram);
	bus.MapHandlers(0xFFFF, 0xFFFF, [](uint32_t) { return uint8_t(0x1F); }, [](uint32_t, uint8_t) {});
	CHECK_EQ(bus.Read8(0xFF44), 0x44);
	bus.Write8(0xFF46, 0xC1);
	CHECK_EQ(lastPort, 0x46);
	CHECK_EQ(lastValue, 0xC1);
	bus.Write8(0xFF90, 0x77);
	CHECK_EQ(hram[0x10], 0x77);
	CHECK_EQ(bus.Read8(0xFF90), 0x77);
	CHECK_EQ(bus.Read8(0xFFFF), 0x1F);
	// Retrait d'une plage partielle : elle redevient non projetée, le reste de la page est intact.
	bus.Unmap(0xFF40, 0xFF4F);
	CHECK_EQ(bus.Read8(0xFF44), 0x00);
	CHECK_EQ(bus.Read8(0xFF50), 0x50);
	// Les adresses sont masquées à la largeur du bus.
	CHECK_EQ(bus.Read8(0x1C010), 0xEF);
	// Miroir non contigu avec la taille des pages (mémoire de 0x300 octets sur des pages de 0x100 décalées).
	emu::AddressSpace odd("odd", 16, 8);
	std::vector<uint8_t> small(0x180);
	for (size_t i = 0; i < small.size(); ++i)
		small[i] = uint8_t(i);
	odd.MapRam(0x1000, 0x1FFF, small);
	CHECK_EQ(odd.Read8(0x1000 + 0x17F), 0x7F);
	CHECK_EQ(odd.Read8(0x1000 + 0x180), 0x00); // répétition
	CHECK_EQ(odd.Read8(0x1000 + 0x300 + 5), 5);
	odd.Write8(0x1000 + 0x181, 0xEE);
	CHECK_EQ(small[1], 0xEE);
}

TEST(Emu_SchedulerSlicesTimersAndAbort) {
	emu::Machine machine("test", 1000);
	auto &fast = machine.Root().Add<FakeCpu>("fast", 1);
	auto &slow = machine.Root().Add<FakeCpu>("slow", 3);
	slow.SetClockDivider(4); // un cycle du second processeur = 4 tops
	machine.Start();
	emu::Scheduler &sched = machine.GetScheduler();
	CHECK_EQ(sched.ExecutableCount(), 2u);

	std::vector<emu::Time> fired;
	const emu::TimerId periodic = sched.AllocTimer("periodic", [&] { fired.push_back(sched.Now()); });
	sched.Adjust(periodic, 100, 250);
	CHECK(sched.Armed(periodic));
	CHECK_EQ(sched.Remaining(periodic), 100u);
	machine.Run(1000);
	CHECK_EQ(sched.Now(), 1000u);
	// Échéances exactes : 100, 350, 600, 850.
	CHECK_EQ(fired.size(), 4u);
	CHECK_EQ(fired[0], 100u);
	CHECK_EQ(fired[3], 850u);
	// Le processeur rapide a exécuté un cycle par top ; le lent, 1000/4 cycles par pas de 3
	// (il peut dépasser la fin d'une tranche de moins d'une instruction).
	CHECK_EQ(fast.steps, 1000u);
	CHECK(slow.TotalCycles() >= 250u && slow.TotalCycles() <= 250u + 3u);
	CHECK_EQ(slow.steps, slow.TotalCycles() / 3);

	// Un temporisateur armé pendant l'exécution tombe au cycle demandé (tranche écourtée).
	emu::Time hit = 0;
	const emu::TimerId oneShot = sched.AllocTimer("one-shot", [&] { hit = sched.Now(); });
	sched.Cancel(periodic);
	uint64_t armedAt = 0;
	fast.onStep = [&] {
		if (fast.steps == 1500 && !armedAt) {
			armedAt = sched.Now();
			sched.Adjust(oneShot, 7);
		}
	};
	machine.Run(1000);
	CHECK_EQ(armedAt, 1500u); // temps exact pendant l'exécution
	CHECK_EQ(hit, 1507u);
	CHECK(!sched.Armed(oneShot));
	CHECK_EQ(sched.Remaining(oneShot), emu::kNever);
	fast.onStep = nullptr;

	// Processeur suspendu : il suit le temps sans exécuter.
	const uint64_t before = slow.steps;
	slow.SetSuspended(true);
	machine.Run(400);
	CHECK_EQ(slow.steps, before);
	slow.SetSuspended(false);
	machine.Run(400);
	CHECK(slow.steps > before);

	// Quantum : les processeurs s'entrelacent par tranches bornées.
	sched.SetQuantum(10);
	uint64_t maxLead = 0;
	fast.onStep = [&] { maxLead = std::max<uint64_t>(maxLead, sched.Now() - (sched.Now() / 10) * 10); };
	machine.Run(100);
	CHECK(maxLead < 10);
}

TEST(Emu_MachineDevicesFrameAndSaveState) {
	emu::Machine machine("demo", 60000);
	auto &cpu = machine.Root().Add<FakeCpu>("cpu", 1);
	auto &screen = machine.Root().Add<emu::Screen>("screen", 4, 2);
	auto &sound = machine.Root().Add<emu::SoundStream>("sound", 48000, 64);
	auto &pad = machine.Root().Add<emu::InputPort>("pad");
	auto &cart = machine.Root().Add<emu::Device>("cart");
	auto &ram = cart.Add<emu::Nvram>("ram", 16, 0x00);
	pad.AddField("a", 0x01, "A");
	pad.AddField("start", 0x08);
	machine.Start();

	// Arbre et recherche par chemin.
	CHECK(ram.FullTag() == "demo:cart:ram");
	CHECK(machine.Find<emu::Nvram>("cart:ram") == &ram);
	CHECK(machine.Find<emu::Screen>("cart:ram") == nullptr);
	CHECK(machine.Root().Find("absent") == nullptr);
	CHECK_EQ(machine.Screens().size(), 1u);
	CHECK_EQ(machine.Sounds().size(), 1u);
	CHECK_EQ(machine.Inputs().size(), 1u);

	// Fin d'image : un temporisateur « balayage » dessine et termine l'image toutes les 1000 tops.
	emu::Scheduler &sched = machine.GetScheduler();
	const emu::TimerId vblank = sched.AllocTimer("vblank", [&] {
		screen.Pixels()[0] = emu::Rgb(uint8_t(screen.FrameNumber() + 1), 0, 0);
		screen.EndFrame();
	});
	sched.Adjust(vblank, 1000, 1000);
	machine.SetFrameTicks(1000);
	machine.RunFrame();
	CHECK_EQ(machine.FrameCount(), 1u);
	CHECK_EQ(sched.Now(), 1000u);
	CHECK_EQ(screen.Frame()[0], emu::Rgb(1, 0, 0));
	machine.RunFrame();
	CHECK_EQ(cpu.steps, 2000u);

	// Entrées : champs nommés, lecture active à l'état bas, notification de changement.
	uint32_t changes = 0;
	pad.OnChange([&](uint32_t, uint32_t) { ++changes; });
	CHECK(pad.SetPressed("a", true));
	CHECK(!pad.SetPressed("inconnu", true));
	CHECK_EQ(pad.Read(), 0x01u);
	CHECK_EQ(pad.ReadActiveLow(), 0x08u);
	pad.SetPressed("a", true); // pas de changement : pas de notification
	CHECK_EQ(changes, 1u);

	// Son : file bornée, complétée par la dernière valeur.
	for (int i = 0; i < 100; ++i)
		sound.Push(int16_t(i), int16_t(-i));
	CHECK_EQ(sound.Available(), 64u); // les 36 plus anciennes ont été perdues
	std::array<int16_t, 8> out{};
	CHECK_EQ(sound.Pull(out), 4u);
	CHECK_EQ(out[0], 36);
	CHECK_EQ(out[1], -36);
	sound.Clear();
	CHECK_EQ(sound.Pull(out), 0u);
	CHECK_EQ(sound.TotalFrames(), 100u);

	// État : sauvegarde, modification, restauration à l'identique.
	ram.Data()[3] = 0x5A;
	const std::vector<uint8_t> state = machine.SaveState();
	const uint64_t steps = cpu.steps;
	const emu::Time now = sched.Now();
	machine.RunFrame();
	ram.Data()[3] = 0x00;
	pad.SetPressed("a", false);
	CHECK(machine.LoadState(state));
	CHECK_EQ(cpu.steps, steps);
	CHECK_EQ(sched.Now(), now);
	CHECK_EQ(ram.Data()[3], 0x5A);
	CHECK_EQ(pad.Read(), 0x01u);
	CHECK_EQ(machine.FrameCount(), 2u);
	CHECK_EQ(sched.Remaining(vblank), 1000u);
	// Déterminisme : rejouer depuis l'état donne le même résultat.
	machine.RunFrame();
	const uint64_t after = cpu.steps;
	CHECK(machine.LoadState(state));
	machine.RunFrame();
	CHECK_EQ(cpu.steps, after);

	// États refusés : tronqué, autre machine, autre version — la machine n'est pas modifiée.
	const uint64_t kept = cpu.steps;
	std::vector<uint8_t> truncated(state.begin(), state.begin() + std::ptrdiff_t(state.size() / 2));
	CHECK(!machine.LoadState(truncated));
	CHECK(!machine.LoadState({}));
	CHECK_EQ(cpu.steps, kept);
	emu::Machine other("autre", 60000);
	other.Root().Add<FakeCpu>("cpu", 1);
	other.Start();
	CHECK(!other.LoadState(state));
	machine.SetStateVersion(2);
	CHECK(!machine.LoadState(state));

	// Réinitialisation.
	machine.Reset();
	CHECK_EQ(cpu.steps, 0u);
	CHECK_EQ(sched.Now(), 0u);
	CHECK(!sched.Armed(vblank));
}

TEST(Emu_NvramFileAndRegistry) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "emu_test_nvram";
	std::filesystem::remove_all(dir);
	const std::filesystem::path file = dir / "jeu.sav";
	{
		emu::Machine machine("m", 1000);
		auto &ram = machine.Root().Add<emu::Nvram>("ram", 8, 0xFF);
		machine.Start();
		CHECK(!ram.SetPath(file)); // pas encore de fichier
		CHECK(ram.Flush());		   // rien à écrire
		CHECK(!std::filesystem::exists(file));
		ram.Data()[2] = 0x12;
		ram.MarkDirty();
		// La destruction de la machine (Stop) enregistre la mémoire modifiée.
	}
	CHECK(std::filesystem::exists(file));
	{
		emu::Machine machine("m", 1000);
		auto &ram = machine.Root().Add<emu::Nvram>("ram", 8, 0x00);
		machine.Start();
		CHECK(ram.SetPath(file));
		CHECK_EQ(ram.Data()[2], 0x12);
		CHECK_EQ(ram.Data()[0], 0xFF);
		CHECK(!ram.Dirty());
	}
	std::filesystem::remove_all(dir);

	emu::SystemRegistry registry;
	emu::SystemInfo info;
	info.name = "gbc";
	info.description = "Game Boy Color";
	info.extensions = {".gb", ".gbc"};
	info.create = [](const emu::Media &media) -> emu::MachineResult {
		if (media.rom.empty())
			return Err(std::string("ROM vide"));
		return Ok(std::make_unique<emu::Machine>("gbc", 4194304));
	};
	CHECK(registry.Register(info));
	CHECK(!registry.Register(info)); // doublon
	CHECK(registry.Find("gbc") != nullptr);
	CHECK(registry.Find("nes") == nullptr);
	CHECK(registry.FindByExtension(".GBC") == registry.Find("gbc"));
	CHECK(registry.FindByExtension("gb") == registry.Find("gbc"));
	CHECK(registry.FindByExtension(".nds") == nullptr);
	emu::Media media;
	CHECK(registry.Find("gbc")->create(media).IsError());
	media.rom = {1, 2, 3};
	media.romPath = "/roms/Mon Jeu.gbc";
	media.options["model"] = "cgb";
	CHECK(registry.Find("gbc")->create(media).IsOk());
	CHECK(media.SavePath() == std::filesystem::path("/roms/Mon Jeu.sav"));
	media.saveDirectory = "/saves";
	CHECK(media.SavePath(".rtc") == std::filesystem::path("/saves/Mon Jeu.rtc"));
	CHECK(media.Option("model") == "cgb");
	CHECK(media.Option("absent", "défaut") == "défaut");
}

TEST(Emu_FrameEndWithoutTimersStopsTheSlice) {
	// Un processeur qui termine lui-même l'image (sans temporisateur) : une image par RunFrame.
	class FrameCpu : public emu::Device, public emu::Executable {
	public:
		FrameCpu(emu::Machine &machine, std::string tag, emu::Device *owner, emu::Screen &screen)
			: Device(machine, std::move(tag), owner), m_screen(screen) {}
		void Start() override { GetMachine().GetScheduler().AddExecutable(*this); }
		void Execute() override {
			while (mIcount > 0) {
				mIcount -= 1000; // une « instruction » = une image entière
				++frames;
				m_screen.EndFrame();
			}
		}
		int frames = 0;

	private:
		emu::Screen &m_screen;
	};
	emu::Machine machine("frames", 60000);
	auto &screen = machine.Root().Add<emu::Screen>("screen", 2, 2);
	auto &cpu = machine.Root().Add<FrameCpu>("cpu", screen);
	machine.SetFrameTicks(1000);
	machine.Start();
	for (int i = 0; i < 5; ++i)
		machine.RunFrame();
	CHECK_EQ(cpu.frames, 5);
	CHECK_EQ(machine.FrameCount(), 5u);
	CHECK_EQ(machine.GetScheduler().Now(), 5000u);
}
