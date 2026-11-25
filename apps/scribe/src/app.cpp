// Scribe — interface graphique.
//   ruban        : onglets (Fichier, Accueil, Insertion, Mise en page, Tableau, Disposition,
//                  Affichage), onglet actif suivant la sélection (SuggestTab) ;
//   espace       : disposition ancrable (ui::DockLayout) — document au centre, Propriétés,
//                  Historique et Arborescence à droite (onglets déplaçables) ;
//   barre d'état : page, sélection, état d'enregistrement, travaux de fond.
// Aucune logique documentaire ici : tout passe par `Editor` et `engine.document`.
#include "scribe/app.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <format>
#include <mutex>
#include <optional>
#include <map>
#include <set>
#include <tuple>

#include <sdl3/core/events_manager.hpp>
#include <sdl3/core/misc.hpp>
#include <sdl3/io/dialog.hpp>
#include <sdl3/render/svg.hpp>
#include <sdl3/render/ttf.hpp>
#include <ui/builder.hpp>
#include <ui/dock.hpp>
#include <ui/icons.hpp>
#include <ui/retained.hpp>
#include <ui/ui.hpp>

#include "scribe/display.hpp"
#include "scribe/draft.hpp"
#include "scribe/editor.hpp"
#include "scribe/i18n.hpp"
#include "scribe/navigator.hpp"
#include "scribe/ribbon.hpp"
#include "scribe/rulers.hpp"
#include "scribe/settings.hpp"
#include "scribe/table_tools.hpp"
#include "scribe/templates.hpp"
#include "scribe/theme.hpp"

namespace scribe {

namespace {

using namespace doc;
using Icon = ui::icons::MaterialIcons;
using sdl3::Rgba;

constexpr float kRibbonHeight = 120.f;
constexpr float kButton = 32.f;
constexpr float kGroupHeight = 60.f; ///< groupe du ruban : outils + légende
constexpr float kRowHeight = 24.f;
/// Ancre des fenêtres ouvertes au clavier (Ctrl+K) : sous le ruban.
constexpr ui::Rect kShortcutAnchor{420.f, 80.f, 0.f, 0.f};



char32_t I(Icon icon) { return ui::icons::Codepoint(icon); }

const double kSizes[] = {8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72};
const double kSpacings[] = {1.0, 1.15, 1.5, 2.0, 2.5, 3.0};
const PageFormat kFormats[] = {PageFormat::A4, PageFormat::A3, PageFormat::A5, PageFormat::Letter, PageFormat::Legal};
// Marges prédéfinies (points) : mêmes marges des quatre côtés.
constexpr float kMarginNarrow = 36;
constexpr float kMarginMedium = 54;
constexpr float kMarginNormal = 72;
constexpr float kMarginWide = 108;
constexpr float kMargins[] = {kMarginNarrow, kMarginMedium, kMarginNormal, kMarginWide};
const double kBorders[] = {0, 0.5, 1, 2};

std::vector<std::string_view> Numbers(std::span<const double> values, std::vector<std::string> &storage) {
	storage.clear();
	for (double v : values)
		storage.push_back(std::format("{:g}", v));
	std::vector<std::string_view> out(storage.begin(), storage.end());
	return out;
}

template <typename T, size_t N> int IndexOfNearest(const T (&values)[N], double v) {
	int best = 0;
	for (size_t i = 1; i < N; ++i)
		if (std::abs(double(values[i]) - v) < std::abs(double(values[size_t(best)]) - v))
			best = int(i);
	return best;
}

/// Couleurs nommées de sdl3 (nom, valeur par `Color::FromName`), dans l'ordre de la table.
const std::vector<std::pair<std::string_view, Color>> &NamedColorList() {
	static const std::vector<std::pair<std::string_view, Color>> list = [] {
		std::vector<std::pair<std::string_view, Color>> out;
		for (std::string_view name : Color::Names())
			if (const auto c = Color::FromName(name))
				out.emplace_back(name, *c);
		return out;
	}();
	return list;
}

/// Le nom contient-il le filtre (casse, espaces et soulignés ignorés) ?
bool MatchesColorFilter(std::string_view name, std::string_view filter) {
	const auto key = [](std::string_view s) {
		std::string k;
		for (char ch : s)
			if (ch != '_' && ch != ' ' && ch != '-')
				k.push_back(char(std::tolower(static_cast<unsigned char>(ch))));
		return k;
	};
	return key(name).find(key(filter)) != std::string::npos;
}

std::string Hex(const Color &c) { return std::format("#{:02x}{:02x}{:02x}{:02x}", c.r, c.g, c.b, c.a); }

std::optional<Color> ParseHex(std::string_view s) {
	if (!s.empty() && s.front() == '#')
		s.remove_prefix(1);
	if (s.size() != 6 && s.size() != 8)
		return std::nullopt;
	uint32_t v = 0;
	for (char ch : s) {
		const int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
		if (d < 0)
			return std::nullopt;
		v = v << 4 | uint32_t(d);
	}
	if (s.size() == 6)
		v = v << 8 | 0xFF;
	return Color(uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v));
}

/// Libellés des énumérations portées par des propriétés entières.
std::vector<std::string_view> EnumLabels(PropertyId id) {
	switch (id) {
	case PropertyId::HAlign: return {"Gauche", "Centré", "Droite", "Justifié", "Aucun"};
	case PropertyId::VAlign: return {"Haut", "Milieu", "Bas"};
	case PropertyId::Position: return {"Dans le flux", "Absolue (page)"};
	case PropertyId::Shape: {
		std::vector<std::string_view> names;
		for (const ShapeInfo &shape : ShapeCatalog())
			names.push_back(shape.name);
		return names;
	}
	case PropertyId::Field: return {"Aucun", "Numéro de page", "Nombre de pages"};
	case PropertyId::Variant: return {"Défaut", "Première page", "Pages paires", "Pages impaires"};
	case PropertyId::Format: return {"A4", "A3", "A5", "Letter", "Legal", "Personnalisé"};
	case PropertyId::PageOrientation: return {"Portrait", "Paysage"};
	case PropertyId::PositionReference: return {"Auto", "Nœud (place dans le flux)", "Parent", "Page"};
	case PropertyId::RotationOriginReference: return {"Forme (décalage du centre)", "Parent", "Page"};
	case PropertyId::HeadingNumbering: return {"Aucune", "1.2", "1.2."};
	case PropertyId::CaptionKind: return {"Aucune", "Figure", "Tableau", "Listing"};
	case PropertyId::ListStyle:
		return {"Aucune", "Puce •", "Tiret –", "Tiret long —", "Chevron »", "1.", "1)", "a.", "A.", "i.", "I."};
	case PropertyId::Script: return {"Normal", "Exposant", "Indice"};
	case PropertyId::TocContent: return {"Titres", "Figures", "Tableaux", "Listings"};
	default: return {};
	}
}

char32_t TypeIcon(NodeType t) {
	switch (t) {
	case NodeType::Document: return I(Icon::DESCRIPTION);
	case NodeType::Groups:
	case NodeType::Group: return I(Icon::WORKSPACES);
	case NodeType::Section: return I(Icon::VIEW_AGENDA);
	case NodeType::Body: return I(Icon::SUBJECT);
	case NodeType::Header:
	case NodeType::Footer: return I(Icon::VERTICAL_ALIGN_TOP);
	case NodeType::Page: return I(Icon::CROP_PORTRAIT);
	case NodeType::Paragraph: return I(Icon::SHORT_TEXT);
	case NodeType::Text: return I(Icon::TEXT_FIELDS);
	case NodeType::Table:
	case NodeType::TableRow:
	case NodeType::TableCell: return I(Icon::TABLE_CHART);
	case NodeType::Image: return I(Icon::IMAGE);
	case NodeType::Shape: return I(Icon::CROP_SQUARE);
	case NodeType::Drawing: return I(Icon::DRAW);
	case NodeType::PageBreak: return I(Icon::INSERT_PAGE_BREAK);
	default: return I(Icon::ARTICLE);
	}
}

std::string_view CategoryLabel(PropertyCategory c) {
	switch (c) {
	case PropertyCategory::Functional: return "Fonctionnelles";
	case PropertyCategory::Structural: return "Structurelles";
	case PropertyCategory::Visual: return "Visuelles";
	}
	return "?";
}

/// Longueur en centimètres pour un champ (« 2,5 »).
std::string FormatCm(double points) {
	std::string s = std::format("{:g}", std::round(points / kPointsPerCm * 100) / 100);
	std::ranges::replace(s, '.', ',');
	return s;
}

/// Nombre tapé (« 10.5 » ou « 10,5 ») ; nullopt si invalide.
std::optional<double> ParseNumber(std::string_view text) {
	std::string s(text);
	std::ranges::replace(s, ',', '.');
	char *end = nullptr;
	const double v = std::strtod(s.c_str(), &end);
	if (end == s.c_str())
		return std::nullopt;
	while (*end == ' ')
		++end;
	if (*end != '\0' || !std::isfinite(v))
		return std::nullopt;
	return v;
}

constexpr sdl3::SystemCursor kHandleCursors[kHandleCount] = {sdl3::SystemCursor::SIZE_NWSE, sdl3::SystemCursor::SIZE_NS, sdl3::SystemCursor::SIZE_NESW,
															 sdl3::SystemCursor::SIZE_EW,	sdl3::SystemCursor::SIZE_NWSE, sdl3::SystemCursor::SIZE_NS,
															 sdl3::SystemCursor::SIZE_NESW, sdl3::SystemCursor::SIZE_EW};
constexpr float kHandlePixels = 7.f; ///< tolérance de saisie des poignées (pixels)
constexpr double kGripWidth = 28.0;	 ///< poignée des blocs dans la marge gauche (points)

/// Bloc du flux qu'on peut saisir par sa poignée et déplacer (zone de dessin comprise).
bool IsBlock(NodeType t) {
	return t == NodeType::Paragraph || t == NodeType::Table || t == NodeType::Image || t == NodeType::Shape || t == NodeType::Drawing;
}

/// Conteneur du flux (cible d'un bloc déplacé) : corps, cellule, en-tête, pied.
bool IsFlowContainer(NodeType t) { return t == NodeType::Body || t == NodeType::TableCell || t == NodeType::Header || t == NodeType::Footer; }

class ScribeApp final : public app::Application {
public:
	explicit ScribeApp(LaunchOptions options) : m_options(std::move(options)) {}
	~ScribeApp() override { m_draftJob.Wait(); } // l'enregistrement du brouillon lit des tables globales

protected:
	bool OnInit() override {
		// Réglages : langue (système au premier lancement), thème, documents récents.
		m_settingsPath = DataDirectory() / "settings.ini";
		m_settings = Settings::Load(m_settingsPath);
		m_templates = TemplateStore(DataDirectory() / "templates");
		m_drafts = DraftStore(DataDirectory());
		m_draft = m_drafts.Load(); // brouillon d'une session précédente
		const std::string language = !m_options.language.empty() ? m_options.language
									 : m_settings.language.empty()  ? std::string(PickLanguage(SystemLanguages()))
																	 : m_settings.language;
		ApplyLanguage(LanguageIndex(language), false);
		ApplyTheme(ThemeIndex(!m_options.theme.empty() ? m_options.theme : m_settings.theme), false);
		m_home = true; // accueil, sauf modèle ou document demandé
		if (!m_options.templateName.empty()) {
			if (!StartFromName(m_options.templateName))
				Ui().Notify(TrF("modèle introuvable : {}", m_options.templateName), Ui().Style().danger);
		} else if (!m_options.path.empty()) {
			if (auto opened = m_editor.Open(m_options.path); !opened)
				Ui().Notify(TrF("ouverture impossible : {}", opened.Error().message), Ui().Style().danger);
			else
				ShowHome(false);
		}
		SetDockTabs();
		m_dock.SetSize(ui::DockLayout::Side::Right, 360.f);
		m_dock.Show(ui::DockLayout::Side::Left, false);
		m_dock.Show(ui::DockLayout::Side::Bottom, false);
		m_editor.ScanFontsAsync(sdl3::detail::SystemFontDirs()); // polices installées (arrière-plan)
		BuildInterface();
		return true;
	}

	void OnUpdate(double) override {
		m_editor.Update();
		UpdateDraft();
		if (m_editor.Fonts() != m_fontLibrary) { // inventaire des polices terminé
			m_fontLibrary = m_editor.Fonts();
			m_fontCache.SetLibrary(m_fontLibrary);
			m_families.clear();
			AddCjkFallback();
		}
		// Document ouvert ou enregistré : en tête des récents.
		if (const std::string path = m_editor.Path().string(); !path.empty() && path != m_lastPath) {
			m_lastPath = path;
			m_settings.AddRecent(path);
			SaveSettings();
		}
		for (std::string &m : m_editor.TakeMessages())
			Ui().Notify(std::move(m));
		ProcessDialogs();
		Shortcuts();
		const std::string title = m_home ? std::string("Scribe") : "Scribe — " + m_editor.Title();
		if (title != m_title) {
			m_title = title;
			SetTitle(m_title);
		}
	}

	void OnRender(const sdl3::FRect &area, double) override {
		if (m_rebuild) // hors de `Present` : aucun rappel de l'ancienne interface n'est en cours
			BuildInterface();
		if (m_status)
			m_status.SetText(StatusText());
		m_area = area;
		if (!m_home) { // état des commandes du ruban, calque des fenêtres surgissantes
			RefreshBindings();
			UpdateOverlay(area);
		}
		m_ui->Present(FrameSeconds(), area);
	}

	void OnShutdown() override {
		FlushDraft(); // dernières modifications non enregistrées : reprises au prochain lancement
	}

	bool OnCloseRequested() override {
		const auto now = std::chrono::steady_clock::now();
		if (!m_editor.Dirty() || now - m_closeArmed < std::chrono::seconds(5))
			return true;
		m_closeArmed = now;
		Ui().Notify(Tr("Modifications non enregistrées : fermer à nouveau pour quitter sans enregistrer."), Ui().Style().warning, 5.f);
		return false;
	}

	void TitleButtons(std::vector<ui::FrameButton> &, std::vector<ui::FrameButton> &right) override {
		// Libellés gardés en membres : les boutons ne retiennent que des vues.
		m_titleCode = std::string(Languages()[m_language].code);
		for (char &ch : m_titleCode)
			ch = char(std::toupper(static_cast<unsigned char>(ch)));
		m_titleLanguage = TrF("Langue : {} (clic : langue suivante)", Languages()[m_language].name);
		m_titleTheme = TrF("Thème : {} (clic : thème suivant)", Tr(Themes()[m_theme].name));
		m_titleHome = Tr("Accueil : modèles et documents récents");
		m_titleSave = Tr("Enregistrer (Ctrl+S)");
		if (!m_home)
			right.push_back({.id = "home", .icon = I(Icon::HOME), .tooltip = m_titleHome});
		right.push_back({.id = "lang", .icon = I(Icon::PUBLIC), .label = m_titleCode, .tooltip = m_titleLanguage});
		right.push_back({.id = "theme", .icon = I(Icon::PALETTE), .tooltip = m_titleTheme});
		if (!m_home)
			right.push_back({.id = "save", .icon = I(Icon::SAVE), .tooltip = m_titleSave});
	}
	void OnTitleButton(std::string_view id) override {
		if (id == "save")
			SaveCurrent();
		else if (id == "home")
			ShowHome(true);
		else if (id == "lang")
			ApplyLanguage((m_language + 1) % Languages().size(), true);
		else if (id == "theme")
			ApplyTheme((m_theme + 1) % Themes().size(), true);
	}

private:
	enum class Pending : uint8_t { Open, SaveAs, ExportPdf, ExportHtml, ExportCsv, InsertImage };

	// ── Interface ──

	/// Interface retenue : accueil, ou ruban et espace de travail. Reconstruite quand le mode, la
	/// langue ou le thème change (onglets traduits, feuille de style du thème).
	/// Reconstruction de l'interface au début de l'image suivante : appelée depuis un rappel de
	/// l'interface (clic sur une vignette de l'accueil, choix de la langue…), une reconstruction
	/// immédiate détruirait le `ui::Ui` en train de dessiner ce rappel.
	void RequestRebuild() { m_rebuild = true; }

	void BuildInterface() {
		m_rebuild = false;
		m_ui = std::make_unique<ui::Ui>(Canvas(), Events());
		m_ui->SetTheme(Ui().Style());
		m_ui->SetImmediateContext(&Ui());
		m_ui->Sheet(ui::StyleSheet::Parse(StyleSheetFor(P()), "scribe"));
		m_status = {};
		m_overlay = {};
		m_bindings.clear();
		if (m_home) {
			m_ui->Root(ui::Column().Children({ui::Custom([this](ui::Context &u) { HomeScreen(u); }).Grow()}));
			return;
		}
		// Ruban (fabrique : onglets défilants, pages qui défilent en largeur), espace de travail,
		// barre d'état, puis le calque des fenêtres surgissantes, dessiné en dernier (devant tout).
		static constexpr std::string_view kTabs[] = {"Fichier", "Accueil", "Insertion", "Références", "Mise en page", "Tableau", "Disposition", "Affichage"};
		const TrList tabs(kTabs);
		m_ui->Root(ui::Column().Children({
			ui::TabWidget()
				.Tabs(std::vector<std::string>(tabs.storage))
				.Bind(m_ribbonTab)
				.Height(kRibbonHeight)
				.Children({FileTab(), HomeTab(), InsertTab(), ReferencesTab(), PageTab(), TableTab(), ArrangeTab(), ViewTab()}),
			ui::Custom([this](ui::Context &u) { Workspace(u); }).Grow(),
			ui::Text("").Ref(m_status).Class("status"),
			ui::Custom([this](ui::Context &u) {
				ColorPopup(u);
				LinkPopup(u);
				ShapesPopup(u);
				PromptPopup(u);
			}).Ref(m_overlay).Hidden(),
		}));
	}

	/// Calque des fenêtres surgissantes : exactement sur celles qui sont ouvertes (il capte alors
	/// le pointeur à la place du ruban et du document), masqué sinon.
	void UpdateOverlay(const sdl3::FRect &area) {
		if (!m_overlay)
			return;
		std::optional<ui::Rect> box;
		const auto add = [&box](const ui::Rect &r) {
			const ui::Rect grown{r.x - 4, r.y - 4, r.w + 12, r.h + 12}; // ombre comprise
			if (!box) {
				box = grown;
				return;
			}
			const float x0 = std::min(box->x, grown.x), y0 = std::min(box->y, grown.y);
			box = ui::Rect{x0, y0, std::max(box->x + box->w, grown.x + grown.w) - x0, std::max(box->y + box->h, grown.y + grown.h) - y0};
		};
		if (m_colorTarget != ColorTarget::None)
			add(m_popupRect);
		if (m_linkOpen)
			add(m_linkRect);
		if (m_shapesOpen)
			add(m_shapesRect);
		if (m_prompt.kind != Prompt::None) // dialogue : toute la fenêtre (assombrie, modale)
			box = area;
		m_overlay.SetHidden(!box);
		if (box)
			m_overlay.Place({box->x - area.x, box->y - area.y, box->w, box->h});
	}

	// ── Langue, thème, réglages ──

	void SetDockTabs() {
		m_dock.SetTabs(ui::DockLayout::Side::Center, {{"document", Tr("Document"), I(Icon::ARTICLE)}});
		m_dock.SetTabs(ui::DockLayout::Side::Right, {{"outline", Tr("Plan"), I(Icon::TOC)},
													 {"properties", Tr("Propriétés"), I(Icon::TUNE)},
													 {"history", Tr("Historique"), I(Icon::HISTORY)},
													 {"tree", Tr("Arborescence"), I(Icon::ACCOUNT_TREE)}});
	}

	void ApplyTheme(size_t index, bool save) {
		m_theme = std::min(index, Themes().size() - 1);
		SetPalette(Themes()[m_theme].make());
		Ui().Style() = P().ui; // widgets immédiats, barre de titre, notifications
		if (m_ui)
			RequestRebuild();
		if (save) {
			m_settings.theme = std::string(Themes()[m_theme].key);
			SaveSettings();
			Ui().Notify(TrF("Thème : {}", Tr(Themes()[m_theme].name)));
		}
	}

	void ApplyLanguage(size_t index, bool save) {
		index = std::min(index, Languages().size() - 1);
		if (!LoadLanguage(Languages()[index].code)) {
			Ui().Notify(TrF("traduction introuvable : {}", Languages()[index].name), Ui().Style().warning);
			return;
		}
		m_language = index;
		m_families.clear();
		SetDockTabs();
		if (m_ui)
			RequestRebuild();
		if (save) {
			m_settings.language = std::string(Languages()[index].code);
			SaveSettings();
			Ui().Notify(TrF("Langue : {}", Languages()[index].name));
		}
	}

	void SaveSettings() {
		if (!m_settings.Save(m_settingsPath))
			Ui().Notify(TrF("réglages non enregistrés : {}", m_settingsPath.string()), Ui().Style().warning);
	}

	/// Police de repli pour les caractères absents des polices de l'interface (chinois, japonais).
	void AddCjkFallback() {
		if (!m_fontLibrary)
			return;
		static constexpr std::string_view kCandidates[] = {"Noto Sans CJK SC", "Noto Sans CJK JP", "Noto Sans CJK TC", "Source Han Sans SC",
														   "WenQuanYi Micro Hei", "WenQuanYi Zen Hei", "Droid Sans Fallback", "Noto Serif CJK JP"};
		for (std::string_view family : kCandidates)
			if (const auto path = m_fontLibrary->PathOf(family, false, false); !path.empty()) {
				Canvas().AddFallbackFont(path.string());
				return;
			}
	}

	void ShowHome(bool home) {
		if (home)
			FlushDraft(); // le bandeau « Reprendre » montre l'état exact du document
		m_home = home;
		m_documentOpen = m_documentOpen || !home; // un document a été ouvert ou créé
		m_homePage = HomePage::New;
		RequestRebuild();
	}

	/// Nouveau document depuis un modèle (intégré ou personnel), puis l'éditeur.
	void StartFrom(const BuiltinTemplate *builtin, const std::string &userTemplate) {
		FlushDraft(); // le document remplacé reste disponible comme brouillon
		if (builtin) {
			builtin->build(m_editor);
			if (builtin->key != "blank")
				m_editor.SetName(Tr(builtin->name));
		} else if (auto s = m_editor.NewFromFile(m_templates.PathOf(userTemplate)); !s) {
			Report(s);
			return;
		} else
			m_editor.SetName(userTemplate);
		m_scrollY = 0;
		m_images.Clear();
		m_lastPath.clear();
		ShowHome(false);
	}

	/// Nouveau document depuis un modèle intégré (identifiant : « demo »…) ou personnel (nom) ;
	/// faux s'il n'existe pas.
	bool StartFromName(const std::string &name) {
		if (const BuiltinTemplate *builtin = FindBuiltinTemplate(name))
			StartFrom(builtin, {});
		else if (m_templates.Contains(name))
			StartFrom(nullptr, name);
		else
			return false;
		return true; // existe (un échec de lecture est déjà signalé)
	}

	// ── Accueil : modèles, documents récents, paramètres ──

	enum class HomePage : uint8_t { New, Settings };

	void HomeScreen(ui::Context &u) {
		sdl3::Canvas &c = u.Canvas();
		const ui::Rect area = u.Remaining();
		const float sideW = 220.f;
		const ui::Rect side{area.x, area.y, sideW, area.h}, main{area.x + sideW, area.y, area.w - sideW, area.h};
		c.FillRect(side, P().sidebar);
		c.FillRect(main, P().ui.background);
		const sdl3::Color sideText = Themes()[m_theme].key == "light" ? Rgba(255, 255, 255) : P().strong;
		c.Text(side.x + 22, side.y + 26, "Scribe", sideText, 24, sdl3::CanvasFont::Bold);
		const auto item = [&](float y, Icon icon, std::string_view label, bool selected) {
			const ui::Rect r{side.x, y, side.w, 44};
			const bool hovered = u.MouseOver(r);
			if (selected)
				c.FillRect(r, Rgba(0, 0, 0, 60));
			else if (hovered)
				c.FillRect(r, Rgba(255, 255, 255, 18));
			c.Icon({r.x + 18, r.y + 11, 22, 22}, I(icon), sideText, 20.f);
			c.Text(r.x + 52, r.y + 13, label, sideText, 14);
			return hovered && u.MousePressed();
		};
		
		if (item(side.y + 80, Icon::FOLDER_OPEN, Tr("Ouvrir…"), false))
			ShowOpen(Pending::Open, {{::String(Tr("Documents Scribe")), "docdb"}});
		if (m_documentOpen && item(side.y + 124, Icon::ARTICLE, Tr("Retour au document"), false))
			ShowHome(false);
		
		if (item(side.y + side.h - 140, Icon::HOME, Tr("Nouveau et modèles"), m_homePage == HomePage::New))
			m_homePage = HomePage::New;
		if (item(side.y + side.h - 96, Icon::SETTINGS, Tr("Paramètres"), m_homePage == HomePage::Settings))
			m_homePage = HomePage::Settings;
		if (item(side.y + side.h - 52, Icon::DELETE_SWEEP, Tr("Effacer l'historique"), false)) {
			m_settings.recent.clear();
			SaveSettings();
		}
		u.BeginScroll("home", main);
		if (m_homePage == HomePage::Settings)
			HomeSettings(u);
		else
			HomeNew(u);
		u.EndScroll();
		PromptPopup(u);
	}

	/// Vignette d'un modèle ; rend vrai au clic (hors des boutons de la vignette).
	bool TemplateCard(ui::Context &u, float x, float y, std::string_view name, Icon icon, bool user, bool &rename, bool &remove) {
		sdl3::Canvas &c = u.Canvas();
		const ui::Rect card{x, y, 130, 170};
		const bool hovered = u.MouseOver(card) && m_prompt.kind == Prompt::None;
		c.FillRect({card.x + 2, card.y + 3, card.w, card.h}, Rgba(0, 0, 0, 40));
		c.FillRect(card, P().card);
		c.StrokeRect(card, hovered ? P().ui.accent : P().border, hovered ? 2.f : 1.f);
		c.Icon({card.x + 40, card.y + 60, 50, 50}, I(icon), P().ui.accent, icon == Icon::ADD ? 44.f : 40.f);
		bool buttons = false;
		if (user && hovered) { // renommer, supprimer
			if (u.IconButton({card.x + card.w - 58, card.y + 4, 26, 26}, I(Icon::EDIT), Tr("Renommer le modèle")))
				rename = buttons = true;
			if (u.IconButton({card.x + card.w - 30, card.y + 4, 26, 26}, I(Icon::DELETE), Tr("Supprimer le modèle")))
				remove = buttons = true;
		}
		// Nom sur deux lignes au plus, sous la vignette.
		const std::string label(name);
		const float maxW = card.w;
		std::string first = label, second;
		if (c.MeasureText(label, 13) > maxW)
			if (const size_t cut = label.rfind(' ', label.size() / 2 + 6); cut != std::string::npos) {
				first = label.substr(0, cut);
				second = label.substr(cut + 1);
			}
		c.Text(card.x, card.y + card.h + 8, c.Ellipsize(first, maxW, 13), P().text, 13);
		if (!second.empty())
			c.Text(card.x, card.y + card.h + 26, c.Ellipsize(second, maxW, 13), P().text, 13);
		return hovered && u.MousePressed() && !buttons && !u.MouseOver({card.x + card.w - 60, card.y, 60, 32});
	}

	void HomeNew(ui::Context &u) {
		sdl3::Canvas &c = u.Canvas();
		const ui::Rect view = u.ScrollViewport();
		const float left = view.x + 32, columns = std::max(1.f, std::floor((view.w - 64 + 16) / 146.f));
		float y = u.ColumnY() + 48;
		if (m_draft && DraftBanner(u, {left, y, view.w - 64, 62})) {
			ResumeDraft();
			return;
		}
		if (m_draft)
			y += 62 + 30;
		c.Text(left, y, Tr("Nouveau document"), P().strong, 24);
		y += 46;
		// Modèles intégrés.
		const auto builtins = BuiltinTemplates();
		size_t index = 0;
		const BuiltinTemplate *chosen = nullptr;
		std::string chosenUser;
		const auto place = [&](size_t i) { return std::pair{left + float(i % size_t(columns)) * 146.f, y + float(i / size_t(columns)) * 232.f}; };
		for (const BuiltinTemplate &t : builtins) {
			const auto [x, cy] = place(index++);
			bool rename = false, remove = false;
			if (TemplateCard(u, x, cy, Tr(t.name), t.key == "blank" ? Icon::ADD : Icon::ARTICLE, false, rename, remove))
				chosen = &t;
		}
		y += float((index + size_t(columns) - 1) / size_t(columns)) * 232.f + 12;
		// Modèles personnels.
		c.Text(left, y, Tr("Mes modèles"), P().strong, 17);
		const std::string saveLabel = Tr("Enregistrer le document comme modèle…");
		if (u.ButtonAt({left + 160, y - 4, c.MeasureText(saveLabel, 14) + 32, 28}, saveLabel, m_documentOpen))
			OpenPrompt(Prompt::CreateTemplate, {}, Tr("Nouveau modèle"));
		y += 40;
		const std::vector<std::string> mine = m_templates.List();
		if (mine.empty()) {
			c.Text(left, y, Tr("Aucun modèle personnel : ouvrez un document puis « Enregistrer le document comme modèle… »."), P().faint, 13);
			y += 40;
		} else {
			index = 0;
			for (const std::string &name : mine) {
				const auto [x, cy] = place(index++);
				bool rename = false, remove = false;
				if (TemplateCard(u, x, cy, name, Icon::DESCRIPTION, true, rename, remove))
					chosenUser = name;
				if (rename)
					OpenPrompt(Prompt::RenameTemplate, name, name);
				if (remove)
					OpenPrompt(Prompt::DeleteTemplate, name, {});
			}
			y += float((index + size_t(columns) - 1) / size_t(columns)) * 232.f + 12;
		}
		// Documents récents.
		c.Text(left, y, Tr("Récents"), P().strong, 17);
		y += 36;
		if (m_settings.recent.empty()) {
			c.Text(left, y, Tr("Aucun document récent."), P().faint, 13);
			y += 30;
		}
		std::string open;
		for (const std::string &path : m_settings.recent) {
			const ui::Rect row{left - 8, y - 4, std::min(view.w - 64, 900.f), 40};
			const bool hovered = u.MouseOver(row) && m_prompt.kind == Prompt::None;
			if (hovered)
				c.FillRoundRect(row, 4.f, P().hover);
			c.Icon({row.x + 6, row.y + 8, 24, 24}, I(Icon::DESCRIPTION), P().muted, 20.f);
			const std::filesystem::path fs(path);
			c.Text(row.x + 38, row.y + 4, fs.filename().string(), P().text, 14);
			c.Text(row.x + 38, row.y + 22, c.Ellipsize(fs.parent_path().string(), row.w - 50, 11), P().faint, 11);
			if (hovered && u.MousePressed())
				open = path;
			y += 44;
		}
		u.Next(y - u.ColumnY() + 40); // hauteur du contenu défilant
		if (chosen || !chosenUser.empty()) {
			StartFrom(chosen, chosenUser);
			return;
		}
		if (!open.empty()) {
			std::error_code ec;
			if (!std::filesystem::exists(open, ec)) {
				Ui().Notify(TrF("fichier introuvable : {}", open), Ui().Style().warning);
				m_settings.RemoveRecent(open);
				SaveSettings();
				return;
			}
			FlushDraft();
			m_editor.OpenAsync(open);
			m_scrollY = 0;
			m_lastPath.clear();
			ShowHome(false);
		}
	}

	// ── Brouillon du dernier document modifié ──

	/// Le brouillon enregistré est celui du document ouvert.
	[[nodiscard]] bool DraftIsLive() const { return m_draft && m_draftDocument == m_editor.DocumentId(); }

	/// Enregistre le brouillon en arrière-plan (au plus toutes les deux secondes) tant que le
	/// document a des modifications non enregistrées ; le supprime une fois le document enregistré.
	void UpdateDraft() {
		if (m_draftJob.Poll() && !m_draftJob.Get()->empty()) {
			Ui().Notify(TrF("brouillon non enregistré : {}", *m_draftJob.Get()), Ui().Style().warning);
			m_draft = m_drafts.Load();
		}
		if (!m_editor.Dirty()) {
			if (DraftIsLive()) // enregistré, ou revenu à l'état enregistré
				DiscardDraft();
			return;
		}
		const uint64_t state = m_editor.History().StateId();
		const auto now = std::chrono::steady_clock::now();
		if ((DraftIsLive() && state == m_draftState) || m_draftJob.Loading() || now - m_draftSaved < std::chrono::seconds(2))
			return;
		m_draftSaved = now;
		m_draftState = state;
		m_draftDocument = m_editor.DocumentId();
		m_draft = DraftInfo{m_editor.Name(), m_editor.Path()};
		m_draftJob.Request(jobs::ThreadPool::Global(),
						   [store = m_drafts, info = *m_draft, snap = m_editor.Document().TakeSnapshot()](std::stop_token) -> std::string {
							   const doc::Status s = store.Save(*snap, info);
							   return s ? std::string() : s.Error().message;
						   });
	}

	/// Enregistre le brouillon tout de suite (avant de remplacer le document, de quitter…).
	void FlushDraft() {
		m_draftJob.Wait();
		(void)m_draftJob.Poll();
		if (!m_editor.Dirty() || (DraftIsLive() && m_editor.History().StateId() == m_draftState))
			return;
		const DraftInfo info{m_editor.Name(), m_editor.Path()};
		if (auto s = m_drafts.Save(m_editor.Document(), info); !s) {
			Ui().Notify(TrF("brouillon non enregistré : {}", s.Error().message), Ui().Style().warning);
			return;
		}
		m_draft = info;
		m_draftDocument = m_editor.DocumentId();
		m_draftState = m_editor.History().StateId();
		m_draftSaved = std::chrono::steady_clock::now();
	}

	void DiscardDraft() {
		m_draftJob.Wait();
		(void)m_draftJob.Poll();
		m_drafts.Remove();
		m_draft.reset();
		m_draftState = UINT64_MAX;
	}

	/// Retour au document du brouillon : celui de l'éditeur s'il est encore ouvert, sinon le
	/// brouillon enregistré (session précédente, ou document remplacé depuis).
	void ResumeDraft() {
		FlushDraft();
		if (DraftIsLive()) {
			ShowHome(false);
			return;
		}
		const std::optional<DraftInfo> info = m_drafts.Load();
		if (!info) {
			m_draft.reset();
			Ui().Notify(Tr("brouillon introuvable"), Ui().Style().warning);
			return;
		}
		if (auto s = m_editor.OpenDraft(m_drafts.DocumentFile(), info->source, info->name); !s) {
			Report(s);
			return;
		}
		m_draft = info;
		m_draftDocument = m_editor.DocumentId();
		m_draftState = m_editor.History().StateId();
		m_scrollY = 0;
		m_images.Clear();
		m_lastPath.clear();
		ShowHome(false);
	}

	/// Bandeau « Brouillon non sauvegardé » de l'accueil ; vrai au clic sur « Reprendre ».
	bool DraftBanner(ui::Context &u, const ui::Rect &r) {
		sdl3::Canvas &c = u.Canvas();
		const bool live = DraftIsLive();
		const sdl3::Color warning = P().ui.warning;
		c.FillRoundRect(r, 8.f, sdl3::FColor::Lerp(P().ui.background, warning, 0.16f).ToByte());
		c.StrokeRoundRect(r, 8.f, sdl3::FColor::Lerp(P().ui.background, warning, 0.7f).ToByte(), 1.f);
		c.Icon({r.x + 18, r.y + 18, 26, 26}, I(Icon::EDIT_NOTE), warning, 24.f);
		const std::string name = live ? m_editor.Name() : m_draft->name.empty() ? Tr("Sans titre") : m_draft->name;
		const std::string label = Tr("Reprendre");
		const float buttonW = c.MeasureText(label, 14, sdl3::CanvasFont::Bold) + 62;
		const ui::Rect button{r.x + r.w - buttonW - 16, r.y + 15, buttonW, 32};
		const float textW = button.x - (r.x + 58) - 12;
		c.Text(r.x + 58, r.y + 14, c.Ellipsize(Tr("Brouillon non sauvegardé"), textW, 14), P().strong, 14, sdl3::CanvasFont::Bold);
		c.Text(r.x + 58, r.y + 35, c.Ellipsize(name, textW, 12), P().muted, 12);
		bool discard = false;
		if (!live) // brouillon d'un document fermé : peut être abandonné
			discard = u.IconButton({button.x - 38, r.y + 18, 26, 26}, I(Icon::CLOSE), Tr("Supprimer le brouillon"));
		const bool hovered = u.MouseOver(button) && m_prompt.kind == Prompt::None;
		c.FillRoundRect(button, button.h / 2, hovered ? P().ui.accentHover : P().ui.accent);
		c.Icon({button.x + 16, button.y + 6, 20, 20}, I(Icon::PLAY_ARROW), P().ui.accentText, 18.f);
		c.Text(button.x + 42, button.y + 8, label, P().ui.accentText, 14, sdl3::CanvasFont::Bold);
		if (discard) {
			DiscardDraft();
			return false;
		}
		return hovered && u.MousePressed();
	}

	void HomeSettings(ui::Context &u) {
		sdl3::Canvas &c = u.Canvas();
		const ui::Rect view = u.ScrollViewport();
		const float left = view.x + 32;
		float y = u.ColumnY() + 48;
		c.Text(left, y, Tr("Paramètres"), P().strong, 24);
		y += 56;
		c.Text(left, y + 6, Tr("Langue de l'interface"), P().text, 14);
		std::vector<std::string_view> languages;
		for (const LanguageInfo &l : Languages())
			languages.push_back(l.name);
		int language = int(m_language);
		if (u.Choice({left + 240, y, 260, 30}, "home.language", language, languages))
			ApplyLanguage(size_t(language), true);
		y += 48;
		c.Text(left, y + 6, Tr("Thème de couleur"), P().text, 14);
		std::vector<std::string> themeNames;
		for (const ThemeInfo &t : Themes())
			themeNames.push_back(Tr(t.name));
		const std::vector<std::string_view> themes(themeNames.begin(), themeNames.end());
		int theme = int(m_theme);
		if (u.Choice({left + 240, y, 260, 30}, "home.theme", theme, themes))
			ApplyTheme(size_t(theme), true);
		y += 60;
		c.Text(left, y, TrF("Langue du système : {}", PickLanguage(SystemLanguages())), P().faint, 12);
		c.Text(left, y + 22, TrF("Réglages et modèles personnels : {}", DataDirectory().string()), P().faint, 12);
		u.Next(y - u.ColumnY() + 80);
	}

	// ── Fenêtre de saisie : créer, renommer, supprimer un modèle ──

	struct Prompt {
		enum Kind : uint8_t { None, CreateTemplate, RenameTemplate, DeleteTemplate } kind = None;
		std::string target, text;
		bool fresh = false;
	};

	void OpenPrompt(Prompt::Kind kind, std::string target, std::string text) {
		m_prompt = {kind, std::move(target), std::move(text), true};
	}

	void PromptPopup(ui::Context &u) {
		if (m_prompt.kind == Prompt::None)
			return;
		sdl3::Canvas &c = u.Canvas();
		const ui::Rect screen = u.Remaining();
		const ui::Rect r{screen.x + (screen.w - 420) / 2, screen.y + screen.h * 0.3f, 420, 150};
		c.FillRect(screen, Rgba(0, 0, 0, 90));
		c.FillRoundRect(r, 8.f, P().popup);
		c.StrokeRoundRect(r, 8.f, P().popupBorder, 1.f);
		const bool remove = m_prompt.kind == Prompt::DeleteTemplate;
		const std::string title = m_prompt.kind == Prompt::CreateTemplate ? Tr("Enregistrer le document comme modèle")
								  : remove								  ? TrF("Supprimer le modèle « {} » ?", m_prompt.target)
																		  : TrF("Renommer le modèle « {} »", m_prompt.target);
		c.Text(r.x + 16, r.y + 14, c.Ellipsize(title, r.w - 32, 14), P().strong, 14);
		bool submitted = false;
		if (remove) {
			c.Text(r.x + 16, r.y + 50, Tr("Le fichier du modèle est supprimé définitivement."), P().muted, 12);
		} else {
			if (m_prompt.fresh)
				u.SetFocus("scribe.prompt");
			u.TextInputAt({r.x + 16, r.y + 46, r.w - 32, 30}, "scribe.prompt", m_prompt.text, &submitted, Tr("Nom du modèle"));
		}
		m_prompt.fresh = false;
		const float bw = (r.w - 44) / 2;
		const bool valid = remove || TemplateStore::ValidName(m_prompt.text);
		bool ok = u.PrimaryButton({r.x + 16, r.y + r.h - 44, bw, 30}, remove ? Tr("Supprimer") : Tr("Valider"), valid) || (submitted && valid);
		const bool cancel = u.ButtonAt({r.x + 28 + bw, r.y + r.h - 44, bw, 30}, Tr("Annuler")) || u.KeycodePressed(SDLK_ESCAPE);
		if (!valid && !m_prompt.text.empty())
			c.Text(r.x + 16, r.y + 82, Tr("Nom invalide (caractères / \\ : * ? \" < > | interdits)."), P().ui.warning, 11);
		if (ok) {
			Status s = Success();
			switch (m_prompt.kind) {
			case Prompt::CreateTemplate: s = m_templates.Create(m_prompt.text, m_editor.Document()); break;
			case Prompt::RenameTemplate: s = m_templates.Rename(m_prompt.target, m_prompt.text); break;
			case Prompt::DeleteTemplate: s = m_templates.Remove(m_prompt.target); break;
			case Prompt::None: break;
			}
			if (s) {
				if (m_prompt.kind == Prompt::CreateTemplate)
					Ui().Notify(TrF("modèle enregistré : {}", m_prompt.text));
				m_prompt.kind = Prompt::None;
			} else {
				Report(s);
			}
		}
		if (cancel)
			m_prompt.kind = Prompt::None;
	}

	// ── Ruban : fabrique de widgets ──
	// Les commandes sont des ToolButton retenus (dans des ToolGroup, sur des pages défilantes) ;
	// leur état (actif, enfoncé, info-bulle, couleur courante) suit le document à chaque image
	// par des liaisons. Les listes modifiables (polices, tailles, marges…) restent des champs
	// immédiats posés dans la mise en page retenue : leurs menus s'affichent au premier plan.

	struct Binding {
		ui::Widget widget;
		std::function<void(ui::Widget &)> update;
	};

	/// Bouton d'outil : icône Material (ou fichier `apps/scribe/icons/<svg>.svg`, teinté),
	/// info-bulle, action (reçoit la boîte du bouton : ancre des fenêtres surgissantes) ; état
	/// recalculé à chaque image (actif, enfoncé, icône, info-bulle, barre de couleur).
	struct ToolSpec {
		char32_t icon = 0;
		std::string svg{};
		std::string tip{};
		std::function<void(const ui::Rect &anchor)> action{};
		std::function<bool()> enabled{};
		std::function<bool()> active{};
		std::function<char32_t()> glyph{};
		std::function<std::string()> tooltip{};
		std::function<Color()> swatch{};
	};

	ui::WidgetBuilder Tool(ToolSpec spec) {
		const std::string path = spec.svg.empty() ? std::string() : std::format("apps/scribe/icons/{}.svg", spec.svg);
		std::error_code ec;
		ui::WidgetBuilder b = !path.empty() && std::filesystem::exists(path, ec) ? ui::ToolButton(path) : ui::ToolButton(spec.icon);
		b.Width(kButton).Height(kButton).Tooltip(spec.tip);
		auto self = std::make_shared<ui::Widget>();
		b.Ref(*self);
		b.OnClick([self, action = std::move(spec.action)] {
			if (action)
				action(self->Bounds());
		});
		if (spec.enabled || spec.active || spec.glyph || spec.tooltip || spec.swatch)
			m_bindings.push_back({*self, [enabled = std::move(spec.enabled), active = std::move(spec.active), glyph = std::move(spec.glyph),
										  tooltip = std::move(spec.tooltip), swatch = std::move(spec.swatch)](ui::Widget &w) {
									  if (enabled)
										  w.SetEnabled(enabled());
									  if (active)
										  w.SetChecked(active());
									  if (glyph)
										  w.SetIcon(glyph());
									  if (tooltip)
										  w.SetTooltip(tooltip());
									  if (swatch)
										  w.SetSwatch(swatch());
								  }});
		return b;
	}

	/// Champ immédiat (liste, liste modifiable…) de largeur fixe dans un groupe ; `draw` reçoit sa
	/// boîte (hauteur d'un champ, centrée).
	ui::WidgetBuilder Field(float width, std::function<void(ui::Context &, const ui::Rect &)> draw) {
		return ui::Custom([draw = std::move(draw)](ui::Context &u) {
				   const ui::Rect r = u.Remaining();
				   draw(u, {r.x, r.y + (r.h - 28.f) / 2, r.w, 28.f});
			   })
			.Width(width)
			.Height(kButton);
	}

	/// Champ grisé : valeur affichée, sans saisie (commande sans objet pour la sélection).
	static void DisabledField(ui::Context &u, const ui::Rect &r, std::string_view text) {
		u.Canvas().FillRoundRect(r, 4.f, sdl3::WithAlpha(P().border, 60));
		u.Canvas().Text(r.x + 8, r.y + 7, u.Canvas().Ellipsize(text, r.w - 16, 13), P().faint, 13);
	}

	/// Liste modifiable immédiate : valeur tapée ou choisie ; info-bulle au survol.
	static bool EditableField(ui::Context &u, const ui::Rect &r, std::string_view id, std::string &value, std::span<const std::string_view> options,
							  std::string_view tip = {}) {
		if (!tip.empty() && u.MouseOver(r))
			u.Tooltip(tip);
		ui::ChoiceOptions o;
		o.arrows = false; // la place va au texte tapé
		return u.Choice(r, id, value, options, o);
	}

	/// Groupe du ruban : légende (fixe, ou recalculée à chaque image par `caption`).
	ui::WidgetBuilder Group(std::string label, std::vector<ui::WidgetBuilder> items, std::function<std::string()> caption = {}) {
		ui::WidgetBuilder g = ui::ToolGroup(label).Height(kGroupHeight);
		for (ui::WidgetBuilder &item : items)
			g.Child(std::move(item));
		if (caption) {
			ui::Widget w;
			g.Ref(w);
			m_bindings.push_back({w, [caption = std::move(caption)](ui::Widget &x) { x.SetText(caption()); }});
		}
		return g;
	}

	/// Page d'onglet : groupes en ligne dans une zone qui défile en largeur (barre de défilement
	/// et molette quand la fenêtre est trop étroite).
	static ui::WidgetBuilder RibbonPage(std::vector<ui::WidgetBuilder> groups) {
		ui::WidgetBuilder row = ui::Row().AlignItems(ui::Align::Start).Padding(math::Sides{4.f, 2.f, 4.f, 0.f});
		for (ui::WidgetBuilder &g : groups)
			row.Child(std::move(g));
		return ui::Scroll().Grow().Child(std::move(row));
	}

	/// Met à jour l'état des commandes du ruban (une image).
	void RefreshBindings() {
		for (Binding &b : m_bindings)
			b.update(b.widget);
	}

	[[nodiscard]] bool TextEditable() const {
		const SelectionContext &ctx = m_editor.Context();
		return !ctx.Empty() && !ctx.locked && (ctx.text || ctx.table || ctx.headerFooter);
	}
	[[nodiscard]] bool ParagraphEditable() const {
		const SelectionContext &ctx = m_editor.Context();
		return !ctx.Empty() && !ctx.locked && !m_editor.ParagraphTargets().empty();
	}
	/// Formes, images et zones de dessin de la sélection.
	[[nodiscard]] std::vector<NodeId> PlaceableSelection() const {
		std::vector<NodeId> out;
		for (NodeId id : m_editor.Selection().Nodes())
			if (const Node *n = m_editor.Document().Find(id);
				n && (n->Type() == NodeType::Shape || n->Type() == NodeType::Image || n->Type() == NodeType::Drawing))
				out.push_back(id);
		return out;
	}

	ui::WidgetBuilder FileTab() {
		return RibbonPage({
			Group(Tr("Document"),
				  {Tool({.icon = I(Icon::HOME), .tip = Tr("Accueil : modèles et documents récents"), .action = [this](const ui::Rect &) { ShowHome(true); }}),
				   Tool({.icon = I(Icon::BOOKMARK),
						 .tip = Tr("Enregistrer comme modèle…"),
						 .action = [this](const ui::Rect &) { OpenPrompt(Prompt::CreateTemplate, {}, m_editor.Title()); }}),
				   Tool({.icon = I(Icon::NOTE_ADD), .tip = Tr("Nouveau (Ctrl+N)"), .action = [this](const ui::Rect &) { NewDocument(); }}),
				   Tool({.icon = I(Icon::FOLDER_OPEN),
						 .tip = Tr("Ouvrir… (Ctrl+O)"),
						 .action = [this](const ui::Rect &) { ShowOpen(Pending::Open, {{::String(Tr("Documents Scribe")), "docdb"}}); }}),
				   Tool({.icon = I(Icon::SAVE), .tip = Tr("Enregistrer (Ctrl+S)"), .action = [this](const ui::Rect &) { SaveCurrent(); }}),
				   Tool({.icon = I(Icon::SAVE_AS),
						 .tip = Tr("Enregistrer sous… (Ctrl+Maj+S)"),
						 .action = [this](const ui::Rect &) { ShowSave(Pending::SaveAs, {{::String(Tr("Documents Scribe")), "docdb"}}); }})}),
			Group(Tr("Exporter"),
				  {Tool({.icon = I(Icon::PICTURE_AS_PDF),
						 .tip = Tr("Exporter en PDF…"),
						 .action = [this](const ui::Rect &) { ShowSave(Pending::ExportPdf, {{::String(Tr("PDF")), "pdf"}}); }}),
				   Tool({.icon = I(Icon::HTML),
						 .tip = Tr("Exporter en HTML…"),
						 .action = [this](const ui::Rect &) { ShowSave(Pending::ExportHtml, {{::String(Tr("Page HTML")), "html"}}); }}),
				   Tool({.icon = I(Icon::TABLE_VIEW),
						 .tip = Tr("Exporter le tableau sélectionné en CSV…"),
						 .action = [this](const ui::Rect &) { ShowSave(Pending::ExportCsv, {{::String(Tr("Tableur (CSV)")), "csv"}}); },
						 .enabled = [this] { return m_editor.Context().enclosingTable != kNoNode; }})}),
		});
	}

	ui::WidgetBuilder HomeTab() {
		const auto textFlag = [this](PropertyId p, Icon icon, std::string tip) {
			return Tool({.icon = I(icon),
						 .tip = std::move(tip),
						 .action = [this, p](const ui::Rect &) { Report(m_editor.ToggleText(p)); },
						 .enabled = [this] { return TextEditable(); },
						 .active = [this, p] { return TextEditable() && m_editor.TextFlag(p); }});
		};
		const auto script = [this](TextScript value, Icon icon, std::string tip) {
			const auto current = [this] {
				return FirstTextTarget() != kNoNode ? m_editor.Document().Get(FirstTextTarget(), props::Script) : TextScript::Normal;
			};
			return Tool({.icon = I(icon),
						 .tip = std::move(tip),
						 .action = [this, value, current](const ui::Rect &) {
							 Report(m_editor.SetText(PropertyId::Script, int64_t(current() == value ? TextScript::Normal : value)));
						 },
						 .enabled = [this] { return TextEditable(); },
						 .active = [this, value, current] { return TextEditable() && current() == value; }});
		};
		const auto align = [this](HorizontalAlign value, Icon icon, std::string tip) {
			return Tool({.icon = I(icon),
						 .tip = std::move(tip),
						 .action = [this, value](const ui::Rect &) { Report(m_editor.SetParagraph(PropertyId::HAlign, int64_t(value))); },
						 .enabled = [this] { return ParagraphEditable(); },
						 .active = [this, value] {
							 return ParagraphEditable() && m_editor.Document().Get(m_editor.ParagraphTargets().front(), props::HAlign) == value;
						 }});
		};
		const auto list = [this](ListStyle value, Icon icon, std::string tip) {
			return Tool({.icon = I(icon),
						 .tip = std::move(tip),
						 .action = [this, value](const ui::Rect &) { Report(m_editor.ToggleList(value)); },
						 .enabled = [this] { return ParagraphEditable(); },
						 .active = [this, value] {
							 return ParagraphEditable() && m_editor.Document().Get(m_editor.ParagraphTargets().front(), props::ListStyle) == value;
						 }});
		};
		const auto canCopy = [this] { return m_editor.HasTextRange() || (!m_editor.Editing() && !m_editor.Selection().Empty()); };
		const auto sampleColor = [this](bool highlight) {
			const NodeId sample = FirstTextTarget();
			if (sample == kNoNode)
				return highlight ? Color::TRANSPARENT() : Color::BLACK();
			if (!highlight)
				return m_editor.Document().Get(sample, props::TextColor);
			return m_editor.Document().Find(sample)->Type() == NodeType::Text ? m_editor.Document().Get(sample, props::BackgroundColor)
																			  : Color::TRANSPARENT();
		};
		return RibbonPage({
			Group(Tr("Historique"),
				  {Tool({.icon = I(Icon::UNDO),
						 .tip = Tr("Annuler (Ctrl+Z)"),
						 .action = [this](const ui::Rect &) { Report(m_editor.Undo()); },
						 .enabled = [this] { return m_editor.History().CanUndo(); },
						 .tooltip =
							 [this] {
								 return m_editor.History().CanUndo() ? TrF("Annuler : {}", m_editor.History().UndoName()) : Tr("Annuler (Ctrl+Z)");
							 }}),
				   Tool({.icon = I(Icon::REDO),
						 .tip = Tr("Rétablir (Ctrl+Y)"),
						 .action = [this](const ui::Rect &) { Report(m_editor.Redo()); },
						 .enabled = [this] { return m_editor.History().CanRedo(); },
						 .tooltip =
							 [this] {
								 return m_editor.History().CanRedo() ? TrF("Rétablir : {}", m_editor.History().RedoName()) : Tr("Rétablir (Ctrl+Y)");
							 }})}),
			Group(Tr("Presse-papiers"),
				  {Tool({.icon = I(Icon::CONTENT_CUT), .tip = Tr("Couper (Ctrl+X)"), .action = [this](const ui::Rect &) { CutToClipboard(); }, .enabled = canCopy}),
				   Tool({.icon = I(Icon::CONTENT_COPY), .tip = Tr("Copier (Ctrl+C)"), .action = [this](const ui::Rect &) { CopyToClipboard(); }, .enabled = canCopy}),
				   Tool({.icon = I(Icon::CONTENT_PASTE), .tip = Tr("Coller (Ctrl+V)"), .action = [this](const ui::Rect &) { PasteFromClipboard(); }})}),
			Group(Tr("Police"),
				  {textFlag(PropertyId::Bold, Icon::FORMAT_BOLD, Tr("Gras (Ctrl+B)")),
				   textFlag(PropertyId::Italic, Icon::FORMAT_ITALIC, Tr("Italique (Ctrl+I)")),
				   textFlag(PropertyId::Underline, Icon::FORMAT_UNDERLINED, Tr("Souligné (Ctrl+U)")),
				   textFlag(PropertyId::Strike, Icon::FORMAT_STRIKETHROUGH, Tr("Barré")),
				   script(TextScript::Superscript, Icon::SUPERSCRIPT, Tr("Exposant")), script(TextScript::Subscript, Icon::SUBSCRIPT, Tr("Indice")),
				   Field(200.f,
						 [this](ui::Context &u, const ui::Rect &r) {
							 const NodeId sample = FirstTextTarget();
							 std::string family = sample != kNoNode ? m_editor.Document().Get(sample, props::FontFamily) : std::string("Helvetica");
							 if (!TextEditable()) {
								 DisabledField(u, r, family);
								 return;
							 }
							 const std::vector<std::string_view> &families = Families();
							 if (!EditableField(u, r, "family", family, families, Tr("Police")))
								 return;
							 const bool known = std::ranges::any_of(families, [&](std::string_view f) { return EqualsNoCase(f, family); });
							 if (family.empty())
								 Ui().Notify(Tr("nom de police vide"), Ui().Style().warning);
							 else {
								 if (!known)
									 Ui().Notify(TrF("police « {} » non installée : Helvetica la remplace à l'affichage et dans le PDF", family),
												 Ui().Style().warning);
								 Report(m_editor.SetText(PropertyId::FontFamily, family));
							 }
						 }),
				   Field(76.f,
						 [this](ui::Context &u, const ui::Rect &r) {
							 const NodeId sample = FirstTextTarget();
							 std::string size = sample != kNoNode ? std::format("{:g}", m_editor.Document().Get(sample, props::FontSize)) : "11";
							 if (!TextEditable()) {
								 DisabledField(u, r, size);
								 return;
							 }
							 std::vector<std::string> storage;
							 const auto sizes = Numbers(kSizes, storage);
							 if (!EditableField(u, r, "size", size, sizes, Tr("Taille de police (points)")))
								 return;
							 if (const auto v = ParseNumber(size); v && *v >= 1 && *v <= 1000)
								 Report(m_editor.SetText(PropertyId::FontSize, *v));
							 else
								 Ui().Notify(TrF("taille de police invalide « {} » (1 à 1000 points, ex. 10,5)", size), Ui().Style().warning);
						 }),
				   Tool({.icon = I(Icon::FORMAT_COLOR_TEXT),
						 .tip = Tr("Couleur du texte…"),
						 .action = [this, sampleColor](const ui::Rect &anchor) { OpenColorPopup(ColorTarget::Text, anchor, sampleColor(false)); },
						 .enabled = [this] { return TextEditable(); },
						 .swatch = [this, sampleColor] { return TextEditable() ? sampleColor(false) : P().border; }}),
				   Tool({.icon = I(Icon::HIGHLIGHT),
						 .tip = Tr("Couleur de surlignage…"),
						 .action =
							 [this, sampleColor](const ui::Rect &anchor) {
								 const Color h = sampleColor(true);
								 OpenColorPopup(ColorTarget::Highlight, anchor, h.a == 0 ? Color(255, 240, 0) : h);
							 },
						 .enabled = [this] { return TextEditable(); },
						 .swatch =
							 [this, sampleColor] {
								 const Color h = sampleColor(true);
								 return TextEditable() && h.a != 0 ? h : P().border;
							 }})}),
			Group(Tr("Paragraphe"),
				  {align(HorizontalAlign::Left, Icon::FORMAT_ALIGN_LEFT, Tr("Aligner à gauche")),
				   align(HorizontalAlign::Center, Icon::FORMAT_ALIGN_CENTER, Tr("Centrer")),
				   align(HorizontalAlign::Right, Icon::FORMAT_ALIGN_RIGHT, Tr("Aligner à droite")),
				   align(HorizontalAlign::Justify, Icon::FORMAT_ALIGN_JUSTIFY, Tr("Justifier")),
				   Field(90.f,
						 [this](ui::Context &u, const ui::Rect &r) {
							 if (!ParagraphEditable()) {
								 DisabledField(u, r, "1,15");
								 return;
							 }
							 int spacing = IndexOfNearest(kSpacings, m_editor.Document().Get(m_editor.ParagraphTargets().front(), props::LineSpacing));
							 std::vector<std::string> storage;
							 const auto spacings = Numbers(kSpacings, storage);
							 if (u.MouseOver(r))
								 u.Tooltip(Tr("Interligne"));
							 if (u.Choice(r, "spacing", spacing, spacings))
								 Report(m_editor.SetParagraph(PropertyId::LineSpacing, kSpacings[size_t(spacing)]));
						 }),
				   list(ListStyle::Bullet, Icon::FORMAT_LIST_BULLETED, Tr("Liste à puces")),
				   list(ListStyle::Decimal, Icon::FORMAT_LIST_NUMBERED, Tr("Liste numérotée")),
				   Tool({.icon = I(Icon::FORMAT_INDENT_DECREASE),
						 .tip = Tr("Diminuer le retrait (Maj+Tab dans une liste)"),
						 .action = [this](const ui::Rect &) { Report(m_editor.ChangeIndent(-1)); },
						 .enabled = [this] { return ParagraphEditable(); }}),
				   Tool({.icon = I(Icon::FORMAT_INDENT_INCREASE),
						 .tip = Tr("Augmenter le retrait (Tab dans une liste)"),
						 .action = [this](const ui::Rect &) { Report(m_editor.ChangeIndent(1)); },
						 .enabled = [this] { return ParagraphEditable(); }})}),
			Group(Tr("Styles"), {Field(130.f,
									   [this](ui::Context &u, const ui::Rect &r) {
										   std::vector<std::string_view> styleNames;
										   for (size_t i = 0; i < size_t(ParagraphStyle::Count); ++i)
											   styleNames.push_back(StyleName(ParagraphStyle(i)));
										   const TrList styles(styleNames);
										   int style = int(m_editor.CurrentStyle());
										   if (u.MouseOver(r))
											   u.Tooltip(Tr("Style de paragraphe"));
										   if (u.Choice(r, "pstyle", style, styles.views) && ParagraphEditable())
											   Report(m_editor.ApplyStyle(ParagraphStyle(style)));
									   })}),
		});
	}

	ui::WidgetBuilder InsertTab() {
		static const std::string_view kCounts[] = {"1", "2", "3", "4", "5", "6", "8", "10", "20", "50", "100", "500", "1000"};
		const auto validTable = [this] {
			const auto rows = ParseNumber(m_tableRowsText), cols = ParseNumber(m_tableColsText);
			return rows && cols && *rows >= 1 && *cols >= 1 && *cols <= 1000 && *rows * *cols <= 1e6;
		};
		const auto shape = [this](ShapeKind kind, Icon icon, std::string tip) {
			return Tool({.icon = I(icon), .tip = std::move(tip), .action = [this, kind](const ui::Rect &) { Report(m_editor.InsertShape(kind)); }});
		};
		const auto counts = [](std::string &text, std::string_view id) {
			return [&text, id](ui::Context &u, const ui::Rect &r) { EditableField(u, r, id, text, kCounts); };
		};
		return RibbonPage({
			Group(Tr("Tableau (lignes × colonnes)"),
				  {Tool({.icon = I(Icon::SHORT_TEXT), .tip = Tr("Paragraphe (Entrée hors champ)"), .action = [this](const ui::Rect &) {
							 Report(m_editor.InsertParagraph());
						 }}),
				   Tool({.icon = I(Icon::TABLE_CHART),
						 .tip = Tr("Insérer un tableau"),
						 .action =
							 [this](const ui::Rect &) {
								 Report(m_editor.InsertTable(int(*ParseNumber(m_tableRowsText)), int(*ParseNumber(m_tableColsText))));
							 },
						 .enabled = validTable,
						 .tooltip =
							 [this, validTable] {
								 return validTable() ? TrF("Insérer un tableau {} × {}", m_tableRowsText, m_tableColsText)
													 : Tr("Dimensions invalides (au plus 1 000 colonnes et 1 000 000 de cellules)");
							 }}),
				   Field(64.f, counts(m_tableRowsText, "rows")), Field(64.f, counts(m_tableColsText, "cols"))}),
			Group(Tr("Illustrations"),
				  {Tool({.icon = I(Icon::IMAGE),
						 .tip = Tr("Image… (PNG, JPEG, BMP…)"),
						 .action = [this](const ui::Rect &) { ShowOpen(Pending::InsertImage, {{::String(Tr("Images")), "png;jpg;jpeg;bmp;gif;webp"}}); }}),
				   shape(ShapeKind::Rectangle, Icon::CROP_SQUARE, Tr("Rectangle")), shape(ShapeKind::Ellipse, Icon::PANORAMA_FISH_EYE, Tr("Ellipse")),
				   shape(ShapeKind::Line, Icon::HORIZONTAL_RULE, Tr("Trait")),
				   Tool({.icon = I(Icon::CATEGORY),
						 .tip = Tr("Formes… (étoiles, flèches, polygones…)"),
						 .action = [this](const ui::Rect &anchor) { OpenShapesPopup(anchor); },
						 .active = [this] { return m_shapesOpen; }}),
				   Tool({.icon = I(Icon::TEXT_FIELDS),
						 .svg = "text_box",
						 .tip = Tr("Zone de texte (dans la zone de dessin sélectionnée, sinon dans le flux)"),
						 .action = [this](const ui::Rect &) { Report(m_editor.InsertTextBox()); }}),
				   Tool({.icon = I(Icon::DRAW),
						 .svg = "drawing_area",
						 .tip = Tr("Zone de dessin : formes, images et textes placés librement"),
						 .action = [this](const ui::Rect &) { Report(m_editor.InsertDrawing()); }}),
				   Tool({.icon = I(Icon::LABEL),
						 .tip = Tr("Légende de figure (sous l'image ou la forme sélectionnée)"),
						 .action = [this](const ui::Rect &) { Report(m_editor.InsertCaption(CaptionKind::Figure)); }})}),
			Group(Tr("Texte"),
				  {Tool({.icon = I(Icon::INFO), .tip = Tr("Encadré (note)"), .action = [this](const ui::Rect &) {
							 Report(m_editor.InsertStyled(ParagraphStyle::Note, "Note : "));
						 }}),
				   Tool({.icon = I(Icon::FORMAT_QUOTE), .tip = Tr("Citation"), .action = [this](const ui::Rect &) {
							 Report(m_editor.InsertStyled(ParagraphStyle::Quote, ""));
						 }}),
				   Tool({.icon = I(Icon::CODE), .tip = Tr("Bloc de code (espaces conservés)"), .action = [this](const ui::Rect &) {
							 Report(m_editor.InsertStyled(ParagraphStyle::Code, ""));
						 }}),
				   Tool({.icon = I(Icon::LINK), .tip = Tr("Lien… (Ctrl+K)"), .action = [this](const ui::Rect &anchor) { OpenLinkPopup(anchor); }})}),
			Group(Tr("Pages"),
				  {Tool({.icon = I(Icon::TOC), .tip = Tr("Table des matières"), .action = [this](const ui::Rect &) {
							 Report(m_editor.InsertTableOfContents(TocContent::Headings));
						 }}),
				   Tool({.icon = I(Icon::INSERT_PAGE_BREAK), .tip = Tr("Saut de page"), .action = [this](const ui::Rect &) {
							 Report(m_editor.InsertPageBreak());
						 }}),
				   Tool({.icon = I(Icon::FORMAT_LIST_NUMBERED), .tip = Tr("Numéro de page (pied de page)"), .action = [this](const ui::Rect &) {
							 Report(m_editor.InsertPageNumber());
						 }})}),
		});
	}

	/// Table des matières sélectionnée (kNoNode sinon).
	[[nodiscard]] NodeId SelectedToc() const {
		const doc::Document &d = m_editor.Document();
		const NodeId id = m_editor.Selection().Primary();
		return d.Contains(id) && d.Find(id)->Type() == NodeType::TableOfContents ? id : kNoNode;
	}

	ui::WidgetBuilder ReferencesTab() {
		const auto toc = [this](TocContent content, Icon icon, std::string svg, std::string tip) {
			return Tool({.icon = I(icon), .svg = std::move(svg), .tip = std::move(tip), .action = [this, content](const ui::Rect &) {
							 Report(m_editor.InsertTableOfContents(content));
						 }});
		};
		const auto caption = [this](CaptionKind kind, Icon icon, std::string svg, std::string tip) {
			return Tool({.icon = I(icon),
						 .svg = std::move(svg),
						 .tip = std::move(tip),
						 .action = [this, kind](const ui::Rect &) { Report(m_editor.InsertCaption(kind)); },
						 .enabled = [this] { return !m_editor.Context().Empty(); }});
		};
		const auto canRef = [this] { return m_editor.Editing() && !m_editor.DocumentOutline().entries.empty(); };
		const auto reference = [this, canRef](FieldKind field, Icon icon, std::string svg, std::string tip) {
			return Tool({.icon = I(icon),
						 .svg = std::move(svg),
						 .tip = std::move(tip),
						 .action =
							 [this, field](const ui::Rect &) {
								 const Outline &outline = m_editor.DocumentOutline();
								 if (m_referenceTarget >= 0 && size_t(m_referenceTarget) < outline.entries.size())
									 Report(m_editor.InsertReference(outline.entries[size_t(m_referenceTarget)].node, field));
							 },
						 .enabled = canRef});
		};
		const auto linkable = [this] { return m_editor.Editing() || !m_editor.Context().Empty(); };
		return RibbonPage({
			Group(Tr("Insérer une table"), {toc(TocContent::Headings, Icon::TOC, {}, Tr("Table des matières")),
											toc(TocContent::Figures, Icon::PHOTO_LIBRARY, "toc_figures", Tr("Table des figures")),
											toc(TocContent::Tables, Icon::TABLE_VIEW, "toc_tables", Tr("Liste des tableaux"))}),
			Group(
				Tr("Sélectionner une table"),
				{Field(80.f,
					   [this](ui::Context &u, const ui::Rect &r) {
						   static const std::string_view kDepths[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
						   const NodeId selected = SelectedToc();
						   int depth = selected != kNoNode ? int(m_editor.Document().Get(selected, props::TocDepth)) - 1 : 2;
						   if (u.MouseOver(r))
							   u.Tooltip(Tr("Profondeur de la table sélectionnée"));
						   if (u.Choice(r, "tocdepth", depth, kDepths) && selected != kNoNode)
							   Report(m_editor.SetSelected(PropertyId::TocDepth, int64_t(depth + 1)));
					   }),
				 Tool({.icon = I(Icon::TOUCH_APP),
					   .tip = Tr("Liens interactifs (Ctrl+clic, liens du PDF)"),
					   .action =
						   [this](const ui::Rect &) {
							   if (const NodeId selected = SelectedToc(); selected != kNoNode)
								   Report(m_editor.SetSelected(PropertyId::Hyperlinks, !m_editor.Document().Get(selected, props::Hyperlinks)));
						   },
					   .enabled = [this] { return SelectedToc() != kNoNode; },
					   .active = [this] { return SelectedToc() != kNoNode && m_editor.Document().Get(SelectedToc(), props::Hyperlinks); }})},
				[this] { return SelectedToc() != kNoNode ? Tr("Table sélectionnée (profondeur, liens)") : Tr("Sélectionner une table"); }),
			Group(Tr("Numérotation des titres"), {Field(170.f,
														 [this](ui::Context &u, const ui::Rect &r) {
															 static const std::string_view kNumbering[] = {"Titres non numérotés", "Numéros 1.2",
																										   "Numéros 1.2."};
															 int numbering = int(m_editor.Numbering());
															 if (u.Choice(r, "numbering", numbering, TrList(kNumbering).views))
																 Report(m_editor.SetHeadingNumbering(doc::HeadingNumbering(numbering)));
														 })}),
			Group(Tr("Légende"), {caption(CaptionKind::Figure, Icon::IMAGE, "caption_figure", Tr("Légende de figure")),
								  caption(CaptionKind::Table, Icon::TABLE_CHART, "caption_table", Tr("Légende de tableau")),
								  caption(CaptionKind::Listing, Icon::CODE, "caption_listing", Tr("Légende de listing"))}),
			Group(Tr("Renvoi au curseur"),
				  {Field(200.f,
						 [this](ui::Context &u, const ui::Rect &r) {
							 const Outline &outline = m_editor.DocumentOutline();
							 std::vector<std::string> labels;
							 for (const OutlineEntry &e : outline.entries)
								 labels.push_back((e.label.empty() ? "" : e.label + "  ") + e.text);
							 if (labels.empty())
								 labels.push_back(Tr("(aucun titre ni légende)"));
							 const std::vector<std::string_view> views(labels.begin(), labels.end());
							 m_referenceTarget = std::clamp(m_referenceTarget, 0, int(views.size()) - 1);
							 if (u.MouseOver(r))
								 u.Tooltip(Tr("Cible du renvoi"));
							 u.Choice(r, "reftarget", m_referenceTarget, views);
						 }),
				   reference(FieldKind::Reference, Icon::TAG, {}, Tr("Insérer le numéro de la cible (« 2.3 », « Tableau 4 »)")),
				   reference(FieldKind::PageReference, Icon::DESCRIPTION, "ref_page", Tr("Insérer la page de la cible"))}),
			Group(Tr("Liens"),
				  {Tool({.icon = I(Icon::LINK),
						 .tip = Tr("Lien… (Ctrl+K)"),
						 .action = [this](const ui::Rect &anchor) { OpenLinkPopup(anchor); },
						 .enabled = linkable}),
				   Tool({.icon = I(Icon::LINK_OFF),
						 .tip = Tr("Retirer le lien"),
						 .action = [this](const ui::Rect &) { Report(m_editor.RemoveLink()); },
						 .enabled = linkable})}),
		});
	}

	ui::WidgetBuilder PageTab() {
		const auto geometry = [this] {
			const auto [section, page] = PageTarget();
			return ResolvePageGeometry(m_editor.Document(), section, page);
		};
		const auto orientation = [this, geometry](Orientation value, Icon icon, std::string tip) {
			return Tool({.icon = I(icon),
						 .tip = std::move(tip),
						 .action = [this, value](const ui::Rect &) { Report(m_editor.SetPage(PropertyId::PageOrientation, int64_t(value))); },
						 .active = [geometry, value] { return geometry().orientation == value; }});
		};
		// Marge d'un côté : icône du bord, valeur au centième de centimètre.
		const auto side = [this, geometry](MarginSide which, Icon icon) {
			return Field(100.f, [this, geometry, which, icon](ui::Context &u, const ui::Rect &r) {
				static constexpr std::string_view kMarginIds[] = {"margin.top", "margin.right", "margin.bottom", "margin.left"};
				static const std::string_view kMarginLengths[] = {"0", "0,5", "1", "1,27", "1,5", "1,91", "2", "2,5", "3", "3,81"};
				const PageGeometry g = geometry();
				u.Canvas().Icon({r.x, r.y + 3, 22, 22}, I(icon), P().text, 22.f);
				std::string text = FormatCm(MarginOf(g.margins, which));
				if (!EditableField(u, {r.x + 24, r.y, r.w - 24, r.h}, kMarginIds[size_t(which)], text, kMarginLengths,
								   std::format("{} (cm)", Tr(MarginName(which)))))
					return;
				const double max = MaxMargin(g, which);
				if (const auto v = ParseNumber(text); v && *v >= 0 && *v * kPointsPerCm <= max + 1e-9)
					Report(m_editor.SetPage(MarginProperty(which), *v * kPointsPerCm));
				else
					Ui().Notify(TrF("marge invalide « {} » : 0 à {} cm", text, FormatCm(max)), Ui().Style().warning);
			});
		};
		return RibbonPage({
			Group(Tr("Format et orientation"),
				  {Field(100.f,
						 [this, geometry](ui::Context &u, const ui::Rect &r) {
							 static const std::string_view kFormatNames[] = {"A4", "A3", "A5", "Letter", "Legal"};
							 const PageGeometry g = geometry();
							 int format = 0;
							 for (size_t i = 0; i < std::size(kFormats); ++i)
								 if (kFormats[i] == g.format)
									 format = int(i);
							 if (u.MouseOver(r))
								 u.Tooltip(Tr("Format de page"));
							 if (u.Choice(r, "format", format, kFormatNames))
								 Report(m_editor.SetPage(PropertyId::Format, int64_t(kFormats[size_t(format)])));
						 }),
				   orientation(Orientation::Portrait, Icon::CROP_PORTRAIT, Tr("Portrait")),
				   orientation(Orientation::Landscape, Icon::CROP_LANDSCAPE, Tr("Paysage"))}),
			Group(Tr("Marges (cm)"), {Field(100.f,
											[this, geometry](ui::Context &u, const ui::Rect &r) {
												// Préréglage : mêmes marges des quatre côtés.
												std::vector<std::string> storage;
												for (float m : kMargins)
													storage.push_back(FormatCm(m));
												const std::vector<std::string_view> presets(storage.begin(), storage.end());
												int margin = IndexOfNearest(kMargins, geometry().margins.left);
												if (u.MouseOver(r))
													u.Tooltip(Tr("Mêmes marges des quatre côtés"));
												if (u.Choice(r, "margins", margin, presets))
													Report(Margins(kMargins[size_t(margin)]));
											}),
									  side(MarginSide::Top, Icon::BORDER_TOP), side(MarginSide::Bottom, Icon::BORDER_BOTTOM),
									  side(MarginSide::Left, Icon::BORDER_LEFT), side(MarginSide::Right, Icon::BORDER_RIGHT)}),
			Group(Tr("Couleur de page"),
				  {Tool({.icon = I(Icon::FORMAT_COLOR_FILL),
						 .tip = Tr("Couleur de fond des pages…"),
						 .action = [this](const ui::Rect &anchor) { OpenColorPopup(ColorTarget::Page, anchor, PageColor()); },
						 .swatch = [this] { return PageColor().a ? PageColor() : P().border; }})},
				  // Portée des réglages de page : pages sélectionnées, sinon la section.
				  [this] {
					  return PageTarget().second != kNoNode ? Tr("Appliqué aux pages sélectionnées") : Tr("Appliqué à toutes les pages de la section");
				  }),
		});
	}

	ui::WidgetBuilder TableTab() {
		const auto table = [this] { return m_editor.CurrentTable().Valid() && !m_editor.Context().locked; };
		const auto count = [this] {
			const auto n = ParseNumber(m_insertCountText);
			return n && *n >= 1 && *n <= 10000 ? size_t(*n) : size_t(1);
		};
		const auto tool = [table](char32_t icon, std::string svg, std::string tip, std::function<void()> run) {
			return ToolSpec{.icon = icon, .svg = std::move(svg), .tip = std::move(tip), .action = [run = std::move(run)](const ui::Rect &) {
								run();
							}, .enabled = table};
		};
		const auto insert = [this, table, count](Icon icon, std::string pattern, bool rows, bool after) {
			return Tool({.icon = I(icon),
						 .tip = TrArg(pattern, "1"),
						 .action =
							 [this, count, rows, after](const ui::Rect &) {
								 Report(rows ? m_editor.InsertRows(after, count()) : m_editor.InsertColumns(after, count()));
							 },
						 .enabled = table,
						 .tooltip = [count, pattern] { return TrArg(pattern, std::to_string(count())); }});
		};
		const auto move = [this, table](Icon icon, std::string tip, bool rows, int delta) {
			return Tool({.icon = I(icon),
						 .tip = std::move(tip),
						 .action = [this, rows, delta](const ui::Rect &) { Report(rows ? m_editor.MoveRows(delta) : m_editor.MoveColumns(delta)); },
						 .enabled =
							 [this, table, rows, delta] {
								 const TableSelection t = m_editor.CurrentTable();
								 if (!table())
									 return false;
								 return rows ? (delta < 0 ? t.row0 > 0 : t.row1 + 1 < t.rows) : (delta < 0 ? t.column0 > 0 : t.column1 + 1 < t.columns);
							 }});
		};
		const auto length = [this, table](std::string_view id, bool width) {
			return Field(70.f, [this, table, id, width](ui::Context &u, const ui::Rect &r) {
				static const std::string_view kLengths[] = {"auto", "1", "1,5", "2", "2,5", "3", "4", "5", "6", "8"};
				if (!table()) {
					DisabledField(u, r, "auto");
					return;
				}
				const TableSelection t = m_editor.CurrentTable();
				const TableGrid g = TableGrid::Of(m_editor.Document(), t.table);
				std::string value = "auto";
				if (width) {
					if (const NodeId cell = g.Cell(0, t.column0); cell != kNoNode)
						if (const double w = m_editor.Document().Get(cell, props::Width); w > 0)
							value = FormatCm(w);
				} else if (const double h = m_editor.Document().Get(g.rows[t.row0], props::MinHeight); h > 0) {
					value = FormatCm(h);
				}
				if (!EditableField(u, r, id, value, kLengths, width ? Tr("Largeur de la colonne (cm)") : Tr("Hauteur de la ligne (cm)")))
					return;
				const auto v = ParseNumber(value);
				Report(width ? m_editor.SetColumnWidth(t.table, t.column0, v ? *v * kPointsPerCm : 0)
							 : m_editor.SetRowHeight(g.rows[t.row0], v ? *v * kPointsPerCm : 0));
			});
		};
		return RibbonPage({
			Group(Tr("Sélection"),
				  {Tool(tool(I(Icon::SELECT_ALL), {}, Tr("Sélectionner le tableau"), [this] { m_editor.SelectTable(m_editor.CurrentTable().table); })),
				   Tool(tool(I(Icon::TABLE_ROWS), {}, Tr("Sélectionner la ou les lignes"),
							 [this] {
								 const TableSelection t = m_editor.CurrentTable();
								 m_editor.SelectRows(t.table, t.row0, t.row1);
							 })),
				   Tool(tool(I(Icon::VIEW_COLUMN), {}, Tr("Sélectionner la ou les colonnes"),
							 [this] {
								 const TableSelection t = m_editor.CurrentTable();
								 m_editor.SelectColumns(t.table, t.column0, t.column1);
							 }))},
				  [this, table] {
					  const TableSelection t = m_editor.CurrentTable();
					  return table() ? std::format("{} × {}", t.rows, t.columns) : Tr("Sélection");
				  }),
			Group(Tr("Insérer"),
				  {insert(Icon::ARROW_UPWARD, "Insérer {} ligne(s) au-dessus", true, false),
				   insert(Icon::ARROW_DOWNWARD, "Insérer {} ligne(s) au-dessous", true, true),
				   insert(Icon::ARROW_BACK, "Insérer {} colonne(s) à gauche", false, false),
				   insert(Icon::ARROW_FORWARD, "Insérer {} colonne(s) à droite", false, true), Field(60.f, [this](ui::Context &u, const ui::Rect &r) {
					   static const std::string_view kCounts[] = {"1", "2", "3", "5", "10", "20", "50", "100"};
					   EditableField(u, r, "inscount", m_insertCountText, kCounts, Tr("Nombre de lignes ou de colonnes insérées"));
				   })}),
			Group(Tr("Lignes et colonnes"),
				  {Tool(tool(I(Icon::DELETE), "rows_delete", Tr("Supprimer les lignes"), [this] { Report(m_editor.DeleteRows()); })),
				   Tool(tool(I(Icon::DELETE), "columns_delete", Tr("Supprimer les colonnes"), [this] { Report(m_editor.DeleteColumns()); })),
				   Tool(tool(I(Icon::CONTENT_COPY), "rows_duplicate", Tr("Dupliquer les lignes"), [this] { Report(m_editor.DuplicateRows()); })),
				   Tool(tool(I(Icon::CONTENT_COPY), "columns_duplicate", Tr("Dupliquer les colonnes"), [this] { Report(m_editor.DuplicateColumns()); }))}),
			Group(Tr("Déplacer"), {move(Icon::KEYBOARD_ARROW_UP, Tr("Monter les lignes"), true, -1),
								   move(Icon::KEYBOARD_ARROW_DOWN, Tr("Descendre les lignes"), true, 1),
								   move(Icon::KEYBOARD_ARROW_LEFT, Tr("Déplacer les colonnes à gauche"), false, -1),
								   move(Icon::KEYBOARD_ARROW_RIGHT, Tr("Déplacer les colonnes à droite"), false, 1)}),
			Group(Tr("Fusion"),
				  {Tool({.icon = I(Icon::CALL_MERGE),
						 .svg = "cells_merge",
						 .tip = Tr("Fusionner les cellules sélectionnées"),
						 .action = [this](const ui::Rect &) { Report(m_editor.MergeCells()); },
						 .enabled =
							 [this, table] {
								 const TableSelection t = m_editor.CurrentTable();
								 return table() && t.RowCount() * t.ColumnCount() > 1;
							 }}),
				   Tool({.icon = I(Icon::CALL_SPLIT),
						 .svg = "cells_split",
						 .tip = Tr("Séparer les cellules fusionnées"),
						 .action = [this](const ui::Rect &) { Report(m_editor.SplitCells()); },
						 .enabled = [this, table] { return table() && m_editor.SelectionHasMergedCells(); }})}),
			Group(Tr("Cellules"),
				  {Tool({.icon = I(Icon::FORMAT_COLOR_FILL),
						 .tip = Tr("Couleur des cellules…"),
						 .action =
							 [this](const ui::Rect &anchor) {
								 OpenColorPopup(ColorTarget::Cells, anchor, Color::FromName("LIGHT_YELLOW").value_or(Color::WHITE()));
							 },
						 .enabled = table}),
				   Field(160.f,
						 [this](ui::Context &u, const ui::Rect &r) {
							 static const std::string_view kPresets[] = {"Toutes les bordures", "Aucune bordure", "Bordure extérieure",
																		 "Filets horizontaux", "Bordures intérieures"};
							 if (u.MouseOver(r))
								 u.Tooltip(Tr("Bordures à appliquer"));
							 u.Choice(r, "borderpreset", m_borderPreset, TrList(kPresets).views);
						 }),
				   Tool(tool(I(Icon::BORDER_ALL), {}, Tr("Appliquer les bordures"),
							 [this] {
								 Report(m_editor.ApplyBorders(BorderPreset(m_borderPreset), 0.75, Color::FromName("DIM_GREY").value_or(Color::BLACK())));
							 })),
				   Tool(tool(I(Icon::TABLE_ROWS), "table_bands", Tr("Lignes en bandes alternées"),
							 [this] { Report(m_editor.BandRows(Color::WHITE(), Color::FromName("ALICE_BLUE").value_or(Color::WHITE()))); })),
				   Tool({.icon = I(Icon::VIEW_AGENDA),
						 .svg = "table_header_repeat",
						 .tip = Tr("Répéter la ligne d'en-tête sur chaque page"),
						 .action = [this](const ui::Rect &) { Report(m_editor.ToggleHeaderRow()); },
						 .enabled = table,
						 .active =
							 [this, table] {
								 return table() &&
										m_editor.Document().Get(TableGrid::Of(m_editor.Document(), m_editor.CurrentTable().table).rows.front(), props::RepeatHeader);
							 }})}),
			Group(Tr("Largeur / hauteur (cm)"),
				  {length("colwidth", true), length("rowheight", false),
				   Tool(tool(I(Icon::VIEW_WEEK), "columns_equalize", Tr("Égaliser la largeur des colonnes"), [this] { Report(m_editor.DistributeColumns()); }))}),
		});
	}

	ui::WidgetBuilder ArrangeTab() {
		const auto any = [this] { return !m_editor.Context().Empty(); };
		const auto object = [this] {
			const SelectionContext &ctx = m_editor.Context();
			return (ctx.image || ctx.shape) && !ctx.locked;
		};
		const auto arrange = [this, object](Icon icon, std::string tip, Arrange how) {
			return Tool({.icon = I(icon),
						 .tip = std::move(tip),
						 .action = [this, how](const ui::Rect &) { Report(m_editor.ArrangeSelection(how)); },
						 .enabled = object});
		};
		const auto placeable = [this] { return !PlaceableSelection().empty() && !m_editor.Context().locked; };
		const auto alignment = [this, placeable](HorizontalAlign value, Icon icon, std::string svg, std::string tip) {
			return Tool({.icon = I(icon),
						 .svg = std::move(svg),
						 .tip = std::move(tip),
						 .action = [this, value](const ui::Rect &) { Report(m_editor.AlignShapes(PlaceableSelection(), value)); },
						 .enabled = placeable,
						 .active =
							 [this, placeable, value] {
								 return placeable() && m_editor.Document().Get(PlaceableSelection().front(), props::HAlign) == value;
							 }});
		};
		return RibbonPage({
			Group(Tr("Groupes"),
				  {Tool({.icon = I(Icon::GROUP_WORK),
						 .tip = Tr("Grouper (Ctrl+G)"),
						 .action = [this](const ui::Rect &) { Report(m_editor.GroupSelection()); },
						 .enabled = [this] { return !m_editor.Context().Empty() && !m_editor.Context().group; }}),
				   Tool({.icon = I(Icon::LAYERS_CLEAR),
						 .tip = Tr("Dissocier (Ctrl+Maj+G)"),
						 .action = [this](const ui::Rect &) { Report(m_editor.UngroupSelection()); },
						 .enabled = any})}),
			Group(Tr("Édition"),
				  {Tool({.icon = I(Icon::CONTENT_COPY),
						 .tip = Tr("Dupliquer (Ctrl+D)"),
						 .action = [this](const ui::Rect &) { Report(m_editor.DuplicateSelection()); },
						 .enabled = [this] { return !m_editor.Context().Empty() && !m_editor.Context().locked; }}),
				   Tool({.icon = I(Icon::DELETE),
						 .tip = Tr("Supprimer (Suppr)"),
						 .action = [this](const ui::Rect &) { Report(m_editor.DeleteSelection()); },
						 .enabled = [this] { return !m_editor.Context().Empty() && !m_editor.Context().locked; }})}),
			Group(Tr("Plans"), {arrange(Icon::FLIP_TO_FRONT, Tr("Premier plan (Ctrl+Maj+Haut)"), Arrange::Front),
								arrange(Icon::ARROW_UPWARD, Tr("Avancer d'un plan (Ctrl+Haut)"), Arrange::Forward),
								arrange(Icon::ARROW_DOWNWARD, Tr("Reculer d'un plan (Ctrl+Bas)"), Arrange::Backward),
								arrange(Icon::FLIP_TO_BACK, Tr("Dernier plan (Ctrl+Maj+Bas)"), Arrange::Back)}),
			Group(Tr("Forme"),
				  {alignment(HorizontalAlign::None, Icon::OPEN_WITH, "align_free", Tr("Alignement : aucun (placement libre)")),
				   alignment(HorizontalAlign::Left, Icon::ALIGN_HORIZONTAL_LEFT, {}, Tr("Aligner à gauche")),
				   alignment(HorizontalAlign::Center, Icon::ALIGN_HORIZONTAL_CENTER, {}, Tr("Centrer horizontalement")),
				   alignment(HorizontalAlign::Right, Icon::ALIGN_HORIZONTAL_RIGHT, {}, Tr("Aligner à droite")),
				   Tool({.icon = I(Icon::FORMAT_COLOR_FILL),
						 .tip = Tr("Remplissage…"),
						 .action =
							 [this](const ui::Rect &anchor) {
								 OpenColorPopup(ColorTarget::Property, anchor, m_editor.Document().Get(PlaceableSelection().front(), props::BackgroundColor),
												PropertyId::BackgroundColor);
							 },
						 .enabled = placeable,
						 .swatch = [this, placeable] { return placeable() ? m_editor.Document().Get(PlaceableSelection().front(), props::BackgroundColor) : P().border; }}),
				   Tool({.icon = I(Icon::BORDER_COLOR),
						 .tip = Tr("Couleur du contour…"),
						 .action =
							 [this](const ui::Rect &anchor) {
								 OpenColorPopup(ColorTarget::Property, anchor, m_editor.Document().Get(PlaceableSelection().front(), props::BorderColor),
												PropertyId::BorderColor);
							 },
						 .enabled = placeable,
						 .swatch = [this, placeable] { return placeable() ? m_editor.Document().Get(PlaceableSelection().front(), props::BorderColor) : P().border; }}),
				   Tool({.icon = I(Icon::ASPECT_RATIO),
						 .tip = Tr("Garder les proportions au redimensionnement (ou Ctrl en glissant une poignée)"),
						 .action =
							 [this](const ui::Rect &) {
								 const std::vector<NodeId> free = PlaceableSelection();
								 if (!free.empty())
									 Report(m_editor.SetNodes(free, PropertyId::KeepAspectRatio, !m_editor.Document().Get(free.front(), props::KeepAspectRatio)));
							 },
						 .enabled = placeable,
						 .active = [this, placeable] { return placeable() && m_editor.Document().Get(PlaceableSelection().front(), props::KeepAspectRatio); }}),
				   Tool({.icon = I(Icon::TITLE),
						 .svg = "shape_text",
						 .tip = Tr("Texte de la forme"),
						 .action =
							 [this](const ui::Rect &) {
								 const std::vector<NodeId> free = PlaceableSelection();
								 if (free.size() == 1)
									 Report(m_editor.EditShapeText(free.front()));
							 },
						 .enabled =
							 [this] {
								 const std::vector<NodeId> free = PlaceableSelection();
								 return free.size() == 1 && m_editor.Document().Find(free.front())->Type() == NodeType::Shape && !m_editor.Context().locked;
							 }})}),
			Group(Tr("État"),
				  {Tool({.icon = I(Icon::LOCK),
						 .tip = Tr("Verrouiller"),
						 .action = [this](const ui::Rect &) { Report(m_editor.ToggleLocked()); },
						 .enabled = any,
						 .active = [this] { return m_editor.Context().locked; },
						 .glyph = [this] { return m_editor.Context().locked ? I(Icon::LOCK_OPEN) : I(Icon::LOCK); },
						 .tooltip = [this] { return m_editor.Context().locked ? Tr("Déverrouiller") : Tr("Verrouiller"); }}),
				   Tool({.icon = I(Icon::VISIBILITY_OFF),
						 .tip = Tr("Masquer"),
						 .action = [this](const ui::Rect &) { Report(m_editor.ToggleHidden()); },
						 .enabled = any,
						 .active = [this] { return m_editor.Context().hidden; },
						 .glyph = [this] { return m_editor.Context().hidden ? I(Icon::VISIBILITY) : I(Icon::VISIBILITY_OFF); },
						 .tooltip = [this] { return m_editor.Context().hidden ? Tr("Afficher") : Tr("Masquer"); }})}),
		});
	}

	ui::WidgetBuilder ViewTab() {
		return RibbonPage({
			Group(Tr("Zoom"),
				  {Tool({.icon = I(Icon::ZOOM_OUT), .tip = Tr("Zoom arrière (Ctrl+molette)"), .action = [this](const ui::Rect &) { Zoom(1 / 1.25f); }}),
				   Tool({.icon = I(Icon::ZOOM_IN), .tip = Tr("Zoom avant (Ctrl+molette)"), .action = [this](const ui::Rect &) { Zoom(1.25f); }}),
				   Tool({.icon = I(Icon::WIDTH_FULL),
						 .tip = Tr("Ajuster à la largeur"),
						 .action = [this](const ui::Rect &) { m_fitWidth = !m_fitWidth; },
						 .active = [this] { return m_fitWidth; }}),
				   Field(64.f,
						 [this](ui::Context &u, const ui::Rect &r) { // valeur affichée (pas une commande)
							 u.Canvas().FillRoundRect(r, 4.f, sdl3::WithAlpha(P().border, 60));
							 u.Canvas().TextCentered(r, std::format("{:.0f} %", m_zoom * 100), P().muted, 13);
						 })}),
			Group(Tr("Affichage"),
				  {Tool({.icon = I(Icon::CROP_FREE),
						 .tip = Tr("Limites des blocs"),
						 .action = [this](const ui::Rect &) { m_editor.SetShowBlockBounds(!m_editor.ShowBlockBounds()); },
						 .active = [this] { return m_editor.ShowBlockBounds(); }}),
				   Tool({.icon = I(Icon::AUTO_AWESOME),
						 .tip = Tr("Ruban contextuel (l'onglet suit la sélection)"),
						 .action = [this](const ui::Rect &) { m_contextual = !m_contextual; },
						 .active = [this] { return m_contextual; }}),
				   Tool({.icon = I(Icon::STRAIGHTEN),
						 .tip = Tr("Règles"),
						 .action = [this](const ui::Rect &) { m_showRulers = !m_showRulers; },
						 .active = [this] { return m_showRulers; }})}),
			Group(Tr("Grille (cm)"),
				  {Tool({.icon = I(Icon::GRID_ON),
						 .tip = Tr("Grille : formes et images aimantées en les plaçant"),
						 .action =
							 [this](const ui::Rect &) {
								 m_settings.grid = !m_settings.grid;
								 SaveSettings();
							 },
						 .active = [this] { return m_settings.grid; },
						 .glyph = [this] { return m_settings.grid ? I(Icon::GRID_ON) : I(Icon::GRID_OFF); }}),
				   Field(70.f,
						 [this](ui::Context &u, const ui::Rect &r) {
							 static const std::string_view kGridSteps[] = {"0,1", "0,25", "0,5", "1", "2"};
							 std::string step = FormatCm(m_settings.gridStep);
							 if (!EditableField(u, r, "gridstep", step, kGridSteps, Tr("Pas de la grille (cm)")))
								 return;
							 if (const auto v = ParseNumber(step); v && *v * kPointsPerCm >= Settings::kMinGridStep && *v <= 20) {
								 m_settings.gridStep = *v * kPointsPerCm;
								 SaveSettings();
							 } else
								 Ui().Notify(TrF("pas de grille invalide « {} » : 0,1 à 20 cm", step), Ui().Style().warning);
						 })}),
		});
	}

	// ── Espace de travail ──

	void Workspace(ui::Context &u) {
		m_dock.Draw(u, u.Remaining(), [this, &u](ui::DockLayout::Side, const sdl3::FRect &r, std::string_view tab) {
			if (tab == "document")
				DocumentView(u, r);
			else if (tab == "properties")
				Properties(u, r);
			else if (tab == "history")
				HistoryPanel(u, r);
			else if (tab == "tree")
				TreePanel(u, r);
			else if (tab == "outline")
				OutlinePanel(u, r);
		});
	}

	void DocumentView(ui::Context &u, const sdl3::FRect &area) {
		sdl3::Canvas &c = u.Canvas();
		c.FillRect(area, P().workspace);
		const Presentation *p = m_editor.Current();
		if (!p) {
			u.Spinner(area);
			return;
		}
		const float ruler = m_showRulers ? kRulerThickness : 0.f;
		const ui::Rect view{area.x + ruler, area.y + ruler, area.w - 12 - ruler, area.h - ruler};
		m_viewTop = view.y;
		if (m_fitWidth)
			m_zoom = FitWidthZoom(p->pages, view.w);
		if (u.MouseOver(view) && u.Ctrl() && u.WheelY() != 0) {
			Zoom(u.WheelY() > 0 ? 1.1f : 1 / 1.1f);
		}
		m_views = PlacePages(p->pages, view, m_zoom, m_scrollY);
		if (m_options.page > 1 && m_views.pages.size() >= m_options.page) { // --page=N : une fois
			float top = 0;
			for (size_t i = 0; i + 1 < m_options.page; ++i)
				top += m_views.pages[i].rect.h + kPageGap;
			m_scrollY = top;
			m_options.page = 0;
			m_views = PlacePages(p->pages, view, m_zoom, m_scrollY);
		}
		const float maxScroll = std::max(0.f, m_views.contentHeight - view.h);
		if (!u.Ctrl()) {
			float sx = 0;
			u.ScrollInput(view, sx, m_scrollY, 0, maxScroll);
		}
		m_scrollY = std::clamp(m_scrollY, 0.f, maxScroll);
		m_views = PlacePages(p->pages, view, m_zoom, m_scrollY);

		m_viewBottom = view.y + view.h;
		c.PushClip(view);
		const Selection &sel = m_editor.Selection();
		const bool editing = m_editor.Editing();
		const NodeId frameNode = FrameTarget();
		if (frameNode != m_rotateNode)
			m_rotateMode = false; // autre forme : mode redimensionnement
		const auto range = m_editor.RangeGeometry();
		for (const PageView &pv : m_views.pages) {
			if (pv.rect.y > view.y + view.h || pv.rect.y + pv.rect.h < view.y)
				continue;
			DrawPage(c, Renderer(), *p->snapshot, p->pages[pv.index], pv, m_images, m_fontCache);
			const PageLayout &page = p->layout.pages[pv.index];
			if (!editing) { // sélection de nœuds : boîtes et segments (cellules : fond translucide)
				for (const LayoutBox &box : page.boxes)
					if (sel.Contains(box.node) && box.node != frameNode) {
						if (box.type == NodeType::TableCell)
							c.FillRect(PageToScreen(pv, box.rect), sdl3::WithAlpha(P().selection, 70));
						else
							c.StrokeRect(PageToScreen(pv, box.rect), P().selection, 2.f);
					}
				for (const PlacedLine &line : page.lines)
					for (const GlyphRun &run : line.runs)
						if (sel.Contains(run.node))
							c.FillRect(PageToScreen(pv, Rect(run.x, line.rect.y, run.width, line.rect.h)), sdl3::WithAlpha(P().selection, 60));
			}
			for (const auto &[index, r] : range) // surbrillance de la plage de texte
				if (index == pv.index) {
					const PlacedLine *turned = nullptr; // ligne tournée (texte d'une forme) qui porte ce morceau
					for (const PlacedLine &line : page.lines)
						if (line.rotation != 0 && line.rect.y <= r.y + r.h / 2 && r.y + r.h / 2 <= line.rect.Bottom() && line.rect.x <= r.x + 0.01 &&
							r.Right() <= line.rect.Right() + 0.01)
							turned = &line;
					if (turned)
						c.PushRotation(float(turned->rotation), pv.rect.x + float(turned->pivotX) * pv.scale, pv.rect.y + float(turned->pivotY) * pv.scale);
					c.FillRect(PageToScreen(pv, r), sdl3::WithAlpha(P().selection, 95));
					if (turned)
						c.PopRotation();
				}
			if (page.page != kNoNode && sel.Contains(page.page) && !editing)
				c.StrokeRect(pv.rect, P().selection, 3.f);
			if (m_settings.grid) // couleur des limites des blocs à 50 %
				DrawGrid(c, pv, m_settings.gridStep, sdl3::WithAlpha(doc::RenderOptions{}.boundsColor, 80));
		}
		DrawCaret(c);
		DocumentMouse(u, view);
		c.PopClip();
		if (m_showRulers)
			Rulers(u, area, view);
		u.ScrollbarV("docscroll", {area.x + area.w - 12, area.y, 12, area.h}, m_scrollY, m_views.contentHeight, view.h);
		if (m_editor.Busy())
			u.Spinner({area.x + area.w - 48, area.y + 8, 28, 28}, 20.f);
	}

	// ── Vue du document : curseur, souris ──

	enum class DragMode : uint8_t {
		None,
		Area,
		Text,
		TextMove,
		Block,
		Move,
		Resize,
		Rotate,
		Pivot,
		Cells,
		ColumnBorder,
		RowBorder,
		SelectRows,
		SelectColumns,
		Padding
	};

	/// Cote affichée près du pointeur pendant un glisser (position, dimensions, marge…).
	static void DragLabel(sdl3::Canvas &c, float mx, float my, std::string_view text, sdl3::Color color = sdl3::Color(240, 200, 150, 255)) {
		c.FillRoundRect({mx + 12, my + 10, c.MeasureText(text, 12) + 12, 20}, 4.f, sdl3::Color(30, 34, 44, 235));
		c.Text(mx + 18, my + 13, text, color, 12);
	}
	/// Cible de la fenêtre des couleurs : texte, surlignage, fond des cellules, des pages, ou une
	/// propriété de couleur quelconque des nœuds sélectionnés (panneau, formes…).
	enum class ColorTarget : uint8_t { None, Text, Highlight, Cells, Page, Property };

	/// Forme ou image seule sélectionnée (hors édition du texte) : cadre de transformation.
	NodeId FrameTarget() const {
		if (m_editor.Editing() || m_editor.Selection().Size() != 1)
			return kNoNode;
		const NodeId id = m_editor.Selection().Primary();
		const Node *n = m_editor.Document().Find(id);
		return n && (n->Type() == NodeType::Shape || n->Type() == NodeType::Image || n->Type() == NodeType::Drawing) ? id : kNoNode;
	}

	const PageView *ViewOf(size_t page) const {
		for (const PageView &v : m_views.pages)
			if (v.index == page)
				return &v;
		return nullptr;
	}

	void DrawCaret(sdl3::Canvas &c) {
		const auto key = std::pair{m_editor.Caret(), m_editor.Anchor()};
		if (key != m_caretKey) { // le curseur réapparaît dès qu'il bouge
			m_caretKey = key;
			m_caretSince = std::chrono::steady_clock::now();
		}
		if (!m_editor.Editing() || m_editor.HasTextRange())
			return;
		const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_caretSince).count();
		if (std::fmod(t, 1.0) > 0.6)
			return;
		const auto caret = m_editor.CaretGeometry(m_editor.Caret());
		if (!caret)
			return;
		if (const PageView *pv = ViewOf(caret->page)) {
			const sdl3::FRect r = PageToScreen(*pv, caret->rect);
			if (caret->rotation != 0) // texte d'une forme tournée
				c.PushRotation(float(caret->rotation), pv->rect.x + float(caret->pivotX) * pv->scale, pv->rect.y + float(caret->pivotY) * pv->scale);
			c.FillRect({r.x - 0.75f, r.y, 1.5f, std::max(r.h, 4.f)}, Rgba(20, 20, 20));
			if (caret->rotation != 0)
				c.PopRotation();
		}
	}

	/// Pas de la grille d'aimantation (0 : grille masquée).
	[[nodiscard]] double GridStep() const { return m_settings.grid ? m_settings.gridStep : 0.0; }

	/// Le point (page) est-il hors du texte : marges sans ligne (en-tête et pied compris).
	bool InMargin(const PagePoint &at) const {
		const PageLayout &page = m_editor.Current()->layout.pages[at.page];
		if (page.content.Contains(at.x, at.y))
			return false;
		for (const PlacedLine &line : page.lines)
			if (at.y >= line.rect.y - 2 && at.y <= line.rect.y + line.rect.h + 2 && at.x >= line.rect.x - 24 && at.x <= line.rect.x + line.rect.w + 24)
				return false;
		return true;
	}

	/// Bloc du corps dont la poignée (marge gauche) est sous le point.
	NodeId GripBlock(const PagePoint &at) const {
		const PageLayout &page = m_editor.Current()->layout.pages[at.page];
		if (at.x >= page.content.x || at.x < page.content.x - kGripWidth)
			return kNoNode;
		const doc::Document &d = m_editor.Document();
		for (const LayoutBox &b : page.boxes) {
			if (!IsBlock(b.type) || at.y < b.rect.y || at.y > b.rect.y + b.rect.h || !d.Contains(b.node))
				continue;
			const Node *parent = d.Find(d.Find(b.node)->Parent());
			if (parent && parent->Type() == NodeType::Body)
				return b.node;
		}
		return kNoNode;
	}

	struct BlockDrop {
		NodeId parent = kNoNode;
		size_t index = 0, page = 0;
		double y = 0, x = 0, w = 0; ///< trait d'insertion (page)
	};
	/// Destination d'un bloc glissé : avant / après le bloc sous le point (le plus imbriqué,
	/// sauf dans la marge : blocs du corps).
	std::optional<BlockDrop> BlockDropAt(const PagePoint &at) const {
		const PageLayout &page = m_editor.Current()->layout.pages[at.page];
		const doc::Document &d = m_editor.Document();
		const bool margin = at.x < page.content.x || at.x > page.content.x + page.content.w;
		const LayoutBox *best = nullptr;
		for (const LayoutBox &b : page.boxes) {
			if (!IsBlock(b.type) || b.node == m_dragNode || !d.Contains(b.node) || d.IsAncestor(m_dragNode, b.node))
				continue;
			if (at.y < b.rect.y || at.y > b.rect.y + b.rect.h || (!margin && (at.x < b.rect.x || at.x > b.rect.x + b.rect.w)))
				continue;
			// Destination : un conteneur du flux qui admet le bloc (pas le texte d'une forme, ni
			// l'intérieur d'une zone de dessin).
			const Node *parent = d.Find(d.Find(b.node)->Parent());
			if (!parent || (margin && parent->Type() != NodeType::Body) || !IsFlowContainer(parent->Type()) ||
				!d.Contains(m_dragNode) || !doc::CanContain(parent->Type(), d.Find(m_dragNode)->Type()))
				continue;
			if (!best || b.depth > best->depth)
				best = &b;
		}
		if (!best)
			return std::nullopt;
		const Node *n = d.Find(best->node);
		const bool after = at.y > best->rect.y + best->rect.h / 2;
		return BlockDrop{n->Parent(), d.Find(n->Parent())->IndexOfChild(best->node) + (after ? 1 : 0), at.page,
						 after ? best->rect.y + best->rect.h : best->rect.y, best->rect.x, best->rect.w};
	}

	bool InColorPopup(ui::Context &u) const {
		return (m_colorTarget != ColorTarget::None && m_popupRect.Contains(u.MouseX(), u.MouseY())) ||
			   (m_linkOpen && m_linkRect.Contains(u.MouseX(), u.MouseY())) || (m_shapesOpen && m_shapesRect.Contains(u.MouseX(), u.MouseY()));
	}

	void DocumentMouse(ui::Context &u, const ui::Rect &view) {
		const Presentation *p = m_editor.Current();
		sdl3::Canvas &c = u.Canvas();
		const auto at = ScreenToPage(m_views.pages, u.MouseX(), u.MouseY());
		const bool over = u.MouseOver(view) && !InColorPopup(u);
		const NodeId frameNode = FrameTarget();
		size_t framePage = 0;
		const LayoutBox *frameBox = frameNode != kNoNode ? m_editor.BoxOf(frameNode, &framePage) : nullptr;
		const PageView *atView = at ? ViewOf(at->page) : nullptr;
		const double tolerance = atView ? double(kHandlePixels / atView->scale) : 4.0;
		// Survol : curseur, outils des tableaux, liens (Ctrl), poignée des blocs.
		const TableUnder tableHover = over && at && m_drag == DragMode::None ? TableAt(*at) : TableUnder{};
		if (over && at && m_drag == DragMode::None) {
			int hit = frameBox && at->page == framePage ? FrameHit(FrameOf(*frameBox), at->x, at->y, tolerance) : -1;
			if (frameBox && frameBox->type == NodeType::Drawing && hit >= kHandleCount)
				hit = -1; // zone de dessin : intérieur et bord visent ses éléments
			const std::string link = u.Ctrl() ? m_editor.LinkAt(at->page, at->x, at->y) : std::string();
			if (!link.empty()) {
				u.SetCursor(sdl3::SystemCursor::POINTER);
				u.Tooltip(LinkTarget(link) != kNoNode ? Tr("Ctrl+clic : aller à la cible") : TrF("Ctrl+clic : ouvrir {}", link));
			} else if (tableHover.hit.kind != TableHitKind::None && hit < 0) {
				DrawTableTool(u, tableHover);
			} else if (const auto padding = hit < 0 ? PaddingUnder(*at) : std::nullopt) {
				DrawPaddingGuide(c, *atView, *padding);
				u.SetCursor(padding->Horizontal() ? sdl3::SystemCursor::SIZE_EW : sdl3::SystemCursor::SIZE_NS);
				u.Tooltip(TrF("Glisser : {} ({})", Tr(PaddingName(padding->side)), FormatLength(padding->Value())));
			} else if (hit >= 0 && hit < kHandleCount)
				u.SetCursor(m_rotateMode ? sdl3::SystemCursor::CROSSHAIR : kHandleCursors[hit]);
			else if (hit == kFrameBorder || hit == kFrameInside)
				u.SetCursor(sdl3::SystemCursor::MOVE);
			else if (const NodeId grip = GripBlock(*at); grip != kNoNode) {
				u.SetCursor(sdl3::SystemCursor::MOVE);
				for (const LayoutBox &b : p->layout.pages[at->page].boxes)
					if (b.node == grip) {
						const sdl3::FRect r = PageToScreen(*atView, b.rect);
						c.StrokeRect(r, sdl3::WithAlpha(P().selection, 120), 1.f);
						c.Icon({r.x - 26, r.y, 22, 22}, I(Icon::DRAG_INDICATOR), P().selection, 18.f);
						break;
					}
			} else if (!InMargin(*at))
				u.SetCursor(sdl3::SystemCursor::TEXT);
		}
		if (over && at && u.MousePressed())
			Press(u, *at, frameBox, framePage, tolerance);
		if (m_drag != DragMode::None)
			Drag(u, view, at);
		// Cadre de transformation (aperçu pendant le glisser).
		if (frameNode != kNoNode) {
			const bool previewing = m_dragMoved && m_dragNode == frameNode &&
									(m_drag == DragMode::Move || m_drag == DragMode::Resize || m_drag == DragMode::Rotate || m_drag == DragMode::Pivot);
			if (frameBox)
				if (const PageView *pv = ViewOf(framePage))
					DrawShapeFrame(c, *pv, previewing ? m_preview : FrameOf(*frameBox), m_rotateMode);
		}
	}

	void Press(ui::Context &u, const PagePoint &at, const LayoutBox *frameBox, size_t framePage, double tolerance) {
		const auto now = std::chrono::steady_clock::now();
		const bool near = std::abs(u.MouseX() - m_clickX) < 5 && std::abs(u.MouseY() - m_clickY) < 5;
		m_clicks = now - m_lastClick < std::chrono::milliseconds(450) && near ? m_clicks % 3 + 1 : 1;
		m_lastClick = now;
		m_clickX = m_dragX = u.MouseX();
		m_clickY = m_dragY = u.MouseY();
		m_dragFrom = at;
		m_dragMoved = false;
		m_drag = DragMode::None;
		const Presentation *p = m_editor.Current();
		// 1. Cadre de la forme sélectionnée : poignées, bord, intérieur, centre de rotation. Zone de
		//    dessin (bloc du flux) : ses poignées seulement (taille) ; un clic à l'intérieur vise
		//    ses éléments.
		if (frameBox && frameBox->type == NodeType::Drawing && at.page == framePage) {
			const ShapeFrame f = FrameOf(*frameBox);
			if (const int hit = FrameHit(f, at.x, at.y, tolerance); hit >= 0 && hit < kHandleCount) {
				m_frame = m_preview = f;
				m_dragNode = frameBox->node;
				m_handle = hit;
				m_drag = DragMode::Resize;
				return;
			}
		} else if (frameBox && at.page == framePage) {
			const ShapeFrame f = FrameOf(*frameBox);
			const int hit = FrameHit(f, at.x, at.y, tolerance);
			m_frame = m_preview = f;
			m_dragNode = frameBox->node;
			if (m_rotateMode && std::hypot(at.x - f.pivotX, at.y - f.pivotY) <= tolerance) {
				m_drag = DragMode::Pivot;
				return;
			}
			if (m_clicks == 2 && hit >= 0 && hit <= kFrameBorder) { // double-clic sur le cadre : rotation
				m_rotateMode = !m_rotateMode;
				m_rotateNode = frameBox->node;
				return;
			}
			if (hit >= 0 && hit <= kFrameBorder) {
				m_handle = hit;
				m_drag = m_rotateMode ? DragMode::Rotate : hit < kHandleCount ? DragMode::Resize : DragMode::Move;
				return;
			}
			if (hit == kFrameInside) {
				m_drag = DragMode::Move;
				return;
			}
		}
		// 2. Ctrl+clic sur un lien : cible interne (titre, légende…) ou adresse externe.
		if (u.Ctrl())
			if (const std::string link = m_editor.LinkAt(at.page, at.x, at.y); !link.empty()) {
				FollowLink(link);
				return;
			}
		// 3. Outils des tableaux : « + », sélection de lignes / colonnes, bords.
		if (const TableUnder t = TableAt(at); t.hit.kind != TableHitKind::None) {
			m_tableDrag = t;
			switch (t.hit.kind) {
			case TableHitKind::InsertColumn: Report(m_editor.InsertColumnAt(t.geometry.table, t.hit.index)); break;
			case TableHitKind::InsertRow: Report(m_editor.InsertRowAt(t.geometry.table, t.hit.index)); break;
			case TableHitKind::SelectTable: m_editor.SelectTable(t.geometry.table); break;
			case TableHitKind::SelectColumn:
			case TableHitKind::SelectRow: {
				// Maj : plage contiguë depuis l'ancre ; Ctrl : ajoute ou retire la ligne / colonne ;
				// Ctrl+Maj : ajoute la plage. Glisser ensuite étend (en ajout si Ctrl).
				const bool rows = t.hit.kind == TableHitKind::SelectRow;
				const bool sameTable = m_editor.CurrentTable().table == t.geometry.table && m_tableAnchorRows == rows;
				m_tableAdd = u.Ctrl();
				TableSelect op = u.Ctrl() ? (u.Shift() ? TableSelect::Add : TableSelect::Toggle) : TableSelect::Replace;
				if (!(u.Shift() && sameTable))
					m_tableAnchor = t.hit.index;
				if (op == TableSelect::Toggle)
					m_tableAnchor = t.hit.index;
				m_tableAnchorRows = rows;
				if (rows)
					m_editor.SelectRows(t.geometry.table, m_tableAnchor, t.hit.index, op);
				else
					m_editor.SelectColumns(t.geometry.table, m_tableAnchor, t.hit.index, op);
				m_drag = op == TableSelect::Toggle ? DragMode::None : rows ? DragMode::SelectRows : DragMode::SelectColumns;
				break;
			}
			case TableHitKind::ColumnBorder: m_drag = DragMode::ColumnBorder; break;
			case TableHitKind::RowBorder: m_drag = DragMode::RowBorder; break;
			case TableHitKind::None: break;
			}
			SelectionMadeByUser();
			return;
		}
		// Bord intérieur d'un bloc à marge intérieure : la régler en glissant.
		if (const auto padding = PaddingUnder(at)) {
			m_paddingDrag = *padding;
			m_drag = DragMode::Padding;
			return;
		}
		// 4. Poignée d'un bloc (marge gauche) : glisser le paragraphe, le tableau…
		if (const NodeId block = GripBlock(at); block != kNoNode) {
			m_editor.SelectNode(block);
			m_dragNode = block;
			m_drag = DragMode::Block;
			SelectionMadeByUser();
			return;
		}
		// 4 bis. Cellules d'un tableau : Maj+clic sélectionne le rectangle contigu depuis la cellule
		//    d'ancrage (dernière cellule cliquée), Ctrl+clic ajoute ou retire la cellule, Ctrl+Maj+clic
		//    ajoute le rectangle. Dans la cellule d'ancrage, Maj+clic étend le texte.
		if (u.Ctrl() || u.Shift())
			if (const CellRef here = CellAt(at); here.Valid()) {
				const bool anchored = m_cellStart.Valid() && m_cellStart.table == here.table;
				if (u.Shift() && anchored && !(m_cellStart == here)) {
					m_editor.SelectCells(here.table, m_cellStart.row, m_cellStart.column, here.row, here.column,
										 u.Ctrl() ? TableSelect::Add : TableSelect::Replace);
					SelectionMadeByUser();
					return;
				}
				if (u.Ctrl() && !u.Shift()) {
					m_editor.SelectCells(here.table, here.row, here.column, here.row, here.column, TableSelect::Toggle);
					m_cellStart = here;
					SelectionMadeByUser();
					return;
				}
			}
		// 5. Ctrl+clic : sélection de nœuds.
		if (u.Ctrl()) {
			m_editor.SelectAt(at.page, at.x, at.y, u.Shift());
			SelectionMadeByUser();
			return;
		}
		// 4. Forme ou image sous le point.
		const NodeId hit = p->layout.HitTest(at.page, at.x, at.y);
		if (const Node *n = m_editor.Document().Find(hit); n && n->Type() == NodeType::Drawing) { // fond d'une zone de dessin
			m_editor.SelectNode(hit, u.Shift());
			SelectionMadeByUser();
			return;
		}
		if (const Node *n = m_editor.Document().Find(hit); n && (n->Type() == NodeType::Shape || n->Type() == NodeType::Image)) {
			m_editor.SelectNode(hit, u.Shift());
			SelectionMadeByUser();
			if (const LayoutBox *box = m_editor.BoxOf(hit); box && !u.Shift()) {
				m_frame = m_preview = FrameOf(*box);
				m_dragNode = hit;
				m_drag = DragMode::Move;
			}
			return;
		}
		// 5. Marges : sélection par zone.
		if (InMargin(at)) {
			m_drag = DragMode::Area;
			return;
		}
		// 6. Texte : curseur, mot (double-clic), paragraphe (triple-clic), glisser la plage.
		if (m_clicks == 3) {
			m_editor.SelectParagraphAt(at.page, at.x, at.y);
			return;
		}
		if (m_clicks == 2) {
			m_editor.SelectWordAt(at.page, at.x, at.y);
			return;
		}
		const auto pos = m_editor.PositionAt(at.page, at.x, at.y);
		if (!pos) {
			m_drag = DragMode::Area;
			return;
		}
		if (!u.Shift() && m_editor.InTextRange(*pos)) {
			m_drag = DragMode::TextMove;
			return;
		}
		const bool wasEditing = m_editor.Editing();
		m_editor.PlaceCaret(*pos, u.Shift() && wasEditing);
		m_drag = DragMode::Text;
		m_cellStart = CellAt(at); // un glisser qui quitte la cellule devient une sélection de cellules
		if (!wasEditing)
			SelectionMadeByUser();
	}

	void Drag(ui::Context &u, const ui::Rect &view, const std::optional<PagePoint> &at) {
		sdl3::Canvas &c = u.Canvas();
		const float mx = u.MouseX(), my = u.MouseY();
		m_dragMoved = m_dragMoved || std::abs(mx - m_dragX) > 3 || std::abs(my - m_dragY) > 3;
		const PageView *start = ViewOf(m_dragFrom.page);
		if (!start || !m_editor.Current()) {
			m_drag = DragMode::None;
			return;
		}
		// Coordonnées dans la page de départ (continuité au-delà de son bord).
		const double px = (mx - start->rect.x) / start->scale, py = (my - start->rect.y) / start->scale;
		const double dx = px - m_dragFrom.x, dy = py - m_dragFrom.y;
		if (m_drag == DragMode::Text || m_drag == DragMode::TextMove || m_drag == DragMode::Block || m_drag == DragMode::Area) {
			if (my < view.y + 16) // défilement automatique près des bords
				m_scrollY = std::max(0.f, m_scrollY - 14.f);
			else if (my > view.y + view.h - 16)
				m_scrollY += 14.f;
		}
		std::optional<doc::TextPosition> dropText;
		std::optional<BlockDrop> dropBlock;
		switch (m_drag) {
		case DragMode::None: break;
		case DragMode::Area:
			if (m_dragMoved)
				c.StrokeRect({std::min(m_dragX, mx), std::min(m_dragY, my), std::abs(mx - m_dragX), std::abs(my - m_dragY)}, P().accentSoft, 1.f);
			break;
		case DragMode::Text:
			if (m_dragMoved && at) {
				if (const CellRef here = CellAt(*at); m_cellStart.Valid() && here.table == m_cellStart.table && !(here == m_cellStart)) {
					m_drag = DragMode::Cells; // plusieurs cellules : sélection rectangulaire
					m_editor.SelectCells(here.table, m_cellStart.row, m_cellStart.column, here.row, here.column);
				} else if (const auto pos = m_editor.PositionAt(at->page, at->x, at->y)) {
					m_editor.PlaceCaret(*pos, true);
				}
			}
			break;
		case DragMode::Cells:
			if (at)
				if (const CellRef here = CellAt(*at); here.table == m_cellStart.table && here.Valid())
					m_editor.SelectCells(here.table, m_cellStart.row, m_cellStart.column, here.row, here.column);
			break;
		case DragMode::SelectRows:
		case DragMode::SelectColumns:
			if (at)
				if (const CellRef here = CellAt(*at); here.table == m_tableDrag.geometry.table && here.Valid()) {
					if (m_drag == DragMode::SelectRows)
						m_editor.SelectRows(here.table, m_tableAnchor, here.row, m_tableAdd ? TableSelect::Add : TableSelect::Replace);
					else
						m_editor.SelectColumns(here.table, m_tableAnchor, here.column, m_tableAdd ? TableSelect::Add : TableSelect::Replace);
				}
			break;
		case DragMode::ColumnBorder:
		case DragMode::RowBorder: {
			const TableGeometry &g = m_tableDrag.geometry;
			const PageView *pv = ViewOf(g.page);
			if (!pv)
				break;
			const bool column = m_drag == DragMode::ColumnBorder;
			const double value = column ? BorderX(g, m_tableDrag.hit.index, px, u.Shift()) : BorderY(g, m_tableDrag.hit.index, py, u.Shift());
			const sdl3::FRect r = column ? PageToScreen(*pv, Rect(value, g.rows.front(), 0, g.rows.back() - g.rows.front()))
										 : PageToScreen(*pv, Rect(g.columns.front(), value, g.columns.back() - g.columns.front(), 0));
			c.FillRect(column ? sdl3::FRect{r.x - 1, r.y, 2, r.h} : sdl3::FRect{r.x, r.y - 1, r.w, 2}, P().guide);
			const double size = column ? value - g.columns[m_tableDrag.hit.index - 1] : value - g.rows[m_tableDrag.hit.index];
			DragLabel(c, mx, my, column ? TrF("Largeur : {}", FormatLength(size)) : TrF("Hauteur : {}", FormatLength(size)));
			u.SetCursor(column ? sdl3::SystemCursor::SIZE_EW : sdl3::SystemCursor::SIZE_NS);
			break;
		}
		case DragMode::TextMove:
			if (m_dragMoved && at)
				if (const auto pos = m_editor.PositionAt(at->page, at->x, at->y); pos && !m_editor.InTextRange(*pos)) {
					dropText = pos;
					if (const auto caret = m_editor.CaretGeometry(*pos))
						if (const PageView *pv = ViewOf(caret->page)) {
							const sdl3::FRect r = PageToScreen(*pv, caret->rect);
							c.FillRect({r.x - 1, r.y, 2, std::max(r.h, 4.f)}, P().guide);
						}
				}
			u.SetCursor(m_dragMoved ? (u.Ctrl() ? sdl3::SystemCursor::CROSSHAIR : sdl3::SystemCursor::MOVE) : sdl3::SystemCursor::TEXT);
			break;
		case DragMode::Block:
			if (m_dragMoved && at && (dropBlock = BlockDropAt(*at)))
				if (const PageView *pv = ViewOf(dropBlock->page)) {
					const sdl3::FRect r = PageToScreen(*pv, Rect(dropBlock->x, dropBlock->y, dropBlock->w, 0));
					c.FillRect({r.x - 6, r.y - 1.5f, r.w + 12, 3}, P().guide);
				}
			break;
		case DragMode::Move: {
			// Maj : déplacement sur un seul axe ; grille : coin haut gauche aimanté.
			const auto [mdx, mdy] = SnappedMove(m_frame.rect, dx, dy, GridStep(), u.Shift());
			m_preview = m_frame;
			m_preview.rect.x += mdx;
			m_preview.rect.y += mdy;
			m_preview.pivotX += mdx;
			m_preview.pivotY += mdy;
			if (m_dragMoved) // coin supérieur gauche (avant rotation), repère de la page
				DragLabel(c, mx, my, TrF("X {}  ·  Y {}", FormatLength(m_preview.rect.x), FormatLength(m_preview.rect.y)));
			break;
		}
		case DragMode::Resize: {
			// Grille (forme non tournée) : la poignée glissée aimantée ; Ctrl ou KeepAspectRatio :
			// proportions gardées.
			double rdx = dx, rdy = dy;
			if (const double step = GridStep(); step > 0 && m_frame.rotation == 0) {
				const auto [hx, hy] = HandlePoint(m_frame, m_handle);
				rdx = SnapToGrid(hx + dx, step) - hx;
				rdy = SnapToGrid(hy + dy, step) - hy;
			}
			const bool keepRatio = u.Ctrl() || (m_editor.Document().Contains(m_dragNode) && m_editor.Document().Get(m_dragNode, props::KeepAspectRatio));
			m_preview = m_frame;
			m_preview.rect = ResizedRect(m_frame, m_handle, rdx, rdy, keepRatio);
			// Centre par défaut : il suit la forme (le moteur le recalcule de même).
			m_preview.pivotX += (m_preview.rect.x + m_preview.rect.w / 2) - (m_frame.rect.x + m_frame.rect.w / 2);
			m_preview.pivotY += (m_preview.rect.y + m_preview.rect.h / 2) - (m_frame.rect.y + m_frame.rect.h / 2);
			if (m_dragMoved)
				DragLabel(c, mx, my, TrF("L {}  ×  H {}", FormatLength(m_preview.rect.w), FormatLength(m_preview.rect.h)));
			break;
		}
		case DragMode::Rotate:
			m_preview = m_frame;
			m_preview.rotation = DraggedRotation(m_frame, m_dragFrom.x, m_dragFrom.y, px, py, u.Shift());
			DragLabel(c, mx, my, std::format("{:.1f}°", m_preview.rotation));
			break;
		case DragMode::Pivot:
			m_preview = m_frame;
			m_preview.pivotX = px;
			m_preview.pivotY = py;
			if (m_dragMoved)
				DragLabel(c, mx, my, TrF("Centre : X {}  ·  Y {}", FormatLength(px), FormatLength(py)));
			break;
		case DragMode::Padding: {
			const PaddingHandle &h = m_paddingDrag;
			const PageView *pv = ViewOf(m_dragFrom.page);
			if (!pv)
				break;
			const double value = DraggedPadding(h, h.Horizontal() ? px : py, u.Shift());
			PaddingHandle preview = h;
			(h.side == PaddingSide::Left ? preview.padding.left : h.side == PaddingSide::Right ? preview.padding.right
														 : h.side == PaddingSide::Top	   ? preview.padding.top
																						   : preview.padding.bottom) = value;
			DrawPaddingGuide(c, *pv, preview);
			DragLabel(c, mx, my, std::format("{} : {}", Tr(PaddingName(h.side)), FormatLength(value)));
			u.SetCursor(h.Horizontal() ? sdl3::SystemCursor::SIZE_EW : sdl3::SystemCursor::SIZE_NS);
			break;
		}
		}
		if (!u.MouseReleased())
			return;
		const DragMode mode = m_drag;
		m_drag = DragMode::None;
		if (!m_dragMoved) {
			if (mode == DragMode::Area)
				m_editor.SelectAt(m_dragFrom.page, m_dragFrom.x, m_dragFrom.y, u.Shift()), SelectionMadeByUser();
			else if (mode == DragMode::TextMove) // clic dans la plage sans glisser : curseur
				(void)m_editor.PlaceCaretAt(m_dragFrom.page, m_dragFrom.x, m_dragFrom.y);
			return;
		}
		switch (mode) {
		case DragMode::Area:
			m_editor.SelectArea(m_dragFrom.page, Rect::FromPoints(m_dragFrom.x, m_dragFrom.y, px, py), u.Shift());
			SelectionMadeByUser();
			break;
		case DragMode::TextMove:
			if (dropText)
				Report(m_editor.DropText(*dropText, u.Ctrl()));
			break;
		case DragMode::Block:
			if (dropBlock)
				Report(m_editor.MoveBlock(m_dragNode, dropBlock->parent, dropBlock->index));
			break;
		case DragMode::Move: Report(m_editor.MoveShape(m_dragNode, m_preview.rect.x - m_frame.rect.x, m_preview.rect.y - m_frame.rect.y)); break;
		case DragMode::Resize: Report(m_editor.ResizeShape(m_dragNode, m_preview.rect)); break;
		case DragMode::Rotate: Report(m_editor.RotateShape(m_dragNode, m_preview.rotation)); break;
		case DragMode::Pivot: Report(m_editor.SetRotationOrigin(m_dragNode, px, py)); break;
		case DragMode::Padding: {
			// Nœud survolé ; s'il fait partie de la sélection : tous les nœuds sélectionnés de son type.
			const PaddingHandle &h = m_paddingDrag;
			std::vector<NodeId> targets{h.node};
			if (m_editor.Selection().Contains(h.node))
				for (NodeId id : m_editor.Selection().Nodes())
					if (id != h.node && m_editor.Document().Contains(id) && m_editor.Document().Find(id)->Type() == h.type)
						targets.push_back(id);
			Report(m_editor.SetNodes(targets, h.Property(), DraggedPadding(h, h.Horizontal() ? px : py, u.Shift())));
			break;
		}
		case DragMode::ColumnBorder:
			ResizeColumnBorder(m_tableDrag.geometry, m_tableDrag.hit.index, BorderX(m_tableDrag.geometry, m_tableDrag.hit.index, px, u.Shift()));
			break;
		case DragMode::RowBorder: {
			const TableGeometry &g = m_tableDrag.geometry;
			const size_t row = m_tableDrag.hit.index;
			Report(m_editor.SetRowHeight(g.rowNodes[row], BorderY(g, row, py, u.Shift()) - g.rows[row]));
			break;
		}
		case DragMode::None:
		case DragMode::Text:
		case DragMode::Cells:
		case DragMode::SelectRows:
		case DragMode::SelectColumns: break;
		}
	}

	// ── Tableaux à l'écran : outils au survol, cellules, bords ──

	struct TableUnder {
		TableGeometry geometry;
		TableHit hit;
	};
	struct CellRef {
		NodeId table = kNoNode;
		size_t row = 0, column = 0;
		[[nodiscard]] bool Valid() const noexcept { return table != kNoNode; }
		friend bool operator==(const CellRef &, const CellRef &) = default;
	};

	/// Outil du tableau sous le point (le plus imbriqué) : bords, « + », poignées de sélection.
	TableUnder TableAt(const PagePoint &at) const {
		const Presentation *p = m_editor.Current();
		const PageView *pv = ViewOf(at.page);
		if (!p || !pv)
			return {};
		TableUnder best;
		for (TableGeometry &g : TablesOn(*p->snapshot, p->layout, at.page)) {
			if (!m_editor.Document().Contains(g.table))
				continue;
			const TableHit hit = HitTable(g, at.x, at.y, double(3.f / pv->scale), double(16.f / pv->scale));
			if (hit.kind != TableHitKind::None)
				best = {std::move(g), hit};
		}
		return best;
	}

	/// Cellule (rang dans le tableau) sous le point ; tableau le plus imbriqué.
	CellRef CellAt(const PagePoint &at) const {
		const Presentation *p = m_editor.Current();
		if (!p)
			return {};
		CellRef best;
		for (const TableGeometry &g : TablesOn(*p->snapshot, p->layout, at.page)) {
			const size_t r = g.RowAt(at.y), c = g.ColumnAt(at.x);
			if (r != SIZE_MAX && c != SIZE_MAX && m_editor.Document().Contains(g.table)) {
				// Case couverte par une fusion : la cellule fusionnée (son ancre).
				const TableGrid grid = TableGrid::Of(m_editor.Document(), g.table);
				const CellSpan span = table::SpanAt(table::Spans(m_editor.Document(), grid.cells), g.rowIndices[r], c);
				best = {g.table, span.row, span.column};
			}
		}
		return best;
	}

	void DrawTableTool(ui::Context &u, const TableUnder &t) {
		sdl3::Canvas &c = u.Canvas();
		const PageView *pv = ViewOf(t.geometry.page);
		if (!pv)
			return;
		const TableGeometry &g = t.geometry;
		const sdl3::Color accent = P().selection;
		const auto screen = [&](double x, double y) { return std::pair{pv->rect.x + float(x) * pv->scale, pv->rect.y + float(y) * pv->scale}; };
		const auto plus = [&](float x, float y) {
			c.FillCircle(x, y, 8.f, accent);
			c.Line(x - 4, y, x + 4, y, Rgba(255, 255, 255), 2.f);
			c.Line(x, y - 4, x, y + 4, Rgba(255, 255, 255), 2.f);
		};
		const auto [left, top] = screen(g.columns.front(), g.rows.front());
		const auto [right, bottom] = screen(g.columns.back(), g.rows.back());
		switch (t.hit.kind) {
		case TableHitKind::InsertColumn: {
			const float x = screen(g.columns[t.hit.index], 0).first;
			c.FillRect({x - 1, top, 2, bottom - top}, sdl3::WithAlpha(P().selection, 180));
			plus(x, top - 9);
			break;
		}
		case TableHitKind::InsertRow: {
			size_t border = 0;
			for (size_t i = 0; i < g.rowIndices.size(); ++i)
				if (g.rowIndices[i] == t.hit.index)
					border = i, i = g.rowIndices.size();
			if (t.hit.index > g.rowIndices.back())
				border = g.rows.size() - 1;
			const float y = screen(0, g.rows[border]).second;
			c.FillRect({left, y - 1, right - left, 2}, sdl3::WithAlpha(P().selection, 180));
			plus(left - 9, y);
			break;
		}
		case TableHitKind::SelectColumn: {
			const auto [x0, unused0] = screen(g.columns[t.hit.index], 0);
			const auto [x1, unused1] = screen(g.columns[t.hit.index + 1], 0);
			(void)unused0;
			(void)unused1;
			c.FillRect({x0, top, x1 - x0, bottom - top}, sdl3::WithAlpha(P().selection, 40));
			c.FillRect({x0 + 2, top - 10, x1 - x0 - 4, 6}, accent);
			break;
		}
		case TableHitKind::SelectRow:
			for (size_t i = 0; i < g.rowIndices.size(); ++i)
				if (g.rowIndices[i] == t.hit.index) {
					const float y0 = screen(0, g.rows[i]).second, y1 = screen(0, g.rows[i + 1]).second;
					c.FillRect({left, y0, right - left, y1 - y0}, sdl3::WithAlpha(P().selection, 40));
					c.FillRect({left - 10, y0 + 2, 6, y1 - y0 - 4}, accent);
				}
			break;
		case TableHitKind::SelectTable:
			c.FillRect({left, top, right - left, bottom - top}, sdl3::WithAlpha(P().selection, 30));
			c.FillRect({left - 12, top - 12, 10, 10}, accent);
			break;
		case TableHitKind::ColumnBorder:
		case TableHitKind::RowBorder: break; // curseur seulement (voir DocumentMouse)
		case TableHitKind::None: break;
		}
		if (t.hit.kind == TableHitKind::ColumnBorder)
			u.SetCursor(sdl3::SystemCursor::SIZE_EW);
		else if (t.hit.kind == TableHitKind::RowBorder)
			u.SetCursor(sdl3::SystemCursor::SIZE_NS);
		else
			u.SetCursor(sdl3::SystemCursor::POINTER);
	}

	/// Nouvelle abscisse du bord `border` (largeur de la colonne de gauche arrondie au demi-mm,
	/// colonnes de 6 pt au moins de part et d'autre).
	static double BorderX(const TableGeometry &g, size_t border, double x, bool fine) {
		const double left = g.columns[border - 1];
		double width = SnapLength(x - left, fine);
		const double max = border + 1 < g.columns.size() ? g.columns[border + 1] - left - 6 : 1e9;
		width = std::clamp(width, 6.0, std::max(6.0, max));
		return left + width;
	}

	static double BorderY(const TableGeometry &g, size_t row, double y, bool fine) {
		return g.rows[row] + std::max(6.0, SnapLength(y - g.rows[row], fine));
	}

	/// Bord vertical glissé : largeur de la colonne de gauche, et de celle de droite (son bord
	/// droit ne bouge pas), en une entrée d'historique.
	void ResizeColumnBorder(const TableGeometry &g, size_t border, double x) {
		m_editor.History().BeginTransaction(Tr("Largeur des colonnes"));
		Status s = m_editor.SetColumnWidth(g.table, border - 1, x - g.columns[border - 1]);
		if (s && border + 1 < g.columns.size())
			s = m_editor.SetColumnWidth(g.table, border, g.columns[border + 1] - x);
		if (!s) {
			(void)m_editor.History().Rollback();
			Report(s);
			return;
		}
		Report(m_editor.History().Commit());
	}

	/// Suit un lien : nœud du document (curseur et défilement) ou adresse externe (navigateur).
	void FollowLink(const std::string &link) {
		if (const NodeId target = LinkTarget(link); target != kNoNode) {
			if (!m_editor.GoTo(target))
				Ui().Notify(Tr("cible du lien introuvable"), Ui().Style().warning);
			ScrollTo(target);
			return;
		}
		if (!sdl3::OpenUrl(::String(link)))
			Ui().Notify(TrF("ouverture impossible : {}", link), Ui().Style().warning);
	}

	// ── Marges intérieures : outil au survol ──

	std::optional<PaddingHandle> PaddingUnder(const PagePoint &at) const {
		const Presentation *p = m_editor.Current();
		const PageView *pv = ViewOf(at.page);
		if (!p || !pv || at.page >= p->layout.pages.size())
			return std::nullopt;
		auto h = PaddingAt(*p->snapshot, p->layout.pages[at.page], at.x, at.y, double(3.f / pv->scale));
		if (h && !m_editor.Document().Contains(h->node))
			return std::nullopt;
		return h;
	}

	/// Zone de contenu de la boîte (cadre fin) et bord intérieur visé (trait orange).
	static void DrawPaddingGuide(sdl3::Canvas &c, const PageView &pv, const PaddingHandle &h) {
		const doc::Rect inner(h.box.x + h.border + h.padding.left, h.box.y + h.border + h.padding.top,
							  std::max(0.0, h.box.w - 2 * h.border - h.padding.left - h.padding.right),
							  std::max(0.0, h.box.h - 2 * h.border - h.padding.top - h.padding.bottom));
		const sdl3::FRect r = PageToScreen(pv, inner), box = PageToScreen(pv, h.box);
		c.StrokeRect(r, sdl3::Color(230, 120, 0, 120), 1.f);
		const sdl3::Color edge(230, 120, 0, 255);
		switch (h.side) {
		case PaddingSide::Left: c.FillRect({r.x - 1, box.y, 2, box.h}, edge); break;
		case PaddingSide::Right: c.FillRect({r.x + r.w - 1, box.y, 2, box.h}, edge); break;
		case PaddingSide::Top: c.FillRect({box.x, r.y - 1, box.w, 2}, edge); break;
		case PaddingSide::Bottom: c.FillRect({box.x, r.y + r.h - 1, box.w, 2}, edge); break;
		}
	}

	// ── Règles : marges de la page, bords du tableau courant ──

	struct RulerDrag {
		bool active = false, horizontal = true;
		RulerHandle handle;
		size_t page = 0;
		TableGeometry table;
		double top = 0, bottom = 0, spaceBefore = 0; ///< paragraphe du curseur (espaces avant / après)
	};

	/// Page dont les règles montrent les marges : celle du curseur, de la sélection, sinon la
	/// première page visible.
	const PageView *ActivePage(const ui::Rect &view) const {
		const Presentation *p = m_editor.Current();
		if (!p)
			return nullptr;
		size_t page = SIZE_MAX;
		double top = 0;
		if (m_editor.Editing()) {
			if (const auto caret = m_editor.CaretGeometry(m_editor.Caret()))
				page = caret->page;
		} else if (p->layout.Locate(m_editor.Selection().Primary(), page, top)) {
		}
		if (const PageView *pv = page != SIZE_MAX ? ViewOf(page) : nullptr; pv && pv->rect.y < view.y + view.h && pv->rect.y + pv->rect.h > view.y)
			return pv;
		for (const PageView &pv : m_views.pages)
			if (pv.rect.y + pv.rect.h > view.y + 20)
				return &pv;
		return nullptr;
	}

	void Rulers(ui::Context &u, const sdl3::FRect &area, const ui::Rect &view) {
		sdl3::Canvas &c = u.Canvas();
		const Presentation *p = m_editor.Current();
		const PageView *pv = ActivePage(view);
		c.FillRect({area.x, area.y, kRulerThickness, kRulerThickness}, P().rulerFrame);
		const ui::Rect hRect{view.x, area.y, view.w, kRulerThickness}, vRect{area.x, view.y, kRulerThickness, view.h};
		if (!p || !pv)
			return;
		const PageGeometry &g = p->layout.pages[pv->index].geometry;
		// Repères : marges, bords du tableau courant sur cette page.
		std::vector<RulerHandle> hHandles{{RulerHandle::MarginStart, g.margins.left, 0}, {RulerHandle::MarginEnd, g.width - g.margins.right, 0}};
		std::vector<RulerHandle> vHandles{{RulerHandle::MarginStart, g.margins.top, 0}, {RulerHandle::MarginEnd, g.height - g.margins.bottom, 0}};
		TableGeometry table;
		if (const TableSelection t = m_editor.CurrentTable(); t.Valid())
			for (TableGeometry &tg : TablesOn(*p->snapshot, p->layout, pv->index))
				if (tg.table == t.table) {
					table = std::move(tg);
					for (size_t i = 1; i < table.columns.size(); ++i)
						hHandles.push_back({RulerHandle::TableBorder, table.columns[i], i});
					for (size_t i = 1; i < table.rows.size(); ++i)
						vHandles.push_back({RulerHandle::TableBorder, table.rows[i], i - 1});
					break;
				}
		// Paragraphe du curseur sur cette page : repères de son espace avant et de son espace après.
		double paragraphTop = 0, paragraphBottom = 0, spaceBefore = 0;
		if (m_editor.Editing()) {
			const doc::Document &d = m_editor.Document();
			const NodeId paragraph = d.AncestorOfType(m_editor.Caret().text, NodeType::Paragraph);
			for (const LayoutBox &b : p->layout.pages[pv->index].boxes)
				if (b.node == paragraph && d.Contains(paragraph)) {
					paragraphTop = b.rect.y;
					paragraphBottom = b.rect.Bottom();
					spaceBefore = d.Get(paragraph, props::SpaceBefore);
					if (!b.continued)
						vHandles.push_back({RulerHandle::SpaceBefore, b.rect.y, 0});
					if (!b.continues)
						vHandles.push_back({RulerHandle::SpaceAfter, b.rect.Bottom() + d.Get(paragraph, props::SpaceAfter), 0});
					break;
				}
		}
		const float tolerance = 6.f;
		int hActive = -1, vActive = -1;
		if (!m_rulerDrag.active) {
			if (u.MouseOver(hRect))
				hActive = HandleAt(hHandles, pv->rect.x, pv->scale, u.MouseX(), tolerance);
			if (u.MouseOver(vRect))
				vActive = HandleAt(vHandles, pv->rect.y, pv->scale, u.MouseY(), tolerance);
			if (hActive >= 0 || vActive >= 0) {
				u.SetCursor(hActive >= 0 ? sdl3::SystemCursor::SIZE_EW : sdl3::SystemCursor::SIZE_NS);
				const RulerHandle &h = hActive >= 0 ? hHandles[size_t(hActive)] : vHandles[size_t(vActive)];
				u.Tooltip(h.kind == RulerHandle::TableBorder  ? Tr("Glisser : bord du tableau (Maj : au dixième de mm)")
						  : h.kind == RulerHandle::SpaceBefore ? Tr("Glisser : espace avant le paragraphe (Maj : au dixième de mm)")
						  : h.kind == RulerHandle::SpaceAfter  ? Tr("Glisser : espace après le paragraphe (Maj : au dixième de mm)")
															   : Tr("Glisser : marge de la page (Maj : au dixième de mm)"));
				if (u.MousePressed())
					m_rulerDrag = {true, hActive >= 0, hActive >= 0 ? hHandles[size_t(hActive)] : vHandles[size_t(vActive)], pv->index, table,
								   paragraphTop, paragraphBottom, spaceBefore};
			}
		}
		// Glisser : valeur aimantée, guide sur la page, cote affichée ; appliqué au relâchement.
		double value = 0;
		if (m_rulerDrag.active && m_rulerDrag.page == pv->index) {
			const bool h = m_rulerDrag.horizontal;
			const double raw = h ? (u.MouseX() - pv->rect.x) / pv->scale : (u.MouseY() - pv->rect.y) / pv->scale;
			const bool fine = u.Shift();
			const RulerHandle &handle = m_rulerDrag.handle;
			std::string label;
			double space = 0; // espace avant / après obtenu
			if (handle.kind == RulerHandle::SpaceBefore) {
				space = DraggedSpaceBefore(m_rulerDrag.top, m_rulerDrag.spaceBefore, raw, fine);
				value = m_rulerDrag.top + (space - m_rulerDrag.spaceBefore);
				label = TrF("Espace avant : {}", FormatLength(space));
			} else if (handle.kind == RulerHandle::SpaceAfter) {
				space = DraggedSpaceAfter(m_rulerDrag.bottom, raw, fine);
				value = m_rulerDrag.bottom + space;
				label = TrF("Espace après : {}", FormatLength(space));
			} else if (handle.kind == RulerHandle::TableBorder) {
				value = h ? BorderX(m_rulerDrag.table, handle.index, raw, fine) : BorderY(m_rulerDrag.table, handle.index, raw, fine);
				const double size = h ? value - m_rulerDrag.table.columns[handle.index - 1] : value - m_rulerDrag.table.rows[handle.index];
				label = h ? TrF("Colonne : {}", FormatLength(size)) : TrF("Ligne : {}", FormatLength(size));
			} else {
				const double length = h ? g.width : g.height;
				const double other = handle.kind == RulerHandle::MarginStart ? length - (h ? g.margins.right : g.margins.bottom) : (h ? g.margins.left : g.margins.top);
				value = handle.kind == RulerHandle::MarginStart ? std::clamp(SnapLength(raw, fine), 0.0, other - 36)
																 : length - std::clamp(SnapLength(length - raw, fine), 0.0, length - other - 36);
				const double margin = handle.kind == RulerHandle::MarginStart ? value : length - value;
				static constexpr std::string_view kNames[2][2] = {{"Marge haute : {}", "Marge basse : {}"}, {"Marge gauche : {}", "Marge droite : {}"}};
				label = TrArg(kNames[h ? 1 : 0][handle.kind == RulerHandle::MarginStart ? 0 : 1], FormatLength(margin));
			}
			c.PushClip(view);
			if (h) {
				const float x = pv->rect.x + float(value) * pv->scale;
				c.FillRect({x - 0.5f, view.y, 1, view.h}, P().guide);
			} else {
				const float y = pv->rect.y + float(value) * pv->scale;
				c.FillRect({view.x, y - 0.5f, view.w, 1}, P().guide);
			}
			c.PopClip();
			DragLabel(c, u.MouseX(), u.MouseY(), label);
			u.SetCursor(h ? sdl3::SystemCursor::SIZE_EW : sdl3::SystemCursor::SIZE_NS);
			if (u.MouseReleased()) {
				m_rulerDrag.active = false;
				if (handle.kind == RulerHandle::SpaceBefore || handle.kind == RulerHandle::SpaceAfter) {
					Report(m_editor.SetParagraph(handle.kind == RulerHandle::SpaceBefore ? PropertyId::SpaceBefore : PropertyId::SpaceAfter, space));
				} else if (handle.kind == RulerHandle::TableBorder) {
					if (h)
						ResizeColumnBorder(m_rulerDrag.table, handle.index, value);
					else
						Report(m_editor.SetRowHeight(m_rulerDrag.table.rowNodes[handle.index], value - m_rulerDrag.table.rows[handle.index]));
				} else {
					const PropertyId prop = h ? (handle.kind == RulerHandle::MarginStart ? PropertyId::MarginLeft : PropertyId::MarginRight)
											  : (handle.kind == RulerHandle::MarginStart ? PropertyId::MarginTop : PropertyId::MarginBottom);
					Report(m_editor.SetPage(prop, handle.kind == RulerHandle::MarginStart ? value : (h ? g.width : g.height) - value));
				}
			}
			if (h)
				hActive = int(std::ranges::find_if(hHandles, [&](const RulerHandle &x) { return x.kind == handle.kind && x.index == handle.index; }) - hHandles.begin());
			else
				vActive = int(std::ranges::find_if(vHandles, [&](const RulerHandle &x) { return x.kind == handle.kind && x.index == handle.index; }) - vHandles.begin());
		} else if (m_rulerDrag.active && !u.MouseDown()) {
			m_rulerDrag.active = false;
		}
		DrawRuler(c, hRect, true, pv->rect.x, pv->scale, g.width, g.margins.left, g.margins.right, hHandles, hActive);
		DrawRuler(c, vRect, false, pv->rect.y, pv->scale, g.height, g.margins.top, g.margins.bottom, vHandles, vActive);
		// Pendant le glisser, le repère suit le pointeur.
		if (m_rulerDrag.active && m_rulerDrag.page == pv->index) {
			if (m_rulerDrag.horizontal) {
				const float x = pv->rect.x + float(value) * pv->scale;
				c.FillTriangle(x - 6, hRect.y + hRect.h - 10, x + 6, hRect.y + hRect.h - 10, x, hRect.y + hRect.h - 2, P().guide);
			} else {
				const float y = pv->rect.y + float(value) * pv->scale;
				c.FillTriangle(vRect.x + vRect.w - 10, y - 6, vRect.x + vRect.w - 10, y + 6, vRect.x + vRect.w - 2, y, P().guide);
			}
		}
	}

	/// Garde le curseur de texte visible après une action au clavier.
	void EnsureCaretVisible() {
		const auto caret = m_editor.CaretGeometry(m_editor.Caret());
		if (!caret)
			return;
		const PageView *pv = ViewOf(caret->page);
		if (!pv)
			return;
		const float top = pv->rect.y + float(caret->rect.y) * pv->scale, bottom = top + float(caret->rect.h) * pv->scale;
		if (top < m_viewTop)
			m_scrollY = std::max(0.f, m_scrollY - (m_viewTop - top) - 24.f);
		else if (bottom > m_viewBottom)
			m_scrollY += bottom - m_viewBottom + 24.f;
	}

	// ── Couleurs du texte (fenêtre surgissante) ──

	/// Couleur de fond des pages visées (pages sélectionnées, sinon la section).
	[[nodiscard]] Color PageColor() const {
		const auto [section, page] = PageTarget();
		return Prop<Color>::FromValue(ResolvePageProperty(m_editor.Document(), section, page, PropertyId::BackgroundColor).value);
	}

	/// Fenêtre des couleurs sous `anchor` (gardée dans la fenêtre) ; `property` : propriété de
	/// couleur des nœuds sélectionnés (cible Property).
	void OpenColorPopup(ColorTarget target, const ui::Rect &anchor, Color initial, PropertyId property = PropertyId::TextColor) {
		m_colorTarget = target;
		m_colorProperty = property;
		m_pickColor = initial;
		m_colorFilter.clear();
		// Gardée dans la zone de l'application (sous la barre de titre).
		const ui::Rect area = m_area.w > 0 ? m_area : ui::Rect{0, 0, Canvas().Width(), Canvas().Height()};
		const float w = 300, h = 486;
		const float x = std::clamp(anchor.x, area.x + 8, std::max(area.x + 8, area.x + area.w - w - 8));
		float y = anchor.y + anchor.h + 8;
		if (y + h > area.y + area.h - 8) // pas la place dessous : au-dessus, sinon le plus haut possible
			y = std::max(area.y + 8, std::min(anchor.y - h - 8, area.y + area.h - h - 8));
		m_popupRect = {x, y, w, h};
		m_popupFresh = true;
	}

	/// Couleur RGBA : sélecteur (teinte, saturation, valeur, opacité, code), ou toute couleur
	/// nommée de sdl3 (liste filtrable) ; « Aucune » : transparent (fonds, contours…).
	void ColorPopup(ui::Context &u) {
		if (m_colorTarget == ColorTarget::None)
			return;
		sdl3::Canvas &c = u.Canvas();
		const ui::Rect r = m_popupRect;
		c.FillRect({r.x + 3, r.y + 4, r.w, r.h}, Rgba(0, 0, 0, 90));
		c.FillRoundRect(r, 8.f, P().popup);
		c.StrokeRoundRect(r, 8.f, P().popupBorder, 1.f);
		std::string title;
		switch (m_colorTarget) {
		case ColorTarget::Text: title = Tr("Couleur du texte"); break;
		case ColorTarget::Highlight: title = Tr("Couleur de surlignage"); break;
		case ColorTarget::Cells: title = Tr("Fond des cellules"); break;
		case ColorTarget::Page: title = Tr("Couleur de page"); break;
		case ColorTarget::Property: title = TrArg("Couleur : {}", Info(m_colorProperty).name); break;
		case ColorTarget::None: break;
		}
		c.Text(r.x + 12, r.y + 8, c.Ellipsize(title, r.w - 24, 13), P().strong, 13);
		u.BeginColumn({r.x + 10, r.y + 32, r.w - 20, r.h - 42});
		u.ColorPicker("scribe.color", m_pickColor, 170.f);
		const ui::Rect current = u.Next(20); // valeur choisie : nom de sdl3, sinon code RGBA
		c.FillRect({current.x, current.y + 2, 16, 16}, Rgba(255, 255, 255));
		c.FillRect({current.x, current.y + 2, 16, 16}, m_pickColor);
		c.StrokeRect({current.x, current.y + 2, 16, 16}, P().border, 1.f);
		const std::optional<std::string_view> named = m_pickColor.Name();
		c.Text(current.x + 24, current.y + 3, named ? std::string(*named) + "  " + Hex(m_pickColor) : Hex(m_pickColor), P().muted, 11);
		u.TextInputAt(u.Next(26), "scribe.colorfilter", m_colorFilter, nullptr, Tr("Rechercher une couleur nommée…"));
		const ui::Rect listArea = u.Next(150);
		u.BeginScroll("scribe.colorlist", listArea);
		for (const auto &[name, color] : NamedColorList()) {
			if (!m_colorFilter.empty() && !MatchesColorFilter(name, m_colorFilter))
				continue;
			const ui::Rect row = u.Next(22);
			const bool hovered = u.MouseOver(row) && listArea.Contains(u.MouseX(), u.MouseY());
			if (color == m_pickColor)
				c.FillRect(row, sdl3::WithAlpha(P().selection, 70));
			else if (hovered)
				c.FillRect(row, P().hover);
			c.FillRect({row.x + 4, row.y + 3, 26, 16}, Rgba(255, 255, 255));
			c.FillRect({row.x + 4, row.y + 3, 26, 16}, color);
			c.StrokeRect({row.x + 4, row.y + 3, 26, 16}, P().border, 1.f);
			c.Text(row.x + 38, row.y + 4, name, P().text, 12);
			if (hovered && u.MousePressed())
				m_pickColor = color;
		}
		u.EndScroll();
		const ui::Rect buttons = u.Next(30);
		const float bw = (buttons.w - 12) / 3;
		bool close = false;
		const auto apply = [this](Color color) {
			switch (m_colorTarget) {
			case ColorTarget::Text: Report(m_editor.SetText(PropertyId::TextColor, color)); break;
			case ColorTarget::Highlight: Report(m_editor.SetText(PropertyId::BackgroundColor, color)); break;
			case ColorTarget::Cells: Report(m_editor.SetCells(PropertyId::BackgroundColor, color)); break;
			case ColorTarget::Page: Report(m_editor.SetPage(PropertyId::BackgroundColor, color)); break;
			case ColorTarget::Property: Report(m_editor.SetSelected(m_colorProperty, color)); break;
			case ColorTarget::None: break;
			}
		};
		if (u.PrimaryButton({buttons.x, buttons.y, bw, 28}, Tr("Appliquer"))) {
			apply(m_pickColor);
			close = true;
		}
		if (m_colorTarget != ColorTarget::Text && u.ButtonAt({buttons.x + bw + 6, buttons.y, bw, 28}, Tr("Aucun"))) {
			apply(Color::TRANSPARENT());
			close = true;
		}
		if (u.ButtonAt({buttons.x + 2 * (bw + 6), buttons.y, bw, 28}, Tr("Annuler")))
			close = true;
		u.EndColumn();
		if (u.KeycodePressed(SDLK_ESCAPE) || (!m_popupFresh && u.MousePressed() && !r.Contains(u.MouseX(), u.MouseY())))
			close = true;
		m_popupFresh = false;
		if (close)
			m_colorTarget = ColorTarget::None;
	}

	// ── Lien (fenêtre surgissante) ──

	void OpenLinkPopup(const ui::Rect &anchor) {
		m_linkOpen = true;
		m_linkUrl.clear();
		m_linkTarget = 0;
		m_linkRect = {anchor.x, anchor.y + anchor.h + 40, 380, 176};
		m_popupFresh = true;
	}

	void LinkPopup(ui::Context &u) {
		if (!m_linkOpen)
			return;
		sdl3::Canvas &c = u.Canvas();
		const ui::Rect r = m_linkRect;
		c.FillRect({r.x + 3, r.y + 4, r.w, r.h}, Rgba(0, 0, 0, 90));
		c.FillRoundRect(r, 8.f, P().popup);
		c.StrokeRoundRect(r, 8.f, P().popupBorder, 1.f);
		c.Text(r.x + 12, r.y + 8, Tr("Lien sur la sélection"), P().strong, 13);
		c.Text(r.x + 12, r.y + 34, Tr("Adresse"), P().muted, 11);
		u.TextInputAt({r.x + 90, r.y + 30, r.w - 102, 26}, "scribe.linkurl", m_linkUrl, nullptr, "https://…");
		c.Text(r.x + 12, r.y + 70, Tr("ou cible"), P().muted, 11);
		const Outline &outline = m_editor.DocumentOutline();
		std::vector<std::string> labels{Tr("(adresse ci-dessus)")};
		for (const OutlineEntry &e : outline.entries)
			labels.push_back((e.label.empty() ? "" : e.label + "  ") + e.text);
		const std::vector<std::string_view> views(labels.begin(), labels.end());
		m_linkTarget = std::clamp(m_linkTarget, 0, int(views.size()) - 1);
		u.Choice({r.x + 90, r.y + 66, r.w - 102, 26}, "scribe.linktarget", m_linkTarget, views);
		const float bw = (r.w - 36) / 3;
		bool close = false;
		const bool ready = m_linkTarget > 0 || !m_linkUrl.empty();
		if (u.PrimaryButton({r.x + 12, r.y + r.h - 40, bw, 28}, Tr("Appliquer"), ready)) {
			Report(m_editor.SetLink(m_linkTarget > 0 ? InternalLink(outline.entries[size_t(m_linkTarget - 1)].node) : m_linkUrl));
			close = true;
		}
		if (u.ButtonAt({r.x + 18 + bw, r.y + r.h - 40, bw, 28}, Tr("Retirer"))) {
			Report(m_editor.RemoveLink());
			close = true;
		}
		if (u.ButtonAt({r.x + 24 + 2 * bw, r.y + r.h - 40, bw, 28}, Tr("Annuler")))
			close = true;
		if (u.KeycodePressed(SDLK_ESCAPE) || (!m_popupFresh && u.MousePressed() && !r.Contains(u.MouseX(), u.MouseY()) && !Ui().MenuOpen()))
			close = true;
		m_popupFresh = false;
		if (close)
			m_linkOpen = false;
	}

	void OpenShapesPopup(const ui::Rect &anchor) {
		m_shapesOpen = !m_shapesOpen;
		const float cols = 6, cell = 52;
		const float rows = std::ceil(float(ShapeCatalog().size()) / cols);
		m_shapesRect = {anchor.x, anchor.y + anchor.h + 40, cols * cell + 20, rows * cell + 44};
		m_popupFresh = true;
	}

	/// Galerie des formes : aperçu de chaque forme (contour du moteur), nom en info-bulle.
	void ShapesPopup(ui::Context &u) {
		if (!m_shapesOpen)
			return;
		sdl3::Canvas &c = u.Canvas();
		const ui::Rect r = m_shapesRect;
		c.FillRect({r.x + 3, r.y + 4, r.w, r.h}, Rgba(0, 0, 0, 90));
		c.FillRoundRect(r, 8.f, P().popup);
		c.StrokeRoundRect(r, 8.f, P().popupBorder, 1.f);
		const NodeId drawing = m_editor.DrawingOfSelection();
		c.Text(r.x + 12, r.y + 9, drawing != kNoNode ? Tr("Formes (dans la zone de dessin)") : Tr("Formes"), P().strong, 13);
		bool close = false;
		size_t i = 0;
		for (const ShapeInfo &shape : ShapeCatalog()) {
			const ui::Rect cell{r.x + 10 + float(i % 6) * 52, r.y + 34 + float(i / 6) * 52, 48, 48};
			++i;
			const bool hovered = u.MouseOver(cell);
			if (hovered) {
				c.FillRoundRect(cell, 6.f, P().hover);
				u.Tooltip(Tr(shape.name));
			}
			DrawShapePreview(c, {cell.x + 9, cell.y + 11, cell.w - 18, cell.h - 22}, shape.kind, sdl3::WithAlpha(P().ui.accent, 70), P().ui.accent);
			if (hovered && u.MousePressed()) {
				Report(m_editor.InsertShape(shape.kind));
				close = true;
			}
		}
		if (u.KeycodePressed(SDLK_ESCAPE) || (!m_popupFresh && u.MousePressed() && !r.Contains(u.MouseX(), u.MouseY()) && !Ui().MenuOpen()))
			close = true;
		m_popupFresh = false;
		if (close)
			m_shapesOpen = false;
	}

	// ── Polices ──

	/// Familles proposées : polices standard du PDF puis polices installées.
	const std::vector<std::string_view> &Families() {
		if (m_families.empty()) {
			m_families = m_fontLibrary ? m_fontLibrary->Families() : std::vector<std::string>{"Helvetica", "Times", "Courier"};
			m_familyViews.assign(m_families.begin(), m_families.end());
		}
		return m_familyViews;
	}

	static bool EqualsNoCase(std::string_view a, std::string_view b) {
		return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) { return std::tolower((unsigned char)x) == std::tolower((unsigned char)y); });
	}

	// ── Presse-papiers ──

	void CopyToClipboard() {
		Clip clip = m_editor.Copy();
		if (clip.Empty())
			return;
		m_clip = std::move(clip);
		(void)sdl3::clipboard::SetText(::String(m_clip.plain));
	}

	void CutToClipboard() {
		Clip clip;
		if (auto s = m_editor.Cut(clip); !s) {
			Report(s);
			return;
		}
		m_clip = std::move(clip);
		(void)sdl3::clipboard::SetText(::String(m_clip.plain));
	}

	/// Colle le contenu interne (texte enrichi, nœuds) s'il correspond encore au presse-papiers
	/// du système, sinon le texte du système (copié depuis une autre application).
	void PasteFromClipboard() {
		std::string system;
		if (sdl3::clipboard::HasText())
			system = std::string(sdl3::clipboard::GetText());
		std::erase(system, '\r');
		if (!m_clip.Empty() && (system.empty() || system == m_clip.plain)) {
			Report(m_editor.Paste(m_clip));
		} else if (!system.empty()) {
			Clip clip;
			clip.plain = std::move(system);
			Report(m_editor.Paste(clip));
		}
		EnsureCaretVisible();
	}

	void Properties(ui::Context &u, const sdl3::FRect &area) {
		sdl3::Canvas &c = u.Canvas();
		c.FillRect(area, P().panel);
		const Selection &sel = m_editor.Selection();
		const doc::Document &d = m_editor.Document();
		const Node *node = d.Find(sel.Primary());
		if (!node) {
			c.TextCentered(area, Tr("Aucune sélection"), P().faint, 13);
			return;
		}
		u.BeginScroll("properties", area);
		u.Heading(std::format("{} #{}{}", NodeTypeName(node->Type()), node->Id(), sel.Size() > 1 ? TrF("  (+{} autres)", sel.Size() - 1) : std::string()));
		const bool carrier = node->Type() == NodeType::Group || node->Type() == NodeType::Document;
		for (PropertyCategory cat : {PropertyCategory::Functional, PropertyCategory::Structural, PropertyCategory::Visual}) {
			u.Section(cat == PropertyCategory::Visual ? I(Icon::FORMAT_COLOR_FILL) : cat == PropertyCategory::Structural ? I(Icon::GRID_ON) : I(Icon::TUNE),
					  Tr(CategoryLabel(cat)));
			for (const PropertyInfo &info : AllProperties())
				// Propriétés qui ciblent ce type (une marge de page héritable n'a rien à faire sur un texte).
				if (info.category == cat && ((info.targets & MaskOf(node->Type())) != 0 || carrier) && info.id != PropertyId::PageGroup)
					PropertyRow(u, d, *node, info, PanelValue(d, *node, info.id));
		}
		u.EndScroll();
	}

	struct PanelEntry {
		Resolved resolved;
		bool mixed = false;
	};

	/// Valeur affichée d'une propriété : résolue pour le nœud principal, « mixte » si un autre
	/// nœud sélectionné diffère. Mémorisée jusqu'au prochain changement du document ou de la
	/// sélection (une sélection de milliers de nœuds ne se recalcule pas à chaque image).
	const PanelEntry &PanelValue(const doc::Document &d, const Node &node, PropertyId id) {
		const auto key = std::tuple{d.Stamp(), d.Revision(), m_editor.Selection().Revision()};
		if (key != m_panelKey) {
			m_panelKey = key;
			m_panel.clear();
		}
		if (auto it = m_panel.find(id); it != m_panel.end())
			return it->second;
		PanelEntry e{d.Resolve(node.Id(), id), false};
		for (NodeId other : m_editor.Selection().Nodes())
			if (other != node.Id() && d.Contains(other) && AppliesTo(id, d.Find(other)->Type()) && !(d.Resolve(other, id).value == e.resolved.value)) {
				e.mixed = true;
				break;
			}
		return m_panel.emplace(id, std::move(e)).first->second;
	}

	/// Ligne du panneau : nom, source de la valeur, contrôle, retour à l'héritage.
	void PropertyRow(ui::Context &u, const doc::Document &d, const Node &node, const PropertyInfo &info, const PanelEntry &entry) {
		const Resolved r = entry.resolved; // copie : une commande ci-dessous peut vider le cache du panneau
		const bool mixed = entry.mixed;
		std::string source;
		switch (r.source) {
		case ValueSource::Explicit: source = Tr("explicite"); break;
		case ValueSource::Group: source = TrF("groupe « {} »", d.Get(r.origin, props::Name)); break;
		case ValueSource::Parent: source = TrF("hérité de {} #{}", NodeTypeName(d.Find(r.origin)->Type()), r.origin); break;
		case ValueSource::Document: source = Tr("document"); break;
		case ValueSource::Default: source = Tr("défaut"); break;
		}
		if (mixed)
			source += Tr(" · mixte");
		const ui::Rect row = u.Next(kRowHeight * 2);
		sdl3::Canvas &c = u.Canvas();
		const float labelW = row.w * 0.42f;
		c.Text(row.x + 4, row.y + 2, info.name, r.source == ValueSource::Explicit ? P().strong : P().muted, 12);
		c.Text(row.x + 4, row.y + 22, source, P().faint, 10);
		const ui::Rect ctrl{row.x + labelW, row.y + 6, row.w - labelW - 34, kRowHeight + 2};
		const std::string id = std::format("prop{}", int(info.id));
		u.PushId(id);
		switch (info.type) {
		case PropertyType::Bool: {
			bool v = std::get<bool>(r.value);
			if (u.Toggle({ctrl.x, ctrl.y + 2, 44, 22}, "toggle", v))
				Report(m_editor.SetSelected(info.id, v));
			break;
		}
		case PropertyType::Int: {
			if (const TrList enumLabels(EnumLabels(info.id)); !enumLabels.views.empty()) {
				std::vector<std::string_view> labels = enumLabels.views;
				const bool placeable = node.Type() == NodeType::Shape || node.Type() == NodeType::Image || node.Type() == NodeType::Drawing;
				if (info.id == PropertyId::HAlign && !placeable)
					labels.resize(size_t(HorizontalAlign::None)); // « aucun » : formes, images et zones de dessin
				int v = int(std::get<int64_t>(r.value));
				if (v < 0 || v >= int(labels.size()))
					v = 0; // « aucun » sur un paragraphe : comme « gauche »
				if (u.Choice(ctrl, "choice", v, labels)) {
					// Changement de repère : les coordonnées sont converties (le nœud ne bouge pas).
					if (info.id == PropertyId::PositionReference || info.id == PropertyId::RotationOriginReference) {
						const std::vector<NodeId> nodes(m_editor.Selection().Nodes().begin(), m_editor.Selection().Nodes().end());
						for (NodeId target : nodes)
							if (d.Contains(target) && AppliesTo(info.id, d.Find(target)->Type()))
								Report(info.id == PropertyId::PositionReference ? m_editor.SetPositionBase(target, PositionBase(v))
																				: m_editor.SetOriginBase(target, OriginBase(v)));
					} else if (info.id == PropertyId::HAlign && placeable) { // replace la forme selon l'alignement
						const std::vector<NodeId> nodes(m_editor.Selection().Nodes().begin(), m_editor.Selection().Nodes().end());
						Report(m_editor.AlignShapes(nodes, HorizontalAlign(v)));
					} else {
						Report(m_editor.SetSelected(info.id, int64_t(v)));
					}
				}
				break;
			}
			NumberField(u, ctrl, info, std::format("{}", std::get<int64_t>(r.value)));
			break;
		}
		case PropertyType::Real: NumberField(u, ctrl, info, std::format("{:g}", std::get<double>(r.value))); break;
		case PropertyType::Rgba: {
			const Color v = std::get<Color>(r.value);
			const ui::Rect swatch{ctrl.x, ctrl.y + 3, 20, 20};
			c.FillRect(swatch, Rgba(255, 255, 255));
			c.FillRect(swatch, v);
			c.StrokeRect(swatch, u.MouseOver(swatch) ? P().ui.accent : P().border, 1.f);
			if (u.MouseOver(swatch)) { // couleur RGBA ou nommée : fenêtre des couleurs
				u.Tooltip(Tr("Choisir une couleur (RGBA ou couleur nommée)…"));
				if (u.MousePressed())
					OpenColorPopup(ColorTarget::Property, swatch, v, info.id);
			}
			// Nom de la table de sdl3 (« LIGHT_YELLOW ») affiché s'il existe, sinon le code ; les deux
			// sont acceptés à la saisie.
			const std::optional<std::string_view> name = v.Name();
			TextField(u, {ctrl.x + 26, ctrl.y, ctrl.w - 26, ctrl.h}, info, name ? std::string(*name) : Hex(v), [this, &info](const std::string &s) {
				if (auto parsed = ParseHex(s))
					Report(m_editor.SetSelected(info.id, *parsed));
				else if (auto named = Color::FromName(s))
					Report(m_editor.SetSelected(info.id, *named));
				else
					Ui().Notify(Tr("couleur attendue : #rrggbb, #rrggbbaa ou un nom (ex. LIGHT_YELLOW)"), Ui().Style().warning);
			});
			break;
		}
		case PropertyType::Text: {
			const std::string current = std::get<std::string>(r.value);
			if (info.id == PropertyId::Text && node.Type() == NodeType::Text) { // frappe en direct, fusionnée dans l'historique
				std::string &buffer = m_buffers[id];
				if (!u.HasFocus("text"))
					buffer = current;
				if (u.TextInputAt(ctrl, "text", buffer) && buffer != current)
					Report(m_editor.EditText(node.Id(), buffer));
			} else {
				TextField(u, ctrl, info, current, [this, &info](const std::string &s) { Report(m_editor.SetSelected(info.id, s)); });
			}
			break;
		}
		}
		if (r.source == ValueSource::Explicit &&
			u.IconButton({row.x + row.w - 30, row.y + 6, 26, 26}, I(Icon::UNDO), Tr("Retour à la valeur héritée")))
			Report(m_editor.ClearSelected(info.id));
		u.PopId();
	}

	void NumberField(ui::Context &u, const ui::Rect &r, const PropertyInfo &info, const std::string &current) {
		TextField(u, r, info, current, [this, &info](const std::string &s) {
			char *end = nullptr;
			const double v = std::strtod(s.c_str(), &end);
			if (end == s.c_str()) {
				Ui().Notify(Tr("nombre attendu"), Ui().Style().warning);
				return;
			}
			PropertyValue value = info.type == PropertyType::Int ? PropertyValue(int64_t(std::llround(v))) : PropertyValue(v);
			if (info.id == PropertyId::PositionX) { // position saisie : placement manuel (alignement « aucun »)
				m_editor.History().BeginTransaction(Tr("Position"));
				std::vector<NodeId> aligned;
				for (NodeId id : m_editor.Selection().Nodes())
					if (m_editor.Document().Contains(id) && AppliesTo(PropertyId::PositionX, m_editor.Document().Find(id)->Type()) &&
						m_editor.Document().Get(id, props::HAlign) != HorizontalAlign::None)
						aligned.push_back(id);
				Status status = aligned.empty() ? Success() : m_editor.SetNodes(aligned, PropertyId::HAlign, int64_t(HorizontalAlign::None));
				if (status)
					status = m_editor.SetSelected(info.id, value);
				if (!status) {
					(void)m_editor.History().Rollback();
					Report(status);
				} else
					Report(m_editor.History().Commit());
				return;
			}
			Report(m_editor.SetSelected(info.id, value));
		});
	}

	/// Champ validé par Entrée (une seule commande par saisie).
	void TextField(ui::Context &u, const ui::Rect &r, const PropertyInfo &info, const std::string &current,
				   const std::function<void(const std::string &)> &commit) {
		const std::string key = std::format("field{}", int(info.id));
		std::string &buffer = m_buffers[key];
		if (!u.HasFocus(key))
			buffer = current;
		bool submitted = false;
		u.TextInputAt(r, key, buffer, &submitted);
		if (submitted && buffer != current)
			commit(buffer);
	}

	// ── Plan : titres, sous-titres et légendes ──

	/// Position (page, ordonnée) du début de chaque paragraphe dans la mise en page affichée.
	const std::map<NodeId, std::pair<size_t, double>> &ParagraphPositions() {
		const Presentation *p = m_editor.Current();
		const auto key = p ? std::pair{p->layout.stamp, p->layout.revision} : std::pair<uint64_t, uint64_t>{0, 0};
		if (key != m_positionsKey) {
			m_positionsKey = key;
			m_positions.clear();
			if (p)
				for (size_t i = 0; i < p->layout.pages.size(); ++i)
					for (const PlacedLine &l : p->layout.pages[i].lines)
						m_positions.try_emplace(l.paragraph, i, l.rect.y);
		}
		return m_positions;
	}

	void OutlinePanel(ui::Context &u, const sdl3::FRect &area) {
		sdl3::Canvas &c = u.Canvas();
		c.FillRect(area, P().panel);
		const Outline &outline = m_editor.DocumentOutline();
		// Filtres.
		const struct {
			bool *flag;
			Icon icon;
			const char *tip;
		} filters[] = {{&m_navFilter.headings, Icon::TITLE, "Titres"},
					   {&m_navFilter.figures, Icon::IMAGE, "Légendes des figures"},
					   {&m_navFilter.tables, Icon::TABLE_CHART, "Légendes des tableaux"},
					   {&m_navFilter.listings, Icon::CODE, "Légendes des listings"}};
		float x = area.x + 6;
		for (const auto &f : filters) {
			const ui::Rect r{x, area.y + 4, 28, 26};
			if (*f.flag)
				c.FillRoundRect(r, 5.f, sdl3::WithAlpha(P().selection, 70));
			if (u.IconButton(r, I(f.icon), Tr(f.tip), true, *f.flag ? P().accentSoft : sdl3::Color{0, 0, 0, 0}))
				*f.flag = !*f.flag;
			x += 32;
		}
		c.Text(x + 6, area.y + 9, TrF("{} entrées", outline.entries.size()), P().faint, 11);
		const ui::Rect list{area.x, area.y + 34, area.w, area.h - 34};
		const std::vector<NavigatorRow> rows = NavigatorRows(outline, m_navFilter, m_navCollapsed);
		if (rows.empty()) {
			c.TextCentered(list, Tr("Aucun titre ni légende : appliquez un style « Titre 1 »…"), P().faint, 12);
			return;
		}
		// Entrée courante : la dernière qui commence avant le curseur (ou la sélection).
		const auto &positions = ParagraphPositions();
		std::pair<size_t, double> here{SIZE_MAX, 0};
		if (m_editor.Editing()) {
			if (const auto caret = m_editor.CaretGeometry(m_editor.Caret()))
				here = {caret->page, caret->rect.y};
		} else if (const auto it = positions.find(m_editor.Selection().Primary()); it != positions.end()) {
			here = it->second;
		}
		const OutlineEntry *current = nullptr;
		for (const OutlineEntry &e : outline.entries)
			if (const auto it = positions.find(e.node); it != positions.end() && here.first != SIZE_MAX && it->second <= here)
				current = &e;
		u.BeginScroll("outline", list);
		for (const NavigatorRow &row : rows) {
			const OutlineEntry &e = *row.entry;
			const ui::Rect r = u.Next(24);
			const float indent = 8.f + 14.f * float(row.depth);
			const bool hovered = u.MouseOver(r);
			if (&e == current)
				c.FillRect(r, sdl3::WithAlpha(P().selection, 55));
			else if (hovered)
				c.FillRect(r, P().hover);
			if (row.hasChildren) {
				const ui::Rect arrow{r.x + indent - 2, r.y + 2, 18, 20};
				c.Icon(arrow, I(row.collapsed ? Icon::CHEVRON_RIGHT : Icon::EXPAND_MORE), P().muted, 16.f);
				if (u.MouseOver(arrow) && u.MousePressed()) {
					if (row.collapsed)
						m_navCollapsed.erase(e.node);
					else
						m_navCollapsed.insert(e.node);
					continue;
				}
			}
			const char32_t icon = e.kind == OutlineKind::Heading ? I(Icon::SHORT_TEXT)
								  : e.kind == OutlineKind::Figure ? I(Icon::IMAGE)
								  : e.kind == OutlineKind::Table  ? I(Icon::TABLE_CHART)
																  : I(Icon::CODE);
			c.Icon({r.x + indent + 16, r.y + 3, 18, 18}, icon, e.kind == OutlineKind::Heading ? P().muted : P().caption, 14.f);
			std::string label = e.label.empty() ? e.text : e.label + "  " + e.text;
			for (char &ch : label) // espaces insécables : espaces simples à l'affichage du panneau
				if (ch == '\n')
					ch = ' ';
			const float textX = r.x + indent + 38;
			const sdl3::Color color = e.kind == OutlineKind::Heading && e.level == 1 ? P().strong : P().text;
			c.Text(textX, r.y + 4, c.Ellipsize(label, r.x + r.w - textX - 6, 12), color, 12);
			if (hovered && u.MousePressed() && u.MouseX() > r.x + indent + 14) {
				m_editor.GoTo(e.node);
				ScrollTo(e.node);
			}
		}
		u.EndScroll();
	}

	void HistoryPanel(ui::Context &u, const sdl3::FRect &area) {
		u.Canvas().FillRect(area, P().panel);
		doc::History &h = m_editor.History();
		u.BeginScroll("history", area);
		const auto entries = h.Entries();
		const auto entry = [&](size_t position, std::string_view label) {
			const bool applied = position <= h.Position();
			const bool current = position == h.Position();
			const ui::Rect r = u.Next(kRowHeight);
			const ui::Id id = u.MakeId(std::format("h{}", position));
			const auto it = u.Interact(id, r);
			if (current)
				u.Canvas().FillRect(r, sdl3::WithAlpha(P().selection, 70));
			else if (it.hovered)
				u.Canvas().FillRect(r, P().hover);
			u.Canvas().Text(r.x + 8, r.y + 4, label, applied ? P().strong : P().faint, 12);
			if (it.leftClicked && !current)
				Report(m_editor.UndoTo(position));
		};
		entry(0, Tr("État initial"));
		for (size_t i = 0; i < entries.size(); ++i)
			entry(i + 1, entries[i].name);
		u.EndScroll();
	}

	void TreePanel(ui::Context &u, const sdl3::FRect &area) {
		u.Canvas().FillRect(area, P().panel);
		const doc::Document &d = m_editor.Document();
		const ui::Rect bar{area.x + 6, area.y + 6, area.w - 12, 28};
		static const std::string_view kModes[] = {"Hiérarchie", "Groupes"};
		u.Choice(bar, "treemode", m_treeMode, TrList(kModes).views);
		u.BeginScroll("tree", {area.x, area.y + 40, area.w, area.h - 40});
		if (m_treeMode == 0) {
			TreeRow(u, d, d.Root(), 0);
		} else {
			for (NodeId g : d.AllGroups()) {
				TreeRow(u, d, g, 0, false);
				if (m_expanded.contains(g))
					for (NodeId member : d.Find(g)->Members())
						TreeRow(u, d, member, 1, false);
			}
		}
		u.EndScroll();
	}

	std::string NodeLabel(const doc::Document &d, const Node &n) {
		std::string label(NodeTypeName(n.Type()));
		const std::string name = d.Get(n.Id(), props::Name);
		if (!name.empty())
			label += " « " + name + " »";
		else if (n.Type() == NodeType::Text) {
			std::string text = d.Get(n.Id(), props::Text);
			if (text.size() > 40)
				text = text.substr(0, 40) + "…";
			label += " « " + text + " »";
		}
		return label + std::format("  #{}", n.Id());
	}

	void TreeRow(ui::Context &u, const doc::Document &d, NodeId id, int depth, bool recursive = true) {
		const Node *n = d.Find(id);
		if (!n)
			return;
		const ui::Rect r = u.Next(kRowHeight);
		const bool selected = m_editor.Selection().Contains(id);
		const bool expandable = recursive ? !n->Children().empty() : (n->Type() == NodeType::Group && !n->Members().empty());
		const bool open = m_expanded.contains(id) || (recursive && depth < 2 && !m_collapsed.contains(id));
		const float x = r.x + 6 + float(depth) * 16.f;
		const auto it = u.Interact(u.MakeId(std::format("t{}", id)), r);
		if (selected)
			u.Canvas().FillRect(r, sdl3::WithAlpha(P().selection, 70));
		else if (it.hovered)
			u.Canvas().FillRect(r, P().hover);
		if (expandable)
			u.Icon({x, r.y + 2, 18, 20}, open ? I(Icon::KEYBOARD_ARROW_DOWN) : I(Icon::KEYBOARD_ARROW_RIGHT), P().muted, 16.f);
		u.Icon({x + 18, r.y + 2, 18, 20}, TypeIcon(n->Type()), P().accentSoft, 16.f);
		const bool hidden = AppliesTo(PropertyId::Visible, n->Type()) && !d.Get(id, props::Visible);
		u.Canvas().Text(x + 40, r.y + 4, NodeLabel(d, *n), hidden ? P().faint : P().text, 12);
		if (it.leftClicked) {
			if (expandable && u.MouseX() < x + 18) { // flèche : déplier / replier
				if (open) {
					m_expanded.erase(id);
					m_collapsed.insert(id);
				} else {
					m_expanded.insert(id);
					m_collapsed.erase(id);
				}
			} else {
				m_editor.SelectNode(id, u.Shift());
				SelectionMadeByUser();
			}
		}
		if (it.doubleClicked)
			ScrollTo(id);
		if (recursive && expandable && open)
			for (NodeId c : n->Children())
				TreeRow(u, d, c, depth + 1);
	}

	// ── Actions ──

	void Report(const Status &s) {
		if (!s && s.Error().code != ErrorCode::Nothing)
			Ui().Notify(s.Error().message, Ui().Style().warning);
	}

	void SelectionMadeByUser() {
		if (m_contextual)
			m_ribbonTab = int(SuggestTab(m_editor.Context(), RibbonTab(m_ribbonTab)));
	}

	void Zoom(float factor) {
		m_fitWidth = false;
		m_zoom = std::clamp(m_zoom * factor, 0.2f, 6.f);
	}

	/// Fait défiler la vue pour montrer le nœud près du haut (début de son texte ou de sa boîte).
	void ScrollTo(NodeId id) {
		const Presentation *p = m_editor.Current();
		size_t page = 0;
		double top = 0;
		if (!p || !p->layout.Locate(id, page, top))
			return;
		float y = 0; // haut de la page dans le contenu défilant
		for (size_t i = 0; i < page && i < m_views.pages.size(); ++i)
			y += m_views.pages[i].rect.h + kPageGap;
		const float scale = m_views.pages.empty() ? 1.f : m_views.pages.front().scale;
		m_scrollY = std::max(0.f, y + float(top) * scale - 40.f);
	}

	NodeId FirstTextTarget() const {
		const auto texts = m_editor.TextTargets();
		if (!texts.empty())
			return texts.front();
		const auto paragraphs = m_editor.ParagraphTargets();
		return paragraphs.empty() ? kNoNode : paragraphs.front();
	}

	/// Section et page visées par l'onglet Mise en page.
	std::pair<NodeId, NodeId> PageTarget() const {
		const doc::Document &d = m_editor.Document();
		const NodeId primary = m_editor.Selection().Primary();
		const Node *n = d.Find(primary);
		NodeId page = n && n->Type() == NodeType::Page ? primary : kNoNode;
		NodeId section = n ? (n->Type() == NodeType::Section ? primary : d.AncestorOfType(primary, NodeType::Section)) : kNoNode;
		if (section == kNoNode)
			section = d.Sections().front();
		return {section, page};
	}

	Status Margins(double value) {
		m_editor.History().BeginTransaction(Tr("Marges"));
		for (PropertyId p : {PropertyId::MarginTop, PropertyId::MarginBottom, PropertyId::MarginLeft, PropertyId::MarginRight})
			if (auto s = m_editor.SetPage(p, value); !s) {
				(void)m_editor.History().Rollback();
				return s;
			}
		return m_editor.History().Commit();
	}

	void NewDocument() {
		FlushDraft();
		m_editor.New();
		m_scrollY = 0;
		m_images.Clear();
	}

	void SaveCurrent() {
		if (m_editor.Path().empty())
			ShowSave(Pending::SaveAs, {{::String(Tr("Documents Scribe")), "docdb"}});
		else
			m_editor.SaveAsync(m_editor.Path());
	}

	void ShowOpen(Pending what, std::vector<sdl3::DialogFilter> filters) {
		sdl3::dialog::ShowOpenFile(
			[this, what](const sdl3::DialogResult &result, int) {
				if (!result.Ok())
					return;
				std::scoped_lock lock(m_dialogMutex);
				m_pending.emplace_back(what, std::string(result.First().CStr()));
			},
			Window(), filters);
	}

	void ShowSave(Pending what, std::vector<sdl3::DialogFilter> filters) {
		sdl3::dialog::ShowSaveFile(
			[this, what](const sdl3::DialogResult &result, int) {
				if (!result.Ok())
					return;
				std::scoped_lock lock(m_dialogMutex);
				m_pending.emplace_back(what, std::string(result.First().CStr()));
			},
			Window(), filters);
	}

	static std::filesystem::path WithExtension(std::filesystem::path p, std::string_view ext) {
		if (p.extension() != ext)
			p += ext;
		return p;
	}

	void ProcessDialogs() {
		std::vector<std::pair<Pending, std::string>> pending;
		{
			std::scoped_lock lock(m_dialogMutex); // rappels des boîtes de dialogue (autre fil)
			pending.swap(m_pending);
		}
		for (auto &[what, path] : pending) {
			switch (what) {
			case Pending::Open:
				FlushDraft();
				m_editor.OpenAsync(path);
				m_scrollY = 0;
				if (m_home) // ouvert depuis l'accueil : l'éditeur
					ShowHome(false);
				break;
			case Pending::SaveAs: m_editor.SaveAsync(WithExtension(path, ".docdb")); break;
			case Pending::ExportPdf: m_editor.ExportAsync(ExportKind::Pdf, WithExtension(path, ".pdf")); break;
			case Pending::ExportHtml: m_editor.ExportAsync(ExportKind::Html, WithExtension(path, ".html")); break;
			case Pending::ExportCsv: m_editor.ExportAsync(ExportKind::Csv, WithExtension(path, ".csv")); break;
			case Pending::InsertImage: Report(m_editor.InsertImageFile(path)); break;
			}
		}
	}

	void Shortcuts() {
		if (Ui().WantsKeyboard() || Ui().MenuOpen() || m_colorTarget != ColorTarget::None || m_linkOpen || m_shapesOpen || m_prompt.kind != Prompt::None || m_home)
			return;
		const bool ctrl = Events().Ctrl(), shift = Events().Shift();
		const auto key = [&](SDL_Keycode k) { return Events().KeycodePressed(k); };
		if (ctrl && key(SDLK_N))
			NewDocument();
		else if (ctrl && key(SDLK_O))
			ShowOpen(Pending::Open, {{::String(Tr("Documents Scribe")), "docdb"}});
		else if (ctrl && shift && key(SDLK_S))
			ShowSave(Pending::SaveAs, {{::String(Tr("Documents Scribe")), "docdb"}});
		else if (ctrl && key(SDLK_S))
			SaveCurrent();
		else if (ctrl && (key(SDLK_Y) || (shift && key(SDLK_Z))))
			Report(m_editor.Redo());
		else if (ctrl && key(SDLK_Z))
			Report(m_editor.Undo());
		else if (ctrl && key(SDLK_C))
			CopyToClipboard();
		else if (ctrl && key(SDLK_X))
			CutToClipboard();
		else if (ctrl && key(SDLK_V))
			PasteFromClipboard();
		else if (ctrl && key(SDLK_B))
			Report(m_editor.ToggleText(PropertyId::Bold));
		else if (ctrl && key(SDLK_I))
			Report(m_editor.ToggleText(PropertyId::Italic));
		else if (ctrl && key(SDLK_U))
			Report(m_editor.ToggleText(PropertyId::Underline));
		else if (ctrl && key(SDLK_K) && (m_editor.Editing() || !m_editor.Selection().Empty()))
			OpenLinkPopup(kShortcutAnchor);
		else if (m_editor.Editing())
			TextKeys(ctrl, shift);
		else if (ctrl && key(SDLK_A))
			m_editor.SelectAll();
		else if (ctrl && key(SDLK_D))
			Report(m_editor.DuplicateSelection());
		else if (ctrl && key(SDLK_UP))
			Report(m_editor.ArrangeSelection(shift ? Arrange::Front : Arrange::Forward));
		else if (ctrl && key(SDLK_DOWN))
			Report(m_editor.ArrangeSelection(shift ? Arrange::Back : Arrange::Backward));
		else if (ctrl && shift && key(SDLK_G))
			Report(m_editor.UngroupSelection());
		else if (ctrl && key(SDLK_G))
			Report(m_editor.GroupSelection());
		else if (key(SDLK_DELETE) || key(SDLK_BACKSPACE))
			Report(m_editor.DeleteSelection());
		else if (key(SDLK_ESCAPE))
			m_editor.ClearSelection();
		else if (key(SDLK_RETURN) || key(SDLK_KP_ENTER))
			Report(m_editor.InsertParagraph());
	}

	/// Clavier pendant l'édition du texte : saisie, effacement, déplacements (Maj : étend la
	/// plage, Ctrl : par mot / début et fin du récit), Échap : sélection du paragraphe.
	/// Tab : cellule suivante dans un tableau (nouvelle ligne après la dernière), niveau de liste
	/// suivant dans une liste, sinon tabulation. Maj+Tab : sens inverse.
	void TabKey(bool shift) {
		const TableSelection t = m_editor.CurrentTable();
		if (t.Valid() && !m_editor.HasTextRange()) {
			const TableGrid g = TableGrid::Of(m_editor.Document(), t.table);
			size_t r = t.row0, col = t.column0;
			if (shift) {
				if (col > 0)
					--col;
				else if (r > 0)
					--r, col = g.columns - 1;
				else
					return;
			} else if (col + 1 < g.cells[r].size()) {
				++col;
			} else if (r + 1 < g.rows.size()) {
				++r, col = 0;
			} else { // dernière cellule : nouvelle ligne
				Report(m_editor.InsertRowAt(t.table, g.rows.size()));
				return;
			}
			m_editor.GoTo(g.Cell(r, col) != kNoNode ? m_editor.Document().Find(g.Cell(r, col))->Children().front() : t.table);
			EnsureCaretVisible();
			return;
		}
		const std::vector<NodeId> &paragraphs = m_editor.ParagraphTargets();
		if (!paragraphs.empty() && m_editor.Document().Get(paragraphs.front(), props::ListStyle) != ListStyle::None) {
			Report(m_editor.ChangeIndent(shift ? -1 : 1));
			return;
		}
		Report(m_editor.TypeText("\t"));
	}

	void TextKeys(bool ctrl, bool shift) {
		const auto key = [&](SDL_Keycode k) { return Events().KeycodePressed(k); };
		if (const std::string_view typed = Events().TextInput(); !typed.empty() && !ctrl)
			Report(m_editor.TypeText(typed));
		bool moved = true;
		if (ctrl && key(SDLK_A))
			m_editor.SelectStory();
		else if (key(SDLK_LEFT))
			m_editor.MoveCaret(ctrl ? Motion::WordLeft : Motion::Left, shift);
		else if (key(SDLK_RIGHT))
			m_editor.MoveCaret(ctrl ? Motion::WordRight : Motion::Right, shift);
		else if (key(SDLK_UP))
			m_editor.MoveCaret(Motion::Up, shift);
		else if (key(SDLK_DOWN))
			m_editor.MoveCaret(Motion::Down, shift);
		else if (key(SDLK_HOME))
			m_editor.MoveCaret(ctrl ? Motion::StoryStart : Motion::LineStart, shift);
		else if (key(SDLK_END))
			m_editor.MoveCaret(ctrl ? Motion::StoryEnd : Motion::LineEnd, shift);
		else if (key(SDLK_BACKSPACE))
			Report(m_editor.Backspace(ctrl));
		else if (key(SDLK_DELETE))
			Report(m_editor.DeleteForward(ctrl));
		else if (key(SDLK_RETURN) || key(SDLK_KP_ENTER)) // Entrée : saut de ligne ; Maj : paragraphe ; Ctrl : page
			Report(ctrl ? m_editor.PageBreakBelow() : shift ? m_editor.Enter() : m_editor.LineBreak());
		else if (key(SDLK_TAB))
			TabKey(shift);
		else if (key(SDLK_ESCAPE)) { // quitte le texte : le paragraphe devient la sélection (copier, glisser…)
			const NodeId paragraph = m_editor.Document().AncestorOfType(m_editor.Caret().text, NodeType::Paragraph);
			m_editor.LeaveText();
			if (paragraph != kNoNode)
				m_editor.SelectNode(paragraph);
			moved = false;
		} else
			moved = !Events().TextInput().empty();
		if (moved)
			EnsureCaretVisible();
	}

	std::string StatusText() const {
		const Presentation *p = m_editor.Current();
		const size_t pages = p ? p->layout.pages.size() : 0;
		size_t current = pages ? 1 : 0;
		for (const PageView &v : m_views.pages) // première page encore visible sous le haut de la vue
			if (v.rect.y + v.rect.h > m_viewTop + 120) {
				current = v.index + 1;
				break;
			}
		const SelectionContext ctx = m_editor.Context();
		return TrF("Page {} / {} · {} nœud(s) sélectionné(s){} · {}{}", current, pages, ctx.count, ctx.locked ? Tr(" (verrouillé)") : std::string(),
				   m_editor.Dirty() ? Tr("modifié") : Tr("enregistré"), m_editor.Busy() ? Tr(" · mise en page…") : std::string());
	}

	LaunchOptions m_options;
	Editor m_editor;
	std::unique_ptr<ui::Ui> m_ui;
	ui::DockLayout m_dock;
	ui::Widget m_status;
	int m_ribbonTab = int(RibbonTab::Home);
	bool m_contextual = true, m_fitWidth = true;
	float m_zoom = 1.f, m_scrollY = 0.f, m_viewTop = 0.f;
	PageLayoutResult m_views;
	ImageCache m_images;
	PropertyCache m_styles;
	FontCache m_fontCache;
	Settings m_settings;
	std::filesystem::path m_settingsPath;
	TemplateStore m_templates{std::filesystem::path()};
	bool m_home = true, m_documentOpen = false, m_rebuild = false;
	HomePage m_homePage = HomePage::New;
	size_t m_language = 0, m_theme = 0;
	DraftStore m_drafts;
	std::optional<DraftInfo> m_draft; ///< brouillon enregistré
	uint64_t m_draftDocument = 0, m_draftState = UINT64_MAX; ///< document et état de l'historique enregistrés dans le brouillon
	std::chrono::steady_clock::time_point m_draftSaved{};
	jobs::AsyncValue<std::string> m_draftJob; ///< enregistrement en cours (message d'erreur, vide si réussi)
	std::string m_lastPath, m_titleCode, m_titleLanguage, m_titleTheme, m_titleHome, m_titleSave;
	Prompt m_prompt;
	NavigatorFilter m_navFilter;
	TableUnder m_tableDrag;
	size_t m_tableAnchor = 0;
	bool m_tableAnchorRows = true; ///< l'ancre est une ligne (sinon une colonne)
	bool m_tableAdd = false;	   ///< glisser sur les poignées : ajoute à la sélection (Ctrl)
	CellRef m_cellStart;
	RulerDrag m_rulerDrag;
	PaddingHandle m_paddingDrag;
	std::set<NodeId> m_navCollapsed;
	std::map<NodeId, std::pair<size_t, double>> m_positions;
	std::pair<uint64_t, uint64_t> m_positionsKey{UINT64_MAX, 0};
	std::shared_ptr<const doc::FontLibrary> m_fontLibrary;
	std::vector<std::string> m_families;
	std::vector<std::string_view> m_familyViews;
	// Souris dans le document.
	DragMode m_drag = DragMode::None;
	PagePoint m_dragFrom;
	float m_dragX = 0, m_dragY = 0, m_clickX = -100, m_clickY = -100, m_viewBottom = 0;
	bool m_dragMoved = false;
	int m_clicks = 0, m_handle = 0;
	std::chrono::steady_clock::time_point m_lastClick{};
	NodeId m_dragNode = kNoNode;
	ShapeFrame m_frame, m_preview;
	bool m_rotateMode = false;
	NodeId m_rotateNode = kNoNode;
	// Curseur de texte et presse-papiers.
	std::pair<doc::TextPosition, doc::TextPosition> m_caretKey;
	std::chrono::steady_clock::time_point m_caretSince{};
	Clip m_clip;
	// Fenêtre des couleurs.
	ColorTarget m_colorTarget = ColorTarget::None;
	PropertyId m_colorProperty = PropertyId::TextColor;
	ui::Rect m_area{}; ///< zone de l'application (dernière image)
	std::string m_colorFilter;
	bool m_linkOpen = false, m_shapesOpen = false;
	ui::Widget m_overlay;			 ///< calque des fenêtres surgissantes (au premier plan)
	std::vector<Binding> m_bindings; ///< état des commandes du ruban
	ui::Rect m_shapesRect;
	std::string m_linkUrl;
	int m_linkTarget = 0;
	ui::Rect m_linkRect;
	Color m_pickColor;
	ui::Rect m_popupRect;
	bool m_popupFresh = false;
	int m_treeMode = 0, m_referenceTarget = 0, m_borderPreset = 3;
	std::string m_tableRowsText = "3", m_tableColsText = "3", m_insertCountText = "1";
	bool m_showRulers = true;
	std::set<NodeId> m_expanded, m_collapsed;
	std::map<std::string, std::string> m_buffers;
	std::map<PropertyId, PanelEntry> m_panel;
	std::tuple<uint64_t, uint64_t, uint64_t> m_panelKey{0, UINT64_MAX, UINT64_MAX};
	std::mutex m_dialogMutex;
	std::vector<std::pair<Pending, std::string>> m_pending;
	std::string m_title;
	std::chrono::steady_clock::time_point m_closeArmed{};
};

} // namespace

int ExportWithoutInterface(const LaunchOptions &options, const std::filesystem::path &output) {
	Editor editor;
	if (!options.templateName.empty()) {
		const TemplateStore mine(DataDirectory() / "templates");
		if (const BuiltinTemplate *builtin = FindBuiltinTemplate(options.templateName))
			builtin->build(editor);
		else if (mine.Contains(options.templateName)) {
			if (auto s = editor.NewFromFile(mine.PathOf(options.templateName)); !s) {
				std::fprintf(stderr, "ouverture impossible : %s\n", s.Error().message.c_str());
				return 1;
			}
		} else {
			std::fprintf(stderr, "modèle introuvable : %s (modèles intégrés : %s)\n", options.templateName.c_str(), BuiltinTemplateKeys().c_str());
			return 1;
		}
	} else if (!options.path.empty())
		if (auto opened = editor.Open(options.path); !opened) {
			std::fprintf(stderr, "ouverture impossible : %s\n", opened.Error().message.c_str());
			return 1;
		}
	editor.SetFonts(doc::FontLibrary::Scan(sdl3::detail::SystemFontDirs()));
	editor.LayoutNow();
	std::string ext = output.extension().string();
	std::ranges::transform(ext, ext.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
	const ExportKind kind = ext == ".html" || ext == ".htm" ? ExportKind::Html : ext == ".csv" ? ExportKind::Csv : ExportKind::Pdf;
	if (kind == ExportKind::Csv)
		if (const auto tables = editor.Document().FindAll(NodeType::Table); !tables.empty())
			editor.SelectNode(tables.front());
	if (auto s = editor.Export(kind, output); !s) {
		std::fprintf(stderr, "export impossible : %s\n", s.Error().message.c_str());
		return 1;
	}
	std::printf("%s : %zu page(s)\n", output.string().c_str(), editor.Current() ? editor.Current()->layout.pages.size() : size_t(0));
	return 0;
}

int RunScribe(app::AppConfig config, const LaunchOptions &options) {
	if (config.title == app::AppConfig{}.title)
		config.title = "Scribe";
	config.width = std::max(config.width, 1440);
	config.height = std::max(config.height, 900);
	config.customFrame = true;
	config.icon = ui::icons::Codepoint(Icon::ARTICLE);
	return ScribeApp(options).Run(config);
}

} // namespace scribe
