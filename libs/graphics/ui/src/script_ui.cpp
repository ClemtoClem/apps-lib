#include "ui/script_ui.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "ui/builder.hpp"

namespace ui {

struct ScriptUi::Item {
	std::string kind;
	NodePtr node;
	script::Value handle;  ///< instance de `Widget` vue par le script
	script::Value onEvent; ///< rappel (clic, validation, changement)
	bool alive = true;
	// Dernières valeurs échangées : seul ce qui a changé d'un côté est recopié de l'autre.
	std::string text;
	double value = 0.0, max = 1.0;
	bool checked = false;
	int index = -1;
};

namespace {

struct WidgetRef {
	ScriptUi *owner = nullptr;
	size_t index = SIZE_MAX;
};

std::optional<sdl3::Color> HexColor(std::string_view s) {
	if (s.starts_with('#'))
		s.remove_prefix(1);
	if (s.size() != 6 && s.size() != 8)
		return std::nullopt;
	uint8_t c[4] = {0, 0, 0, 255};
	for (size_t k = 0; k < s.size() / 2; ++k) {
		int v = 0;
		for (size_t j = 0; j < 2; ++j) {
			const char ch = s[2 * k + j];
			const int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
			if (d < 0)
				return std::nullopt;
			v = v * 16 + d;
		}
		c[k] = uint8_t(v);
	}
	return sdl3::Color{c[0], c[1], c[2], c[3]};
}

Anchor AnchorOf(std::string_view a) {
	static constexpr std::pair<std::string_view, Anchor> kAnchors[] = {{"top_left", Anchor::TopLeft},	  {"top", Anchor::Top},		  {"top_right", Anchor::TopRight},
																	   {"left", Anchor::Left},			  {"center", Anchor::Center}, {"right", Anchor::Right},
																	   {"bottom_left", Anchor::BottomLeft}, {"bottom", Anchor::Bottom}, {"bottom_right", Anchor::BottomRight}};
	for (const auto &[name, value] : kAnchors)
		if (name == a)
			return value;
	return Anchor::TopLeft;
}

/// Options communes → style du widget.
void ApplyOptions(WidgetBuilder &b, const script::Value &options) {
	const script::Map *o = options.AsMap();
	if (!o)
		return;
	const auto get = [&](std::string_view key) -> const script::Value * {
		const auto it = o->find(key);
		return it == o->end() ? nullptr : &it->second;
	};
	if (const auto *v = get("w"); v && v->IsNumber())
		b.Width(Length::Px(float(v->AsNumber())));
	if (const auto *v = get("h"); v && v->IsNumber())
		b.Height(Length::Px(float(v->AsNumber())));
	if (const auto *v = get("gap"); v && v->IsNumber())
		b.Gap(float(v->AsNumber()));
	if (const auto *v = get("pad"); v && v->IsNumber())
		b.Padding(float(v->AsNumber()));
	if (const auto *v = get("bg"); v && v->IsString())
		if (const auto c = HexColor(v->AsString()))
			b.Background(*c);
	if (const auto *v = get("radius"); v && v->IsNumber())
		b.BorderRadius(math::Corners(float(v->AsNumber())));
	if (const auto *v = get("color"); v && v->IsString())
		if (const auto c = HexColor(v->AsString()))
			b.Color(*c);
	if (const auto *v = get("font_size"); v && v->IsNumber())
		b.FontSize(float(v->AsNumber()));
	if (const auto *v = get("bold"); v && v->Truthy())
		b.Bold();
	if (const auto *v = get("align"); v && v->IsString())
		b.AlignItems(v->AsString() == "center" ? Align::Center : v->AsString() == "end" ? Align::End : Align::Start);
	// Identifiant, espace extensible, visibilité et état actif.
	if (const auto *v = get("id"); v && v->IsString())
		b.Id(std::string(v->AsString()));
	if (const auto *v = get("grow"); v && (v->Truthy() || v->IsNumber()))
		b.Grow(v->IsNumber() ? float(v->AsNumber()) : 1.f);
	if (const auto *v = get("visible"); v && !v->Truthy())
		b.Hidden(true);
	if (const auto *v = get("disabled"); v && v->Truthy())
		b.Disabled(true);
	if (const auto *v = get("tooltip"); v && v->IsString())
		b.Tooltip(std::string(v->AsString()));
	if (const auto *v = get("icon"); v && v->IsNumber())
		b.Icon(char32_t(v->AsNumber()));
	// Ancre et décalage : placement absolu dans le parent ; le décalage passe par les marges du côté
	// de l'ancre (doublées pour une ancre centrée).
	if (const auto *v = get("anchor"); v && v->IsString()) {
		const Anchor a = AnchorOf(v->AsString());
		b.Position(Position::Absolute).Anchor(a);
		float dx = 0.f, dy = 0.f;
		if (const auto *off = get("offset"); off && off->AsList() && off->AsList()->size() >= 2) {
			dx = float((*off->AsList())[0].AsNumber());
			dy = float((*off->AsList())[1].AsNumber());
		}
		const int col = int(a) % 3, row = int(a) / 3; // 0 : début, 1 : centre, 2 : fin
		math::Sides m{};
		const auto axis = [](int where, float d, float &start, float &end) {
			if (where == 0)
				start = d;
			else if (where == 2)
				end = -d;
			else if (d >= 0.f)
				start = 2.f * d;
			else
				end = -2.f * d;
		};
		axis(col, dx, m.left, m.right);
		axis(row, dy, m.top, m.bottom);
		b.Margin(m);
	}
}

} // namespace

ScriptUi::ScriptUi(script::Interpreter &interp, Ui &ui, std::string_view module) : m_interp(interp), m_ui(ui) {
	using script::Value;
	using R = Result<Value, std::string>;
	// Classe native des poignées : méthodes qui agissent sur le nœud.
	const auto node = [](WidgetRef &w) -> Item * {
		if (!w.owner || w.index >= w.owner->m_items.size() || !w.owner->m_items[w.index]->alive)
			return nullptr;
		return w.owner->m_items[w.index].get();
	};
	m_widgetClass = script::ClassBinder<WidgetRef>(interp, "Widget")
						.Method("set_bg", [node](WidgetRef &w, std::string_view color) -> R {
							Item *it = node(w);
							const auto c = HexColor(color);
							if (!it || !c)
								return Err(std::string("set_bg : widget retiré ou couleur invalide"));
							it->node->style.backgroundColor = *c; // style recalculé à l'image suivante
							return Ok(Value());
						})
						.Method("set_color", [node](WidgetRef &w, std::string_view color) -> R {
							Item *it = node(w);
							const auto c = HexColor(color);
							if (!it || !c)
								return Err(std::string("set_color : widget retiré ou couleur invalide"));
							it->node->style.color = *c;
							return Ok(Value());
						})
						.Method("set_text", [node](WidgetRef &w, std::string_view text) -> R {
							Item *it = node(w);
							if (!it)
								return Err(std::string("set_text : widget retiré"));
							it->node->text = std::string(text);
							it->text = it->node->text; // évite un aller-retour à l'image suivante
							if (script::Instance *o = it->handle.AsInstance())
								o->fields.insert_or_assign("text", script::Value(it->text));
							return Ok(Value());
						})
						.Method("set_visible", [node](WidgetRef &w, bool on) -> R {
							Item *it = node(w);
							if (!it)
								return Err(std::string("set_visible : widget retiré"));
							it->node->hidden = !on;
							if (script::Instance *o = it->handle.AsInstance())
								o->fields.insert_or_assign("visible", script::Value(on));
							return Ok(Value());
						})
						.Method("set_enabled", [node](WidgetRef &w, bool on) -> R {
							Item *it = node(w);
							if (!it)
								return Err(std::string("set_enabled : widget retiré"));
							it->node->disabled = !on;
							if (script::Instance *o = it->handle.AsInstance())
								o->fields.insert_or_assign("enabled", script::Value(on));
							return Ok(Value());
						})
						.Method("set_value", [node](WidgetRef &w, double v) -> R {
							Item *it = node(w);
							if (!it)
								return Err(std::string("set_value : widget retiré"));
							it->node->value = float(v);
							if (script::Instance *o = it->handle.AsInstance())
								o->fields.insert_or_assign("value", script::Value(v));
							return Ok(Value());
						})
						.Method("set_checked", [node](WidgetRef &w, bool on) -> R {
							Item *it = node(w);
							if (!it)
								return Err(std::string("set_checked : widget retiré"));
							it->node->checked = it->checked = on;
							if (script::Instance *o = it->handle.AsInstance())
								o->fields.insert_or_assign("checked", script::Value(on));
							return Ok(Value());
						})
						.Method("set_index", [node](WidgetRef &w, double i) -> R {
							Item *it = node(w);
							if (!it)
								return Err(std::string("set_index : widget retiré"));
							it->node->selectedIndex = it->index = int(i);
							if (script::Instance *o = it->handle.AsInstance())
								o->fields.insert_or_assign("value", script::Value(i));
							return Ok(Value());
						})
						.Method("get_text", [node](WidgetRef &w) -> R {
							Item *it = node(w);
							if (!it)
								return Err(std::string("get_text : widget retiré"));
							return Ok(script::Value(it->node->text));
						})
						.Method("get_value", [node](WidgetRef &w) -> R {
							Item *it = node(w);
							if (!it)
								return Err(std::string("get_value : widget retiré"));
							return Ok(script::Value(double(it->node->value)));
						})
						.Method("get_checked", [node](WidgetRef &w) -> R {
							Item *it = node(w);
							if (!it)
								return Err(std::string("get_checked : widget retiré"));
							return Ok(script::Value(it->node->checked));
						})
						.Method("get_visible", [node](WidgetRef &w) -> R {
							Item *it = node(w);
							if (!it)
								return Err(std::string("get_visible : widget retiré"));
							return Ok(script::Value(!it->node->hidden));
						})
						.Method("remove", [](WidgetRef &w) -> R {
							if (w.owner)
								w.owner->Remove(w.index);
							return Ok(Value());
						})
						.Class();
	const auto opts = [](std::span<const Value> a, size_t i) { return i < a.size() ? a[i] : Value(); };
	const std::string mod(module);
	m_interp.RegisterModule(
		mod, {
				 {"column", [this, opts](script::Interpreter &, std::span<const Value> a) -> R { return Ok(Add("column", Column(), opts(a, 0), {})); }},
				 {"row", [this, opts](script::Interpreter &, std::span<const Value> a) -> R { return Ok(Add("row", Row(), opts(a, 0), {})); }},
				 {"label",
				  [this, opts](script::Interpreter &, std::span<const Value> a) -> R {
					  return Ok(Add("label", Text(a.empty() ? std::string() : a[0].ToString()), opts(a, 1), {}));
				  }},
				 {"button",
				  [this, opts](script::Interpreter &, std::span<const Value> a) -> R {
					  return Ok(Add("button", Button(a.empty() ? std::string() : a[0].ToString()), opts(a, 2), opts(a, 1)));
				  }},
				 {"input",
				  [this, opts](script::Interpreter &, std::span<const Value> a) -> R {
					  WidgetBuilder b = LineEdit();
					  if (!a.empty())
						  b.Placeholder(a[0].ToString());
					  return Ok(Add("input", std::move(b), opts(a, 2), opts(a, 1)));
				  }},
				 {"progress",
				  [this, opts](script::Interpreter &, std::span<const Value> a) -> R {
					  Value w = Add("progress", ProgressBar(0.f), opts(a, 2), {});
					  if (script::Instance *o = w.AsInstance()) {
						  o->fields.insert_or_assign("value", a.size() > 0 ? a[0] : Value(0.0));
						  o->fields.insert_or_assign("max", a.size() > 1 ? a[1] : Value(1.0));
					  }
					  return Ok(std::move(w));
				  }},
				 {"separator", [this, opts](script::Interpreter &, std::span<const Value> a) -> R { return Ok(Add("separator", Separator(), opts(a, 0), {})); }},
				 {"checkbox",
				  [this, opts](script::Interpreter &, std::span<const Value> a) -> R {
					  Value w = Add("checkbox", Checkbox(a.empty() ? std::string() : a[0].ToString()), opts(a, 3), opts(a, 2));
					  if (script::Instance *o = w.AsInstance())
						  o->fields.insert_or_assign("checked", Value(a.size() > 1 && a[1].Truthy()));
					  return Ok(std::move(w));
				  }},
				 {"slider",
				  [this, opts](script::Interpreter &, std::span<const Value> a) -> R {
					  const float lo = a.size() > 0 ? float(a[0].AsNumber()) : 0.f, hi = a.size() > 1 ? float(a[1].AsNumber(1.0)) : 1.f;
					  Value w = Add("slider", Slider(lo, hi), opts(a, 4), opts(a, 3));
					  if (script::Instance *o = w.AsInstance())
						  o->fields.insert_or_assign("value", a.size() > 2 ? a[2] : Value(double(lo)));
					  return Ok(std::move(w));
				  }},
				 {"choice",
				  [this, opts](script::Interpreter &, std::span<const Value> a) -> R {
					  std::vector<std::string> items;
					  if (const script::List *l = a.empty() ? nullptr : a[0].AsList())
						  for (const Value &v : *l)
							  items.push_back(v.ToString());
					  WidgetBuilder b = ComboBox();
					  b.Options(items);
					  Value w = Add("choice", std::move(b), opts(a, 3), opts(a, 2));
					  if (script::Instance *o = w.AsInstance())
						  o->fields.insert_or_assign("value", a.size() > 1 ? a[1] : Value(0.0));
					  return Ok(std::move(w));
				  }},
				 {"textarea",
				  [this, opts](script::Interpreter &, std::span<const Value> a) -> R {
					  WidgetBuilder b = TextArea();
					  if (!a.empty())
						  b.Placeholder(a[0].ToString());
					  return Ok(Add("textarea", std::move(b), opts(a, 2), opts(a, 1)));
				  }},
				 {"image",
				  [this, opts](script::Interpreter &, std::span<const Value> a) -> R {
					  const char32_t icon = a.empty() ? char32_t(0) : char32_t(a[0].AsNumber());
					  return Ok(Add("image", ImageView(icon), opts(a, 1), {}));
				  }},
				 {"toggle",
				  [this, opts](script::Interpreter &, std::span<const Value> a) -> R {
					  Value w = Add("toggle", Switch(a.empty() ? std::string() : a[0].ToString()), opts(a, 3), opts(a, 2));
					  if (script::Instance *o = w.AsInstance())
						  o->fields.insert_or_assign("checked", Value(a.size() > 1 && a[1].Truthy()));
					  return Ok(std::move(w));
				  }},
				 {"selectable",
				  [this, opts](script::Interpreter &, std::span<const Value> a) -> R {
					  return Ok(Add("selectable", Selectable(a.empty() ? std::string() : a[0].ToString()), opts(a, 2), opts(a, 1)));
				  }},
				 {"heading",
				  [this, opts](script::Interpreter &, std::span<const Value> a) -> R {
					  return Ok(Add("heading", Heading(a.empty() ? std::string() : a[0].ToString()), opts(a, 1), {}));
				  }},
				 {"badge",
				  [this, opts](script::Interpreter &, std::span<const Value> a) -> R {
					  Color c = sdl3::Rgba(74, 144, 255);
					  if (a.size() > 1 && a[1].IsString())
						  if (const auto hc = HexColor(a[1].AsString()))
							  c = *hc;
					  return Ok(Add("badge", Badge(a.empty() ? std::string() : a[0].ToString(), c), opts(a, 2), {}));
				  }},
				 {"richtext",
				  [this, opts](script::Interpreter &, std::span<const Value> a) -> R {
					  return Ok(Add("richtext", RichTextLabel(a.empty() ? std::string() : a[0].ToString()), opts(a, 1), {}));
				  }},
				 {"remove",
				  [](script::Interpreter &, std::span<const Value> a) -> R {
					  if (!a.empty())
						  if (const script::Instance *o = a[0].AsInstance())
							  for (const auto &[native, state] : o->states)
								  if (native->name == "Widget" && state)
									  if (auto *w = static_cast<WidgetRef *>(state.get()); w->owner)
										  w->owner->Remove(w->index);
					  return Ok(Value());
				  }},
				 {"clear",
				  [this](script::Interpreter &, std::span<const Value>) -> R {
					  Clear();
					  return Ok(Value());
				  }},
			 });
}

ScriptUi::~ScriptUi() { Clear(); }

Node *ScriptUi::RootNode() {
	Widget root = m_ui.Find("script-ui-root");
	if (!root) { // racine plein écran, transparente, partagée
		m_ui.Root(Box().Id("script-ui-root").Width(Length::Pct(100.f)).Height(Length::Vh(100.f))); // hauteur : celle de la vue
		root = m_ui.Find("script-ui-root");
	}
	return root.Get();
}

script::Value ScriptUi::Add(std::string kind, WidgetBuilder builder, const script::Value &options, script::Value onEvent) {
	ApplyOptions(builder, options);
	Node *parent = RootNode();
	if (const script::Map *o = options.AsMap())
		if (const auto it = o->find("parent"); it != o->end())
			if (const script::Instance *p = it->second.AsInstance())
				for (const auto &[native, state] : p->states)
					if (native == m_widgetClass.get() && state)
						if (const auto *w = static_cast<WidgetRef *>(state.get()); w->owner == this && w->index < m_items.size() && m_items[w->index]->alive)
							parent = m_items[w->index]->node.get();
	auto item = std::make_unique<Item>();
	item->kind = std::move(kind);
	item->node = builder.Build();
	item->onEvent = std::move(onEvent);
	item->node->parent = parent;
	parent->children.push_back(item->node);
	const size_t index = m_items.size();
	auto handle = m_interp.Instantiate(script::Value(m_widgetClass), {}, [this, index](script::Instance &o) {
		o.SetState(m_widgetClass.get(), std::make_shared<WidgetRef>(WidgetRef{this, index}));
	});
	item->handle = handle.IsOk() ? handle.Value() : script::Value();
	if (script::Instance *o = item->handle.AsInstance()) {
		item->text = item->node->text;
		o->fields.insert_or_assign("text", script::Value(item->text));
		o->fields.insert_or_assign("visible", script::Value(true));
		o->fields.insert_or_assign("kind", script::Value(item->kind));
	}
	// Évènements du widget : différés jusqu'à `Collect` (jamais pendant le dessin).
	Item *raw = item.get();
	if (raw->kind == "button")
		raw->node->onClick = [this, raw] {
			if (raw->onEvent.IsCallable())
				m_pending.push_back({raw->onEvent, {raw->handle}});
		};
	if (raw->kind == "selectable")
		raw->node->onClick = [this, raw] {
			if (raw->onEvent.IsCallable())
				m_pending.push_back({raw->onEvent, {raw->handle}});
		};
	if (raw->kind == "input")
		raw->node->onSubmit = [this, raw] {
			if (raw->onEvent.IsCallable())
				m_pending.push_back({raw->onEvent, {raw->handle, script::Value(raw->node->text)}});
		};
	m_items.push_back(std::move(item));
	return m_items.back()->handle;
}

void ScriptUi::Remove(size_t index) {
	if (index >= m_items.size() || !m_items[index]->alive)
		return;
	Node *gone = m_items[index]->node.get();
	if (Node *parent = gone->parent)
		std::erase_if(parent->children, [&](const NodePtr &n) { return n.get() == gone; });
	for (auto &it : m_items) // le widget et ses descendants
		for (const Node *n = it->node.get(); n; n = n->parent)
			if (n == gone) {
				it->alive = false;
				break;
			}
}

void ScriptUi::Clear() {
	for (size_t i = 0; i < m_items.size(); ++i)
		Remove(i);
	m_pending.clear();
}

void ScriptUi::Sync() {
	for (auto &it : m_items) {
		if (!it->alive)
			continue;
		script::Instance *o = it->handle.AsInstance();
		if (!o)
			continue;
		Node &n = *it->node;
		const auto field = [&](std::string_view key) -> const script::Value * {
			const auto f = o->fields.find(key);
			return f == o->fields.end() ? nullptr : &f->second;
		};
		if (const auto *v = field("text"); v && v->ToString() != it->text) { // modifié par le script
			it->text = v->ToString();
			n.text = it->text;
		}
		if (const auto *v = field("visible"))
			n.hidden = !v->Truthy();
		if (const auto *v = field("enabled"))
			n.disabled = !v->Truthy();
		if (it->kind == "progress") {
			const double value = field("value") ? field("value")->AsNumber() : 0.0, max = field("max") ? field("max")->AsNumber(1.0) : 1.0;
			n.value = float(max > 0.0 ? std::clamp(value / max, 0.0, 1.0) : 0.0);
		} else if (it->kind == "slider") {
			if (const auto *v = field("value"); v && v->AsNumber() != it->value)
				n.value = float(it->value = v->AsNumber());
		} else if (it->kind == "checkbox" || it->kind == "toggle") {
			if (const auto *v = field("checked"); v && v->Truthy() != it->checked)
				n.checked = it->checked = v->Truthy();
		} else if (it->kind == "choice") {
			if (const auto *v = field("value"); v && v->AsInt() != it->index)
				n.selectedIndex = it->index = v->AsInt();
		}
	}
}

void ScriptUi::Collect() {
	for (auto &it : m_items) {
		if (!it->alive)
			continue;
		script::Instance *o = it->handle.AsInstance();
		if (!o)
			continue;
		const Node &n = *it->node;
		if (n.text != it->text) { // saisie de l'utilisateur
			it->text = n.text;
			o->fields.insert_or_assign("text", script::Value(it->text));
		}
		const auto changed = [&](script::Value v) {
			if (it->onEvent.IsCallable())
				m_pending.push_back({it->onEvent, {it->handle, std::move(v)}});
		};
		if (it->kind == "slider" && double(n.value) != it->value) {
			it->value = n.value;
			o->fields.insert_or_assign("value", script::Value(it->value));
			changed(script::Value(it->value));
		} else if ((it->kind == "checkbox" || it->kind == "toggle") && n.checked != it->checked) {
			it->checked = n.checked;
			o->fields.insert_or_assign("checked", script::Value(it->checked));
			changed(script::Value(it->checked));
		} else if (it->kind == "choice" && n.selectedIndex != it->index) {
			it->index = n.selectedIndex;
			o->fields.insert_or_assign("value", script::Value(it->index));
			changed(script::Value(it->index));
		}
	}
	std::vector<std::pair<script::Value, std::vector<script::Value>>> calls;
	calls.swap(m_pending);
	for (auto &[fn, args] : calls)
		if (auto r = m_interp.Call(fn, args); r.IsError())
			m_errors.push_back(r.Error().Format());
}

} // namespace ui
