#include "emu/machine.hpp"

#include <algorithm>

#include "emu/debug.hpp"
#include "emu/input.hpp"
#include "emu/screen.hpp"
#include "emu/sound.hpp"
#include "emu/state.hpp"

namespace emu {

// ── Device ──────────────────────────────────────────────────────────────────

Device::Device(Machine &machine, std::string tag, Device *owner) : m_machine(machine), m_tag(std::move(tag)), m_owner(owner) {}
Device::~Device() = default;

std::string Device::FullTag() const {
	if (!m_owner)
		return m_tag;
	return m_owner->FullTag() + ":" + m_tag;
}

Device *Device::Find(std::string_view path) const {
	const size_t colon = path.find(':');
	const std::string_view head = path.substr(0, colon);
	for (const auto &child : m_children)
		if (child->m_tag == head)
			return colon == std::string_view::npos ? child.get() : child->Find(path.substr(colon + 1));
	return nullptr;
}

// ── Machine ─────────────────────────────────────────────────────────────────

namespace {
constexpr uint32_t kStateMagic = 0x53554D45; // « EMUS »

void Visit(Device &device, const std::function<void(Device &)> &visit) {
	visit(device);
	for (const auto &child : device.Children())
		Visit(*child, visit);
}
void VisitReverse(Device &device, const std::function<void(Device &)> &visit) {
	const auto children = device.Children();
	for (auto it = children.rbegin(); it != children.rend(); ++it)
		VisitReverse(**it, visit);
	visit(device);
}
} // namespace

Machine::Machine(std::string name, uint64_t masterClock)
	: m_name(std::move(name)), m_masterClock(masterClock ? masterClock : 1), m_frameTicks(m_masterClock / 60 + 1) {
	m_root = std::make_unique<Device>(*this, m_name, nullptr);
}

Machine::~Machine() {
	if (m_started)
		VisitReverse(*m_root, [](Device &d) { d.Stop(); });
}

void Machine::ForEachDevice(const std::function<void(Device &)> &visit) { Visit(*m_root, visit); }

void Machine::Start() {
	if (m_started)
		return;
	m_started = true;
	ForEachDevice([this](Device &d) {
		if (auto *screen = dynamic_cast<Screen *>(&d))
			m_screens.push_back(screen);
		if (auto *sound = dynamic_cast<SoundStream *>(&d))
			m_sounds.push_back(sound);
		if (auto *input = dynamic_cast<InputPort *>(&d))
			m_inputs.push_back(input);
		if (auto *touch = dynamic_cast<TouchPanel *>(&d))
			m_touch.push_back(touch);
		if (!m_debugger)
			m_debugger = dynamic_cast<Debuggable *>(&d);
		d.Start();
	});
	Reset();
}

void Machine::Reset() {
	m_scheduler.Reset();
	m_frames = 0;
	ForEachDevice([](Device &d) { d.Reset(); });
}

void Machine::SignalFrame() {
	m_frameDone = true;
	++m_frames;
	m_scheduler.RequestExit();
}

void Machine::RunFrame() {
	if (!m_started)
		Start();
	m_frameDone = false;
	const Time limit = m_scheduler.Now() + m_frameTicks * 2;
	while (!m_frameDone && m_scheduler.Now() < limit)
		m_scheduler.RunUntil(limit);
}

void Machine::Run(Time ticks) {
	if (!m_started)
		Start();
	const Time target = m_scheduler.Now() + ticks;
	while (m_scheduler.Now() < target)
		m_scheduler.RunUntil(target); // les fins d'image n'interrompent pas une durée demandée
}

void Machine::StepInstruction() {
	if (!m_started)
		Start();
	const uint64_t before = m_scheduler.PrimaryCycles();
	// Borne : un processeur arrêté (HALT) peut attendre longtemps sa prochaine interruption.
	for (Time i = 0; i < m_frameTicks * 2 && m_scheduler.PrimaryCycles() == before; ++i)
		m_scheduler.RunUntil(m_scheduler.Now() + 1);
}

// Format : magie, version, nom, images, bloc de l'ordonnanceur, puis par périphérique
// (parcours en profondeur) son chemin et son bloc, tous deux préfixés de leur longueur.
std::vector<uint8_t> Machine::SaveState() {
	if (!m_started)
		Start();
	StateArchive archive;
	archive.Marker(kStateMagic);
	archive.Io(m_stateVersion);
	std::string name = m_name;
	archive.IoString(name);
	archive.Io(m_frames);
	{
		StateArchive block;
		m_scheduler.State(block);
		std::vector<uint8_t> bytes = block.TakeData();
		archive.IoVector(bytes);
	}
	ForEachDevice([&](Device &d) {
		StateArchive block;
		d.State(block);
		std::string tag = d.FullTag();
		archive.IoString(tag);
		std::vector<uint8_t> bytes = block.TakeData();
		archive.IoVector(bytes);
	});
	return archive.TakeData();
}

bool Machine::LoadState(std::span<const uint8_t> data) {
	if (!m_started)
		Start();
	// Première passe : tout lire et vérifier la structure, sans rien modifier.
	StateArchive archive(data);
	archive.Marker(kStateMagic);
	uint32_t version = 0;
	archive.Io(version);
	std::string name;
	archive.IoString(name);
	uint64_t frames = 0;
	archive.Io(frames);
	std::vector<uint8_t> schedulerBytes;
	archive.IoVector(schedulerBytes);
	if (archive.Failed() || version != m_stateVersion || name != m_name)
		return false;
	struct Block {
		Device *device;
		std::vector<uint8_t> bytes;
	};
	std::vector<Block> blocks;
	bool ok = true;
	ForEachDevice([&](Device &d) {
		std::string tag;
		archive.IoString(tag);
		std::vector<uint8_t> bytes;
		archive.IoVector(bytes);
		if (archive.Failed() || tag != d.FullTag())
			ok = false;
		blocks.push_back({&d, std::move(bytes)});
	});
	if (!ok || archive.Failed() || archive.Remaining() != 0)
		return false;
	// Seconde passe : application ; chaque bloc doit être consommé exactement.
	StateArchive schedulerBlock{std::span<const uint8_t>(schedulerBytes)};
	m_scheduler.State(schedulerBlock);
	ok = !schedulerBlock.Failed() && schedulerBlock.Remaining() == 0;
	for (Block &b : blocks) {
		if (!ok)
			break;
		StateArchive block{std::span<const uint8_t>(b.bytes)};
		b.device->State(block);
		ok = !block.Failed() && block.Remaining() == 0;
	}
	if (!ok) {
		Reset(); // état partiellement appliqué : on repart d'un état sain
		return false;
	}
	m_frames = frames;
	return true;
}

} // namespace emu
