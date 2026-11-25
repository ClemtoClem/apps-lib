// Mesures de performance d'engine.document sur un long document (affichées ; les assertions
// portent sur des rapports — incrémental contre complet — et non sur des durées absolues).
#include <cpp-maker/test.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>

#include <document/export.hpp>
#include <document/persistence.hpp>
#include <document/render.hpp>

using namespace doc;

namespace {

using Clock = std::chrono::steady_clock;

double Ms(Clock::time_point since) { return std::chrono::duration<double, std::milli>(Clock::now() - since).count(); }

struct Long {
	Document d;
	NodeId body = d.Body(d.Sections().front());
	std::vector<NodeId> texts;

	Long(int paragraphs, int tables) {
		const ResourceId image = d.AddResource({64, 48, std::vector<uint8_t>(64 * 48 * 4, 180)});
		for (int i = 0; i < paragraphs; ++i) {
			const NodeId p = d.CreateNode(NodeType::Paragraph, body).Value();
			const NodeId t = d.CreateNode(NodeType::Text, p).Value();
			(void)d.Set(t, props::Text,
						std::format("Paragraphe {} : la mise en page incrémentale ne remesure que les blocs modifiés, "
									"la pagination place les blocs page après page selon leurs règles de coupure.",
									i));
			texts.push_back(t);
			if (i % 75 == 0 && tables > 0) {
				--tables;
				const NodeId table = d.CreateNode(NodeType::Table, body).Value();
				for (int r = 0; r < 50; ++r) {
					const NodeId row = d.CreateNode(NodeType::TableRow, table).Value();
					for (int c = 0; c < 4; ++c) {
						const NodeId cell = d.CreateNode(NodeType::TableCell, row).Value();
						const NodeId cp = d.CreateNode(NodeType::Paragraph, cell).Value();
						(void)d.Set(d.CreateNode(NodeType::Text, cp).Value(), props::Text, std::format("{}×{}", r, c));
					}
				}
			}
			if (i % 200 == 0)
				(void)d.Set(d.CreateNode(NodeType::Image, body).Value(), props::Resource, int64_t(image));
		}
	}
};

} // namespace

TEST(Performance, LongDocument) {
	auto start = Clock::now();
	Long doc(3000, 40);
	const double build = Ms(start);
	LayoutCache cache;

	start = Clock::now();
	LayoutResult cold = Layout(doc.d, &cache);
	const double coldMs = Ms(start);
	LayoutOptions sequential;
	sequential.parallel = false;
	LayoutCache sequentialCache;
	start = Clock::now();
	(void)Layout(doc.d, &sequentialCache, sequential);
	const double sequentialMs = Ms(start);
	start = Clock::now();
	LayoutResult warm = Layout(doc.d, &cache);
	const double warmMs = Ms(start);

	REQUIRE(doc.d.Set(doc.texts[1500], props::Text, "Paragraphe modifié au milieu du document.").IsOk());
	start = Clock::now();
	LayoutResult typed = Layout(doc.d, &cache);
	const double typedMs = Ms(start);

	// Style hérité modifié sur un seul paragraphe : seuls ce paragraphe et ses segments changent.
	REQUIRE(doc.d.Set(doc.d.Find(doc.texts[10])->Parent(), props::FontSize, 14.0).IsOk());
	start = Clock::now();
	LayoutResult local = Layout(doc.d, &cache);
	const double localMs = Ms(start);
	const size_t localMeasured = local.stats.measured;

	// Style hérité du document entier : tout est à remesurer.
	REQUIRE(doc.d.Set(doc.d.Root(), props::FontSize, 10.0).IsOk());
	start = Clock::now();
	LayoutResult global = Layout(doc.d, &cache);
	const double globalMs = Ms(start);

	start = Clock::now();
	const auto pages = RenderDocument(doc.d, global);
	const double renderMs = Ms(start);
	start = Clock::now();
	const Snapshot snap = doc.d.TakeSnapshot();
	const double snapshotMs = Ms(start);
	const auto path = std::filesystem::temp_directory_path() / "apps-lib-perf.docdb";
	start = Clock::now();
	REQUIRE(SaveDocument(*snap, path).IsOk());
	const double saveMs = Ms(start);
	start = Clock::now();
	auto loaded = LoadDocument(path);
	const double loadMs = Ms(start);
	REQUIRE(loaded.IsOk());
	start = Clock::now();
	auto pdf = ExportPdf(*snap, global);
	const double pdfMs = Ms(start);
	REQUIRE(pdf.IsOk());
	std::filesystem::remove(path);

	std::printf("\n    %zu nœuds, %zu pages : construction %.0f ms, mise en page à froid %.0f ms (%.0f ms sur un seul fil), à chaud %.1f ms,"
				"\n    après frappe %.1f ms (%zu bloc(s)), style local %.1f ms (%zu bloc(s)), style global %.0f ms (%zu blocs),"
				"\n    rendu %.0f ms, instantané %.2f ms, enregistrement %.0f ms, chargement %.0f ms, PDF %.0f ms (%zu Kio)\n",
				doc.d.NodeCount(), global.pages.size(), build, coldMs, sequentialMs, warmMs, typedMs, typed.stats.measured, localMs, localMeasured,
				globalMs, global.stats.measured, renderMs, snapshotMs, saveMs, loadMs, pdfMs, pdf.Value().size() / 1024);

	CHECK_EQ(warm.stats.measured, size_t(0));
	CHECK_EQ(typed.stats.measured, size_t(1));
	CHECK(localMeasured <= 2);	  // le paragraphe dont le style hérité a changé (pas tout le document)
	CHECK(warmMs < coldMs / 3);	  // pagination seule
	CHECK(typedMs < coldMs / 3);
	CHECK(localMs < coldMs / 3);
	CHECK_EQ(pages.size(), global.pages.size());
	CHECK_EQ(loaded.Value().NodeCount(), doc.d.NodeCount());
	CHECK_EQ(cold.pages.size(), warm.pages.size());
}
