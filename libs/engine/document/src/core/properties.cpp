#include "document/properties.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace doc {

std::string_view ErrorName(ErrorCode code) noexcept {
	switch (code) {
	case ErrorCode::NotFound: return "NotFound";
	case ErrorCode::InvalidParent: return "InvalidParent";
	case ErrorCode::Cycle: return "Cycle";
	case ErrorCode::Locked: return "Locked";
	case ErrorCode::Protected: return "Protected";
	case ErrorCode::TypeMismatch: return "TypeMismatch";
	case ErrorCode::InvalidValue: return "InvalidValue";
	case ErrorCode::Io: return "Io";
	case ErrorCode::Corrupt: return "Corrupt";
	case ErrorCode::Unsupported: return "Unsupported";
	case ErrorCode::Nothing: return "Nothing";
	}
	return "?";
}

PageSize StandardSize(PageFormat format) noexcept {
	switch (format) {
	case PageFormat::A4: return {Millimeters(210), Millimeters(297)};
	case PageFormat::A3: return {Millimeters(297), Millimeters(420)};
	case PageFormat::A5: return {Millimeters(148), Millimeters(210)};
	case PageFormat::Letter: return {Inches(8.5), Inches(11)};
	case PageFormat::Legal: return {Inches(8.5), Inches(14)};
	case PageFormat::Custom: break;
	}
	return {};
}

std::string_view PageFormatName(PageFormat format) noexcept {
	switch (format) {
	case PageFormat::A4: return "A4";
	case PageFormat::A3: return "A3";
	case PageFormat::A5: return "A5";
	case PageFormat::Letter: return "Letter";
	case PageFormat::Legal: return "Legal";
	case PageFormat::Custom: return "Custom";
	}
	return "?";
}

namespace {

using enum NodeType;
using P = PropertyId;
using C = PropertyCategory;

constexpr NodeTypeMask kFlow = MaskOf(Paragraph, Table, Image, Shape, PageBreak, TableOfContents, Drawing);
constexpr NodeTypeMask kContent = kFlow | MaskOf(Text, TableRow, TableCell, Header, Footer, Page, Section, Body);
constexpr NodeTypeMask kPageSetup = MaskOf(Document, Section, Page);
constexpr NodeTypeMask kSized = MaskOf(Image, Shape, Table, TableRow, TableCell, Drawing);
constexpr NodeTypeMask kBoxed = MaskOf(Page, Paragraph, Table, TableCell, Image, Shape, Header, Footer, TableOfContents, Drawing);
constexpr double kUnbounded = -1; // minimum > maximum : non borné
constexpr double kMaxLength = 14400; // 200 pouces : plus grande page PDF admise

struct Builder {
	std::array<PropertyInfo, kPropertyCount> table{};

	void Add(P id, std::string_view name, C cat, PropertyValue def, bool inheritable, NodeTypeMask targets,
			 double lo = 1, double hi = kUnbounded) {
		PropertyInfo &i = table[size_t(id)];
		i.id = id;
		i.name = name;
		i.category = cat;
		i.type = TypeOf(def);
		i.inheritable = inheritable;
		i.targets = targets;
		i.minimum = lo;
		i.maximum = hi;
		i.defaultValue = std::move(def);
	}
	void Bool(P id, std::string_view name, C cat, bool def, bool inh, NodeTypeMask targets) { Add(id, name, cat, def, inh, targets); }
	void Int(P id, std::string_view name, C cat, int64_t def, bool inh, NodeTypeMask targets, double lo, double hi) {
		Add(id, name, cat, def, inh, targets, lo, hi);
	}
	template <typename E> void Enum(P id, std::string_view name, C cat, E def, E last, bool inh, NodeTypeMask targets) {
		Add(id, name, cat, int64_t(def), inh, targets, 0, double(last));
	}
	void Real(P id, std::string_view name, C cat, double def, bool inh, NodeTypeMask targets, double lo, double hi) {
		Add(id, name, cat, def, inh, targets, lo, hi);
	}
	void Length(P id, std::string_view name, double def, bool inh, NodeTypeMask targets) {
		Real(id, name, C::Structural, def, inh, targets, 0, kMaxLength);
	}
	void Paint(P id, std::string_view name, Color def, bool inh, NodeTypeMask targets) { Add(id, name, C::Visual, def, inh, targets); }
	void Text(P id, std::string_view name, C cat, std::string def, bool inh, NodeTypeMask targets) {
		Add(id, name, cat, std::move(def), inh, targets);
	}
};

const std::array<PropertyInfo, kPropertyCount> &Catalogue() {
	static const std::array<PropertyInfo, kPropertyCount> table = [] {
		Builder b;
		const C F = C::Functional, S = C::Structural, V = C::Visual;
		// Fonctionnelles
		b.Bool(P::Visible, "Visible", F, true, false, kContent);
		b.Bool(P::Locked, "Locked", F, false, true, kAllNodeTypes);
		b.Bool(P::Editable, "Editable", F, true, true, kAllNodeTypes);
		b.Bool(P::Selectable, "Selectable", F, true, false, kContent);
		b.Bool(P::SelectViaGroup, "SelectViaGroup", F, false, false, kContent);
		b.Bool(P::KeepTogether, "KeepTogether", F, false, false, kFlow);
		b.Bool(P::KeepWithNext, "KeepWithNext", F, false, false, kFlow);
		b.Bool(P::PageBreakBefore, "PageBreakBefore", F, false, false, kFlow);
		b.Bool(P::AllowSplit, "AllowSplit", F, true, false, MaskOf(Paragraph, Table));
		b.Int(P::MinimumLines, "MinimumLines", F, 2, false, MaskOf(Paragraph), 1, 1000);
		b.Int(P::MaximumLines, "MaximumLines", F, 0, false, MaskOf(Paragraph), 0, 100000); // 0 : illimité
		b.Bool(P::RepeatHeader, "RepeatHeader", F, false, false, MaskOf(TableRow));
		b.Enum(P::Variant, "Variant", F, HeaderFooterVariant::Default, HeaderFooterVariant::Odd, false, MaskOf(Header, Footer));
		b.Enum(P::Field, "Field", F, FieldKind::None, FieldKind::Chapter, false, MaskOf(Text));
		b.Text(P::Name, "Name", F, "", false, kAllNodeTypes);
		b.Int(P::Priority, "Priority", F, 0, false, MaskOf(Group), -1000000, 1000000);
		b.Text(P::Text, "Text", F, "", false, MaskOf(Text));
		b.Int(P::Resource, "Resource", F, 0, false, MaskOf(Image), 0, 9.0e15);
		b.Enum(P::Shape, "Shape", F, ShapeKind::Rectangle, ShapeKind::ArrowLine, false, MaskOf(Shape));
		b.Int(P::PageGroup, "PageGroup", F, 0, false, MaskOf(Section), 0, 9.0e15);
		// Structurelles (longueurs en points ; 0 pour Width / Height : automatique)
		b.Length(P::Width, "Width", 0, false, kSized);
		b.Length(P::Height, "Height", 0, false, kSized);
		b.Length(P::MinWidth, "MinWidth", 0, false, kSized);
		b.Length(P::MaxWidth, "MaxWidth", 0, false, kSized); // 0 : sans limite
		b.Length(P::MinHeight, "MinHeight", 0, false, kSized | MaskOf(Paragraph));
		b.Length(P::MaxHeight, "MaxHeight", 0, false, kSized);
		b.Length(P::MarginTop, "MarginTop", 72, true, kPageSetup);
		b.Length(P::MarginBottom, "MarginBottom", 72, true, kPageSetup);
		b.Length(P::MarginLeft, "MarginLeft", 72, true, kPageSetup);
		b.Length(P::MarginRight, "MarginRight", 72, true, kPageSetup);
		const NodeTypeMask padded = MaskOf(TableCell, Shape, Paragraph, TableOfContents, Drawing);
		b.Length(P::PaddingTop, "PaddingTop", 0, false, padded);
		b.Length(P::PaddingBottom, "PaddingBottom", 0, false, padded);
		b.Length(P::PaddingLeft, "PaddingLeft", 0, false, padded);
		b.Length(P::PaddingRight, "PaddingRight", 0, false, padded);
		b.Length(P::SpaceBefore, "SpaceBefore", 0, false, kFlow);
		b.Length(P::SpaceAfter, "SpaceAfter", 6, false, kFlow);
		b.Real(P::LineSpacing, "LineSpacing", S, 1.15, true, MaskOf(Paragraph), 0.5, 10);
		b.Length(P::IndentLeft, "IndentLeft", 0, false, MaskOf(Paragraph));
		b.Length(P::IndentRight, "IndentRight", 0, false, MaskOf(Paragraph));
		b.Real(P::FirstLineIndent, "FirstLineIndent", S, 0, false, MaskOf(Paragraph), -kMaxLength, kMaxLength);
		b.Enum(P::HAlign, "HorizontalAlign", S, HorizontalAlign::Left, HorizontalAlign::None, true, kFlow);
		b.Enum(P::VAlign, "VerticalAlign", S, VerticalAlign::Top, VerticalAlign::Bottom, false, MaskOf(TableCell, Shape)); // forme : son texte
		b.Enum(P::Position, "Positioning", S, Positioning::Flow, Positioning::Absolute, false, MaskOf(Image, Shape));
		b.Real(P::PositionX, "PositionX", S, 0, false, MaskOf(Image, Shape), -kMaxLength, kMaxLength);
		b.Real(P::PositionY, "PositionY", S, 0, false, MaskOf(Image, Shape), -kMaxLength, kMaxLength);
		b.Enum(P::Format, "PageFormat", S, PageFormat::A4, PageFormat::Custom, true, kPageSetup);
		b.Real(P::PageWidth, "PageWidth", S, 0, true, kPageSetup, 0, kMaxLength);	// format Custom
		b.Real(P::PageHeight, "PageHeight", S, 0, true, kPageSetup, 0, kMaxLength); // format Custom
		b.Enum(P::PageOrientation, "Orientation", S, Orientation::Portrait, Orientation::Landscape, true, kPageSetup);
		b.Length(P::HeaderDistance, "HeaderDistance", 36, true, kPageSetup);
		b.Length(P::FooterDistance, "FooterDistance", 36, true, kPageSetup);
		// Visuelles
		b.Paint(P::TextColor, "TextColor", Color::BLACK(), true, kContent);
		b.Paint(P::BackgroundColor, "BackgroundColor", Color::TRANSPARENT(), false, kBoxed | MaskOf(Text));
		b.Text(P::FontFamily, "FontFamily", V, "Helvetica", true, kContent);
		b.Real(P::FontSize, "FontSize", V, 11, true, kContent, 1, 1638);
		b.Bool(P::Bold, "Bold", V, false, true, kContent);
		b.Bool(P::Italic, "Italic", V, false, true, kContent);
		b.Bool(P::Underline, "Underline", V, false, true, kContent);
		b.Real(P::BorderWidth, "BorderWidth", V, 0, false, kBoxed, 0, 144);
		b.Paint(P::BorderColor, "BorderColor", Color::BLACK(), false, kBoxed);
		b.Real(P::Opacity, "Opacity", V, 1, false, MaskOf(Image, Shape), 0, 1);
		b.Real(P::Rotation, "Rotation", S, 0, false, MaskOf(Image, Shape), -360, 360); // degrés, sens horaire
		b.Enum(P::PositionReference, "PositionReference", S, PositionBase::Auto, PositionBase::Page, false, MaskOf(Image, Shape));
		b.Real(P::RotationOriginX, "RotationOriginX", S, 0, false, MaskOf(Image, Shape), -kMaxLength, kMaxLength);
		b.Real(P::RotationOriginY, "RotationOriginY", S, 0, false, MaskOf(Image, Shape), -kMaxLength, kMaxLength);
		b.Enum(P::RotationOriginReference, "RotationOriginReference", S, OriginBase::Node, OriginBase::Page, false, MaskOf(Image, Shape));
		// Structure : titres, légendes, listes, liens, table des matières.
		b.Int(P::OutlineLevel, "OutlineLevel", F, 0, false, MaskOf(Paragraph), 0, 9); // 0 : corps de texte
		b.Enum(P::HeadingNumbering, "HeadingNumbering", F, HeadingNumbering::None, HeadingNumbering::DecimalDot, true, MaskOf(Document, Section));
		b.Enum(P::CaptionKind, "CaptionKind", F, CaptionKind::None, CaptionKind::Listing, false, MaskOf(Paragraph));
		b.Text(P::CaptionLabel, "CaptionLabel", F, "", false, MaskOf(Paragraph)); // vide : « Figure », « Tableau », « Listing »
		b.Enum(P::ListStyle, "ListStyle", F, ListStyle::None, ListStyle::UpperRoman, false, MaskOf(Paragraph));
		b.Int(P::ListLevel, "ListLevel", F, 0, false, MaskOf(Paragraph), 0, 8);
		b.Int(P::ListStart, "ListStart", F, 0, false, MaskOf(Paragraph), 0, 1000000); // 0 : suite de la liste
		b.Text(P::Link, "Link", F, "", false, MaskOf(Text)); // URL, ou « #id » : nœud du document
		b.Bool(P::Strike, "Strike", V, false, true, kContent);
		b.Enum(P::Script, "Script", V, TextScript::Normal, TextScript::Subscript, true, kContent);
		b.Int(P::BorderSides, "BorderSides", V, border::kAll, false, kBoxed, 0, 15);
		b.Bool(P::PreserveSpaces, "PreserveSpaces", S, false, false, MaskOf(Paragraph));
		b.Enum(P::TocContent, "TocContent", F, TocContent::Headings, TocContent::Listings, false, MaskOf(TableOfContents));
		b.Int(P::TocDepth, "TocDepth", F, 3, false, MaskOf(TableOfContents), 1, 9);
		b.Bool(P::Hyperlinks, "Hyperlinks", F, true, false, MaskOf(TableOfContents));
		// Ordre d'empilement des images et formes entre elles (plus grand : dessus) ; à valeur
		// égale, ordre du document. Le texte reste au premier plan.
		b.Int(P::ZIndex, "ZIndex", S, 0, false, MaskOf(Image, Shape), -1000000, 1000000);
		// Redimensionnement à la souris : garder le rapport largeur / hauteur.
		b.Bool(P::KeepAspectRatio, "KeepAspectRatio", S, false, false, MaskOf(Image, Shape, Drawing));
		// Cellules fusionnées : la cellule d'ancrage (coin haut gauche) couvre les suivantes.
		b.Int(P::ColumnSpan, "ColumnSpan", S, 1, false, MaskOf(TableCell), 1, 1000);
		b.Int(P::RowSpan, "RowSpan", S, 1, false, MaskOf(TableCell), 1, 1000000);
		for (P local : {P::Name, P::Text, P::Resource, P::PageGroup, P::Field, P::Variant, P::Link})
			b.table[size_t(local)].cascades = false;
		return b.table;
	}();
	return table;
}

} // namespace

const PropertyInfo &Info(PropertyId id) noexcept {
	const auto &table = Catalogue();
	return table[std::min(size_t(id), kPropertyCount - 1)];
}

std::span<const PropertyInfo> AllProperties() noexcept { return Catalogue(); }

std::optional<PropertyId> FindProperty(std::string_view name) noexcept {
	for (const PropertyInfo &i : Catalogue())
		if (i.name == name)
			return i.id;
	return std::nullopt;
}

std::string_view CategoryName(PropertyCategory category) noexcept {
	switch (category) {
	case PropertyCategory::Functional: return "Functional";
	case PropertyCategory::Structural: return "Structural";
	case PropertyCategory::Visual: return "Visual";
	}
	return "?";
}

bool AppliesTo(PropertyId id, NodeType type) noexcept {
	const PropertyInfo &i = Info(id);
	return i.inheritable || (i.targets & MaskOf(type)) != 0;
}

Status Validate(PropertyId id, const PropertyValue &value) {
	if (size_t(id) >= kPropertyCount)
		return Failure(ErrorCode::NotFound, "propriété inconnue");
	const PropertyInfo &i = Info(id);
	if (TypeOf(value) != i.type)
		return Failure(ErrorCode::TypeMismatch, std::format("type de valeur incorrect pour {}", i.name));
	if (i.minimum > i.maximum)
		return Success();
	double v = 0;
	if (const auto *n = std::get_if<int64_t>(&value))
		v = double(*n);
	else if (const auto *r = std::get_if<double>(&value))
		v = *r;
	else
		return Success();
	if (!std::isfinite(v) || v < i.minimum || v > i.maximum)
		return Failure(ErrorCode::InvalidValue, std::format("{} hors domaine [{}, {}] : {}", i.name, i.minimum, i.maximum, v));
	return Success();
}

// ── PropertyMap ──

const PropertyValue *PropertyMap::Find(PropertyId id) const noexcept {
	const auto it = std::ranges::lower_bound(m_entries, id, {}, &Entry::first);
	return it != m_entries.end() && it->first == id ? &it->second : nullptr;
}

bool PropertyMap::Set(PropertyId id, PropertyValue value) {
	const auto it = std::ranges::lower_bound(m_entries, id, {}, &Entry::first);
	if (it != m_entries.end() && it->first == id) {
		if (it->second == value)
			return false;
		it->second = std::move(value);
		return true;
	}
	m_entries.insert(it, Entry{id, std::move(value)});
	return true;
}

bool PropertyMap::Erase(PropertyId id) {
	const auto it = std::ranges::lower_bound(m_entries, id, {}, &Entry::first);
	if (it == m_entries.end() || it->first != id)
		return false;
	m_entries.erase(it);
	return true;
}

} // namespace doc
