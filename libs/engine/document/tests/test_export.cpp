// Tests des exports d'engine.document : PDF relu par foundation.pdf (pages, formats, texte,
// métadonnées, signets, images, déterminisme) et validé par poppler s'il est installé ; HTML
// (structure, styles, échappement, tableaux à en-tête répété, image PNG embarquée, sauts de
// page) ; CSV des tableaux (échappement, cellules multi-paragraphes, lignes complétées) ;
// export d'un instantané sur un autre fil.
#include <cpp-maker/test.hpp>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <thread>

#include <dataio/base64.hpp>
#include <dataio/documents/csv.hpp>
#include <document/export.hpp>
#include <pdf/reader.hpp>

using namespace doc;

namespace {

struct Fixture {
	Document d;
	NodeId section = d.Sections().front();
	NodeId body = d.Body(section);
	NodeId table = kNoNode, header = kNoNode, image = kNoNode, hidden = kNoNode, bold = kNoNode;

	NodeId Para(std::string_view text, NodeId parent = kNoNode) {
		const NodeId p = d.CreateNode(NodeType::Paragraph, parent == kNoNode ? body : parent).Value();
		const NodeId t = d.CreateNode(NodeType::Text, p).Value();
		(void)d.Set(t, props::Text, text);
		return p;
	}

	Fixture() {
		(void)d.Set(d.Root(), props::Name, "Rapport <annuel> & bilan");
		const NodeId title = Para("Introduction");
		(void)d.Set(title, props::Name, "Introduction");
		const NodeId p = Para("Texte « accentué » & <balises> ");
		bold = d.CreateNode(NodeType::Text, p).Value();
		(void)d.Set(bold, props::Text, "gras");
		(void)d.Set(bold, props::Bold, true);
		table = d.CreateNode(NodeType::Table, body).Value();
		(void)d.Set(table, props::Name, "Tableau des ventes");
		for (int r = 0; r < 3; ++r) {
			const NodeId row = d.CreateNode(NodeType::TableRow, table).Value();
			if (r == 0) {
				header = row;
				(void)d.Set(row, props::RepeatHeader, true);
			}
			for (int c = 0; c < 2; ++c) {
				const NodeId cell = d.CreateNode(NodeType::TableCell, row).Value();
				Para(r == 0 ? std::format("Colonne {}", c) : std::format("v{}{}", r, c), cell);
			}
		}
		const ResourceId res = d.AddResource({2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128}});
		image = d.CreateNode(NodeType::Image, body).Value();
		(void)d.Set(image, props::Resource, int64_t(res));
		(void)d.Set(image, props::Width, 40.0);
		(void)d.CreateNode(NodeType::PageBreak, body);
		const NodeId second = Para("Deuxième page");
		(void)d.Set(second, props::Name, "Suite");
		hidden = Para("invisible");
		(void)d.Set(hidden, props::Visible, false);
		const NodeId h = d.CreateNode(NodeType::Header, section).Value();
		Para("En-tête du rapport", h);
	}
};

bool HasTool(const char *tool) { return std::system(std::format("command -v {} > /dev/null 2>&1", tool).c_str()) == 0; }

std::string ReadFile(const std::filesystem::path &p) {
	std::string s;
	if (std::FILE *f = std::fopen(p.string().c_str(), "rb")) {
		char buf[4096];
		for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
			s.append(buf, n);
		std::fclose(f);
	}
	return s;
}

} // namespace

TEST(ExportPdf, PagesTextMetadataBookmarksImages) {
	Fixture f;
	const LayoutResult layout = Layout(f.d);
	REQUIRE(layout.pages.size() == 2);
	auto bytes = ExportPdf(f.d, layout);
	REQUIRE(bytes.IsOk());
	auto r = pdf::Reader::Open(bytes.Value());
	REQUIRE(r.IsOk());
	const pdf::Reader &file = r.Value();
	CHECK(!file.Repaired());
	REQUIRE(file.PageCount() == 2);
	CHECK_NEAR(file.Page(0).width, layout.pages[0].geometry.width, 1e-3);
	CHECK(file.InfoString("Title") == std::optional<std::string>("Rapport <annuel> & bilan"));
	CHECK(file.InfoString("Creator") == std::optional<std::string>("engine.document"));
	const std::string page1 = file.ExtractText(0).Value();
	CHECK(page1.find("Introduction") != std::string::npos);
	CHECK(page1.find("Texte « accentué » & <balises> gras") != std::string::npos);
	CHECK(page1.find("Colonne 0") != std::string::npos);
	CHECK(page1.find("En-tête du rapport") != std::string::npos);
	CHECK(page1.find("invisible") == std::string::npos);
	CHECK(file.ExtractText(1).Value().find("Deuxième page") != std::string::npos);
	// Gras : police Helvetica-Bold déclarée sur la page 1.
	auto res = file.Resolve(*file.Page(0).dictionary.Get("Resources"));
	auto fonts = file.Resolve(*res.Value().Get("Font"));
	bool hasBold = false;
	for (const pdf::Object &font : fonts.Value().items)
		hasBold = hasBold || file.Resolve(font).Value().Get("BaseFont")->text == "Helvetica-Bold";
	CHECK(hasBold);
	CHECK(res.Value().Get("XObject") != nullptr); // image
	// Signets : nœuds nommés dans l'ordre du document.
	auto catalog = file.Resolve(*file.Trailer().Get("Root"));
	auto outlines = file.Resolve(*catalog.Value().Get("Outlines"));
	REQUIRE(outlines.IsOk());
	CHECK_EQ(outlines.Value().Get("Count")->integer, int64_t(3));
	CHECK(file.Resolve(*outlines.Value().Get("First")).Value().Get("Title")->text == "Introduction");
	// Déterminisme et contrôle de cohérence.
	CHECK(ExportPdf(f.d, layout).Value() == bytes.Value());
	Document other;
	CHECK(ExportPdf(other, layout).Error().code == ErrorCode::InvalidValue);
}

TEST(ExportPdf, SavedFileAcceptedByPoppler) {
	Fixture f;
	const auto path = std::filesystem::temp_directory_path() / "apps-lib-export.pdf";
	REQUIRE(SavePdf(f.d, Layout(f.d), path).IsOk());
	CHECK(!std::filesystem::exists(path.string() + ".tmp"));
	CHECK(SavePdf(f.d, Layout(f.d), "/dossier/inexistant/x.pdf").Error().code == ErrorCode::Io);
	if (HasTool("pdftotext")) {
		const auto txt = std::filesystem::temp_directory_path() / "apps-lib-export.txt";
		CHECK(std::system(std::format("pdftotext '{}' '{}' 2>/dev/null", path.string(), txt.string()).c_str()) == 0);
		const std::string text = ReadFile(txt);
		CHECK(text.find("Deuxième page") != std::string::npos);
		CHECK(text.find("v21") != std::string::npos);
		std::filesystem::remove(txt);
	}
	std::filesystem::remove(path);
}

TEST(ExportHtml, StructureStylesTablesAndImages) {
	Fixture f;
	REQUIRE(f.d.Set(f.d.Root(), props::PageOrientation, Orientation::Landscape).IsOk());
	auto html = ExportHtml(f.d);
	REQUIRE(html.IsOk());
	const std::string &s = html.Value();
	CHECK(s.starts_with("<!DOCTYPE html>"));
	CHECK(s.find("<title>Rapport &lt;annuel&gt; &amp; bilan</title>") != std::string::npos);
	CHECK(s.find("@page { size: 841.89pt 595.28pt;") != std::string::npos);
	CHECK(s.find("Texte « accentué » &amp; &lt;balises&gt; ") != std::string::npos);
	CHECK(s.find("font-weight: bold;\">gras</span>") != std::string::npos);
	CHECK(s.find("<thead>\n<tr") != std::string::npos);
	CHECK(s.find("<th") != std::string::npos);
	CHECK(s.find("<tbody>") != std::string::npos);
	CHECK(s.find("class=\"page-break\"") != std::string::npos);
	CHECK(s.find("<header>") != std::string::npos);
	CHECK(s.find("En-tête du rapport") != std::string::npos);
	CHECK(s.find("invisible") == std::string::npos);
	// Image : PNG valide embarqué.
	const std::string marker = "data:image/png;base64,";
	const size_t at = s.find(marker);
	REQUIRE(at != std::string::npos);
	const size_t end = s.find('"', at);
	auto png = dataio::Base64Decode(std::string_view(s).substr(at + marker.size(), end - at - marker.size()));
	REQUIRE(png.has_value());
	REQUIRE(png->size() > 8);
	CHECK((*png)[0] == 0x89 && (*png)[1] == 'P' && (*png)[2] == 'N' && (*png)[3] == 'G');
	CHECK(s.find("width: 40pt; height: 40pt;") != std::string::npos);
	// Sans images embarquées : cadre de substitution.
	HtmlExportOptions noImages;
	noImages.embedImages = false;
	CHECK(ExportHtml(f.d, noImages).Value().find(marker) == std::string::npos);
}

TEST(ExportCsv, TablesForSpreadsheets) {
	Fixture f;
	auto csv = ExportCsv(f.d, f.table);
	REQUIRE(csv.IsOk());
	CHECK(csv.Value() == "Colonne 0,Colonne 1\r\nv10,v11\r\nv20,v21\r\n");
	// Cellules délicates : virgule, guillemets, plusieurs paragraphes, tableau imbriqué, ligne
	// incomplète, ligne masquée.
	const NodeId t = f.d.CreateNode(NodeType::Table, f.body).Value();
	const NodeId r1 = f.d.CreateNode(NodeType::TableRow, t).Value();
	const NodeId a = f.d.CreateNode(NodeType::TableCell, r1).Value();
	f.Para("1,5", a);
	const NodeId b = f.d.CreateNode(NodeType::TableCell, r1).Value();
	f.Para("dit \"oui\"", b);
	f.Para("seconde ligne", b);
	const NodeId c = f.d.CreateNode(NodeType::TableCell, r1).Value();
	const NodeId inner = f.d.CreateNode(NodeType::Table, c).Value();
	const NodeId ir = f.d.CreateNode(NodeType::TableRow, inner).Value();
	f.Para("imbriqué", f.d.CreateNode(NodeType::TableCell, ir).Value());
	const NodeId r2 = f.d.CreateNode(NodeType::TableRow, t).Value();
	f.Para("seule", f.d.CreateNode(NodeType::TableCell, r2).Value());
	const NodeId r3 = f.d.CreateNode(NodeType::TableRow, t).Value();
	REQUIRE(f.d.Set(r3, props::Visible, false).IsOk());
	f.Para("masquée", f.d.CreateNode(NodeType::TableCell, r3).Value());
	csv = ExportCsv(f.d, t);
	REQUIRE(csv.IsOk());
	auto grid = dataio::CsvDocument::DecodeRows(csv.Value());
	REQUIRE(grid.IsOk());
	const std::vector<std::vector<String>> expected = {{"1,5", "dit \"oui\"\nseconde ligne", "imbriqué"}, {"seule", "", ""}};
	CHECK(grid.Value() == expected);
	CHECK(ExportCsv(f.d, f.body).Error().code == ErrorCode::InvalidValue);
	const auto path = std::filesystem::temp_directory_path() / "apps-lib-export.csv";
	REQUIRE(SaveCsv(f.d, f.table, path).IsOk());
	CHECK(ReadFile(path) == "Colonne 0,Colonne 1\r\nv10,v11\r\nv20,v21\r\n");
	std::filesystem::remove(path);
}

TEST(Export, SnapshotExportedOnAnotherThread) {
	Fixture f;
	const Snapshot snap = f.d.TakeSnapshot();
	DocResult<std::vector<uint8_t>> pdfBytes = Failure(ErrorCode::Nothing, "");
	DocResult<std::string> html = Failure(ErrorCode::Nothing, "");
	std::thread worker([&] {
		pdfBytes = ExportPdf(*snap, Layout(*snap));
		html = ExportHtml(*snap);
	});
	for (int i = 0; i < 100; ++i)
		f.Para("ajout " + std::to_string(i));
	worker.join();
	REQUIRE(pdfBytes.IsOk());
	REQUIRE(html.IsOk());
	CHECK(html.Value().find("ajout 1") == std::string::npos);
	auto r = pdf::Reader::Open(pdfBytes.Value());
	REQUIRE(r.IsOk());
	CHECK_EQ(r.Value().PageCount(), size_t(2));
}
