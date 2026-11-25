// Définitions de ui/builder.hpp — fabrique déclarative.
#include "ui/builder.hpp"
#include "ui/ui.hpp" // Context complet (feuilles `Custom`, sélecteurs)

namespace ui {

// ============================================================================
// WidgetBuilder
// ============================================================================

WidgetBuilder::WidgetBuilder(std::string type) {
	m_node = std::make_shared<Node>();
	m_node->type = std::move(type);
}

WidgetBuilder &WidgetBuilder::Id(std::string id) {
	m_node->id = std::move(id);
	return *this;
}
WidgetBuilder &WidgetBuilder::Class(std::string name) {
	m_node->classes.push_back(std::move(name));
	return *this;
}
WidgetBuilder &WidgetBuilder::Classes(std::initializer_list<std::string> names) {
	for (const std::string &n : names)
		m_node->classes.push_back(n);
	return *this;
}
WidgetBuilder &WidgetBuilder::Type(std::string type) {
	m_node->type = std::move(type);
	return *this;
}
WidgetBuilder &WidgetBuilder::Import(std::string path) {
	if (auto sheet = StyleSheet::Load(path); sheet.IsOk())
		m_node->localSheets.push_back(std::move(sheet.Value()));
	else
		core::log::Warn("ui: {}", sheet.Error());
	return *this;
}
WidgetBuilder &WidgetBuilder::Css(std::string text) {
	m_node->localSheets.push_back(StyleSheet::Parse(text));
	return *this;
}

WidgetBuilder &WidgetBuilder::Width(Length v) { m_node->style.width = v; return *this; }
WidgetBuilder &WidgetBuilder::Height(Length v) { m_node->style.height = v; return *this; }
WidgetBuilder &WidgetBuilder::MinWidth(Length v) { m_node->style.minWidth = v; return *this; }
WidgetBuilder &WidgetBuilder::MinHeight(Length v) { m_node->style.minHeight = v; return *this; }
WidgetBuilder &WidgetBuilder::MaxWidth(Length v) { m_node->style.maxWidth = v; return *this; }
WidgetBuilder &WidgetBuilder::MaxHeight(Length v) { m_node->style.maxHeight = v; return *this; }
WidgetBuilder &WidgetBuilder::Position(ui::Position p) { m_node->style.position = p; return *this; }
WidgetBuilder &WidgetBuilder::Anchor(ui::Anchor a) { m_node->style.anchor = a; return *this; }
WidgetBuilder &WidgetBuilder::Top(Length v) { m_node->style.top = v; return *this; }
WidgetBuilder &WidgetBuilder::Right(Length v) { m_node->style.right = v; return *this; }
WidgetBuilder &WidgetBuilder::Bottom(Length v) { m_node->style.bottom = v; return *this; }
WidgetBuilder &WidgetBuilder::Left(Length v) { m_node->style.left = v; return *this; }

WidgetBuilder &WidgetBuilder::Margin(float all) { m_node->style.SetMargin(math::Sides(all)); return *this; }
WidgetBuilder &WidgetBuilder::Margin(const math::Sides &s) { m_node->style.SetMargin(s); return *this; }
WidgetBuilder &WidgetBuilder::Padding(float all) { m_node->style.SetPadding(math::Sides(all)); return *this; }
WidgetBuilder &WidgetBuilder::Padding(const math::Sides &s) { m_node->style.SetPadding(s); return *this; }
WidgetBuilder &WidgetBuilder::Border(const math::Sides &widths, sdl3::Color color) {
	m_node->style.SetBorderWidth(widths);
	m_node->style.borderColor = color;
	return *this;
}
WidgetBuilder &WidgetBuilder::BorderRadius(const math::Corners &c) { m_node->style.SetBorderRadius(c); return *this; }
WidgetBuilder &WidgetBuilder::Background(sdl3::Color c) { m_node->style.backgroundColor = c; return *this; }
WidgetBuilder &WidgetBuilder::Opacity(float a) { m_node->style.opacity = a; return *this; }
WidgetBuilder &WidgetBuilder::ZIndex(int z) { m_node->style.zIndex = z; return *this; }

WidgetBuilder &WidgetBuilder::Display(ui::Display d) { m_node->style.display = d; return *this; }
WidgetBuilder &WidgetBuilder::FlexDirection(ui::FlexDirection d) { m_node->style.flexDirection = d; return *this; }
WidgetBuilder &WidgetBuilder::Wrap() { m_node->style.flexWrap = FlexWrap::Wrap; return *this; }
WidgetBuilder &WidgetBuilder::Justify(ui::Justify j) { m_node->style.justifyContent = j; return *this; }
WidgetBuilder &WidgetBuilder::AlignItems(ui::Align a) { m_node->style.alignItems = a; return *this; }
WidgetBuilder &WidgetBuilder::AlignSelf(ui::Align a) { m_node->style.alignSelf = a; return *this; }
WidgetBuilder &WidgetBuilder::Grow(float g) { m_node->style.flexGrow = g; return *this; }
WidgetBuilder &WidgetBuilder::Shrink(float s) { m_node->style.flexShrink = s; return *this; }
WidgetBuilder &WidgetBuilder::Gap(float g) { m_node->style.gap = g; return *this; }
WidgetBuilder &WidgetBuilder::GridColumns(int n) { m_node->style.gridColumns = n; return *this; }
WidgetBuilder &WidgetBuilder::OverflowX(Overflow o) { m_node->style.overflowX = o; return *this; }
WidgetBuilder &WidgetBuilder::OverflowY(Overflow o) { m_node->style.overflowY = o; return *this; }

WidgetBuilder &WidgetBuilder::Color(sdl3::Color c) { m_node->style.color = c; return *this; }
WidgetBuilder &WidgetBuilder::FontSize(float px) { m_node->style.fontSize = px; return *this; }
WidgetBuilder &WidgetBuilder::FontWeight(int w) { m_node->style.fontWeight = w; return *this; }
WidgetBuilder &WidgetBuilder::Bold() { m_node->style.fontWeight = 700; return *this; }
WidgetBuilder &WidgetBuilder::Italic() { m_node->style.fontStyle = FontStyle::Italic; return *this; }
WidgetBuilder &WidgetBuilder::Underline() { m_node->style.underline = true; return *this; }
WidgetBuilder &WidgetBuilder::TextAlign(ui::TextAlign a) { m_node->style.textAlign = a; return *this; }
WidgetBuilder &WidgetBuilder::WordWrap(bool on) { m_node->wordWrap = on; return *this; }
WidgetBuilder &WidgetBuilder::Ellipsis(bool on) { m_node->ellipsis = on; return *this; }

WidgetBuilder &WidgetBuilder::Hidden(bool on) { m_node->hidden = on; return *this; }
WidgetBuilder &WidgetBuilder::Disabled(bool on) { m_node->disabled = on; return *this; }

WidgetBuilder &WidgetBuilder::Children(std::initializer_list<WidgetBuilder> children) {
	for (const WidgetBuilder &c : children)
		m_node->children.push_back(c.m_node);
	return *this;
}
WidgetBuilder &WidgetBuilder::Child(WidgetBuilder child) {
	m_node->children.push_back(std::move(child.m_node));
	return *this;
}
WidgetBuilder &WidgetBuilder::Ref(Widget &out) {
	out = Widget(m_node);
	return *this;
}

WidgetBuilder &WidgetBuilder::Bind(float &v) {
	m_node->valueRef = &v;
	m_node->value = v;
	return *this;
}
WidgetBuilder &WidgetBuilder::Bind(bool &v) {
	m_node->checkedRef = &v;
	m_node->checked = v;
	return *this;
}
WidgetBuilder &WidgetBuilder::Bind(int &v) {
	m_node->indexRef = &v;
	m_node->selectedIndex = v;
	return *this;
}
WidgetBuilder &WidgetBuilder::Bind(std::string &v) {
	m_node->textRef = &v;
	m_node->text = v;
	return *this;
}
WidgetBuilder &WidgetBuilder::OnClick(std::function<void()> fn) {
	m_node->onClick = std::move(fn);
	return *this;
}
WidgetBuilder &WidgetBuilder::OnChange(std::function<void(float)> fn) {
	m_node->onChanged = std::move(fn);
	return *this;
}
WidgetBuilder &WidgetBuilder::OnIndexChanged(std::function<void(int)> fn) {
	m_node->onIndexChanged = std::move(fn);
	return *this;
}
WidgetBuilder &WidgetBuilder::OnSubmit(std::function<void()> fn) {
	m_node->onSubmit = std::move(fn);
	return *this;
}
WidgetBuilder &WidgetBuilder::OnEdited(std::function<void()> fn) {
	m_node->onEdited = std::move(fn);
	return *this;
}
WidgetBuilder &WidgetBuilder::OnLink(std::function<void(std::string)> fn) {
	m_node->onLink = std::move(fn);
	return *this;
}

WidgetBuilder &WidgetBuilder::OnGainFocus(std::function<void()> fn) { m_node->onGainFocus = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnLoseFocus(std::function<void()> fn) { m_node->onLoseFocus = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnMouseEnter(std::function<void()> fn) { m_node->onMouseEnter = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnMouseLeave(std::function<void()> fn) { m_node->onMouseLeave = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnResize(std::function<void()> fn) { m_node->onResize = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnKeyEvent(std::function<void(const KeyEvent &)> fn) { m_node->onKeyEvent = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnKeyEnter(std::function<void()> fn) { m_node->onKeyEnter = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnMouseEvent(std::function<void(const PointerEvent &)> fn) { m_node->onMouseEvent = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnTouchEvent(std::function<void(const PointerEvent &)> fn) { m_node->onTouchEvent = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnButtonLeft(std::function<void()> fn) { m_node->onButtonLeft = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnButtonRight(std::function<void()> fn) { m_node->onButtonRight = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnButtonMiddle(std::function<void()> fn) { m_node->onButtonMiddle = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnButtonX1(std::function<void()> fn) { m_node->onButtonX1 = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnButtonX2(std::function<void()> fn) { m_node->onButtonX2 = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnWheelX(std::function<void(float)> fn) { m_node->onWheelX = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnWheelY(std::function<void(float)> fn) { m_node->onWheelY = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnRotate(std::function<void(float)> fn) { m_node->onRotate = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnGesture(std::function<void(const GestureEvent &)> fn) { m_node->onGesture = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnSwipe(std::function<void(int)> fn) { m_node->onSwipe = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnZoom(std::function<void(float)> fn) { m_node->onZoom = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnHidden(std::function<void()> fn) { m_node->onHidden = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnVisible(std::function<void()> fn) { m_node->onVisible = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnEnable(std::function<void()> fn) { m_node->onEnable = std::move(fn); return *this; }
WidgetBuilder &WidgetBuilder::OnDisable(std::function<void()> fn) { m_node->onDisable = std::move(fn); return *this; }

WidgetBuilder &WidgetBuilder::GestureZoom(bool on) { m_node->gestureZoom = on; return *this; }
WidgetBuilder &WidgetBuilder::GestureRotate(bool on) { m_node->gestureRotate = on; return *this; }
WidgetBuilder &WidgetBuilder::GesturePrevNext(bool on) { m_node->gesturePrevNext = on; return *this; }
WidgetBuilder &WidgetBuilder::Cursor(sdl3::SystemCursor cursor) { m_node->style.cursor = cursor; return *this; }
WidgetBuilder &WidgetBuilder::Cursor(Image image, int hotX, int hotY) {
	m_node->cursorImage = std::make_shared<Image>(std::move(image));
	m_node->cursorHotX = hotX;
	m_node->cursorHotY = hotY;
	return *this;
}
WidgetBuilder &WidgetBuilder::Cursor(std::string path, int hotX, int hotY) {
	if (auto img = Image::Load(path); img.IsOk())
		return Cursor(std::move(img.Value()), hotX, hotY);
	else
		core::log::Warn("ui: {}", img.Error());
	return *this;
}

WidgetBuilder &WidgetBuilder::Title(std::string text) {
	m_node->text = std::move(text);
	return *this;
}
WidgetBuilder &WidgetBuilder::Tooltip(std::string text) {
	m_node->tooltip = Label(text).Build();
	return *this;
}
WidgetBuilder &WidgetBuilder::Tooltip(WidgetBuilder content) {
	m_node->tooltip = content.Build();
	return *this;
}
WidgetBuilder &WidgetBuilder::OnClose(std::function<void()> fn) {
	m_node->onClose = std::move(fn);
	return *this;
}

WidgetBuilder &WidgetBuilder::Placeholder(std::string text) {
	m_node->placeholder = std::move(text);
	return *this;
}
WidgetBuilder &WidgetBuilder::Password(bool on) {
	m_node->password = on;
	return *this;
}
WidgetBuilder &WidgetBuilder::Editable(bool on) {
	m_node->editable = on;
	return *this;
}
WidgetBuilder &WidgetBuilder::Step(float s) {
	m_node->step = s;
	return *this;
}
WidgetBuilder &WidgetBuilder::Indeterminate(bool on) {
	m_node->indeterminate = on;
	return *this;
}
WidgetBuilder &WidgetBuilder::Options(std::initializer_list<std::string_view> items) {
	m_node->options.clear();
	for (std::string_view s : items)
		m_node->options.emplace_back(s);
	return *this;
}
WidgetBuilder &WidgetBuilder::Options(std::vector<std::string> items) {
	m_node->options = std::move(items);
	return *this;
}
WidgetBuilder &WidgetBuilder::Group(std::string name) {
	m_node->group = std::move(name);
	return *this;
}
WidgetBuilder &WidgetBuilder::Icon(char32_t code) {
	m_node->icon = code;
	return *this;
}
WidgetBuilder &WidgetBuilder::Icon(Image image) {
	m_node->iconImage = std::make_shared<Image>(std::move(image));
	return *this;
}
WidgetBuilder &WidgetBuilder::Icon(std::string path) {
	if (auto img = Image::Load(path); img.IsOk())
		return Icon(std::move(img.Value()));
	else
		core::log::Warn("ui: {}", img.Error());
	return *this;
}
WidgetBuilder &WidgetBuilder::Selected(bool on) {
	m_node->checked = on;
	return *this;
}
WidgetBuilder &WidgetBuilder::TintIcon(bool on) {
	m_node->tintIcon = on;
	return *this;
}
WidgetBuilder &WidgetBuilder::Swatch(sdl3::Color color) {
	m_node->swatch = color;
	return *this;
}
WidgetBuilder &WidgetBuilder::PassThrough(bool on) {
	m_node->passThrough = on;
	return *this;
}
WidgetBuilder &WidgetBuilder::Subtitle(std::string text) {
	m_node->placeholder = std::move(text);
	return *this;
}
WidgetBuilder &WidgetBuilder::Url(std::string url) {
	m_node->url = std::move(url);
	return *this;
}
WidgetBuilder &WidgetBuilder::Open(bool on) {
	m_node->open = on;
	return *this;
}
WidgetBuilder &WidgetBuilder::Vertical(bool on) {
	m_node->style.flexDirection = on ? FlexDirection::Column : FlexDirection::Row;
	return *this;
}

WidgetBuilder &WidgetBuilder::Model(TableModel model) {
	m_node->modelView.model = std::move(model);
	return *this;
}
WidgetBuilder &WidgetBuilder::Headers(std::initializer_list<std::string_view> headers) {
	m_node->modelView.headers.clear();
	for (std::string_view h : headers)
		m_node->modelView.headers.emplace_back(h);
	return *this;
}
WidgetBuilder &WidgetBuilder::MultiSelect(bool on) {
	m_node->modelView.multiSelect = on;
	return *this;
}
WidgetBuilder &WidgetBuilder::Nodes(std::vector<TreeItem> roots) {
	m_node->modelView.tree = std::move(roots);
	return *this;
}
WidgetBuilder &WidgetBuilder::OnSelect(std::function<void(int)> fn) {
	m_node->modelView.onSelect = std::move(fn);
	return *this;
}
WidgetBuilder &WidgetBuilder::OnSelectId(std::function<void(std::string)> fn) {
	m_node->modelView.onSelectId = std::move(fn);
	return *this;
}
WidgetBuilder &WidgetBuilder::OnActivate(std::function<void(int)> fn) {
	m_node->modelView.onActivate = std::move(fn);
	return *this;
}

WidgetBuilder &WidgetBuilder::Tabs(std::initializer_list<std::string_view> tabs) {
	m_node->tabs.clear();
	for (std::string_view t : tabs)
		m_node->tabs.emplace_back(t);
	return *this;
}
WidgetBuilder &WidgetBuilder::Tabs(std::vector<std::string> tabs) {
	m_node->tabs = std::move(tabs);
	return *this;
}
WidgetBuilder &WidgetBuilder::Menus(std::vector<Menu> menus) {
	m_node->menus = std::move(menus);
	return *this;
}

WidgetBuilder &WidgetBuilder::ContextMenu(std::vector<MenuItem> items) {
	m_node->menus = {Menu{"", std::move(items)}};
	m_node->contextMenu = true;
	return *this;
}

WidgetBuilder &WidgetBuilder::At(float x, float y) {
	m_node->floating.x = x;
	m_node->floating.y = y;
	return *this;
}
WidgetBuilder &WidgetBuilder::Size(float w, float h) {
	m_node->floating.w = w;
	m_node->floating.h = h;
	return *this;
}
WidgetBuilder &WidgetBuilder::Resizable(bool on) {
	m_node->floating.resizable = on;
	return *this;
}

WidgetBuilder &WidgetBuilder::SlideIn(float duration, float offset, Easing easing) {
	m_node->animTime = 0.f;
	m_node->animDuration = duration;
	m_node->animOffset = offset;
	m_node->animEasing = easing;
	return *this;
}

WidgetBuilder &WidgetBuilder::Draggable(std::string payload) {
	m_node->dragDrop.draggable = true;
	m_node->dragDrop.payload = std::move(payload);
	return *this;
}
WidgetBuilder &WidgetBuilder::DropTarget(std::function<void(std::string)> fn) {
	m_node->dragDrop.dropTarget = true;
	m_node->dragDrop.onDrop = std::move(fn);
	return *this;
}

WidgetBuilder &WidgetBuilder::Plot(PlotData data) {
	m_node->plot = std::move(data);
	return *this;
}
WidgetBuilder &WidgetBuilder::Pie(std::vector<PieSlice> slices) {
	m_node->pie = std::move(slices);
	return *this;
}
WidgetBuilder &WidgetBuilder::Heatmap(std::vector<float> values, size_t cols, float min, float max) {
	m_node->heat = std::move(values);
	m_node->heatCols = cols;
	m_node->heatMin = min;
	m_node->heatMax = max;
	return *this;
}

NodePtr WidgetBuilder::Build() const { return m_node; }

Widget &Widget::SetText(std::string text) {
	if (m_node)
		m_node->text = std::move(text);
	return *this;
}
Widget &Widget::AddClass(std::string name) {
	if (m_node)
		m_node->classes.push_back(std::move(name));
	return *this;
}
Widget &Widget::RemoveClass(std::string name) {
	if (m_node)
		std::erase(m_node->classes, name);
	return *this;
}
Widget &Widget::SetValue(float value) {
	if (m_node)
		m_node->value = value;
	return *this;
}
Widget &Widget::SetChecked(bool on) {
	if (m_node)
		m_node->checked = on;
	return *this;
}
Widget &Widget::SetSelectedIndex(int index) {
	if (m_node)
		m_node->selectedIndex = index;
	return *this;
}
Widget &Widget::SetHidden(bool on) {
	if (m_node)
		m_node->hidden = on;
	return *this;
}
Widget &Widget::SetEnabled(bool on) {
	if (m_node)
		m_node->disabled = !on;
	return *this;
}
Widget &Widget::SetCursor(sdl3::SystemCursor cursor) {
	if (m_node)
		m_node->style.cursor = cursor;
	return *this;
}
Widget &Widget::SetIcon(char32_t icon) {
	if (m_node)
		m_node->icon = icon;
	return *this;
}
Widget &Widget::SetSwatch(Color color) {
	if (m_node)
		m_node->swatch = color;
	return *this;
}
Widget &Widget::Place(const sdl3::FRect &r) {
	if (m_node) {
		m_node->style.position = ui::Position::Absolute;
		m_node->style.left = Length(r.x);
		m_node->style.top = Length(r.y);
		m_node->style.width = Length(r.w);
		m_node->style.height = Length(r.h);
	}
	return *this;
}
Widget &Widget::SetTooltip(std::string text) {
	if (m_node) {
		if (m_node->tooltip && (m_node->tooltip->type == wt::label || m_node->tooltip->type == wt::text))
			m_node->tooltip->text = std::move(text);
		else
			m_node->tooltip = Label(text).Build();
	}
	return *this;
}

// ============================================================================
// Fabriques du catalogue
// ============================================================================

WidgetBuilder Box() { return WidgetBuilder(wt::box); }
WidgetBuilder Row() { return WidgetBuilder(wt::box).FlexDirection(FlexDirection::Row); }
WidgetBuilder Column() { return WidgetBuilder(wt::box).FlexDirection(FlexDirection::Column); }
WidgetBuilder Text(std::string_view text) {
	WidgetBuilder b(wt::text);
	b.Spec().text = std::string(text);
	return b;
}
WidgetBuilder Label(std::string_view text) {
	WidgetBuilder b(wt::label);
	b.Spec().text = std::string(text);
	return b;
}
WidgetBuilder Button(std::string_view label) {
	WidgetBuilder b(wt::button);
	b.Spec().text = std::string(label);
	return b;
}
WidgetBuilder Spacer() { return WidgetBuilder(wt::box).Grow(); }

WidgetBuilder Slider(float min, float max) {
	WidgetBuilder b(wt::slider);
	b.Spec().valueMin = min;
	b.Spec().valueMax = max;
	return b;
}
WidgetBuilder Checkbox(std::string_view label) {
	WidgetBuilder b(wt::checkbox);
	b.Spec().text = std::string(label);
	return b;
}
WidgetBuilder ProgressBar(float fraction) {
	WidgetBuilder b(wt::progress);
	b.Spec().value = fraction;
	b.Spec().valueMin = 0.f;
	b.Spec().valueMax = 1.f;
	return b;
}
WidgetBuilder Scroll() { return WidgetBuilder(wt::scroll).Display(Display::Flex).OverflowX(Overflow::Hidden).OverflowY(Overflow::Auto); }
WidgetBuilder Panel(std::string_view title) {
	WidgetBuilder b(wt::panel);
	b.Spec().text = std::string(title);
	return b;
}
WidgetBuilder Modal() { return WidgetBuilder(wt::modal); }

WidgetBuilder Separator() { return WidgetBuilder(wt::separator).FlexDirection(FlexDirection::Row); }
WidgetBuilder ImageView(char32_t icon) {
	WidgetBuilder b(wt::image);
	b.Spec().icon = icon;
	return b;
}
WidgetBuilder ImageView(Image image) {
	WidgetBuilder b(wt::image);
	b.Spec().iconImage = std::make_shared<Image>(std::move(image));
	return b;
}
WidgetBuilder ImageView(std::string path) {
	WidgetBuilder b(wt::image);
	if (auto img = Image::Load(path); img.IsOk())
		b.Spec().iconImage = std::make_shared<Image>(std::move(img.Value()));
	else
		core::log::Warn("ui: {}", img.Error());
	return b;
}
WidgetBuilder RichTextLabel(std::string_view markdown) {
	WidgetBuilder b(wt::richtext);
	b.Spec().text = std::string(markdown);
	return b;
}
WidgetBuilder Heading(std::string_view text) {
	WidgetBuilder b(wt::text);
	b.Spec().text = std::string(text);
	b.Spec().classes.push_back("heading");
	return b;
}
WidgetBuilder Badge(std::string_view text, Color color) {
	WidgetBuilder b(wt::badge);
	b.Spec().text = std::string(text);
	b.Spec().style.color = color;
	b.Spec().style.backgroundColor = sdl3::WithAlpha(color, 40);
	b.Spec().style.SetPadding(math::Sides(10.f, 4.f));
	b.Spec().style.SetBorderRadius(math::Corners(999.f));
	return b;
}
WidgetBuilder ToggleButton(std::string_view label) {
	WidgetBuilder b(wt::toggle);
	b.Spec().text = std::string(label);
	return b;
}
WidgetBuilder Switch(std::string_view label) {
	WidgetBuilder b(wt::switchbox);
	b.Spec().text = std::string(label);
	return b;
}
WidgetBuilder Selectable(std::string_view label) {
	WidgetBuilder b(wt::selectable);
	b.Spec().text = std::string(label);
	return b;
}
WidgetBuilder MenuButton(std::string_view title, std::string_view subtitle, char32_t icon) {
	WidgetBuilder b(wt::menubutton);
	b.Spec().text = std::string(title);
	b.Spec().placeholder = std::string(subtitle);
	b.Spec().icon = icon;
	return b;
}
WidgetBuilder SettingRow(std::string_view title, std::string_view description) {
	WidgetBuilder b(wt::settingrow);
	b.Spec().text = std::string(title);
	b.Spec().placeholder = std::string(description);
	return b;
}
WidgetBuilder RadioButton(std::string_view label) {
	WidgetBuilder b(wt::radio);
	b.Spec().text = std::string(label);
	return b;
}
WidgetBuilder ToolButton(char32_t icon) {
	WidgetBuilder b(wt::toolbutton);
	b.Spec().icon = icon;
	return b;
}
WidgetBuilder ToolButton(std::string path, bool tint) {
	WidgetBuilder b(wt::toolbutton);
	b.Icon(std::move(path));
	b.Spec().tintIcon = tint;
	return b;
}
WidgetBuilder ToolGroup(std::string_view label) {
	WidgetBuilder b(wt::toolgroup);
	b.Spec().text = std::string(label);
	// Outils en ligne, centrés ; la légende occupe le rembourrage du bas (dessinée au rendu).
	b.FlexDirection(FlexDirection::Row).AlignItems(Align::Center).Gap(4.f).Padding(math::Sides{8.f, 4.f, 10.f, 22.f}); // gauche, haut, droite (filet), bas (légende)
	return b;
}
WidgetBuilder PrimaryButton(std::string_view label) {
	WidgetBuilder b(wt::button);
	b.Spec().text = std::string(label);
	b.Spec().classes.push_back("primary");
	return b;
}
WidgetBuilder DangerButton(std::string_view label) {
	WidgetBuilder b(wt::button);
	b.Spec().text = std::string(label);
	b.Spec().classes.push_back("danger");
	return b;
}
WidgetBuilder SuccessButton(std::string_view label) {
	WidgetBuilder b(wt::button);
	b.Spec().text = std::string(label);
	b.Spec().classes.push_back("success");
	return b;
}
WidgetBuilder LineEdit() { return WidgetBuilder(wt::lineedit); }
WidgetBuilder TextArea() { return WidgetBuilder(wt::textarea); }
WidgetBuilder CodeEditor() { return WidgetBuilder(wt::codeeditor); }
WidgetBuilder &WidgetBuilder::Syntax(std::shared_ptr<const SyntaxRules> rules) {
	m_node->syntax = std::move(rules);
	return *this;
}
WidgetBuilder &WidgetBuilder::ErrorLine(int line) {
	m_node->errorLine = line;
	return *this;
}
WidgetBuilder SpinBox(float min, float max, float step) {
	WidgetBuilder b(wt::spinbox);
	b.Spec().valueMin = min;
	b.Spec().valueMax = max;
	b.Spec().step = step;
	return b;
}
WidgetBuilder Knob(float min, float max) {
	WidgetBuilder b(wt::knob);
	b.Spec().valueMin = min;
	b.Spec().valueMax = max;
	return b;
}
WidgetBuilder ComboBox() { return WidgetBuilder(wt::combobox); }
WidgetBuilder Expander(std::string_view title) {
	WidgetBuilder b(wt::expander);
	b.Spec().text = std::string(title);
	b.Spec().open = true;
	return b;
}
WidgetBuilder Splitter() {
	WidgetBuilder b(wt::splitter);
	b.FlexDirection(FlexDirection::Row);
	b.Spec().value = 0.5f; // poignée au milieu tant qu'elle n'a pas été déplacée
	return b;
}
WidgetBuilder Flow() { return WidgetBuilder(wt::box).FlexDirection(FlexDirection::Row).Wrap(); }
WidgetBuilder ListView() { return WidgetBuilder(wt::listview).OverflowY(Overflow::Auto); }
WidgetBuilder TreeView() { return WidgetBuilder(wt::treeview).OverflowY(Overflow::Auto); }
WidgetBuilder TableView() { return WidgetBuilder(wt::tableview).OverflowY(Overflow::Auto); }
WidgetBuilder TabBar() { return WidgetBuilder(wt::tabbar); }
WidgetBuilder TabWidget() { return WidgetBuilder(wt::tabwidget); }
WidgetBuilder MenuBar() { return WidgetBuilder(wt::menubar); }
WidgetBuilder SubWindow(std::string_view title) {
	WidgetBuilder b(wt::subwindow);
	b.Spec().text = std::string(title);
	b.Spec().floating.active = true;
	b.Spec().floating.w = 320.f;
	b.Spec().floating.h = 220.f;
	return b;
}
WidgetBuilder Popup() {
	WidgetBuilder b(wt::popup);
	b.Spec().floating.active = true;
	b.Spec().floating.w = 200.f;
	b.Spec().floating.h = 120.f;
	return b;
}
WidgetBuilder Plot() { return WidgetBuilder(wt::plot); }
WidgetBuilder PieChart() { return WidgetBuilder(wt::pie); }
WidgetBuilder Heatmap() { return WidgetBuilder(wt::heatmap); }

WidgetBuilder Custom(std::function<void(sdl3::Canvas &, const sdl3::FRect &content)> paint) {
	WidgetBuilder b("custom");
	b.Spec().customPaint = std::move(paint);
	return b;
}
WidgetBuilder Custom(std::function<void(Context &)> draw) {
	WidgetBuilder b("custom");
	b.Spec().customImmediate = std::move(draw);
	return b;
}

WidgetBuilder ColorPicker(Color &color, float height) {
	WidgetBuilder b("custom");
	b.Spec().customImmediate = [&color, height](Context &ctx) { ctx.ColorPicker("color-picker", color, height); };
	return b;
}

WidgetBuilder DatePicker(Date &date) {
	WidgetBuilder b("custom");
	b.Spec().customImmediate = [&date](Context &ctx) { ctx.DatePicker("date-picker", date); };
	return b;
}

} // namespace ui
