// Tests de foundation.pdf : métriques des polices standard et encodage WinAnsi, analyseur
// d'objets, filtres, écriture (structure, références croisées, coordonnées, transparence,
// images, liens, signets), relecture complète, PDF 1.5 à flux de références et flux d'objets
// construit à la main, mises à jour incrémentales, réparation, et — si poppler est installé —
// validation par une implémentation indépendante (pdfinfo, pdftotext).
#include <cpp-maker/test.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include <archive/archive_deflate.hpp>
#include <pdf/pdf.hpp>

using namespace pdf;

namespace {

std::string Str(const Bytes &b) { return std::string(b.begin(), b.end()); }
Bytes ToBytes(std::string_view s) { return Bytes(s.begin(), s.end()); }

/// Document de référence : deux pages (A4 portrait, A5 paysage), texte, image, lien, signets.
Writer Sample() {
	Writer w;
	DocumentInfo info;
	info.title = "Rapport d'été";
	info.author = "Équipe";
	info.creator = "tests";
	w.SetInfo(info);
	Canvas &a4 = w.AddPage(595.28, 841.89);
	a4.FillRect(72, 72, 200, 40, math::Color(230, 240, 255));
	a4.Text(72, 100, StandardFont::Helvetica, 12, "Bonjour", math::Color(0, 0, 0));
	a4.Text(72 + TextWidth(StandardFont::Helvetica, "Bonjour ", 12), 100, StandardFont::Helvetica, 12, "le", math::Color(0, 0, 0));
	a4.Text(72, 130, StandardFont::TimesBold, 14, "Deuxième ligne", math::Color(200, 0, 0, 128));
	a4.Link(72, 88, 100, 16, "https://example.org/a(b)");
	Canvas &a5 = w.AddPage(595.28, 419.53);
	a5.Ellipse(100, 100, 80, 40);
	a5.Fill();
	a5.Text(50, 50, StandardFont::Courier, 10, "Page 2", math::Color(0, 0, 0));
	const uint8_t rgba[16] = {255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 255, 255, 255, 255};
	auto image = w.AddImage(2, 2, rgba);
	if (image)
		a5.DrawImage(image.Value(), 10, 10, 20, 20);
	w.AddBookmark("Introduction", 0, 72);
	w.AddBookmark("Détail", 0, 120, 1);
	w.AddBookmark("Annexe", 1, 0);
	return w;
}

std::filesystem::path TempFile(std::string_view name) {
	return std::filesystem::temp_directory_path() / std::format("apps-lib-pdf-{}", name);
}

bool HasTool(const char *tool) { return std::system(std::format("command -v {} > /dev/null 2>&1", tool).c_str()) == 0; }

} // namespace

// ── Polices ──

TEST(Fonts, AfmWidthsAndMetrics) {
	CHECK_EQ(Metrics(StandardFont::Helvetica).widths[size_t('A' - 32)], uint16_t(667));
	CHECK_EQ(Metrics(StandardFont::HelveticaBold).widths[size_t('a' - 32)], uint16_t(556));
	CHECK_EQ(Metrics(StandardFont::TimesRoman).widths[size_t('W' - 32)], uint16_t(944));
	CHECK_EQ(Metrics(StandardFont::TimesItalic).widths[size_t('A' - 32)], uint16_t(611));
	CHECK_EQ(Metrics(StandardFont::Courier).widths[size_t('i' - 32)], uint16_t(600));
	CHECK(Metrics(StandardFont::TimesBoldItalic).baseName == "Times-BoldItalic");
	CHECK_NEAR(TextWidth(StandardFont::Helvetica, "Hello", 10), (722 + 556 + 222 + 222 + 556) / 100.0, 1e-9);
	// Lettres accentuées : largeur de la lettre de base.
	CHECK_NEAR(TextWidth(StandardFont::Helvetica, "Été", 10), TextWidth(StandardFont::Helvetica, "Ete", 10), 1e-9);
	CHECK_NEAR(TextWidth(StandardFont::Courier, "naïf €", 10), 6 * 6.0, 1e-9);
	CHECK_NEAR(Ascent(StandardFont::TimesRoman, 10), 6.83, 1e-9);
	CHECK_NEAR(Descent(StandardFont::Helvetica, 10), 2.07, 1e-9);
	// Octets 0x80–0xFF : largeurs AFM exactes (Nimbus Sans / Roman, mêmes métriques qu'Adobe).
	CHECK_NEAR(TextWidth(StandardFont::Helvetica, "œ", 1000), 944.0, 1e-9);
	CHECK_NEAR(TextWidth(StandardFont::Helvetica, "æ", 1000), 889.0, 1e-9);
	CHECK_NEAR(TextWidth(StandardFont::Helvetica, "ß", 1000), 611.0, 1e-9);
	CHECK_NEAR(TextWidth(StandardFont::Helvetica, "€", 1000), 556.0, 1e-9);
	CHECK_NEAR(TextWidth(StandardFont::Helvetica, "«", 1000), 556.0, 1e-9);
	CHECK_NEAR(TextWidth(StandardFont::HelveticaBold, "©", 1000), 737.0, 1e-9);
	CHECK_NEAR(TextWidth(StandardFont::TimesRoman, "œ", 1000), 722.0, 1e-9);
	CHECK_NEAR(TextWidth(StandardFont::TimesRoman, "Æ", 1000), 889.0, 1e-9);
	CHECK_EQ(Metrics(StandardFont::Helvetica).widths[0x81 - 32], uint16_t(0)); // octet non attribué
}

TEST(Fonts, WinAnsiEncoding) {
	CHECK_EQ(ToWinAnsi(U'A'), uint8_t('A'));
	CHECK_EQ(ToWinAnsi(U'é'), uint8_t(0xE9));
	CHECK_EQ(ToWinAnsi(U'€'), uint8_t(0x80));
	CHECK_EQ(ToWinAnsi(U'—'), uint8_t(0x97));
	CHECK_EQ(ToWinAnsi(U'日'), uint8_t(0));
	CHECK(EncodeWinAnsi("été 日") == std::string("\xE9t\xE9 ?"));
	CHECK(DecodeWinAnsi(EncodeWinAnsi("Œuvre « ça » – 10 €")) == std::string("Œuvre « ça » – 10 €"));
	CHECK(FromWinAnsi(0x81) == U'�');
}

TEST(Fonts, MatchFamilies) {
	CHECK(MatchFont("Arial", false, false) == StandardFont::Helvetica);
	CHECK(MatchFont("Liberation Sans", true, true) == StandardFont::HelveticaBoldOblique);
	CHECK(MatchFont("Times New Roman", false, true) == StandardFont::TimesItalic);
	CHECK(MatchFont("serif", true, false) == StandardFont::TimesBold);
	CHECK(MatchFont("DejaVu Sans Mono", false, false) == StandardFont::Courier);
	CHECK(IsBold(StandardFont::CourierBold) && !IsItalic(StandardFont::CourierBold));
}

// ── Analyseur ──

TEST(Parser, ObjectsOfEveryKind) {
	const Bytes src = ToBytes(
		"<< /Name /A#20B /Int -42 /Real -.25 /Str (a\\(b\\)\\n\\101\\\\) /Hex <48656C6C6F> /Odd <414> "
		"/Arr [1 2.5 true null [ /x ] << /k 3 0 R >>] /Ref 12 0 R /Bool false >>");
	Parser p(src);
	auto obj = p.ParseObject();
	REQUIRE(obj.IsOk());
	const Object &d = obj.Value();
	REQUIRE(d.kind == ObjectKind::Dictionary);
	CHECK(d.Get("Name")->IsName("A B"));
	CHECK_EQ(d.Get("Int")->integer, int64_t(-42));
	CHECK_NEAR(d.Get("Real")->Number(), -0.25, 1e-12);
	CHECK(d.Get("Str")->text == std::string("a(b)\nA\\"));
	CHECK(d.Get("Hex")->text == std::string("Hello"));
	CHECK(d.Get("Odd")->text == std::string("A@"));
	const Object &arr = *d.Get("Arr");
	REQUIRE(arr.items.size() == 6);
	CHECK(arr.items[2].boolean);
	CHECK(arr.items[3].IsNull());
	CHECK(arr.items[4].items[0].IsName("x"));
	CHECK(arr.items[5].Get("k")->kind == ObjectKind::Reference);
	CHECK_EQ(d.Get("Ref")->ref.number, uint32_t(12));
	CHECK(d.Get("Bool")->kind == ObjectKind::Bool && !d.Get("Bool")->boolean);
}

TEST(Parser, StreamsAndIndirectObjects) {
	const Bytes src = ToBytes("7 0 obj\n<< /Length 5 >>\nstream\nHELLO\nendstream\nendobj\n"
							  "8 0 obj << /Length 99 >> stream\r\nABC\r\nendstream endobj");
	Parser p(src);
	auto first = p.ParseIndirect();
	REQUIRE(first.IsOk());
	CHECK_EQ(first.Value().first.number, uint32_t(7));
	CHECK(Str(first.Value().second.data) == "HELLO");
	auto second = p.ParseIndirect(); // /Length faux : repli sur « endstream »
	REQUIRE(second.IsOk());
	CHECK(Str(second.Value().second.data) == "ABC");
	const Bytes truncated = ToBytes("<< /A 1");
	Parser bad(truncated);
	CHECK(bad.ParseObject().IsError());
	const Bytes nested = ToBytes(std::string(300, '['));
	Parser deep(nested);
	CHECK(deep.ParseObject().IsError());
}

// ── Écriture ──

TEST(Writer, StructureAndCrossReferences) {
	Writer w = Sample();
	auto bytes = w.Serialize({.compress = false});
	REQUIRE(bytes.IsOk());
	const std::string s = Str(bytes.Value());
	CHECK(s.starts_with("%PDF-1.4\n"));
	CHECK(s.ends_with("%%EOF\n"));
	// Chaque entrée « n » de la table pointe sur « k 0 obj ».
	const size_t xref = s.rfind("\nxref\n") + 1;
	const size_t startxref = std::stoul(s.substr(s.rfind("startxref\n") + 10));
	CHECK_EQ(startxref, xref);
	size_t pos = s.find('\n', s.find('\n', xref) + 1) + 1; // après « 0 N »
	pos += 20;											   // entrée 0 (libre)
	for (int n = 1; s.compare(pos, 7, "trailer") != 0; ++n, pos += 20) {
		CHECK(s.compare(pos + 10, 10, " 00000 n \n") == 0);
		const size_t offset = std::stoul(s.substr(pos, 10));
		CHECK(s.compare(offset, std::format("{} 0 obj", n).size(), std::format("{} 0 obj", n)) == 0);
	}
	// Repère PDF : y retourné ; transparence par ExtGState ; polices avec /Widths.
	CHECK(s.find("72 729.89 200 40 re") != std::string::npos);
	CHECK(s.find("/ca 0.502") != std::string::npos);
	CHECK(s.find("/BaseFont /Times-Bold /Encoding /WinAnsiEncoding /FirstChar 32 /LastChar 255 /Widths [250 333") != std::string::npos);
	CHECK(s.find("/URI (https://example.org/a\\(b\\))") != std::string::npos);
	CHECK(s.find("/SMask") != std::string::npos);
	CHECK(s.find("/Title <FEFF0052") != std::string::npos); // UTF-16BE (accent)
	CHECK(s.find("/PageMode /UseOutlines") != std::string::npos);
	// Sortie déterministe.
	CHECK(Sample().Serialize({.compress = false}).Value() == bytes.Value());
}

TEST(Writer, InternalLinks) {
	Writer w;
	Canvas &first = w.AddPage(200, 300);
	w.AddPage(200, 400);
	first.LinkToPage(10, 20, 50, 12, 1, 100); // page 2, 100 pt sous le haut
	first.LinkToPage(10, 40, 50, 12, 7);	   // page absente : ignorée
	first.Link(10, 60, 50, 12, "https://example.org");
	auto bytes = w.Serialize({.compress = false});
	REQUIRE(bytes.IsOk());
	const std::string s = Str(bytes.Value());
	// Objets : catalogue 1, pages 2, page 1 = 3, contenu 4, page 2 = 5 (ordre d'écriture du Writer).
	const size_t dest = s.find("/Dest [");
	REQUIRE(dest != std::string::npos);
	CHECK(s.find("/XYZ 0 300 null]", dest) != std::string::npos); // 400 − 100 en repère PDF
	CHECK(s.find("/Dest [", dest + 1) == std::string::npos);	  // le lien vers la page 8 n'est pas écrit
	CHECK(s.find("/URI (https://example.org)") != std::string::npos);
	auto read = Reader::Open(bytes.Value());
	REQUIRE(read.IsOk());
	CHECK_EQ(read.Value().PageCount(), size_t(2));
}

TEST(Writer, RejectsInvalidInput) {
	Writer w;
	const uint8_t px[4] = {0, 0, 0, 255};
	CHECK(w.AddImage(2, 2, px).IsError());
	CHECK(w.AddImage(0, 1, {}).IsError());
	w.AddPage(100, 100);
	w.AddBookmark("perdu", 3);
	CHECK(w.Serialize().IsError());
	Writer clamp;
	CHECK_NEAR(clamp.AddPage(1, 1e9).Height(), 14400.0, 1e-9);
	CHECK_NEAR(clamp.Page(0).Width(), 3.0, 1e-9);
	CHECK(DateString(0) == std::string("D:19700101000000Z"));
	CHECK(DateString(1795343400) == std::string("D:20261122103000Z"));
	CHECK(TextString("abc") == std::string("(abc)"));
	CHECK(TextString("é") == std::string("<FEFF00E9>"));
}

// ── Relecture ──

TEST(Reader, RoundTripOfWriterOutput) {
	for (bool compress : {false, true}) {
		auto bytes = Sample().Serialize({.compress = compress});
		REQUIRE(bytes.IsOk());
		auto r = Reader::Open(bytes.Value());
		REQUIRE(r.IsOk());
		const Reader &file = r.Value();
		CHECK(!file.Repaired());
		CHECK(file.Version() == "1.4");
		REQUIRE(file.PageCount() == 2);
		CHECK_NEAR(file.Page(0).width, 595.28, 1e-6);
		CHECK_NEAR(file.Page(1).height, 419.53, 1e-6);
		CHECK(file.InfoString("Title") == std::optional<std::string>("Rapport d'été"));
		CHECK(file.InfoString("Author") == std::optional<std::string>("Équipe"));
		CHECK(file.InfoString("Producer").has_value());
		auto text = file.ExtractText(0);
		REQUIRE(text.IsOk());
		CHECK(text.Value() == "Bonjour le\nDeuxième ligne");
		CHECK(file.ExtractText(1).Value() == "Page 2");
		// Image : RVB et masque alpha décodés.
		const Object *res = file.Page(1).dictionary.Get("Resources");
		REQUIRE(res != nullptr);
		auto xobjects = file.Resolve(*res->Get("XObject"));
		REQUIRE(xobjects.IsOk());
		auto image = file.Resolve(xobjects.Value().items[0]);
		REQUIRE(image.IsOk());
		auto rgb = file.DecodeStream(image.Value());
		REQUIRE(rgb.IsOk());
		CHECK(rgb.Value() == (Bytes{255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255}));
		auto mask = file.Resolve(*image.Value().Get("SMask"));
		REQUIRE(mask.IsOk());
		CHECK(file.DecodeStream(mask.Value()).Value() == (Bytes{255, 128, 0, 255}));
		// Signets.
		auto catalog = file.Resolve(*file.Trailer().Get("Root"));
		auto outlines = file.Resolve(*catalog.Value().Get("Outlines"));
		REQUIRE(outlines.IsOk());
		CHECK_EQ(outlines.Value().Get("Count")->integer, int64_t(3));
		auto firstItem = file.Resolve(*outlines.Value().Get("First"));
		CHECK(firstItem.Value().Get("Title")->text == "Introduction");
		CHECK_EQ(firstItem.Value().Get("Count")->integer, int64_t(1)); // « Détail » imbriqué
		// Lien.
		const Object *annots = file.Page(0).dictionary.Get("Annots");
		REQUIRE(annots != nullptr);
		CHECK(annots->items[0].Get("A")->Get("URI")->text == "https://example.org/a(b)");
	}
}

TEST(Reader, Pdf15CrossReferenceAndObjectStreams) {
	// Objets 1 (catalogue) et 2 (arbre des pages) dans un flux d'objets ; table des références
	// en flux compressé avec prédicteur PNG « Up ».
	const std::string obj1 = "<< /Type /Catalog /Pages 2 0 R >>";
	const std::string obj2 = "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 200 100] >>";
	const std::string header = std::format("1 0 2 {} ", obj1.size() + 1);
	const std::string objstm = header + obj1 + " " + obj2;
	const std::string content = "BT /F1 10 Tf 10 50 Td (Hello) Tj 0 -12 Td [(Wor) -20 (ld) -400 (again)] TJ ET";
	std::string file = "%PDF-1.5\n";
	const size_t off3 = file.size();
	file += "3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R /Resources << /Font << /F1 << /Type /Font /Subtype /Type1 "
			"/BaseFont /Courier >> >> >> >> endobj\n";
	const size_t off4 = file.size();
	file += std::format("4 0 obj << /Length {} >> stream\n{}\nendstream endobj\n", content.size(), content);
	const size_t off5 = file.size();
	file += std::format("5 0 obj << /Type /ObjStm /N 2 /First {} /Length {} >> stream\n{}\nendstream endobj\n", header.size(), objstm.size(), objstm);
	const size_t off6 = file.size();
	// Entrées (type, champ 2 sur 2 octets, champ 3) pour les objets 0 à 6.
	const std::vector<std::array<uint8_t, 4>> rows = {
		{0, 0, 0, 255}, {2, 0, 5, 0}, {2, 0, 5, 1}, {1, uint8_t(off3 >> 8), uint8_t(off3), 0},
		{1, uint8_t(off4 >> 8), uint8_t(off4), 0}, {1, uint8_t(off5 >> 8), uint8_t(off5), 0}, {1, uint8_t(off6 >> 8), uint8_t(off6), 0}};
	Bytes predicted;
	std::array<uint8_t, 4> previous{};
	for (const auto &row : rows) {
		predicted.push_back(2); // Up
		for (size_t k = 0; k < 4; ++k)
			predicted.push_back(uint8_t(row[k] - previous[k]));
		previous = row;
	}
	const Bytes packed = archive::ZlibCompress(predicted);
	file += std::format("6 0 obj << /Type /XRef /Size 7 /W [1 2 1] /Root 1 0 R /Filter /FlateDecode "
						"/DecodeParms << /Predictor 12 /Columns 4 >> /Length {} >> stream\n",
						packed.size());
	file.append(packed.begin(), packed.end());
	file += std::format("\nendstream endobj\nstartxref\n{}\n%%EOF\n", off6);

	auto r = Reader::Open(ToBytes(file));
	REQUIRE(r.IsOk());
	CHECK(!r.Value().Repaired());
	CHECK(r.Value().Version() == "1.5");
	REQUIRE(r.Value().PageCount() == 1);
	CHECK_NEAR(r.Value().Page(0).width, 200.0, 1e-9); // MediaBox héritée
	auto text = r.Value().ExtractText(0);
	REQUIRE(text.IsOk());
	CHECK(text.Value() == "Hello\nWorld again");
}

TEST(Reader, IncrementalUpdateWins) {
	auto bytes = Sample().Serialize({.compress = true});
	REQUIRE(bytes.IsOk());
	std::string s = Str(bytes.Value());
	const size_t oldXref = std::stoul(s.substr(s.rfind("startxref\n") + 10));
	const size_t offInfo = s.size();
	s += "3 0 obj << /Title (Nouveau titre) >> endobj\n";
	const size_t newXref = s.size();
	const size_t size = std::stoul(s.substr(s.rfind("/Size ") + 6));
	s += std::format("xref\n0 1\n0000000000 65535 f \n3 1\n{:010} 00000 n \ntrailer\n<< /Size {} /Root 1 0 R /Info 3 0 R /Prev {} >>\n"
					 "startxref\n{}\n%%EOF\n",
					 offInfo, size, oldXref, newXref);
	auto r = Reader::Open(ToBytes(s));
	REQUIRE(r.IsOk());
	CHECK(!r.Value().Repaired());
	CHECK(r.Value().InfoString("Title") == std::optional<std::string>("Nouveau titre"));
	CHECK_EQ(r.Value().PageCount(), size_t(2));
}

TEST(Reader, RepairsBrokenCrossReferences) {
	auto bytes = Sample().Serialize({.compress = true});
	REQUIRE(bytes.IsOk());
	std::string s = Str(bytes.Value());
	const size_t xref = s.rfind("\nxref\n") + 1;
	s.replace(xref, 4, "xrfx"); // table illisible
	auto r = Reader::Open(ToBytes(s));
	REQUIRE(r.IsOk());
	CHECK(r.Value().Repaired());
	CHECK_EQ(r.Value().PageCount(), size_t(2));
	CHECK(r.Value().ExtractText(1).Value() == "Page 2");
	CHECK(r.Value().InfoString("Title") == std::optional<std::string>("Rapport d'été"));
	CHECK(Reader::Open(ToBytes("pas un pdf")).IsError());
	CHECK(Reader::Open(ToBytes("%PDF-1.4\nrien\n%%EOF")).IsError());
}

TEST(Reader, Ascii85AndHexFilters) {
	// ASCII85 : « 9jqo^ » = « Man », « BlbD- » = « is d », groupe final incomplet « Bl » = « i ».
	const std::string file = "%PDF-1.4\n1 0 obj << /Length 15 /Filter /ASCII85Decode >> stream\n9jqo^BlbD-Bl~>\nendstream endobj\n"
							 "2 0 obj << /Length 9 /Filter [/ASCIIHexDecode] >> stream\n4D616E 2>\nendstream endobj\n";
	const Bytes data = ToBytes(file); // le Parser ne garde qu'une vue sur les octets
	Parser p(data, 9);
	auto a85 = p.ParseIndirect();
	auto hex = p.ParseIndirect();
	REQUIRE(a85.IsOk() && hex.IsOk());
	// Un Reader minimal suffit pour DecodeStream : on passe par un document valide.
	auto host = Reader::Open(Sample().Serialize().Value());
	REQUIRE(host.IsOk());
	CHECK(Str(host.Value().DecodeStream(a85.Value().second).Value()) == "Man is di");
	CHECK(Str(host.Value().DecodeStream(hex.Value().second).Value()) == "Man ");
}

// ── Fichiers et validation externe ──

TEST(Files, AtomicSaveAndLoad) {
	const auto path = TempFile("save.pdf");
	auto saved = Sample().Save(path);
	REQUIRE(saved.IsOk());
	CHECK(!std::filesystem::exists(path.string() + ".tmp"));
	CHECK_EQ(std::filesystem::file_size(path), uintmax_t(saved.Value()));
	auto loaded = Reader::Load(path);
	REQUIRE(loaded.IsOk());
	CHECK_EQ(loaded.Value().PageCount(), size_t(2));
	std::filesystem::remove(path);
	CHECK(Reader::Load(TempFile("absent.pdf")).IsError());
	CHECK(Sample().Save("/dossier/inexistant/x.pdf").IsError());
}

TEST(Files, PopplerAcceptsOutput) {
	if (!HasTool("pdfinfo") || !HasTool("pdftotext"))
		SKIP("poppler-utils absent (pdfinfo, pdftotext)");
	const auto path = TempFile("poppler.pdf");
	const auto txt = TempFile("poppler.txt");
	REQUIRE(Sample().Save(path).IsOk());
	// pdfinfo échoue (ou signale « Syntax Error ») sur un fichier mal formé.
	const auto log = TempFile("poppler.log");
	CHECK(std::system(std::format("pdfinfo '{}' > '{}' 2>&1", path.string(), log.string()).c_str()) == 0);
	std::string info;
	if (std::FILE *f = std::fopen(log.string().c_str(), "rb")) {
		char buf[4096];
		for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
			info.append(buf, n);
		std::fclose(f);
	}
	CHECK(info.find("Syntax Error") == std::string::npos);
	const size_t pages = info.find("Pages:");
	REQUIRE(pages != std::string::npos);
	CHECK(std::stoi(info.substr(pages + 6)) == 2);
	CHECK(info.find("Rapport d'été") != std::string::npos);
	CHECK(std::system(std::format("pdftotext -f 1 -l 1 '{}' '{}' 2>/dev/null", path.string(), txt.string()).c_str()) == 0);
	std::string text;
	if (std::FILE *f = std::fopen(txt.string().c_str(), "rb")) {
		char buf[4096];
		for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
			text.append(buf, n);
		std::fclose(f);
	}
	CHECK(text.find("Bonjour le") != std::string::npos);
	CHECK(text.find("Deuxième ligne") != std::string::npos);
	std::filesystem::remove(path);
	std::filesystem::remove(txt);
	std::filesystem::remove(log);
}

// ── Polices TrueType / OpenType ──

namespace {
const std::filesystem::path kUbuntu = "assets/fonts/Ubuntu";
}

TEST(TrueType, ParsesNamesMetricsAndCharacterMap) {
	auto loaded = TrueTypeFont::Load(kUbuntu / "Ubuntu-BoldItalic.ttf");
	if (!loaded)
		SKIP("polices Ubuntu du dépôt absentes (tests lancés hors de la racine ?)");
	const TrueTypeFont &f = *loaded.Value();
	// Valeurs de référence : fc-query (fontconfig), implémentation indépendante.
	CHECK(f.Family() == "Ubuntu Nerd Font");
	CHECK(f.Style() == "Bold Italic");
	CHECK(f.PostScriptName() == "UbuntuNF-BoldItalic");
	CHECK(f.Bold());
	CHECK(f.Italic());
	CHECK(!f.IsCff());
	CHECK(f.ItalicAngle() < 0);
	CHECK(f.Ascent() > 600 && f.Ascent() < 1200);
	CHECK(f.Descent() < 0);
	CHECK(f.HasGlyph(U'A'));
	CHECK(f.HasGlyph(U'é'));
	CHECK(f.HasGlyph(U'€'));
	CHECK(!f.HasGlyph(U'\U0001F600') || f.HasGlyph(U'A'));
	// Largeurs : une lettre accentuée a la largeur de sa base ; « W » plus large que « i ».
	CHECK_NEAR(f.GlyphAdvance(f.GlyphIndex(U'é')), f.GlyphAdvance(f.GlyphIndex(U'e')), 1e-9);
	CHECK(f.GlyphAdvance(f.GlyphIndex(U'W')) > f.GlyphAdvance(f.GlyphIndex(U'i')));
	CHECK_EQ(f.WinAnsiWidths()[size_t('W' - 32)], uint16_t(std::lround(f.GlyphAdvance(f.GlyphIndex(U'W')))));
	CHECK_NEAR(f.TextWidth("Wi", 10), (f.WinAnsiWidths()[size_t('W' - 32)] + f.WinAnsiWidths()[size_t('i' - 32)]) / 100.0, 1e-9);
	CHECK(f.Embeddable());
	CHECK_EQ(f.FontProgram().size(), std::filesystem::file_size(kUbuntu / "Ubuntu-BoldItalic.ttf"));
	// Police OpenType CFF : programme = table CFF.
	auto otf = TrueTypeFont::Load("assets/fonts/MaterialIcons-Regular.otf");
	REQUIRE(otf.IsOk());
	CHECK(otf.Value()->IsCff());
	CHECK(otf.Value()->Family() == "Material Icons");
	CHECK(otf.Value()->FontProgram().size() < std::filesystem::file_size("assets/fonts/MaterialIcons-Regular.otf"));
	// Données invalides.
	CHECK(TrueTypeFont::Parse(Bytes{1, 2, 3}).IsError());
	CHECK(TrueTypeFont::Load("absent.ttf").IsError());
}

TEST(TrueType, ScansDirectoriesAndCollections) {
	const std::vector<std::filesystem::path> dirs{kUbuntu};
	const auto fonts = ScanFonts(dirs);
	if (fonts.empty())
		SKIP("polices Ubuntu du dépôt absentes");
	CHECK(fonts.size() >= 8);
	CHECK(std::ranges::is_sorted(fonts, [](const FontDescription &a, const FontDescription &b) { return a.family < b.family; }));
	const auto regular = std::ranges::find_if(fonts, [](const FontDescription &d) { return d.postscript == "UbuntuNF"; });
	REQUIRE(regular != fonts.end());
	CHECK(!regular->bold && !regular->italic);
	CHECK(!DescribeFontFile(kUbuntu / "README.md").has_value());
	// Collection .ttc du système (si présente) : première face extraite et autonome.
	std::filesystem::path ttc;
	std::error_code ec;
	for (auto it = std::filesystem::recursive_directory_iterator("/usr/share/fonts", ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
		if (it->path().extension() == ".ttc") {
			ttc = it->path();
			break;
		}
	if (!ttc.empty()) {
		auto face = TrueTypeFont::Load(ttc);
		REQUIRE(face.IsOk());
		auto again = TrueTypeFont::Parse(face.Value()->FontProgram());
		CHECK(again.IsOk() || face.Value()->IsCff());
		if (again)
			CHECK(again.Value()->Family() == face.Value()->Family());
	}
}

TEST(TrueType, EmbeddedInPdfAndRotation) {
	auto loaded = TrueTypeFont::Load(kUbuntu / "Ubuntu-Regular.ttf");
	if (!loaded)
		SKIP("polices Ubuntu du dépôt absentes");
	Writer w;
	Canvas &page = w.AddPage(300, 200);
	FontRef font;
	font.face = loaded.Value();
	page.Text(20, 50, font, 14, "Police incorporée é €", math::Color(0, 0, 0));
	page.Text(20, 80, font, 10, "Deuxième ligne", math::Color(0, 0, 0));
	page.Save();
	page.Rotate(30, 150, 100);
	page.FillRect(120, 80, 60, 40, math::Color(255, 0, 0));
	page.Restore();
	auto bytes = w.Serialize();
	REQUIRE(bytes.IsOk());
	const std::string s(bytes.Value().begin(), bytes.Value().end());
	CHECK(s.find("/FontFile2") != std::string::npos);
	CHECK(s.find("/BaseFont /UbuntuNF") != std::string::npos);
	CHECK(s.find("/Subtype /TrueType") != std::string::npos);
	auto r = Reader::Open(bytes.Value());
	REQUIRE(r.IsOk());
	CHECK(r.Value().ExtractText(0).Value().find("Police incorporée é €") != std::string::npos);
	// Rotation : matrice cos/sin de -30° autour de (150, 100) en repère PDF.
	auto content = r.Value().PageContent(0);
	REQUIRE(content.IsOk());
	const std::string ops(content.Value().begin(), content.Value().end());
	CHECK(ops.find("0.866 -0.5 0.5 0.866") != std::string::npos);
	// Validation indépendante : poppler voit la police incorporée et extrait le texte.
	if (std::system("command -v pdffonts > /dev/null 2>&1") == 0) {
		const auto path = std::filesystem::temp_directory_path() / "apps-lib-ttf.pdf";
		const auto log = std::filesystem::temp_directory_path() / "apps-lib-ttf.txt";
		REQUIRE(w.Save(path).IsOk());
		CHECK(std::system(std::format("pdffonts '{}' > '{}' 2>&1", path.string(), log.string()).c_str()) == 0);
		std::string fonts;
		if (std::FILE *f = std::fopen(log.string().c_str(), "rb")) {
			char buf[4096];
			for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
				fonts.append(buf, n);
			std::fclose(f);
		}
		CHECK(fonts.find("UbuntuNF") != std::string::npos);
		CHECK(fonts.find("TrueType") != std::string::npos);
		CHECK(fonts.find(" yes ") != std::string::npos); // colonne « emb »
		CHECK(std::system(std::format("pdftotext '{}' '{}' 2>/dev/null", path.string(), log.string()).c_str()) == 0);
		std::string text;
		if (std::FILE *f = std::fopen(log.string().c_str(), "rb")) {
			char buf[4096];
			for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
				text.append(buf, n);
			std::fclose(f);
		}
		CHECK(text.find("Police incorporée é €") != std::string::npos);
		std::filesystem::remove(path);
		std::filesystem::remove(log);
	}
}
