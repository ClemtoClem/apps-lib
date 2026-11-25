#include "scribe/demo.hpp"

#include <format>

namespace scribe {

using namespace doc;

namespace {

Color Named(std::string_view name) { return Color::FromName(name).value_or(Color::BLACK()); }

/// Construction directe (le document de démonstration n'a pas d'historique).
struct Builder {
	Editor &editor;
	doc::Document &d;
	NodeId section = d.Sections().front();

	/// Premier segment de texte d'une cellule.
	NodeId CellText(NodeId table, size_t row, size_t column) {
		const NodeId cell = TableGrid::Of(d, table).Cell(row, column);
		return d.Find(d.Find(cell)->Children().front())->Children().front();
	}
	void SetCell(NodeId table, size_t row, size_t column, std::string_view text, bool bold = false) {
		const NodeId t = CellText(table, row, column);
		(void)d.Set(t, props::Text, text);
		if (bold)
			(void)d.Set(t, props::Bold, true);
	}
	/// Segment ajouté à un paragraphe (exposant, lien, champ…).
	NodeId Run(NodeId paragraph, std::string_view text) {
		const NodeId t = d.CreateNode(NodeType::Text, paragraph).Value();
		(void)d.Set(t, props::Text, text);
		return t;
	}
	NodeId Paragraph(NodeId parent, std::string_view text) {
		const NodeId p = d.CreateNode(NodeType::Paragraph, parent).Value();
		Run(p, text);
		return p;
	}
	/// Paragraphe inséré après le bloc courant avec un style.
	void Styled(ParagraphStyle style, std::string_view text) {
		(void)editor.InsertParagraph(text);
		(void)editor.ApplyStyle(style);
	}
	void Bullets(std::initializer_list<std::string_view> items, ListStyle style = ListStyle::Bullet, int64_t level = 0) {
		for (std::string_view item : items) {
			(void)editor.InsertParagraph(item);
			(void)editor.SetParagraph(PropertyId::ListStyle, int64_t(style));
			if (level > 0)
				(void)editor.SetParagraph(PropertyId::ListLevel, level);
			(void)editor.SetParagraph(PropertyId::SpaceAfter, 2.0);
		}
	}
	/// Tableau de données : en-tête gris répété, filets horizontaux, cellules remplies.
	NodeId DataTable(const std::vector<std::vector<std::string>> &rows) {
		(void)editor.InsertTable(int(rows.size()), int(rows.front().size()));
		const NodeId table = editor.CurrentTable().table;
		for (size_t r = 0; r < rows.size(); ++r)
			for (size_t c = 0; c < rows[r].size(); ++c)
				SetCell(table, r, c, rows[r][c], r == 0);
		(void)editor.ApplyBorders(BorderPreset::Horizontal, 0.75, Named("DIM_GREY"));
		const TableGrid g = TableGrid::Of(d, table);
		for (NodeId cell : g.cells.front())
			(void)d.Set(cell, props::BackgroundColor, Named("GAINSBORO"));
		(void)d.Set(g.rows.front(), props::RepeatHeader, true);
		return table;
	}
};

} // namespace

void BuildDemo(Editor &editor) {
	editor.New();
	doc::Document &d = editor.Document();
	Builder b{editor, d};
	(void)d.Set(d.Root(), props::Name, "Module radio Aurora AR-24 — fiche technique");
	(void)d.Set(d.Root(), props::HeadingNumbering, HeadingNumbering::Decimal);
	(void)d.Set(d.Root(), props::FontSize, 10.5);
	(void)d.Set(d.Root(), props::LineSpacing, 1.2);

	// ── Page de titre ──
	const NodeId title = editor.Selection().Primary(); // segment du paragraphe vide du nouveau document
	(void)editor.EditText(title, "Module radio Aurora AR-24");
	(void)editor.ApplyStyle(ParagraphStyle::Title);
	(void)editor.SetParagraph(PropertyId::PaddingTop, 150.0); // l'espace avant disparaît en haut de page
	b.Styled(ParagraphStyle::Subtitle, "Fiche technique · version 1.3 · document de démonstration de Scribe");
	(void)editor.InsertShape(ShapeKind::Rectangle); // bandeau décoratif
	const NodeId band = editor.Selection().Primary();
	(void)d.Set(band, props::Height, 10.0);
	(void)d.Set(band, props::BackgroundColor, Named("FIRE_BRICK"));
	(void)d.Set(band, props::BorderWidth, 0.0);
	b.Styled(ParagraphStyle::Normal, "Toutes les valeurs, références et noms de ce document sont fictifs : il illustre la mise en page "
									  "(titres numérotés, tables des matières, légendes, renvois, listes, tableaux, encadrés, code).");
	(void)editor.SetParagraph(PropertyId::SpaceBefore, 24.0);
	(void)editor.InsertPageBreak();

	// ── Tables ──
	(void)editor.InsertTableOfContents(TocContent::Headings);
	(void)editor.InsertTableOfContents(TocContent::Tables);
	(void)editor.InsertTableOfContents(TocContent::Figures);
	(void)editor.InsertPageBreak();

	// ── 1 Présentation ──
	b.Styled(ParagraphStyle::Heading1, "Présentation du module");
	b.Styled(ParagraphStyle::Normal, "L'AR-24 est un module radio basse consommation destiné aux capteurs autonomes. Il associe un "
									  "microcontrôleur, une mémoire flash et une antenne intégrée sur un circuit de 18 × 25 mm.");
	(void)editor.SetParagraph(PropertyId::HAlign, int64_t(HorizontalAlign::Justify));
	b.Styled(ParagraphStyle::Note, "Note : la dernière version de ce document est publiée sur ");
	const NodeId note = d.AncestorOfType(editor.Caret().text, NodeType::Paragraph);
	const NodeId url = b.Run(note, "https://example.org/aurora");
	(void)d.Set(url, props::Link, "https://example.org/aurora");
	(void)d.Set(url, props::TextColor, Named("COBALT_BLUE"));
	(void)d.Set(url, props::Underline, true);
	b.Run(note, ".");

	b.Styled(ParagraphStyle::Heading2, "Caractéristiques");
	// Deux colonnes : tableau sans bordure, une liste à puces par cellule.
	(void)editor.InsertTable(1, 2);
	const NodeId features = editor.CurrentTable().table;
	(void)editor.ApplyBorders(BorderPreset::None, 0, Color::BLACK());
	const struct {
		size_t column;
		const char *heading;
		std::initializer_list<std::string_view> items;
	} groups[] = {{0, "Processeur et mémoire", {"Cœur 32 bits à 160 MHz", "384 Kio de ROM, 256 Kio de RAM", "4 Mio de flash série"}},
				  {0, "Radio", {"Bande 2,4 GHz, débit jusqu'à 2 Mbit/s", "Puissance d'émission réglable de -20 à +8 dBm", "Antenne intégrée"}},
				  {1, "Périphériques", {"18 entrées / sorties", "UART, SPI, I²C, PWM", "Convertisseur 12 bits, 6 voies"}},
				  {1, "Conditions d'utilisation", {"Alimentation de 2,8 à 3,6 V", "Température de -40 à 85 °C"}}};
	for (const auto &g : groups) {
		const NodeId cell = TableGrid::Of(d, features).Cell(0, g.column);
		const NodeId heading = b.Paragraph(cell, g.heading);
		(void)d.Set(d.Find(heading)->Children().front(), props::Bold, true);
		(void)d.Set(d.Find(heading)->Children().front(), props::TextColor, Named("FIRE_BRICK"));
		(void)d.Set(heading, props::SpaceBefore, 6.0);
		for (std::string_view item : g.items) {
			const NodeId p = b.Paragraph(cell, item);
			(void)d.Set(p, props::ListStyle, ListStyle::Bullet);
			(void)d.Set(p, props::SpaceAfter, 2.0);
		}
	}
	for (size_t c = 0; c < 2; ++c) { // paragraphe vide initial des cellules retiré
		const NodeId cell = TableGrid::Of(d, features).Cell(0, c);
		(void)d.DeleteNode(d.Find(cell)->Children().front());
	}
	editor.SelectNode(features);

	b.Styled(ParagraphStyle::Heading2, "Variantes");
	b.Styled(ParagraphStyle::Normal, "Deux variantes existent ; elles ne diffèrent que par l'antenne.");
	const NodeId variants = b.DataTable({{"Référence", "Flash", "Température", "Dimensions"},
										 {"AR-24-P", "4 Mio", "-40 … 85 °C", "18 × 25 × 3,1 mm"},
										 {"AR-24-U", "4 Mio", "-40 … 85 °C", "18 × 19 × 3,2 mm"}});
	// Appels de note en exposant.
	for (size_t c : {size_t(2), size_t(3)}) {
		const NodeId cell = TableGrid::Of(d, variants).Cell(0, c);
		const NodeId sup = b.Run(d.Find(cell)->Children().front(), c == 2 ? "1" : "2");
		(void)d.Set(sup, props::Script, TextScript::Superscript);
	}
	(void)editor.InsertCaption(CaptionKind::Table, "Variantes du module");
	editor.SelectNode(variants);
	b.Styled(ParagraphStyle::Normal, "1 Température ambiante autour du module.  2 Hors connecteur d'antenne externe.");
	(void)editor.SetText(PropertyId::FontSize, 8.5);
	(void)editor.SetText(PropertyId::TextColor, Named("DIM_GREY"));

	b.Styled(ParagraphStyle::Heading2, "Applications");
	b.Bullets({"Capteurs domestiques et bâtiments connectés", "Suivi d'objets et de colis", "Agriculture : humidité, température, "
																								  "irrigation"});
	b.Bullets({"Stations météo autonomes", "Compteurs d'eau et d'énergie"}, ListStyle::Dash, 1);

	// ── 2 Brochage ──
	b.Styled(ParagraphStyle::Heading1, "Brochage");
	(void)editor.SetParagraph(PropertyId::PageBreakBefore, true);
	b.Styled(ParagraphStyle::Heading2, "Disposition");
	b.Styled(ParagraphStyle::Normal, "Le schéma ci-dessous montre la position des broches, vue de dessus.");
	(void)editor.InsertShape(ShapeKind::Rectangle);
	const NodeId module = editor.Selection().Primary();
	(void)d.Set(module, props::Width, 220.0);
	(void)d.Set(module, props::Height, 120.0);
	(void)d.Set(module, props::HAlign, HorizontalAlign::Center);
	(void)d.Set(module, props::BackgroundColor, Named("HONEYDEW"));
	(void)d.Set(module, props::BorderColor, Named("FOREST_GREEN"));
	(void)d.Set(module, props::BorderWidth, 1.5);
	(void)editor.InsertCaption(CaptionKind::Figure, "Disposition des broches (vue de dessus)");
	const NodeId figure = d.AncestorOfType(editor.Caret().text, NodeType::Paragraph);

	b.Styled(ParagraphStyle::Heading2, "Description des broches");
	const NodeId pinsHeading = d.AncestorOfType(editor.Caret().text, NodeType::Paragraph);
	b.Styled(ParagraphStyle::Normal, "Le module a 24 broches, décrites dans le ");
	std::vector<std::vector<std::string>> pins{{"Nom", "N°", "Type", "Fonction"}};
	static constexpr const char *kTypes[] = {"P", "I/O", "I", "O"};
	for (int i = 1; i <= 24; ++i)
		pins.push_back({i == 1 || i == 24 ? std::string("GND") : i == 2 ? std::string("3V3") : std::format("IO{}", i), std::to_string(i),
						kTypes[i % 4], i == 2 ? "Alimentation" : i % 4 == 1 ? "Entrée / sortie, convertisseur" : "Entrée / sortie générale"});
	const NodeId pinParagraph = d.AncestorOfType(editor.Caret().text, NodeType::Paragraph);
	const NodeId pinTable = b.DataTable(pins);
	(void)editor.BandRows(Color::WHITE(), Named("ALICE_BLUE"));
	(void)editor.SetColumnWidth(pinTable, 1, 40);
	(void)editor.InsertCaption(CaptionKind::Table, "Description des broches");
	const NodeId pinCaption = d.AncestorOfType(editor.Caret().text, NodeType::Paragraph);
	// Renvoi « … décrites dans le Tableau 2, page n. » (numéro et page calculés).
	const NodeId ref = b.Run(pinParagraph, "");
	(void)d.Set(ref, props::Field, FieldKind::Reference);
	(void)d.Set(ref, props::Link, InternalLink(pinCaption));
	(void)d.Set(ref, props::TextColor, Named("COBALT_BLUE"));
	b.Run(pinParagraph, ", page ");
	const NodeId page = b.Run(pinParagraph, "");
	(void)d.Set(page, props::Field, FieldKind::PageReference);
	(void)d.Set(page, props::Link, InternalLink(pinCaption));
	b.Run(pinParagraph, ".");
	editor.SelectNode(pinTable);

	// ── 3 Mise en route ──
	b.Styled(ParagraphStyle::Heading1, "Mise en route");
	b.Styled(ParagraphStyle::Heading2, "Interroger le module");
	b.Styled(ParagraphStyle::Normal, "Le module répond aux requêtes HTTP de son réseau local. Pour lire les mesures :");
	b.Bullets({"Brancher le module et attendre le clignotement de la diode.", "Rejoindre son réseau « AR24-xxxx ».", "Envoyer la requête :"},
			  ListStyle::Decimal);
	b.Bullets({"GET /mesures pour toutes les voies ;", "GET /mesures?voie=2 pour une seule voie."}, ListStyle::LowerAlpha, 1);
	b.Styled(ParagraphStyle::Code, "{\n  \"module\": \"AR-24\",\n  \"mesures\": [\n    { \"voie\": 1, \"valeur\": 21.5, \"unite\": \"°C\" },\n"
								   "    { \"voie\": 2, \"valeur\": 48, \"unite\": \"%\" }\n  ]\n}");
	(void)editor.InsertCaption(CaptionKind::Listing, "Réponse du module (JSON)");
	b.Styled(ParagraphStyle::Heading2, "Pour aller plus loin");
	b.Styled(ParagraphStyle::Normal, "Le brochage complet est donné en section ");
	const NodeId further = d.AncestorOfType(editor.Caret().text, NodeType::Paragraph);
	const NodeId sectionRef = b.Run(further, "");
	(void)d.Set(sectionRef, props::Field, FieldKind::Reference);
	(void)d.Set(sectionRef, props::Link, InternalLink(pinsHeading));
	(void)d.Set(sectionRef, props::TextColor, Named("COBALT_BLUE"));
	b.Run(further, " et la disposition en ");
	const NodeId figureRef = b.Run(further, "");
	(void)d.Set(figureRef, props::Field, FieldKind::Reference);
	(void)d.Set(figureRef, props::Link, InternalLink(figure));
	(void)d.Set(figureRef, props::TextColor, Named("COBALT_BLUE"));
	b.Run(further, ". Ctrl+clic sur un renvoi ou une entrée de la table des matières mène à sa cible.");
	b.Styled(ParagraphStyle::Quote, "Un capteur qui ne consomme rien n'existe pas ; un capteur qui consomme peu se conçoit.");

	// ── 4 Caractéristiques électriques ──
	b.Styled(ParagraphStyle::Heading1, "Caractéristiques électriques");
	b.Styled(ParagraphStyle::Normal, "Au-delà des valeurs limites, le module peut être endommagé.");
	const NodeId limits = b.DataTable({{"Grandeur", "Symbole", "Min.", "Max.", "Unité"},
									   {"Tension d'alimentation", "V", "-0,3", "3,6", "V"},
									   {"Courant par broche", "I", "—", "40", "mA"},
									   {"Température de stockage", "T", "-40", "105", "°C"}});
	for (const auto &[row, index] : {std::pair{size_t(1), "DD"}, std::pair{size_t(2), "IO"}, std::pair{size_t(3), "STG"}}) { // indices
		const NodeId cell = TableGrid::Of(d, limits).Cell(row, 1);
		const NodeId sub = b.Run(d.Find(cell)->Children().front(), index);
		(void)d.Set(sub, props::Script, TextScript::Subscript);
	}
	(void)editor.InsertCaption(CaptionKind::Table, "Valeurs limites absolues");

	// ── En-tête (titre courant) et pied (numéro de page) ──
	// Page de titre : en-tête vide (variante « première page »).
	const NodeId cover = d.CreateNode(NodeType::Header, b.section).Value();
	(void)d.Set(cover, props::Variant, HeaderFooterVariant::First);
	b.Paragraph(cover, "");
	const NodeId header = d.CreateNode(NodeType::Header, b.section).Value();
	const NodeId hp = d.CreateNode(NodeType::Paragraph, header).Value();
	const NodeId chapter = b.Run(hp, "");
	(void)d.Set(chapter, props::Field, FieldKind::Chapter);
	(void)d.Set(hp, props::FontSize, 8.5);
	(void)d.Set(hp, props::TextColor, Named("DIM_GREY"));
	(void)d.Set(hp, props::BorderWidth, 0.5);
	(void)d.Set(hp, props::BorderSides, border::kBottom);
	(void)d.Set(hp, props::BorderColor, Named("SILVER"));
	(void)d.Set(hp, props::PaddingBottom, 3.0);
	(void)editor.InsertPageNumber();

	// Contenu rédigé en français : légendes « Figure », « Tableau » quelle que soit la langue de
	// l'interface (InsertCaption pose le libellé de l'interface).
	for (NodeId p : d.FindAll(NodeType::Paragraph))
		if (d.Find(p)->Properties().Contains(PropertyId::CaptionLabel))
			(void)d.ClearProperty(p, PropertyId::CaptionLabel);
	editor.History().Clear(); // document de départ : historique vierge
	editor.History().MarkClean();
	editor.SelectNode(title);
}

} // namespace scribe
