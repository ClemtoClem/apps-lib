// Modèles de documents : catalogue intégré et modèles personnels.
#include "scribe/templates.hpp"

#include <algorithm>
#include <format>

#include <document/persistence.hpp>

#include "scribe/demo.hpp"

namespace scribe {

using namespace doc;

namespace {

/// Construction d'un modèle avec les actions de l'éditeur.
struct Writer {
	explicit Writer(Editor &editor) : e(editor) {}
	Editor &e;

	void Start(std::string_view name) {
		e.New();
		(void)e.Document().Set(e.Document().Root(), props::Name, name);
	}
	/// Paragraphe stylé après le bloc courant (le premier remplit le paragraphe vide initial).
	void Add(ParagraphStyle style, std::string_view text) {
		if (m_first) {
			(void)e.TypeText(text);
			m_first = false;
		} else {
			(void)e.InsertParagraph(text);
		}
		(void)e.ApplyStyle(style);
	}
	void Text(std::string_view text) { Add(ParagraphStyle::Normal, text); }
	void List(std::initializer_list<std::string_view> items, ListStyle style = ListStyle::Bullet) {
		for (std::string_view item : items) {
			Text(item);
			(void)e.ToggleList(style);
		}
	}
	void Table(const std::vector<std::vector<std::string_view>> &rows, bool header = true) {
		(void)e.InsertTable(int(rows.size()), int(rows.front().size()));
		const NodeId table = e.CurrentTable().table;
		const TableGrid g = TableGrid::Of(e.Document(), table);
		for (size_t r = 0; r < rows.size(); ++r)
			for (size_t c = 0; c < rows[r].size() && c < g.cells[r].size(); ++c) {
				const NodeId t = e.Document().Find(e.Document().Find(g.cells[r][c])->Children().front())->Children().front();
				(void)e.Document().Set(t, props::Text, rows[r][c]);
				if (header && r == 0)
					(void)e.Document().Set(t, props::Bold, true);
			}
		if (header) {
			for (NodeId cell : g.cells.front())
				(void)e.Document().Set(cell, props::BackgroundColor, Color::FromName("GAINSBORO").value_or(Color::WHITE()));
			(void)e.Document().Set(g.rows.front(), props::RepeatHeader, true);
		}
		e.SelectNode(table); // la suite après le tableau
	}
	void Finish() {
		doc::Document &d = e.Document(); // modèles rédigés en français : légendes françaises
		for (NodeId p : d.FindAll(NodeType::Paragraph))
			if (d.Find(p)->Properties().Contains(PropertyId::CaptionLabel))
				(void)d.ClearProperty(p, PropertyId::CaptionLabel);
		e.History().Clear();
		e.History().MarkClean();
		e.LeaveText();
	}

private:
	bool m_first = true;
};

void Blank(Editor &e) { e.New(); }

void Letter(Editor &e) {
	Writer w(e);
	w.Start("Lettre administrative");
	for (std::string_view line : {"Prénom Nom", "Adresse", "Code postal Ville", "Téléphone · courriel"}) {
		w.Text(line);
		(void)e.SetParagraph(PropertyId::SpaceAfter, 0.0);
	}
	for (std::string_view line : {"Destinataire", "Service", "Adresse", "Code postal Ville"}) { // bloc décalé à droite
		w.Text(line);
		(void)e.SetParagraph(PropertyId::IndentLeft, 260.0);
		(void)e.SetParagraph(PropertyId::SpaceAfter, 0.0);
	}
	w.Text("Ville, le 1er janvier 2027");
	(void)e.SetParagraph(PropertyId::HAlign, int64_t(HorizontalAlign::Right));
	w.Text("Objet : demande de …");
	(void)e.SetText(PropertyId::Bold, true);
	w.Text("Madame, Monsieur,");
	w.Text("Corps de la lettre : exposez ici l'objet de votre demande, les éléments utiles et ce que vous attendez de votre "
		   "interlocuteur.");
	(void)e.SetParagraph(PropertyId::HAlign, int64_t(HorizontalAlign::Justify));
	w.Text("Je vous prie d'agréer, Madame, Monsieur, l'expression de mes salutations distinguées.");
	w.Text("Signature");
	(void)e.SetParagraph(PropertyId::IndentLeft, 300.0);
	w.Finish();
}

void Minutes(Editor &e) {
	Writer w(e);
	w.Start("Compte rendu de réunion");
	w.Add(ParagraphStyle::Title, "Compte rendu de réunion");
	w.Add(ParagraphStyle::Subtitle, "Projet · date · lieu");
	w.Add(ParagraphStyle::Heading1, "Participants");
	w.List({"Prénom Nom (rôle)", "Prénom Nom (rôle)", "Excusés : …"});
	w.Add(ParagraphStyle::Heading1, "Ordre du jour");
	w.List({"Point d'avancement", "Décisions à prendre", "Questions diverses"}, ListStyle::Decimal);
	w.Add(ParagraphStyle::Heading1, "Décisions");
	w.Text("Résumé des décisions prises.");
	w.Add(ParagraphStyle::Heading1, "Actions");
	w.Table({{"Action", "Responsable", "Échéance"}, {"…", "…", "…"}, {"…", "…", "…"}});
	w.Add(ParagraphStyle::Heading1, "Prochaine réunion");
	w.Text("Date, heure et lieu.");
	w.Finish();
}

void Report(Editor &e) {
	Writer w(e);
	w.Start("Rapport de projet");
	(void)e.Document().Set(e.Document().Root(), props::HeadingNumbering, HeadingNumbering::Decimal);
	w.Add(ParagraphStyle::Title, "Rapport de projet");
	w.Add(ParagraphStyle::Subtitle, "Équipe · version 1.0");
	(void)e.InsertTableOfContents(TocContent::Headings);
	(void)e.InsertPageBreak();
	w.Add(ParagraphStyle::Heading1, "Introduction");
	w.Text("Contexte, objectifs et périmètre du projet.");
	w.Add(ParagraphStyle::Heading1, "Avancement");
	w.Add(ParagraphStyle::Heading2, "Réalisé");
	w.List({"Lot 1 : …", "Lot 2 : …"});
	w.Add(ParagraphStyle::Heading2, "Planning");
	w.Table({{"Jalon", "Prévu", "Réel", "État"}, {"Cadrage", "…", "…", "Terminé"}, {"Réalisation", "…", "…", "En cours"}});
	(void)e.InsertCaption(CaptionKind::Table, "Jalons du projet");
	e.SelectNode(e.Document().FindAll(NodeType::Table).back());
	w.Add(ParagraphStyle::Heading1, "Risques");
	w.Add(ParagraphStyle::Note, "Note : listez les risques, leur probabilité et les mesures prévues.");
	w.Add(ParagraphStyle::Heading1, "Conclusion");
	w.Text("Bilan et prochaines étapes.");
	w.Finish();
}

void Resume(Editor &e) {
	Writer w(e);
	w.Start("Curriculum vitae");
	w.Add(ParagraphStyle::Title, "Prénom Nom");
	w.Add(ParagraphStyle::Subtitle, "Intitulé du poste recherché");
	w.Text("Téléphone · courriel · ville · site personnel");
	w.Add(ParagraphStyle::Heading1, "Expérience");
	w.Add(ParagraphStyle::Heading3, "Poste — Entreprise (2023 – aujourd'hui)");
	w.List({"Réalisation marquante, chiffrée si possible", "Responsabilité principale"});
	w.Add(ParagraphStyle::Heading3, "Poste — Entreprise (2019 – 2023)");
	w.List({"Réalisation marquante", "Outils et méthodes"});
	w.Add(ParagraphStyle::Heading1, "Formation");
	w.List({"Diplôme — Établissement (année)", "Certification (année)"});
	w.Add(ParagraphStyle::Heading1, "Compétences");
	w.Table({{"Domaine", "Niveau"}, {"…", "Avancé"}, {"…", "Intermédiaire"}}, false);
	(void)e.ApplyBorders(BorderPreset::None, 0, Color::BLACK());
	w.Add(ParagraphStyle::Heading1, "Langues");
	w.List({"Français : langue maternelle", "Anglais : courant"});
	w.Finish();
}

void Article(Editor &e) {
	Writer w(e);
	w.Start("Article de blog");
	w.Add(ParagraphStyle::Title, "Titre de l'article");
	w.Add(ParagraphStyle::Subtitle, "Chapeau : une phrase qui donne envie de lire la suite.");
	w.Text("Introduction : posez le sujet et la question à laquelle l'article répond.");
	w.Add(ParagraphStyle::Heading2, "Première partie");
	w.Text("Développement, exemples, illustrations.");
	w.Add(ParagraphStyle::Quote, "Une citation qui appuie le propos.");
	w.Add(ParagraphStyle::Heading2, "Deuxième partie");
	w.Text("Suite du développement.");
	w.Add(ParagraphStyle::Code, "// extrait de code\nint main() {\n    return 0;\n}");
	w.Add(ParagraphStyle::Heading2, "Conclusion");
	w.Text("Ce qu'il faut retenir, et un appel à commenter.");
	w.Finish();
}

void Recipe(Editor &e) {
	Writer w(e);
	w.Start("Recette de cuisine");
	w.Add(ParagraphStyle::Title, "Nom de la recette");
	w.Add(ParagraphStyle::Subtitle, "Pour 4 personnes · préparation 20 min · cuisson 30 min");
	w.Add(ParagraphStyle::Heading1, "Ingrédients");
	w.List({"200 g de …", "2 …", "1 pincée de sel"});
	w.Add(ParagraphStyle::Heading1, "Préparation");
	w.List({"Préparer les ingrédients.", "Mélanger …", "Cuire …", "Servir chaud."}, ListStyle::Decimal);
	w.Add(ParagraphStyle::Note, "Astuce : une variante ou un conseil de présentation.");
	w.Finish();
}

void Tasks(Editor &e) {
	Writer w(e);
	w.Start("Liste de tâches");
	w.Add(ParagraphStyle::Title, "Liste de tâches");
	w.Add(ParagraphStyle::Heading2, "Aujourd'hui");
	w.List({"Tâche prioritaire", "Tâche suivante"}, ListStyle::Decimal);
	w.Add(ParagraphStyle::Heading2, "Cette semaine");
	w.List({"…", "…"});
	w.Add(ParagraphStyle::Heading2, "Suivi");
	w.Table({{"Tâche", "Responsable", "Échéance", "État"}, {"…", "…", "…", "À faire"}, {"…", "…", "…", "En cours"}});
	w.Finish();
}

void QuickNote(Editor &e) {
	Writer w(e);
	w.Start("Note rapide");
	w.Add(ParagraphStyle::Heading1, "Note");
	w.Text("");
	w.Finish();
}

void Demo(Editor &e) {
	BuildDemo(e); // fiche technique de démonstration : toutes les fonctions de l'éditeur
	e.LeaveText();
}

constexpr BuiltinTemplate kBuiltins[] = {
	{"blank", "Document vierge", &Blank},		 {"letter", "Lettre administrative", &Letter}, {"minutes", "Compte rendu de réunion", &Minutes},
	{"report", "Rapport de projet", &Report},	 {"resume", "Curriculum vitae", &Resume},		  {"demo", "Démo", &Demo},
	{"article", "Article de blog", &Article},	 {"recipe", "Recette de cuisine", &Recipe},	  {"tasks", "Liste de tâches", &Tasks},
	{"note", "Note rapide", &QuickNote},
};

} // namespace

std::span<const BuiltinTemplate> BuiltinTemplates() noexcept { return kBuiltins; }

const BuiltinTemplate *FindBuiltinTemplate(std::string_view key) noexcept {
	for (const BuiltinTemplate &t : kBuiltins)
		if (t.key == key)
			return &t;
	return nullptr;
}

std::string BuiltinTemplateKeys() {
	std::string out;
	for (const BuiltinTemplate &t : kBuiltins)
		out += (out.empty() ? "" : ", ") + std::string(t.key);
	return out;
}

// ── Modèles personnels ──

bool TemplateStore::ValidName(std::string_view name) noexcept {
	if (name.empty() || name.size() > 80 || name.front() == '.' || name.front() == ' ' || name.back() == ' ')
		return false;
	return name.find_first_of("/\\:*?\"<>|") == std::string_view::npos &&
		   std::ranges::none_of(name, [](char c) { return static_cast<unsigned char>(c) < 32; });
}

std::filesystem::path TemplateStore::PathOf(std::string_view name) const { return m_directory / (std::string(name) + ".docdb"); }

bool TemplateStore::Contains(std::string_view name) const {
	std::error_code ec;
	return ValidName(name) && std::filesystem::exists(PathOf(name), ec);
}

std::vector<std::string> TemplateStore::List() const {
	std::vector<std::string> out;
	std::error_code ec;
	for (const auto &entry : std::filesystem::directory_iterator(m_directory, ec))
		if (entry.is_regular_file(ec) && entry.path().extension() == ".docdb")
			out.push_back(entry.path().stem().string());
	std::ranges::sort(out);
	return out;
}

Status TemplateStore::Create(std::string_view name, const DocumentView &document) {
	if (!ValidName(name))
		return Failure(ErrorCode::InvalidValue, std::format("nom de modèle invalide : « {} »", name));
	if (Contains(name))
		return Failure(ErrorCode::InvalidValue, std::format("le modèle « {} » existe déjà", name));
	std::error_code ec;
	std::filesystem::create_directories(m_directory, ec);
	if (ec)
		return Failure(ErrorCode::Io, std::format("dossier des modèles inaccessible : {}", ec.message()));
	return SaveDocument(document, PathOf(name));
}

Status TemplateStore::Rename(std::string_view from, std::string_view to) {
	if (!Contains(from))
		return Failure(ErrorCode::NotFound, std::format("modèle « {} » introuvable", from));
	if (!ValidName(to))
		return Failure(ErrorCode::InvalidValue, std::format("nom de modèle invalide : « {} »", to));
	if (from == to)
		return Failure(ErrorCode::Nothing, "nom inchangé");
	if (Contains(to))
		return Failure(ErrorCode::InvalidValue, std::format("le modèle « {} » existe déjà", to));
	std::error_code ec;
	std::filesystem::rename(PathOf(from), PathOf(to), ec);
	if (ec)
		return Failure(ErrorCode::Io, std::format("renommage impossible : {}", ec.message()));
	return Success();
}

Status TemplateStore::Remove(std::string_view name) {
	if (!Contains(name))
		return Failure(ErrorCode::NotFound, std::format("modèle « {} » introuvable", name));
	std::error_code ec;
	std::filesystem::remove(PathOf(name), ec);
	if (ec)
		return Failure(ErrorCode::Io, std::format("suppression impossible : {}", ec.message()));
	return Success();
}

} // namespace scribe
