#pragma once
/**
 * @file properties.hpp
 * @brief Catalogue des propriétés du moteur : identifiants, métadonnées (catégorie, type,
 *        héritage, cibles, domaine, valeur par défaut), valeurs typées, descripteurs `Prop<T>`
 *        et table de propriétés explicites d'un nœud.
 *
 * Trois catégories :
 *  - **fonctionnelles** : comportement (visible, verrouillé, sélectionnable, règles de pagination…) ;
 *  - **structurelles** : géométrie et organisation (dimensions, marges, alignements, format de page…) ;
 *  - **visuelles** : apparence (couleurs, police, bordures, opacité).
 *
 * Une propriété **héritable** (police, couleur du texte, alignement, mise en page de la page…)
 * se transmet du parent à l'enfant. Une propriété non héritable ne s'applique, par les groupes
 * et par le document, qu'aux types de nœuds qu'elle **cible** (un fond de cellule ne colore pas
 * les paragraphes de la cellule).
 *
 *   doc.Set(id, props::FontSize, 14.0);
 *   const double size = doc.Get(id, props::FontSize);   // valeur résolue (cascade)
 */
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "document/types.hpp"

namespace doc {

enum class NodeType : uint8_t {
	Document,  ///< racine
	Groups,	   ///< conteneur des groupes (non rendu)
	Group,	   ///< groupe de propriétés (hiérarchique)
	Section,   ///< séquence de pages partageant en-têtes, pieds et flux
	Body,	   ///< flux de contenu d'une section
	Header,	   ///< en-tête (modèle de section ou propre à une page)
	Footer,	   ///< pied de page
	Page,	   ///< page produite par la pagination
	Paragraph, ///< paragraphe (enfants : TextNode)
	Text,	   ///< segment de texte de style homogène
	Table,
	TableRow,
	TableCell,
	Image,
	Shape,
	PageBreak, ///< saut de page explicite
	TableOfContents, ///< table des matières, des figures ou des tableaux (générée à la mise en page)
	Drawing,   ///< zone de dessin : formes, images et zones de texte placées librement (bloc du flux)
	Count
};
inline constexpr size_t kNodeTypeCount = size_t(NodeType::Count);

/// Ensemble de types de nœuds (masque de bits).
using NodeTypeMask = uint32_t;
[[nodiscard]] constexpr NodeTypeMask MaskOf(NodeType t) noexcept { return NodeTypeMask(1) << uint32_t(t); }
template <typename... T> [[nodiscard]] constexpr NodeTypeMask MaskOf(NodeType a, T... rest) noexcept { return MaskOf(a) | MaskOf(rest...); }
inline constexpr NodeTypeMask kAllNodeTypes = (NodeTypeMask(1) << kNodeTypeCount) - 1;

// ── Énumérations portées par des propriétés entières ────────────────────────

/// None : placement libre d'une image, d'une forme ou d'une zone de dessin (seule sa position
/// compte) ; pour le texte, comme Left.
enum class HorizontalAlign : uint8_t { Left, Center, Right, Justify, None };
enum class VerticalAlign : uint8_t { Top, Middle, Bottom };
/// Flow : placé dans le flux ; Absolute : positionné sur la page (PositionX / PositionY depuis le coin).
enum class Positioning : uint8_t { Flow, Absolute };
/// Formes : Line et ArrowLine sont des traits (à mi-hauteur de la boîte, flèche à droite) ; les
/// autres des contours fermés inscrits dans la boîte (voir `ShapeOutline`).
enum class ShapeKind : uint8_t {
	Rectangle,
	Ellipse,
	Line,
	RoundedRectangle,
	Triangle,
	RightTriangle,
	Diamond,
	Parallelogram,
	Trapezoid,
	Pentagon,
	Hexagon,
	Octagon,
	Star5,
	Star6,
	Cross,
	ArrowRight,
	ArrowLeft,
	ArrowUp,
	ArrowDown,
	DoubleArrow,
	Chevron,
	Heart,
	Callout,
	Lightning,
	ArrowLine,
};
/// La forme est-elle un trait (Line, ArrowLine) ?
[[nodiscard]] constexpr bool IsLineShape(ShapeKind kind) noexcept { return kind == ShapeKind::Line || kind == ShapeKind::ArrowLine; }
/// Champ dynamique d'un TextNode, résolu à la mise en page.
/// Reference : numéro de la cible du lien (« 2.3 », « Tableau 4 ») ; PageReference : sa page ;
/// Chapter : titre de niveau 1 de la page (numéro compris), pour les en-têtes courants.
enum class FieldKind : uint8_t { None, PageNumber, PageCount, Reference, PageReference, Chapter };
/// Repère de PositionX / PositionY. Auto : le nœud dans le flux (Positioning::Flow : décalage de
/// sa place), la page (Positioning::Absolute).
enum class PositionBase : uint8_t { Auto, Node, Parent, Page };
/// Repère du centre de rotation : Node = décalage depuis le centre de la forme (0, 0 : centre),
/// Parent = depuis le coin du conteneur, Page = depuis le coin de la page.
enum class OriginBase : uint8_t { Node, Parent, Page };
/// Numérotation des titres (OutlineLevel ≥ 1) : aucune, « 1.2 », « 1.2. ».
enum class HeadingNumbering : uint8_t { None, Decimal, DecimalDot };
/// Légende numérotée : « Figure 3 : … », « Tableau 1 : … », « Listing 2 : … ».
enum class CaptionKind : uint8_t { None, Figure, Table, Listing };
/// Puces (caractères WinAnsi, affichables avec les polices standard) et numérotations.
enum class ListStyle : uint8_t { None, Bullet, Dash, EmDash, Chevron, Decimal, DecimalParen, LowerAlpha, UpperAlpha, LowerRoman, UpperRoman };
enum class TextScript : uint8_t { Normal, Superscript, Subscript };
/// Entrées d'une table des matières : titres, ou légendes d'un type.
enum class TocContent : uint8_t { Headings, Figures, Tables, Listings };
/// Côtés d'une bordure (BorderSides, masque de bits).
namespace border {
inline constexpr int64_t kTop = 1, kRight = 2, kBottom = 4, kLeft = 8, kAll = 15;
}
/// Variante d'en-tête / de pied : page par défaut, première page de la section, paires, impaires.
enum class HeaderFooterVariant : uint8_t { Default, First, Even, Odd };

// ── Catalogue ───────────────────────────────────────────────────────────────

enum class PropertyId : uint16_t {
	// Fonctionnelles
	Visible,
	Locked,
	Editable,
	Selectable,
	SelectViaGroup,
	KeepTogether,
	KeepWithNext,
	PageBreakBefore,
	AllowSplit,
	MinimumLines,
	MaximumLines,
	RepeatHeader,
	Variant,
	Field,
	Name,
	Priority,
	Text,
	Resource,
	Shape,
	PageGroup,
	// Structurelles
	Width,
	Height,
	MinWidth,
	MaxWidth,
	MinHeight,
	MaxHeight,
	MarginTop,
	MarginBottom,
	MarginLeft,
	MarginRight,
	PaddingTop,
	PaddingBottom,
	PaddingLeft,
	PaddingRight,
	SpaceBefore,
	SpaceAfter,
	LineSpacing,
	IndentLeft,
	IndentRight,
	FirstLineIndent,
	HAlign,
	VAlign,
	Position,
	PositionX,
	PositionY,
	Format,
	PageWidth,
	PageHeight,
	PageOrientation,
	HeaderDistance,
	FooterDistance,
	// Visuelles
	TextColor,
	BackgroundColor,
	FontFamily,
	FontSize,
	Bold,
	Italic,
	Underline,
	BorderWidth,
	BorderColor,
	Opacity,
	// Ajouts (enregistrés par nom : l'ordre de l'énumération n'importe pas aux fichiers)
	Rotation,
	PositionReference,
	RotationOriginX,
	RotationOriginY,
	RotationOriginReference,
	OutlineLevel,
	HeadingNumbering,
	CaptionKind,
	CaptionLabel,
	ListStyle,
	ListLevel,
	ListStart,
	Link,
	Strike,
	Script,
	BorderSides,
	PreserveSpaces,
	TocContent,
	TocDepth,
	Hyperlinks,
	ZIndex,
	KeepAspectRatio,
	ColumnSpan,
	RowSpan,
	Count
};
inline constexpr size_t kPropertyCount = size_t(PropertyId::Count);

enum class PropertyCategory : uint8_t { Functional, Structural, Visual };
enum class PropertyType : uint8_t { Bool, Int, Real, Rgba, Text }; // Rgba : doc::Color

/// Valeur d'une propriété ; l'alternative active correspond au `PropertyType`.
using PropertyValue = std::variant<bool, int64_t, double, Color, std::string>;

[[nodiscard]] constexpr PropertyType TypeOf(const PropertyValue &v) noexcept { return PropertyType(v.index()); }

struct PropertyInfo {
	PropertyId id{};
	std::string_view name;	///< identifiant stable (persistance, interface)
	PropertyCategory category{};
	PropertyType type{};
	bool inheritable = false;
	/// Faux : propriété d'identité ou de contenu (nom, texte, ressource, champ, variante, groupe
	/// de pages) — jamais reçue d'un groupe, d'un parent ou du document : explicite ou défaut.
	bool cascades = true;
	NodeTypeMask targets = kAllNodeTypes; ///< types auxquels elle s'applique (hors héritage)
	double minimum = 0, maximum = 0;	  ///< domaine des Int / Real (minimum > maximum : non borné)
	PropertyValue defaultValue;
};

[[nodiscard]] const PropertyInfo &Info(PropertyId id) noexcept;
[[nodiscard]] std::span<const PropertyInfo> AllProperties() noexcept;
[[nodiscard]] std::optional<PropertyId> FindProperty(std::string_view name) noexcept;
[[nodiscard]] std::string_view CategoryName(PropertyCategory category) noexcept;
/// La propriété s'applique-t-elle au type (cible, ou héritable) ?
[[nodiscard]] bool AppliesTo(PropertyId id, NodeType type) noexcept;
/// Vérifie le type et le domaine d'une valeur.
[[nodiscard]] Status Validate(PropertyId id, const PropertyValue &value);

// ── Descripteurs typés ──────────────────────────────────────────────────────

namespace detail {
template <typename T> struct Storage;
template <> struct Storage<bool> { using Type = bool; };
template <> struct Storage<int64_t> { using Type = int64_t; };
template <> struct Storage<double> { using Type = double; };
template <> struct Storage<Color> { using Type = Color; };
template <> struct Storage<std::string> { using Type = std::string; };
template <typename T>
	requires std::is_enum_v<T>
struct Storage<T> {
	using Type = int64_t;
};
} // namespace detail

/// Descripteur typé : `Prop<double>`, `Prop<HorizontalAlign>`… (les énumérations sont stockées en entier).
template <typename T> struct Prop {
	PropertyId id;
	using ValueType = T;
	using StorageType = typename detail::Storage<T>::Type;

	[[nodiscard]] static PropertyValue ToValue(const T &v) {
		if constexpr (std::is_enum_v<T>)
			return PropertyValue(int64_t(v));
		else
			return PropertyValue(v);
	}
	/// Extrait la valeur ; une alternative inattendue donne la valeur construite par défaut.
	[[nodiscard]] static T FromValue(const PropertyValue &v) {
		const auto *p = std::get_if<StorageType>(&v);
		if (!p)
			return T{};
		if constexpr (std::is_enum_v<T>)
			return T(*p);
		else
			return *p;
	}
};

namespace props {
// Fonctionnelles
inline constexpr Prop<bool> Visible{PropertyId::Visible};
inline constexpr Prop<bool> Locked{PropertyId::Locked};
inline constexpr Prop<bool> Editable{PropertyId::Editable};
inline constexpr Prop<bool> Selectable{PropertyId::Selectable};
inline constexpr Prop<bool> SelectViaGroup{PropertyId::SelectViaGroup};
inline constexpr Prop<bool> KeepTogether{PropertyId::KeepTogether};
inline constexpr Prop<bool> KeepWithNext{PropertyId::KeepWithNext};
inline constexpr Prop<bool> PageBreakBefore{PropertyId::PageBreakBefore};
inline constexpr Prop<bool> AllowSplit{PropertyId::AllowSplit};
inline constexpr Prop<int64_t> MinimumLines{PropertyId::MinimumLines};
inline constexpr Prop<int64_t> MaximumLines{PropertyId::MaximumLines};
inline constexpr Prop<bool> RepeatHeader{PropertyId::RepeatHeader};
inline constexpr Prop<HeaderFooterVariant> Variant{PropertyId::Variant};
inline constexpr Prop<FieldKind> Field{PropertyId::Field};
inline constexpr Prop<std::string> Name{PropertyId::Name};
inline constexpr Prop<int64_t> Priority{PropertyId::Priority};
inline constexpr Prop<std::string> Text{PropertyId::Text};
inline constexpr Prop<int64_t> Resource{PropertyId::Resource};
inline constexpr Prop<ShapeKind> Shape{PropertyId::Shape};
inline constexpr Prop<int64_t> PageGroup{PropertyId::PageGroup};
// Structurelles
inline constexpr Prop<double> Width{PropertyId::Width};
inline constexpr Prop<double> Height{PropertyId::Height};
inline constexpr Prop<double> MinWidth{PropertyId::MinWidth};
inline constexpr Prop<double> MaxWidth{PropertyId::MaxWidth};
inline constexpr Prop<double> MinHeight{PropertyId::MinHeight};
inline constexpr Prop<double> MaxHeight{PropertyId::MaxHeight};
inline constexpr Prop<double> MarginTop{PropertyId::MarginTop};
inline constexpr Prop<double> MarginBottom{PropertyId::MarginBottom};
inline constexpr Prop<double> MarginLeft{PropertyId::MarginLeft};
inline constexpr Prop<double> MarginRight{PropertyId::MarginRight};
inline constexpr Prop<double> PaddingTop{PropertyId::PaddingTop};
inline constexpr Prop<double> PaddingBottom{PropertyId::PaddingBottom};
inline constexpr Prop<double> PaddingLeft{PropertyId::PaddingLeft};
inline constexpr Prop<double> PaddingRight{PropertyId::PaddingRight};
inline constexpr Prop<double> SpaceBefore{PropertyId::SpaceBefore};
inline constexpr Prop<double> SpaceAfter{PropertyId::SpaceAfter};
inline constexpr Prop<double> LineSpacing{PropertyId::LineSpacing};
inline constexpr Prop<double> IndentLeft{PropertyId::IndentLeft};
inline constexpr Prop<double> IndentRight{PropertyId::IndentRight};
inline constexpr Prop<double> FirstLineIndent{PropertyId::FirstLineIndent};
inline constexpr Prop<HorizontalAlign> HAlign{PropertyId::HAlign};
inline constexpr Prop<VerticalAlign> VAlign{PropertyId::VAlign};
inline constexpr Prop<Positioning> Position{PropertyId::Position};
inline constexpr Prop<double> PositionX{PropertyId::PositionX};
inline constexpr Prop<double> PositionY{PropertyId::PositionY};
inline constexpr Prop<PageFormat> Format{PropertyId::Format};
inline constexpr Prop<double> PageWidth{PropertyId::PageWidth};
inline constexpr Prop<double> PageHeight{PropertyId::PageHeight};
inline constexpr Prop<Orientation> PageOrientation{PropertyId::PageOrientation};
inline constexpr Prop<double> HeaderDistance{PropertyId::HeaderDistance};
inline constexpr Prop<double> FooterDistance{PropertyId::FooterDistance};
// Visuelles
inline constexpr Prop<Color> TextColor{PropertyId::TextColor};
inline constexpr Prop<Color> BackgroundColor{PropertyId::BackgroundColor};
inline constexpr Prop<std::string> FontFamily{PropertyId::FontFamily};
inline constexpr Prop<double> FontSize{PropertyId::FontSize};
inline constexpr Prop<bool> Bold{PropertyId::Bold};
inline constexpr Prop<bool> Italic{PropertyId::Italic};
inline constexpr Prop<bool> Underline{PropertyId::Underline};
inline constexpr Prop<double> BorderWidth{PropertyId::BorderWidth};
inline constexpr Prop<Color> BorderColor{PropertyId::BorderColor};
inline constexpr Prop<double> Opacity{PropertyId::Opacity};
inline constexpr Prop<double> Rotation{PropertyId::Rotation};
inline constexpr Prop<PositionBase> PositionReference{PropertyId::PositionReference};
inline constexpr Prop<double> RotationOriginX{PropertyId::RotationOriginX};
inline constexpr Prop<double> RotationOriginY{PropertyId::RotationOriginY};
inline constexpr Prop<OriginBase> RotationOriginReference{PropertyId::RotationOriginReference};
// Structure du document
inline constexpr Prop<int64_t> OutlineLevel{PropertyId::OutlineLevel};
inline constexpr Prop<HeadingNumbering> HeadingNumbering{PropertyId::HeadingNumbering};
inline constexpr Prop<CaptionKind> CaptionKind{PropertyId::CaptionKind};
inline constexpr Prop<std::string> CaptionLabel{PropertyId::CaptionLabel};
inline constexpr Prop<ListStyle> ListStyle{PropertyId::ListStyle};
inline constexpr Prop<int64_t> ListLevel{PropertyId::ListLevel};
inline constexpr Prop<int64_t> ListStart{PropertyId::ListStart};
inline constexpr Prop<std::string> Link{PropertyId::Link};
inline constexpr Prop<bool> Strike{PropertyId::Strike};
inline constexpr Prop<TextScript> Script{PropertyId::Script};
inline constexpr Prop<int64_t> BorderSides{PropertyId::BorderSides};
inline constexpr Prop<bool> PreserveSpaces{PropertyId::PreserveSpaces};
inline constexpr Prop<TocContent> TocContent{PropertyId::TocContent};
inline constexpr Prop<int64_t> TocDepth{PropertyId::TocDepth};
inline constexpr Prop<bool> Hyperlinks{PropertyId::Hyperlinks};
inline constexpr Prop<int64_t> ZIndex{PropertyId::ZIndex};
inline constexpr Prop<bool> KeepAspectRatio{PropertyId::KeepAspectRatio};
inline constexpr Prop<int64_t> ColumnSpan{PropertyId::ColumnSpan}; ///< cellule fusionnée : colonnes couvertes (1 : aucune fusion)
inline constexpr Prop<int64_t> RowSpan{PropertyId::RowSpan};		 ///< cellule fusionnée : lignes couvertes
} // namespace props

// ── Table de propriétés explicites ──────────────────────────────────────────

/// Propriétés explicites d'un nœud : vecteur trié par identifiant (peu d'entrées par nœud,
/// recherche dichotomique, itération dans l'ordre du catalogue).
class PropertyMap {
public:
	using Entry = std::pair<PropertyId, PropertyValue>;

	[[nodiscard]] const PropertyValue *Find(PropertyId id) const noexcept;
	[[nodiscard]] bool Contains(PropertyId id) const noexcept { return Find(id) != nullptr; }
	/// Insère ou remplace ; retourne vrai si la valeur a changé.
	bool Set(PropertyId id, PropertyValue value);
	/// Retourne vrai si une valeur a été retirée.
	bool Erase(PropertyId id);
	[[nodiscard]] size_t Size() const noexcept { return m_entries.size(); }
	[[nodiscard]] bool Empty() const noexcept { return m_entries.empty(); }
	[[nodiscard]] auto begin() const noexcept { return m_entries.begin(); }
	[[nodiscard]] auto end() const noexcept { return m_entries.end(); }
	friend bool operator==(const PropertyMap &, const PropertyMap &) = default;

private:
	std::vector<Entry> m_entries;
};

} // namespace doc
