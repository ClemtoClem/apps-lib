#include "document/node.hpp"

#include <algorithm>
#include <array>

namespace doc {

namespace {

using enum NodeType;

constexpr NodeTypeMask kBlocks = MaskOf(Paragraph, Table, Image, Shape, TableOfContents, Drawing);

constexpr std::array<NodeTypeInfo, kNodeTypeCount> kTypes{{
	{Document, "Document", MaskOf(Groups, Section), false, true, false},
	{Groups, "Groups", MaskOf(Group), true, true, false},
	{Group, "Group", MaskOf(Group), false, false, false},
	{Section, "Section", MaskOf(Header, Footer, Body, Page), false, false, false},
	{Body, "Body", kBlocks | MaskOf(PageBreak), true, true, false},
	{Header, "Header", kBlocks, false, false, false},
	{Footer, "Footer", kBlocks, false, false, false},
	{Page, "Page", MaskOf(Header, Footer, Image, Shape), false, false, false},
	{Paragraph, "Paragraph", MaskOf(Text), false, false, true},
	{Text, "Text", 0, false, false, false},
	{Table, "Table", MaskOf(TableRow), false, false, true},
	{TableRow, "TableRow", MaskOf(TableCell), false, false, false},
	{TableCell, "TableCell", kBlocks, false, false, false},
	{Image, "Image", 0, false, false, true},
	{Shape, "Shape", MaskOf(Paragraph), false, false, true}, // paragraphes : texte de la forme
	{PageBreak, "PageBreak", 0, false, false, true},
	{TableOfContents, "TableOfContents", 0, false, false, true},
	{Drawing, "Drawing", MaskOf(Shape, Image), false, false, true},
}};

static_assert([] {
	for (size_t i = 0; i < kTypes.size(); ++i)
		if (size_t(kTypes[i].type) != i)
			return false;
	return true;
}(), "kTypes doit suivre l'ordre de NodeType");

} // namespace

const NodeTypeInfo &TypeInfo(NodeType type) noexcept { return kTypes[std::min(size_t(type), kNodeTypeCount - 1)]; }

std::optional<NodeType> FindNodeType(std::string_view name) noexcept {
	for (const NodeTypeInfo &i : kTypes)
		if (i.name == name)
			return i.type;
	return std::nullopt;
}

bool Node::InGroup(NodeId group) const noexcept {
	return std::ranges::any_of(m_groups, [group](const Membership &m) { return m.group == group; });
}

size_t Node::IndexOfChild(NodeId child) const noexcept {
	const auto it = std::ranges::find(m_children, child);
	return it == m_children.end() ? SIZE_MAX : size_t(it - m_children.begin());
}

} // namespace doc
