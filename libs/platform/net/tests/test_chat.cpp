// Tests de net::chat : trames, balises, noms de fichiers, puis deux nœuds réels en
// boucle locale (identification, chat, fichier avec CRC, annulation, départ).
#include <core/core.hpp>
#include <cpp-maker/test.hpp>
#include <net/chat.hpp>
#include <sdl3/sdl3.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

namespace fs = std::filesystem;
using namespace net;

namespace {

[[nodiscard]] fs::path TempDir(const char* name) {
	auto dir = fs::temp_directory_path() / (std::string("adhoc_chat_test_") + name);
	fs::remove_all(dir);
	fs::create_directories(dir);
	return dir;
}

/// Fait tourner les deux nœuds jusqu'à `done` (ou ~4 s).
template <typename Pred>
bool PumpUntil(chat::ChatNode& a, chat::ChatNode& b, uint64_t& clock, Pred done) {
	for (int i = 0; i < 400; ++i) {
		clock += 10;
		a.Poll(clock);
		b.Poll(clock);
		if (done())
			return true;
		SDL_Delay(10);
	}
	return false;
}

[[nodiscard]] const chat::PeerInfo* PeerById(const std::vector<chat::PeerInfo>& peers,
											 uint64_t id) {
	for (auto& p : peers)
		if (p.id == id)
			return &p;
	return nullptr;
}

} // namespace

TEST(Chat, FramesSurviveArbitrarySplits) {
	std::vector<uint8_t> stream;
	std::vector<uint8_t> big(100000);
	for (size_t i = 0; i < big.size(); ++i)
		big[i] = uint8_t(i * 7);
	for (auto& f :
		 {chat::EncodeFrame(chat::FrameType::CHAT, std::vector<uint8_t>{'s', 'a', 'l', 'u', 't'}),
		  chat::EncodeFrame(chat::FrameType::FILE_CHUNK, big),
		  chat::EncodeFrame(chat::FrameType::BYE, {})})
		stream.insert(stream.end(), f.begin(), f.end());

	chat::FrameDecoder dec;
	std::vector<chat::Frame> frames;
	size_t step = 1;
	for (size_t pos = 0; pos < stream.size(); pos += step, step = step * 3 % 977 + 1) {
		size_t n = std::min(step, stream.size() - pos);
		dec.Feed({stream.data() + pos, n});
		while (auto f = dec.Next())
			frames.push_back(std::move(*f));
	}
	ASSERT_EQ(frames.size(), size_t(3));
	EXPECT_TRUE(frames[0].type == chat::FrameType::CHAT);
	EXPECT_EQ(frames[0].payload.size(), size_t(5));
	EXPECT_TRUE(frames[1].payload == big);
	EXPECT_TRUE(frames[2].type == chat::FrameType::BYE);
	EXPECT_FALSE(dec.Corrupted());

	chat::FrameDecoder bad;
	std::vector<uint8_t> garbage = {'G', 'E', 'T', ' ', '/', ' ', 'H'};
	bad.Feed(garbage);
	EXPECT_TRUE(bad.Next().IsNone());
	EXPECT_TRUE(bad.Corrupted());
	std::cout << "FrameDecoder (découpage arbitraire, flux invalide): ok\n";
}

TEST(Chat, BeaconsAndNames) {
	chat::Beacon b{chat::BeaconKind::PROBE, 0x1122334455667788ull, 48621, "Élodie"};
	auto bytes = chat::EncodeBeacon(b);
	auto back = chat::DecodeBeacon(bytes);
	ASSERT_TRUE(back.IsSome());
	EXPECT_TRUE(back->kind == chat::BeaconKind::PROBE);
	EXPECT_EQ(back->nodeId, 0x1122334455667788ull);
	EXPECT_EQ(back->tcpPort, uint16_t(48621));
	EXPECT_TRUE(back->nickname == "Élodie");
	bytes[0] = 'X';
	EXPECT_TRUE(chat::DecodeBeacon(bytes).IsNone());
	bytes = chat::EncodeBeacon(b);
	bytes.resize(bytes.size() - 2); // pseudo tronqué
	EXPECT_TRUE(chat::DecodeBeacon(bytes).IsNone());

	EXPECT_TRUE(*chat::SubnetBroadcast("169.254.3.7") == "169.254.255.255");
	EXPECT_TRUE(*chat::SubnetBroadcast("10.42.0.2") == "10.42.0.255");
	EXPECT_TRUE(chat::SubnetBroadcast("127.0.0.1").IsNone());
	EXPECT_TRUE(chat::SubnetBroadcast("fe80::1").IsNone());

	EXPECT_TRUE(chat::SanitizeFileName("../../etc/passwd") == "passwd");
	EXPECT_TRUE(chat::SanitizeFileName("C:\\Users\\a\\photo.jpg") == "photo.jpg");
	EXPECT_TRUE(chat::SanitizeFileName("..") == "fichier");
	EXPECT_TRUE(chat::SanitizeFileName("a:b?.txt") == "a_b_.txt");

	auto dir = TempDir("names");
	std::ofstream(dir / "note.txt") << "x";
	EXPECT_TRUE(chat::FreePath(String(dir.string()), "note.txt") ==
				String((dir / "note (2).txt").string()));
	EXPECT_TRUE(chat::FreePath(String(dir.string()), "autre.txt") ==
				String((dir / "autre.txt").string()));
	fs::remove_all(dir);
	std::cout << "Balises / SubnetBroadcast / SanitizeFileName / FreePath: ok\n";
}

// ── Deux nœuds en boucle locale ──────────────────────────────────────────────

TEST(Chat, TwoNodesChatAndFile) {
	auto netCtx = sdl3::NetContext::Create();
	ASSERT_TRUE(netCtx.IsOk());
	auto dirA = TempDir("a");
	auto dirB = TempDir("b");

	// Pas de diffusion : le test ne dépend pas du réseau de la machine. B
	// connaît A par son adresse ; A apprend B par la trame HELLO.
	chat::NodeConfig ca{.nickname = "Alice",
						.tcpPort = 48731,
						.downloadDir = String(dirA.string()),
						.broadcast = false};
	chat::NodeConfig cb{.nickname = "Bob",
						.tcpPort = 48731,
						.downloadDir = String(dirB.string()),
						.broadcast = false};
	auto na = chat::ChatNode::Start(ca);
	ASSERT_TRUE(na.IsOk());
	auto nb = chat::ChatNode::Start(cb);
	ASSERT_TRUE(nb.IsOk());
	auto& a = *na.Value();
	auto& b = *nb.Value();
	EXPECT_TRUE(b.TcpPort() != a.TcpPort()); // port suivant pris automatiquement

	EXPECT_TRUE(b.AddManualPeer("127.0.0.1", a.TcpPort()).IsNone());
	EXPECT_TRUE(b.AddManualPeer("127.0.0.1", a.TcpPort()).IsSome()); // doublon refusé
	uint64_t clock = 1000;
	bool linked = PumpUntil(a, b, clock, [&] {
		auto pa = a.Peers();
		auto pb = b.Peers();
		auto* bobSeenByA = PeerById(pa, b.Id());
		auto* aliceSeenByB = PeerById(pb, a.Id());
		return bobSeenByA && bobSeenByA->connected && aliceSeenByB && aliceSeenByB->connected;
	});
	ASSERT_TRUE(linked);
	EXPECT_EQ(b.Peers().size(), size_t(1)); // le pair manuel a été identifié, pas dupliqué
	EXPECT_TRUE(PeerById(b.Peers(), a.Id())->nickname == "Alice");

	// Chat dans les deux sens.
	a.SendChat("Bonjour Bob");
	b.SendChat("Salut Alice ✓");
	auto lastText = [](chat::ChatNode& n) {
		for (auto it = n.Messages().rbegin(); it != n.Messages().rend(); ++it)
			if (!it->system && !it->outgoing)
				return it->text;
		return String();
	};
	ASSERT_TRUE(PumpUntil(a, b, clock, [&] {
		return lastText(a) == "Salut Alice ✓" && lastText(b) == "Bonjour Bob";
	}));

	// Renommage propagé.
	a.SetNickname("Alice B.");
	ASSERT_TRUE(PumpUntil(a, b, clock,
						  [&] { return PeerById(b.Peers(), a.Id())->nickname == "Alice B."; }));

	// Fichier de 1 Mo (plusieurs fenêtres d'envoi), vérifié octet par octet.
	auto src = dirA / "données.bin";
	std::vector<char> content(1024 * 1024 + 123);
	for (size_t i = 0; i < content.size(); ++i)
		content[i] = char((i * 2654435761u) >> 13);
	std::ofstream(src, std::ios::binary).write(content.data(), std::streamsize(content.size()));
	auto sent = a.SendFile(b.Id(), String(src.string()));
	ASSERT_TRUE(sent.IsOk());
	auto findT = [](chat::ChatNode& n, bool outgoing) -> const chat::TransferInfo* {
		for (auto& t : n.Transfers())
			if (t.outgoing == outgoing)
				return &t;
		return nullptr;
	};
	ASSERT_TRUE(PumpUntil(a, b, clock, [&] {
		auto* in = findT(b, false);
		return in && in->state != chat::TransferState::RUNNING;
	}));
	auto* in = findT(b, false);
	EXPECT_TRUE(in->state == chat::TransferState::DONE);
	EXPECT_TRUE(findT(a, true)->state == chat::TransferState::DONE);
	EXPECT_EQ(in->done, uint64_t(content.size()));
	EXPECT_TRUE(in->fileName == "données.bin");
	std::ifstream got(in->path.CStr(), std::ios::binary);
	std::vector<char> received((std::istreambuf_iterator<char>(got)),
							   std::istreambuf_iterator<char>());
	EXPECT_TRUE(received == content);
	EXPECT_FALSE(fs::exists(dirB / "données.bin.part"));

	// Second envoi du même nom : pas d'écrasement.
	ASSERT_TRUE(a.SendFile(b.Id(), String(src.string())).IsOk());
	ASSERT_TRUE(PumpUntil(a, b, clock, [&] {
		return b.Transfers().size() == 2 && b.Transfers()[1].state == chat::TransferState::DONE;
	}));
	EXPECT_TRUE(fs::exists(dirB / "données (2).bin"));

	// Annulation par le destinataire : l'expéditeur s'arrête aussi.
	b.ClearFinishedTransfers();
	a.ClearFinishedTransfers();
	EXPECT_TRUE(b.Transfers().empty());
	std::vector<char> huge(16 * 1024 * 1024, 'z');
	auto bigSrc = dirA / "gros.bin";
	std::ofstream(bigSrc, std::ios::binary).write(huge.data(), std::streamsize(huge.size()));
	ASSERT_TRUE(a.SendFile(b.Id(), String(bigSrc.string())).IsOk());
	ASSERT_TRUE(PumpUntil(a, b, clock,
						  [&] { return !b.Transfers().empty() && b.Transfers()[0].done > 0; }));
	b.CancelTransfer(b.Transfers()[0].id);
	ASSERT_TRUE(PumpUntil(a, b, clock,
						  [&] { return a.Transfers()[0].state != chat::TransferState::RUNNING; }));
	EXPECT_TRUE(a.Transfers()[0].state == chat::TransferState::CANCELLED);
	EXPECT_TRUE(b.Transfers()[0].state == chat::TransferState::CANCELLED);
	EXPECT_FALSE(fs::exists(dirB / "gros.bin.part"));
	EXPECT_FALSE(fs::exists(dirB / "gros.bin"));

	// Départ d'Alice (destruction du nœud : trame BYE) : Bob la retire de
	// ses pairs. `a` n'est plus valable, on ne fait plus tourner que Bob.
	na.Value().reset();
	uint64_t t = clock;
	bool gone = false;
	for (int i = 0; i < 300 && !gone; ++i) {
		t += 10;
		b.Poll(t);
		gone = b.Peers().empty();
		SDL_Delay(10);
	}
	EXPECT_TRUE(gone);
	fs::remove_all(dirA);
	fs::remove_all(dirB);
	std::cout << "Deux nœuds : identification, chat, renommage, fichier 1 Mo + CRC, annulation, "
				 "départ: ok\n";
}

TEST(Chat, ManualPeerPointingToItselfIsDropped) {
	auto netCtx = sdl3::NetContext::Create();
	ASSERT_TRUE(netCtx.IsOk());
	auto dir = TempDir("self");
	chat::NodeConfig c{.nickname = "Seul",
					   .tcpPort = 48771,
					   .downloadDir = String(dir.string()),
					   .broadcast = false};
	auto n = chat::ChatNode::Start(c);
	ASSERT_TRUE(n.IsOk());
	auto& node = *n.Value();
	EXPECT_TRUE(node.AddManualPeer("127.0.0.1", node.TcpPort()).IsNone());
	EXPECT_EQ(node.Peers().size(), size_t(1));
	bool dropped = false;
	uint64_t clock = 1000;
	for (int i = 0; i < 300 && !dropped; ++i) {
		clock += 10;
		node.Poll(clock);
		dropped = node.Peers().empty();
		SDL_Delay(10);
	}
	EXPECT_TRUE(dropped);
	EXPECT_TRUE(!node.Messages().empty() && node.Messages().back().text.Contains("lui-même"));
	n.Value().reset();
	fs::remove_all(dir);
	std::cout << "Pair manuel désignant ce nœud lui-même : retiré: ok\n";
}
