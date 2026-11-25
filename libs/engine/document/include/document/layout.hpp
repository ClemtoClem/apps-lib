#pragma once
/**
 * @file layout.hpp
 * @brief Mise en page et pagination : géométrie des pages, mesure des blocs (paragraphes,
 *        tableaux, images, formes), coupure des lignes, pagination selon les règles des blocs,
 *        en-têtes / pieds de page, champs de numérotation, cache incrémental, synchronisation
 *        des pages du document.
 *
 * Le calcul lit une vue en lecture seule (`DocumentView`, typiquement un `Snapshot`) : il peut
 * tourner sur un fil de fond. Les mesures des blocs du flux sont faites en parallèle
 * (`jobs::ParallelFor`) puis réutilisées par `LayoutCache` tant que le bloc, les styles et la
 * largeur ne changent pas. Seule `SyncPages` modifie le document (fil propriétaire).
 *
 *   doc::LayoutCache cache;
 *   doc::LayoutResult layout = doc::Layout(*document.TakeSnapshot(), &cache);
 *   doc::SyncPages(document, layout);                 // crée / supprime les PageNode
 *
 * Unités : points, origine en haut à gauche de chaque page.
 */
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <core/robin_map.hpp>
#include <pdf/fonts.hpp>

#include "document/document.hpp"
#include "document/fonts.hpp"
#include "document/outline.hpp"

namespace jobs {
class ThreadPool;
}

namespace doc {

namespace detail {
struct OutlineItem;
}

struct Insets {
	double top = 0, right = 0, bottom = 0, left = 0;
};

/// Style de texte résolu d'un TextNode (ou d'un paragraphe pour ses lignes vides).
struct TextStyle {
	pdf::FontRef font; ///< police standard ou installée (FontLibrary)
	double size = 11;
	Color color = Color::BLACK();
	Color background = Color::TRANSPARENT();
	bool underline = false;
	bool strike = false;
	double rise = 0;  ///< décalage de la ligne de base (exposant > 0, indice < 0) ; `size` est déjà réduit
	std::string link; ///< URL, ou « #id » (nœud du document)
};

/// Portion de texte d'un TextNode sur une ligne.
struct GlyphRun {
	NodeId node = kNoNode; ///< TextNode
	std::string text;	   ///< UTF-8
	size_t offset = 0;	   ///< position (octets) du début de `text` dans le texte du TextNode
	double x = 0;		   ///< relatif à l'origine de la ligne (mesure) ou absolu (page)
	double width = 0;
	TextStyle style;
	FieldKind field = FieldKind::None;
	/// Texte produit par le moteur (numéro de titre, légende, puce, entrée de table des matières) :
	/// `node` est alors le paragraphe ou la table des matières, pas un TextNode.
	bool generated = false;
	NodeId target = kNoNode; ///< nœud visé (entrée de table des matières, renvoi)
};

struct LineBox {
	double y = 0;		 ///< haut de la ligne, relatif au haut du contenu du paragraphe
	double height = 0;
	double baseline = 0; ///< depuis le haut de la ligne
	double width = 0;	 ///< largeur occupée par le texte
	std::vector<GlyphRun> runs;
};

struct BlockLayout;
using BlockPtr = std::shared_ptr<const BlockLayout>;

struct CellLayout {
	NodeId node = kNoNode;
	double x = 0, width = 0;	   ///< largeur : somme des colonnes couvertes (cellule fusionnée)
	size_t rows = 1, columns = 1;  ///< lignes et colonnes couvertes (cellule fusionnée)
	double contentHeight = 0; ///< padding et bordure compris
	Insets padding;
	double border = 0;
	VerticalAlign align = VerticalAlign::Top;
	std::vector<std::pair<double, BlockPtr>> blocks; ///< (y relatif au contenu de la cellule, bloc)
};

struct RowLayout {
	NodeId node = kNoNode;
	double height = 0;
	bool header = false; ///< répétée en tête de chaque page (RepeatHeader)
	bool joinNext = false; ///< liée à la suivante par une cellule fusionnée (jamais coupées entre deux pages)
	std::vector<CellLayout> cells;
};

/// Mesure d'un bloc à une largeur donnée (indépendante de sa position).
struct BlockLayout {
	NodeId node = kNoNode;
	NodeType type = NodeType::Paragraph;
	double x = 0;			  ///< décalage horizontal dans la zone de contenu (retrait, alignement)
	double width = 0, height = 0; ///< boîte de bordure
	double spaceBefore = 0, spaceAfter = 0;
	Insets padding;
	double border = 0;
	// Règles de pagination
	bool keepTogether = false, keepWithNext = false, pageBreakBefore = false, allowSplit = true;
	int minLines = 2, maxLines = 0;
	bool absolute = false;
	double positionX = 0, positionY = 0;
	PositionBase base = PositionBase::Auto;
	double rotation = 0;				  ///< degrés, sens horaire
	double originX = 0, originY = 0;	  ///< centre de rotation dans le repère `originBase`
	OriginBase originBase = OriginBase::Node;
	int64_t z = 0;				///< ZIndex (images, formes)
	std::vector<LineBox> lines; ///< paragraphe
	std::vector<RowLayout> rows; ///< tableau
	/// Forme : ses paragraphes (y relatif à sa zone de texte) ; zone de dessin : ses éléments (y = 0,
	/// placés par leur position).
	std::vector<std::pair<double, BlockPtr>> content;
	double contentHeight = 0; ///< forme : hauteur de son texte
	VerticalAlign contentAlign = VerticalAlign::Top;
};

/// Élément positionné sur une page (fond, bordure, image, forme, conteneur, test de position).
struct LayoutBox {
	NodeId node = kNoNode;
	NodeType type = NodeType::Paragraph;
	Rect rect;
	uint16_t depth = 0;		///< profondeur d'imbrication (dessin et test de position)
	bool continued = false; ///< fragment qui fait suite à la page précédente
	bool continues = false; ///< fragment qui se poursuit page suivante
	bool overflow = false;	///< ne tenait pas dans la page (débordement)
	// Images et formes : rotation et repères (conversion des coordonnées entre repères).
	double rotation = 0;			   ///< degrés, sens horaire, autour de (pivotX, pivotY)
	double pivotX = 0, pivotY = 0;	   ///< centre de rotation (coordonnées de la page)
	double slotX = 0, slotY = 0;	   ///< place dans le flux (origine du repère « nœud »)
	double parentX = 0, parentY = 0;   ///< coin du conteneur (origine du repère « parent »)
	int64_t z = 0;					   ///< ZIndex : ordre d'empilement des images et formes
	double alignX = 0;				   ///< décalage d'alignement dans la place (HAlign ; 0 si None)
	/// Couche de dessin : ZIndex d'une image ou d'une forme (et des paragraphes de son texte), 0
	/// pour les autres boîtes (ordre stable : à couche égale, ordre de la mise en page).
	[[nodiscard]] int64_t Layer() const noexcept { return z; }
	/// Le point (page) est-il dans la boîte (rotation comprise) ?
	[[nodiscard]] bool Contains(double x, double y) const noexcept;
};

struct PlacedLine {
	NodeId paragraph = kNoNode;
	Rect rect;
	double baseline = 0;		///< ordonnée absolue de la ligne de base
	std::vector<GlyphRun> runs; ///< x absolus
	/// Texte d'une forme tournée : rotation (degrés, sens horaire) autour de (pivotX, pivotY) ;
	/// `rect`, `baseline` et les segments restent dans le repère non tourné.
	double rotation = 0, pivotX = 0, pivotY = 0;
	/// Point (page) ramené dans le repère non tourné de la ligne.
	[[nodiscard]] std::pair<double, double> Unrotate(double x, double y) const noexcept;
};

struct PageGeometry {
	double width = 0, height = 0;
	Insets margins;
	double headerDistance = 36, footerDistance = 36;
	PageFormat format = PageFormat::A4;
	Orientation orientation = Orientation::Portrait;
};

struct PageLayout {
	NodeId page = kNoNode;	  ///< PageNode (kNoNode tant que `SyncPages` ne l'a pas créé)
	NodeId section = kNoNode;
	size_t number = 1;		  ///< numéro dans le document (1…)
	size_t indexInSection = 0;
	PageGeometry geometry;
	Rect content;			  ///< zone du flux (marges et en-tête / pied déduits)
	NodeId header = kNoNode, footer = kNoNode;
	std::vector<LayoutBox> boxes; ///< ordre de dessin
	std::vector<PlacedLine> lines;
};

struct SectionLayout {
	NodeId section = kNoNode;
	size_t firstPage = 0, pageCount = 0;
};

struct LayoutStats {
	size_t measured = 0; ///< blocs mesurés (corps, en-têtes exclus)
	size_t reused = 0;	 ///< blocs repris du cache (corps et cellules)
	size_t cells = 0;	 ///< blocs de cellules de tableaux mesurés
};

struct LayoutResult {
	uint64_t stamp = 0, revision = 0; ///< document et révision mis en page
	std::vector<PageLayout> pages;
	std::vector<SectionLayout> sections;
	LayoutStats stats;

	/// Nœud le plus profond sous le point (texte d'abord, puis boîtes) ; kNoNode sinon.
	[[nodiscard]] NodeId HitTest(size_t page, double x, double y) const;
	/// Pages où le nœud apparaît (boîtes ou lignes).
	[[nodiscard]] std::vector<size_t> PagesOf(NodeId node) const;
	/// Première apparition d'un nœud : page et ordonnée du haut (ligne ou boîte) ; faux si absent.
	[[nodiscard]] bool Locate(NodeId node, size_t &page, double &y) const;
	/// Segment de texte sous le point (liens, entrées de table des matières) ; nullptr sinon.
	[[nodiscard]] const GlyphRun *RunAt(size_t page, double x, double y) const;
};

/// Mesures réutilisables d'une mise en page à la suivante. Une mise en page à la fois.
class LayoutCache {
public:
	[[nodiscard]] size_t Size() const noexcept { return m_entries.Size(); }
	void Clear() noexcept {
		m_entries.Clear();
		m_outline.Clear();
	}

private:
	friend class Layouter;
	struct Entry {
		BlockPtr block;
		uint64_t generation = 0;
		// Clé complète (vérifiée : une collision de hachage ne rend jamais une mauvaise mesure).
		NodeId node = kNoNode;
		uint64_t revision = 0, style = 0, ancestry = 0;
		int64_t width = 0, height = 0;
		std::string context; ///< préfixes générés du bloc (numéros de titres, légendes, puces)
		std::vector<uint64_t> nested; ///< tableau : clés des blocs de ses cellules (gardés avec lui)
	};
	core::RobinMap<uint64_t, Entry> m_entries;
	/// Contribution de chaque bloc du corps à la structure (titres, légendes, listes), même clé.
	struct OutlineMemo {
		uint64_t revision = 0, style = 0, ancestry = 0, generation = 0;
		std::shared_ptr<const std::vector<detail::OutlineItem>> items;
	};
	core::RobinMap<NodeId, OutlineMemo> m_outline;
	uint64_t m_stamp = 0, m_generation = 0, m_fonts = 0;
};

struct LayoutOptions {
	jobs::ThreadPool *pool = nullptr; ///< nullptr : pool global ; mesure parallèle si plusieurs blocs
	bool parallel = true;
	/// Polices installées ; nullptr : polices standard PDF seulement.
	std::shared_ptr<const FontLibrary> fonts;
};

/// Géométrie d'une page existante (cascade complète) ou, si `page` vaut kNoNode, d'une page
/// virtuelle de `section` (groupe de pages de la section, puis section, puis document).
[[nodiscard]] PageGeometry ResolvePageGeometry(const DocumentView &view, NodeId section, NodeId page);
/// Propriété d'une page existante, ou d'une page virtuelle (kNoNode) résolue comme une future
/// page de `section` : groupe de pages, puis section (héritables) ou document (pages ciblées).
[[nodiscard]] Resolved ResolvePageProperty(const DocumentView &view, NodeId section, NodeId page, PropertyId property);
/// Style de texte résolu d'un nœud (police standard rapprochée de la famille demandée).
[[nodiscard]] TextStyle ResolveTextStyle(const DocumentView &view, PropertyCache &cache, NodeId node, const FontLibrary *fonts = nullptr);

/// Mesure un bloc (Paragraph, Table, Image, Shape, PageBreak, TableOfContents) à la largeur
/// `width`, sans structure du document (ni numéros de titres, ni puces, ni table des matières).
[[nodiscard]] BlockPtr MeasureBlock(const DocumentView &view, PropertyCache &cache, NodeId block, double width,
									double maxHeight = 1e9, const FontLibrary *fonts = nullptr);

[[nodiscard]] LayoutResult Layout(const DocumentView &view, LayoutCache *cache = nullptr, const LayoutOptions &options = {});

/// Crée ou supprime les PageNode générés pour qu'ils correspondent au résultat, et y reporte
/// leurs identifiants. Rend le nombre de pages créées ou supprimées.
size_t SyncPages(Document &document, LayoutResult &layout);

} // namespace doc
