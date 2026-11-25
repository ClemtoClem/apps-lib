#pragma once
/**
 * @file editor.hpp
 * @brief Contrôleur de l'éditeur, sans interface graphique : document, historique, sélection,
 *        mise en page et rendu asynchrones, fichiers et exports en arrière-plan, actions du
 *        ruban. L'interface (`app.cpp`) ne fait qu'appeler ces actions et dessiner le résultat ;
 *        toute la logique documentaire reste dans `engine.document`.
 */
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <document/commands.hpp>
#include <document/export.hpp>
#include <document/layout.hpp>
#include <document/render.hpp>
#include <document/fonts.hpp>
#include <document/selection.hpp>
#include <document/outline.hpp>
#include <document/tables.hpp>
#include <document/text.hpp>
#include <jobs/jobs.hpp>

namespace scribe {

/// Résultat d'une mise en page de fond : instantané, pages, listes d'affichage.
struct Presentation {
	doc::Snapshot snapshot;
	doc::LayoutResult layout;
	std::vector<doc::DisplayList> pages;
};

enum class ExportKind : uint8_t { Pdf, Html, Csv };

/// Déplacements du curseur de texte.
enum class Motion : uint8_t { Left, Right, WordLeft, WordRight, Up, Down, LineStart, LineEnd, StoryStart, StoryEnd };

/// Contenu du presse-papiers interne : texte enrichi ou nœuds, avec leur texte brut (celui du
/// presse-papiers du système, pour coller dans une autre application).
struct Clip {
	std::string plain;
	doc::TextFragment text;
	doc::NodeFragment nodes;
	bool isNodes = false;
	[[nodiscard]] bool Empty() const noexcept { return isNodes ? nodes.Empty() : text.paragraphs.empty(); }
};

/// Styles de paragraphe prédéfinis : groupes du document (créés à la première utilisation,
/// modifiables comme tout groupe : tous les paragraphes du style suivent).
enum class ParagraphStyle : uint8_t { Normal, Title, Subtitle, Heading1, Heading2, Heading3, Heading4, Quote, Code, Note, Caption, Count };
[[nodiscard]] std::string_view StyleName(ParagraphStyle style) noexcept;

/// Ordre d'empilement : premier plan, dernier plan, un plan en avant, un plan en arrière.
enum class Arrange : uint8_t { Front, Back, Forward, Backward };

/// Partie d'un tableau visée par la sélection : rectangle englobant et, sélection non contiguë
/// comprise (Ctrl+clic), lignes et colonnes touchées.
/// Sélection de cellules, lignes ou colonnes : remplace (clic, Maj+clic), ajoute (Ctrl+Maj+clic),
/// bascule (Ctrl+clic : ajoute, ou retire si tout est déjà sélectionné).
enum class TableSelect : uint8_t { Replace, Add, Toggle };

struct TableSelection {
	doc::NodeId table = kNoTable;
	size_t row0 = 0, row1 = 0, column0 = 0, column1 = 0; ///< bornes incluses (rectangle englobant)
	size_t rows = 0, columns = 0;						 ///< taille du tableau
	bool wholeRows = false, wholeColumns = false;		 ///< lignes ou colonnes entières sélectionnées
	std::vector<size_t> rowList, columnList;			 ///< lignes / colonnes touchées (triées, sans doublon)
	[[nodiscard]] bool ContiguousRows() const noexcept { return rowList.size() == RowCount(); }
	[[nodiscard]] bool ContiguousColumns() const noexcept { return columnList.size() == ColumnCount(); }
	[[nodiscard]] bool Valid() const noexcept { return table != kNoTable; }
	[[nodiscard]] size_t RowCount() const noexcept { return row1 - row0 + 1; }
	[[nodiscard]] size_t ColumnCount() const noexcept { return column1 - column0 + 1; }
	static constexpr doc::NodeId kNoTable = doc::kNoNode;
};

class Editor {
public:
	explicit Editor(jobs::ThreadPool *pool = nullptr);
	~Editor();
	Editor(const Editor &) = delete;
	Editor &operator=(const Editor &) = delete;

	[[nodiscard]] doc::Document &Document() noexcept { return *m_document; }
	[[nodiscard]] const doc::Document &Document() const noexcept { return *m_document; }
	[[nodiscard]] doc::History &History() noexcept { return *m_history; }
	[[nodiscard]] const doc::Selection &Selection() const noexcept { return m_selection; }

	// ── Fichiers ──
	/// Nouveau document : une section, un paragraphe vide sélectionné.
	void New();
	doc::Status Open(const std::filesystem::path &path);
	/// Nouveau document à partir d'un fichier (modèle personnel) : « Sans titre », sans chemin
	/// (« Enregistrer » demande où), historique vierge, curseur au début du texte.
	doc::Status NewFromFile(const std::filesystem::path &path);
	/// Reprend un brouillon : document lu dans `file`, rattaché à `source` (document d'origine,
	/// vide s'il n'a jamais été enregistré), nommé `name`, marqué modifié jusqu'au prochain
	/// enregistrement (même en annulant), curseur au début du texte.
	doc::Status OpenDraft(const std::filesystem::path &file, const std::filesystem::path &source, std::string name);
	/// Chargement en arrière-plan ; le document est remplacé par `Update` quand il est prêt.
	void OpenAsync(const std::filesystem::path &path);
	doc::Status Save(const std::filesystem::path &path);
	/// Enregistrement d'un instantané en arrière-plan ; l'édition continue.
	void SaveAsync(const std::filesystem::path &path);
	doc::Status Export(ExportKind kind, const std::filesystem::path &path);
	void ExportAsync(ExportKind kind, const std::filesystem::path &path);
	[[nodiscard]] const std::filesystem::path &Path() const noexcept { return m_path; }
	[[nodiscard]] bool Dirty() const noexcept { return m_history->IsDirty(); }
	/// Nom d'un document sans chemin (modèle d'origine…) ; oublié au changement de document.
	void SetName(std::string name) { m_name = std::move(name); }
	/// Nom du document : fichier, sinon nom donné par `SetName`, sinon « Sans titre ».
	[[nodiscard]] std::string Name() const;
	/// Nom affiché : `Name()` et « • » si modifié.
	[[nodiscard]] std::string Title() const;
	/// Numéro du document courant, incrémenté à chaque remplacement (nouveau, ouverture…).
	[[nodiscard]] uint64_t DocumentId() const noexcept { return m_documentId; }

	// ── Mise en page et tâches de fond ──
	void RequestLayout();
	/// Récupère les résultats des tâches de fond, synchronise les pages, relance la mise en page
	/// si le document a changé. Vrai si la présentation a changé.
	bool Update();
	/// Mise en page immédiate (tests, export) ; synchronise les pages.
	void LayoutNow();
	[[nodiscard]] const Presentation *Current() const noexcept { return m_presentation ? &*m_presentation : nullptr; }
	[[nodiscard]] bool Busy() const noexcept;
	/// Notifications à afficher (fichier enregistré, export terminé, erreur…).
	[[nodiscard]] std::vector<std::string> TakeMessages();
	void SetShowBlockBounds(bool on);
	[[nodiscard]] bool ShowBlockBounds() const noexcept { return m_render.blockBounds; }

	// ── Polices ──
	/// Polices installées utilisées par la mise en page et les exports (nullptr : standard).
	void SetFonts(std::shared_ptr<const doc::FontLibrary> fonts);
	/// Inventaire des polices des dossiers en arrière-plan ; appliqué par `Update`.
	void ScanFontsAsync(std::vector<std::filesystem::path> directories);
	[[nodiscard]] const std::shared_ptr<const doc::FontLibrary> &Fonts() const noexcept { return m_fonts; }

	// ── Édition du texte ──
	[[nodiscard]] bool Editing() const noexcept { return m_caret.Valid(); }
	[[nodiscard]] const doc::TextPosition &Caret() const noexcept { return m_caret; }
	[[nodiscard]] const doc::TextPosition &Anchor() const noexcept { return m_anchor; }
	[[nodiscard]] bool HasTextRange() const noexcept { return m_caret.Valid() && !(m_caret == m_anchor); }
	/// Place le curseur (`extend` : étend la plage depuis l'ancre) ; le segment devient la sélection.
	void PlaceCaret(doc::TextPosition position, bool extend = false);
	/// Curseur au point (page, x, y) de la mise en page affichée ; faux s'il n'y a pas de texte.
	bool PlaceCaretAt(size_t page, double x, double y, bool extend = false);
	void SelectWordAt(size_t page, double x, double y);
	void SelectParagraphAt(size_t page, double x, double y);
	/// Tout le texte du récit du curseur (Ctrl+A en édition).
	void SelectStory();
	void LeaveText();
	void MoveCaret(Motion motion, bool extend);
	doc::Status TypeText(std::string_view utf8);
	doc::Status Backspace(bool word);
	doc::Status DeleteForward(bool word);
	/// Nouveau paragraphe du même style sous celui du curseur, la suite du texte y passe
	/// (Maj+Entrée ; la plage sélectionnée est d'abord effacée).
	doc::Status Enter();
	/// Saut de ligne dans le paragraphe du curseur (Entrée).
	doc::Status LineBreak();
	/// Saut de page sous le paragraphe du curseur, puis nouveau paragraphe du même style sur la
	/// nouvelle page, où passe la suite du texte (Ctrl+Entrée) ; corps du document seulement.
	doc::Status PageBreakBelow();
	/// Position dans la plage de texte sélectionnée (début d'un glisser-déposer).
	[[nodiscard]] bool InTextRange(doc::TextPosition position) const;
	/// Position de texte sous un point de la mise en page affichée.
	[[nodiscard]] std::optional<doc::TextPosition> PositionAt(size_t page, double x, double y) const;
	/// Dépose la plage sélectionnée à `target` (déplacement, ou copie avec `copy`).
	doc::Status DropText(doc::TextPosition target, bool copy);
	[[nodiscard]] std::optional<doc::CaretBox> CaretGeometry(doc::TextPosition position) const;
	[[nodiscard]] std::vector<std::pair<size_t, doc::Rect>> RangeGeometry() const;

	// ── Presse-papiers ──
	/// Copie la plage de texte, sinon les nœuds sélectionnés.
	[[nodiscard]] Clip Copy() const;
	doc::Status Cut(Clip &clip);
	/// Colle : texte à la place du curseur (ou de la plage), nœuds après le bloc sélectionné.
	doc::Status Paste(const Clip &clip);
	/// Déplace un bloc (glisser-déposer) sous `parent` à `position`.
	doc::Status MoveBlock(doc::NodeId block, doc::NodeId parent, size_t position);

	// ── Formes et images ──
	/// Boîte affichée d'un nœud (première page où il apparaît) et sa page.
	[[nodiscard]] const doc::LayoutBox *BoxOf(doc::NodeId id, size_t *page = nullptr) const;
	/// Nouveau rectangle (coordonnées de la page, avant rotation) : largeur, hauteur, position.
	/// Une forme ou une image passe en alignement « aucun » à la même place (rectangle demandé) ;
	/// une zone de dessin ne change que de taille.
	doc::Status ResizeShape(doc::NodeId id, const doc::Rect &rect);
	/// Déplacement manuel : l'alignement horizontal passe à « aucun », la forme reste à sa place.
	doc::Status MoveShape(doc::NodeId id, double dx, double dy);
	/// Alignement horizontal de formes, images ou zones de dessin : « aucun » garde la place ; un
	/// autre alignement les replace — dans la colonne (flux : position remise à zéro), dans la
	/// zone de dessin ou dans la zone de contenu de la page (position absolue).
	doc::Status AlignShapes(const std::vector<doc::NodeId> &ids, doc::HorizontalAlign align);
	/// Zone de dessin de la sélection (sélectionnée, ou contenant l'élément sélectionné).
	[[nodiscard]] doc::NodeId DrawingOfSelection() const;
	doc::Status RotateShape(doc::NodeId id, double degrees);
	/// Centre de rotation au point (page), exprimé dans le repère choisi du nœud.
	doc::Status SetRotationOrigin(doc::NodeId id, double pageX, double pageY);
	/// Change le repère de la position en convertissant les coordonnées (la forme ne bouge pas).
	doc::Status SetPositionBase(doc::NodeId id, doc::PositionBase base);
	/// Change le repère du centre de rotation en convertissant ses coordonnées.
	doc::Status SetOriginBase(doc::NodeId id, doc::OriginBase base);

	// ── Structure : styles, listes, légendes, liens, tables des matières ──
	doc::Status ApplyStyle(ParagraphStyle style);
	/// Style du premier paragraphe visé (Normal si aucun style prédéfini).
	[[nodiscard]] ParagraphStyle CurrentStyle() const;
	/// Puces ou numérotation des paragraphes visés ; même style déjà posé partout : retiré.
	doc::Status ToggleList(doc::ListStyle style);
	/// Niveau de liste (±1, borné à 0…8) ; hors liste : retrait gauche de ±18 pt.
	doc::Status ChangeIndent(int delta);
	doc::Status SetHeadingNumbering(doc::HeadingNumbering numbering);
	[[nodiscard]] doc::HeadingNumbering Numbering() const;
	/// Légende numérotée : sous l'image ou la forme sélectionnée, au-dessus d'un tableau, sinon
	/// après le bloc courant. Le curseur se place après son préfixe.
	doc::Status InsertCaption(doc::CaptionKind kind, std::string_view text = {});
	/// Table des matières (ou des figures, des tableaux) précédée d'un titre non numéroté.
	doc::Status InsertTableOfContents(doc::TocContent content, std::string_view title = {});
	/// Paragraphe stylé inséré après le bloc courant (encadré, code…), curseur dedans.
	doc::Status InsertStyled(ParagraphStyle style, std::string_view text);
	/// Lien (URL ou « #id ») sur la plage de texte, sinon sur les segments visés : couleur et
	/// soulignement des liens.
	doc::Status SetLink(std::string link);
	doc::Status RemoveLink();
	/// Renvoi au curseur : numéro (Reference) ou page (PageReference) de `target`.
	doc::Status InsertReference(doc::NodeId target, doc::FieldKind kind);
	/// Structure du document (titres, légendes), mémorisée tant que le document ne change pas.
	[[nodiscard]] const doc::Outline &DocumentOutline() const;
	/// Curseur au début du texte de `node` (sinon sélection du nœud) ; faux si absent.
	bool GoTo(doc::NodeId node);
	/// Lien sous un point de la mise en page affichée (« #id » pour une entrée de table des
	/// matières ou un renvoi) ; vide sinon.
	[[nodiscard]] std::string LinkAt(size_t page, double x, double y) const;

	// ── Tableaux : sélection et opérations ──
	[[nodiscard]] TableSelection CurrentTable() const;
	void SelectTable(doc::NodeId table);
	void SelectRows(doc::NodeId table, size_t first, size_t last, TableSelect op = TableSelect::Replace);
	void SelectColumns(doc::NodeId table, size_t first, size_t last, TableSelect op = TableSelect::Replace);
	/// Rectangle de cellules (agrandi pour contenir entières les cellules fusionnées qu'il touche).
	void SelectCells(doc::NodeId table, size_t row0, size_t column0, size_t row1, size_t column1, TableSelect op = TableSelect::Replace);
	/// Fusionne le rectangle sélectionné (sa cellule haut gauche le couvre, le contenu y est réuni).
	doc::Status MergeCells();
	/// Sépare les cellules fusionnées que touche la sélection.
	doc::Status SplitCells();
	/// La sélection touche-t-elle une cellule fusionnée ?
	[[nodiscard]] bool SelectionHasMergedCells() const;
	/// `count` lignes au-dessus / au-dessous de la sélection.
	doc::Status InsertRows(bool below, size_t count = 1);
	doc::Status InsertColumns(bool right, size_t count = 1);
	/// Ligne ou colonne au rang `at` (bouton « + » sur un bord du tableau).
	doc::Status InsertRowAt(doc::NodeId table, size_t at);
	doc::Status InsertColumnAt(doc::NodeId table, size_t at);
	doc::Status DeleteRows();
	doc::Status DeleteColumns();
	doc::Status DuplicateRows();
	doc::Status DuplicateColumns();
	/// Lignes sélectionnées déplacées de `delta` rangs (négatif : vers le haut).
	doc::Status MoveRows(int delta);
	doc::Status MoveColumns(int delta);
	doc::Status SetColumnWidth(doc::NodeId table, size_t column, double width);
	doc::Status SetRowHeight(doc::NodeId row, double height);
	doc::Status DistributeColumns();
	doc::Status ApplyBorders(doc::BorderPreset preset, double width, doc::Color color);
	doc::Status BandRows(doc::Color even, doc::Color odd);
	/// Première ligne répétée en tête de chaque page (bascule).
	doc::Status ToggleHeaderRow();

	// ── Commandes ──
	doc::Status Run(doc::CommandPtr command);
	doc::Status Undo();
	doc::Status Redo();
	doc::Status UndoTo(size_t position);

	// Accueil : texte et paragraphes de la sélection.
	doc::Status ToggleText(doc::PropertyId flag);
	[[nodiscard]] bool TextFlag(doc::PropertyId flag) const;
	doc::Status SetText(doc::PropertyId property, doc::PropertyValue value);
	doc::Status SetParagraph(doc::PropertyId property, doc::PropertyValue value);
	/// Texte d'un TextNode (frappe : les modifications successives fusionnent dans l'historique).
	doc::Status EditText(doc::NodeId text, std::string value);

	// Insertion (après le bloc sélectionné, sinon en fin de document).
	doc::Status InsertParagraph(std::string_view text = {});
	doc::Status InsertTable(int rows, int columns);
	doc::Status InsertImage(doc::ImageResource image);
	doc::Status InsertImageFile(const std::filesystem::path &path);
	/// Forme : dans la zone de dessin de la sélection (placement libre), sinon dans le flux.
	doc::Status InsertShape(doc::ShapeKind kind);
	/// Zone de dessin (largeur de la colonne, 200 pt de haut) après le bloc sélectionné.
	doc::Status InsertDrawing();
	/// Zone de texte : rectangle sans fond ni bordure, hauteur automatique, curseur dans son texte.
	doc::Status InsertTextBox();
	/// Texte d'une forme : paragraphe créé s'il n'y en a pas, curseur à la fin de son texte.
	doc::Status EditShapeText(doc::NodeId shape);
	doc::Status InsertPageBreak();
	/// Numéro de page « Page n / N » dans le pied de page par défaut (créé si absent).
	doc::Status InsertPageNumber();

	// Mise en page : pages sélectionnées, sinon sections de la sélection (toutes leurs pages).
	doc::Status SetPage(doc::PropertyId property, doc::PropertyValue value);

	// Tableau (tableau englobant le nœud principal).
	doc::Status AddRow(bool below);
	doc::Status AddColumn(bool right);
	doc::Status DeleteRow();
	doc::Status DeleteColumn();
	/// Propriété des cellules sélectionnées (ou de la cellule du nœud principal) : fond, bordure…
	doc::Status SetCells(doc::PropertyId property, doc::PropertyValue value);

	// Disposition.
	doc::Status GroupSelection();
	doc::Status UngroupSelection();
	doc::Status DuplicateSelection();
	doc::Status DeleteSelection();
	doc::Status ToggleLocked();
	/// Images et formes sélectionnées déplacées dans la pile des images et formes de leur page
	/// (ZIndex renuméroté 1…n dans le nouvel ordre, une entrée d'historique).
	doc::Status ArrangeSelection(Arrange arrange);
	/// Même propriété pour plusieurs nœuds (marge intérieure glissée…), une entrée d'historique.
	doc::Status SetNodes(const std::vector<doc::NodeId> &nodes, doc::PropertyId property, doc::PropertyValue value);
	doc::Status ToggleHidden();

	// Panneau des propriétés : appliqué à chaque nœud sélectionné qui accepte la propriété.
	doc::Status SetSelected(doc::PropertyId property, doc::PropertyValue value);
	doc::Status ClearSelected(doc::PropertyId property);

	// ── Sélection ──
	void SelectAt(size_t page, double x, double y, bool add);
	void SelectArea(size_t page, const doc::Rect &area, bool add);
	/// Depuis l'arborescence : les nœuds masqués restent sélectionnables.
	void SelectNode(doc::NodeId id, bool add = false);
	void SelectAll();
	void ClearSelection();
	/// Résumé de la sélection (mémorisé tant que le document et la sélection ne changent pas :
	/// le ruban l'interroge à chaque image).
	[[nodiscard]] const doc::SelectionContext &Context() const;

	/// Segments de texte visés par les commandes de texte (texte sélectionné, texte des
	/// paragraphes et conteneurs sélectionnés).
	[[nodiscard]] const std::vector<doc::NodeId> &TextTargets() const;
	[[nodiscard]] const std::vector<doc::NodeId> &ParagraphTargets() const;

private:
	struct LayoutShared;
	struct Task {
		std::string message;  ///< motif français à un « {} » (traduit sur le fil de l'interface)
		std::string argument; ///< valeur du motif
		bool ok = true;
		uint64_t cleanState = 0;			 ///< état enregistré (sauvegarde réussie)
		uint64_t documentId = 0;			 ///< document enregistré (ignoré s'il a été remplacé depuis)
		std::filesystem::path path;			 ///< fichier enregistré ou ouvert
		std::shared_ptr<doc::Document> opened; ///< document chargé
		bool save = false;
	};

	void Reset(std::unique_ptr<doc::Document> document, std::filesystem::path path);
	void CaretAtStart(); ///< curseur au début du premier texte du corps
	doc::NodeId FreeInsertionPoint(size_t &position, double &offset) const;
	doc::Status PlaceManually(doc::NodeId id, const doc::LayoutBox &box, double dx, double dy);
	void PlaceFreely(doc::NodeId created, double offset, std::vector<doc::CommandPtr> &props) const;
	/// Exécute une commande de création et rend le nœud créé (kNoNode en cas d'échec).
	doc::NodeId Create(doc::CommandPtr command);
	/// Position d'insertion : après le bloc de flux du nœud principal, sinon fin du corps.
	doc::NodeId InsertionPoint(size_t &position, bool bodyOnly = false) const;
	doc::Status Transaction(std::string name, const std::function<doc::Status()> &body);
	doc::Status ForEachRun(bool rows, std::string_view name, const std::function<doc::Status(size_t first, size_t count)> &run);
	doc::NodeId NewParagraph(doc::NodeId parent, size_t position, std::string_view text);
	/// Sélectionne le texte de la cellule `column` de la ligne `row` (bornée à la dernière).
	void SelectCell(doc::NodeId row, size_t column);
	void Changed();
	/// Ordre de lecture du document courant (reconstruit quand le document change).
	const doc::TextOrder &Order() const;
	/// Segments de texte de la plage sélectionnée (ordre de lecture).
	std::vector<doc::NodeId> RangeTexts() const;
	/// Groupe du style (créé avec ses propriétés s'il manque) ; kNoNode pour Normal.
	doc::NodeId StyleGroup(ParagraphStyle style);
	[[nodiscard]] doc::NodeId FindStyleGroup(ParagraphStyle style) const;
	/// Paragraphe inséré après le bloc courant ; rend son segment de texte.
	doc::NodeId InsertBlockParagraph(std::string_view text, size_t *position = nullptr, doc::NodeId *parent = nullptr);

	/// Valeurs dérivées de la sélection, recalculées quand le document ou la sélection change.
	struct Memo {
		uint64_t stamp = 0, revision = UINT64_MAX, selection = UINT64_MAX, caret = UINT64_MAX;
		doc::SelectionContext context;
		std::vector<doc::NodeId> texts, paragraphs;
		std::vector<std::pair<doc::PropertyId, bool>> flags;
	};
	const Memo &Derived() const;

	mutable Memo m_memo;
	jobs::ThreadPool &m_pool;
	std::unique_ptr<doc::Document> m_document;
	std::unique_ptr<doc::History> m_history;
	doc::Selection m_selection;
	std::filesystem::path m_path;
	std::string m_name;
	uint64_t m_documentId = 0;
	std::shared_ptr<LayoutShared> m_layoutShared;
	jobs::AsyncValue<Presentation> m_layout;
	jobs::AsyncValue<Task> m_task;
	std::optional<Presentation> m_presentation;
	uint64_t m_requestedRevision = UINT64_MAX, m_requestedStamp = 0;
	doc::RenderOptions m_render;
	std::vector<std::string> m_messages;
	int m_groupCounter = 0;
	std::shared_ptr<const doc::FontLibrary> m_fonts;
	jobs::AsyncValue<std::shared_ptr<const doc::FontLibrary>> m_fontScan;
	doc::TextPosition m_caret, m_anchor;
	uint64_t m_caretRevision = 0;
	double m_caretX = -1; ///< abscisse visée par les déplacements verticaux
	mutable std::unique_ptr<doc::TextOrder> m_order;
	mutable uint64_t m_orderRevision = UINT64_MAX, m_orderStamp = 0;
	mutable std::unique_ptr<doc::TextOrder> m_viewOrder; ///< ordre de la vue affichée (surbrillance)
	mutable doc::Outline m_outline;
	mutable uint64_t m_outlineRevision = UINT64_MAX, m_outlineStamp = 0;
	mutable const doc::DocumentView *m_viewOrderOf = nullptr;
	mutable uint64_t m_viewOrderRevision = UINT64_MAX;
};

} // namespace scribe
