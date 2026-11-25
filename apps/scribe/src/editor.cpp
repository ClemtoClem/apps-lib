#include "scribe/editor.hpp"
#include "scribe/i18n.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <atomic>
#include <mutex>
#include <thread>

#include <document/persistence.hpp>
#include <document/tables.hpp>
#include <sdl3/render/render.hpp>

namespace scribe {

using namespace doc;

namespace {

constexpr double kCellPadding = 4;
constexpr double kCellBorder = 0.5;
const Color kGridColor = Color(150, 150, 150);
const Color kShapeFill = Color(214, 228, 245);
const Color kShapeStroke = Color(60, 90, 140);
const Color kDrawingBorder = Color(190, 196, 208);

bool IsContainer(NodeType t) {
	return t == NodeType::Body || t == NodeType::TableCell || t == NodeType::Header || t == NodeType::Footer;
}

} // namespace

struct Editor::LayoutShared {
	std::mutex mutex; ///< une mise en page à la fois (le cache n'est pas synchronisé)
	std::atomic<std::thread::id> owner{}; ///< thread qui détient `mutex`
	LayoutCache cache;
};

namespace {

/// Accès au cache partagé des mises en page. Un thread qui attend dans `jobs::ParallelFor`
/// (pré-mesure de la mise en page) exécute des tâches en file : il peut ainsi lancer une autre
/// mise en page du même éditeur alors qu'il détient déjà le verrou. Celle-ci se fait alors avec
/// un cache temporaire (même résultat, plus lente) au lieu de se bloquer elle-même.
template <typename Shared> class LayoutAccess {
public:
	explicit LayoutAccess(Shared &shared) : m_shared(shared) {
		if (shared.owner.load(std::memory_order_relaxed) == std::this_thread::get_id())
			return; // imbriquée : seul ce thread écrit son propre identifiant
		shared.mutex.lock();
		shared.owner.store(std::this_thread::get_id(), std::memory_order_relaxed);
		m_locked = true;
	}
	~LayoutAccess() {
		if (m_locked) {
			m_shared.owner.store(std::thread::id{}, std::memory_order_relaxed);
			m_shared.mutex.unlock();
		}
	}
	LayoutAccess(const LayoutAccess &) = delete;
	LayoutAccess &operator=(const LayoutAccess &) = delete;
	[[nodiscard]] LayoutCache *Cache() noexcept { return m_locked ? &m_shared.cache : &m_local; }

private:
	Shared &m_shared;
	bool m_locked = false;
	LayoutCache m_local;
};

} // namespace

Editor::Editor(jobs::ThreadPool *pool) : m_pool(pool ? *pool : jobs::ThreadPool::Global()), m_layoutShared(std::make_shared<LayoutShared>()) {
	m_render.paper = true;
	New();
}

Editor::~Editor() {
	// Les tâches de fond lisent des instantanés et l'état partagé qu'elles détiennent, mais aussi
	// des tables globales du module document (catalogue des propriétés) : on attend qu'elles se
	// terminent, sinon une mise en page encore en vol à la sortie du programme lirait ces tables
	// pendant leur destruction. Leurs résultats sont ignorés.
	m_layout.Cancel();
	m_task.Cancel();
	m_fontScan.Cancel();
	m_layout.Wait();
	m_task.Wait();
	m_fontScan.Wait();
}

// ── Fichiers ──

void Editor::Reset(std::unique_ptr<doc::Document> document, std::filesystem::path path) {
	m_document = std::move(document);
	m_history = std::make_unique<doc::History>(*m_document);
	m_selection.Clear();
	m_caret = m_anchor = {};
	m_order.reset();
	++m_caretRevision;
	m_path = std::move(path);
	m_name.clear();
	++m_documentId;
	m_presentation.reset();
	m_requestedRevision = UINT64_MAX;
	RequestLayout();
}

void Editor::New() {
	auto d = std::make_unique<doc::Document>();
	const NodeId body = d->Body(d->Sections().front());
	const NodeId p = d->CreateNode(NodeType::Paragraph, body).Value();
	const NodeId t = d->CreateNode(NodeType::Text, p).Value();
	Reset(std::move(d), {});
	PlaceCaret({t, 0}); // prêt à écrire
}

Status Editor::Open(const std::filesystem::path &path) {
	auto loaded = LoadDocument(path);
	if (!loaded)
		return Err(loaded.Error());
	Reset(std::make_unique<doc::Document>(std::move(loaded.Value())), path);
	return Success();
}

Status Editor::NewFromFile(const std::filesystem::path &path) {
	auto loaded = LoadDocument(path);
	if (!loaded)
		return Err(loaded.Error());
	Reset(std::make_unique<doc::Document>(std::move(loaded.Value())), {});
	CaretAtStart();
	return Success();
}

Status Editor::OpenDraft(const std::filesystem::path &file, const std::filesystem::path &source, std::string name) {
	auto loaded = LoadDocument(file);
	if (!loaded)
		return Err(loaded.Error());
	Reset(std::make_unique<doc::Document>(std::move(loaded.Value())), source);
	m_name = std::move(name);
	m_history->MarkCleanAt(UINT64_MAX); // aucun état de l'historique ne correspond au disque
	CaretAtStart();
	return Success();
}

void Editor::CaretAtStart() {
	NodeId first = kNoNode;
	for (NodeId section : m_document->Sections())
		m_document->Visit(m_document->Body(section), [&](const Node &n) {
			if (first == kNoNode && n.Type() == NodeType::Text && Order().Contains(n.Id()))
				first = n.Id();
		});
	if (first != kNoNode)
		PlaceCaret({first, 0});
}

void Editor::OpenAsync(const std::filesystem::path &path) {
	m_task.Request(m_pool, [path](std::stop_token) {
		Task t;
		auto loaded = LoadDocument(path);
		if (!loaded) {
			t.ok = false;
			t.message = "ouverture impossible : {}";
			t.argument = loaded.Error().message;
			return t;
		}
		t.opened = std::make_shared<doc::Document>(std::move(loaded.Value()));
		t.path = path;
		t.message = "ouvert : {}";
			t.argument = path.filename().string();
		return t;
	});
}

Status Editor::Save(const std::filesystem::path &path) {
	if (auto s = SaveDocument(*m_document, path); !s)
		return s;
	m_path = path;
	m_history->MarkClean();
	return Success();
}

void Editor::SaveAsync(const std::filesystem::path &path) {
	const Snapshot snap = m_document->TakeSnapshot();
	const uint64_t state = m_history->StateId();
	m_task.Request(m_pool, [snap, state, path, id = m_documentId](std::stop_token) {
		Task t;
		t.save = true;
		t.path = path;
		t.cleanState = state;
		t.documentId = id;
		if (auto s = SaveDocument(*snap, path); !s) {
			t.ok = false;
			t.message = "enregistrement impossible : {}";
			t.argument = s.Error().message;
		} else {
			t.message = "enregistré : {}";
			t.argument = path.filename().string();
		}
		return t;
	});
}

namespace {

Status ExportSnapshot(const DocumentView &view, NodeId table, ExportKind kind, const std::filesystem::path &path,
					  const std::shared_ptr<const FontLibrary> &fonts) {
	LayoutOptions options;
	options.fonts = fonts; // mêmes polices qu'à l'écran : polices installées incorporées au PDF
	switch (kind) {
	case ExportKind::Pdf: return SavePdf(view, Layout(view, nullptr, options), path);
	case ExportKind::Html: return SaveHtml(view, path);
	case ExportKind::Csv:
		if (table == kNoNode)
			return Failure(ErrorCode::InvalidValue, "sélectionner un tableau à exporter");
		return SaveCsv(view, table, path);
	}
	return Failure(ErrorCode::Unsupported, "format inconnu");
}

} // namespace

Status Editor::Export(ExportKind kind, const std::filesystem::path &path) {
	return ExportSnapshot(*m_document, Context().enclosingTable, kind, path, m_fonts);
}

void Editor::ExportAsync(ExportKind kind, const std::filesystem::path &path) {
	const Snapshot snap = m_document->TakeSnapshot();
	const NodeId table = Context().enclosingTable;
	m_task.Request(m_pool, [snap, table, kind, path, fonts = m_fonts](std::stop_token) {
		Task t;
		if (auto s = ExportSnapshot(*snap, table, kind, path, fonts); !s) {
			t.ok = false;
			t.message = "export impossible : {}";
			t.argument = s.Error().message;
		} else {
			t.message = "exporté : {}";
			t.argument = path.filename().string();
		}
		return t;
	});
}

std::string Editor::Name() const {
	if (!m_path.empty())
		return m_path.filename().string();
	return m_name.empty() ? Tr("Sans titre") : m_name;
}

std::string Editor::Title() const {
	const std::string name = Name();
	return Dirty() ? name + " •" : name;
}

// ── Mise en page ──

void Editor::RequestLayout() {
	const Snapshot snap = m_document->TakeSnapshot();
	m_requestedRevision = snap->Revision();
	m_requestedStamp = snap->Stamp();
	const RenderOptions render = m_render;
	LayoutOptions options;
	options.fonts = m_fonts;
	m_layout.Request(m_pool, [snap, shared = m_layoutShared, render, options](std::stop_token) {
		LayoutAccess access(*shared);
		Presentation p;
		p.snapshot = snap;
		p.layout = Layout(*snap, access.Cache(), options);
		p.pages = RenderDocument(*snap, p.layout, render);
		return p;
	});
}

void Editor::LayoutNow() {
	m_layout.Cancel(); // une mise en page de fond encore en file serait inutile
	LayoutAccess access(*m_layoutShared);
	for (int pass = 0; pass < 4; ++pass) { // pages créées : seconde passe pour leurs identifiants
		Presentation p;
		p.snapshot = m_document->TakeSnapshot();
		LayoutOptions options;
		options.fonts = m_fonts;
		p.layout = Layout(*p.snapshot, access.Cache(), options);
		const size_t changes = SyncPages(*m_document, p.layout);
		if (changes == 0) {
			p.pages = RenderDocument(*p.snapshot, p.layout, m_render);
			m_presentation = std::move(p);
			m_requestedRevision = m_document->Revision();
			m_requestedStamp = m_document->Stamp();
			return;
		}
	}
}

bool Editor::Update() {
	bool changed = false;
	if (m_fontScan.Poll() && *m_fontScan.Get()) {
		SetFonts(*m_fontScan.Get());
		m_messages.push_back(TrF("{} polices installées disponibles", m_fonts->InstalledCount()));
	}
	if (m_task.Poll()) {
		Task &t = *m_task.Get();
		if (!t.message.empty()) // motif traduit ici, sur le fil de l'interface
			m_messages.push_back(TrArg(t.message, t.argument));
		if (t.ok && t.save && t.documentId == m_documentId) {
			m_path = t.path;
			m_history->MarkCleanAt(t.cleanState);
		}
		if (t.ok && t.opened) {
			Reset(std::make_unique<doc::Document>(std::move(*t.opened)), t.path);
			changed = true;
		}
	}
	if (m_layout.Poll()) {
		Presentation &p = *m_layout.Get();
		if (p.snapshot->Stamp() == m_document->Stamp()) { // pas un document remplacé entre-temps
			SyncPages(*m_document, p.layout);
			m_presentation = std::move(p);
			changed = true;
		}
	}
	if (m_document->Revision() != m_requestedRevision || m_document->Stamp() != m_requestedStamp)
		RequestLayout();
	return changed;
}

bool Editor::Busy() const noexcept { return m_layout.Loading() || m_task.Loading(); }

std::vector<std::string> Editor::TakeMessages() { return std::exchange(m_messages, {}); }

void Editor::SetShowBlockBounds(bool on) {
	if (m_render.blockBounds == on)
		return;
	m_render.blockBounds = on;
	RequestLayout();
}

// ── Commandes ──

void Editor::Changed() {
	m_selection.Prune(*m_document);
	if (m_caret.Valid()) { // le curseur suit le document (segment supprimé : édition quittée)
		const TextOrder &order = Order();
		m_caret = order.Normalize(m_caret);
		m_anchor = order.Normalize(m_anchor);
		if (!m_anchor.Valid())
			m_anchor = m_caret;
		++m_caretRevision;
	}
}

Status Editor::Run(CommandPtr command) {
	auto s = m_history->Execute(std::move(command));
	Changed();
	return s;
}

Status Editor::Undo() {
	auto s = m_history->Undo();
	Changed();
	return s;
}

Status Editor::Redo() {
	auto s = m_history->Redo();
	Changed();
	return s;
}

Status Editor::UndoTo(size_t position) {
	auto s = m_history->JumpTo(position);
	Changed();
	return s;
}

NodeId Editor::Create(CommandPtr command) {
	Command *raw = command.get();
	if (!m_history->Execute(std::move(command)))
		return kNoNode;
	return raw->Created();
}

Status Editor::Transaction(std::string name, const std::function<Status()> &body) {
	if (m_history->InTransaction()) // dans une transaction plus large : c'est elle qui valide ou annule
		return body();
	m_history->BeginTransaction(std::move(name));
	if (auto s = body(); !s) {
		(void)m_history->Rollback();
		Changed();
		return s;
	}
	auto s = m_history->Commit();
	Changed();
	return s;
}

const Editor::Memo &Editor::Derived() const {
	Memo &m = m_memo;
	if (m.stamp == m_document->Stamp() && m.revision == m_document->Revision() && m.selection == m_selection.Revision() &&
		m.caret == m_caretRevision)
		return m;
	m.stamp = m_document->Stamp();
	m.revision = m_document->Revision();
	m.selection = m_selection.Revision();
	m.caret = m_caretRevision;
	m.context = m_selection.Context(*m_document);
	m.texts.clear();
	m.paragraphs.clear();
	m.flags.clear();
	const auto add = [](std::vector<NodeId> &out, NodeId id) {
		if (std::ranges::find(out, id) == out.end())
			out.push_back(id);
	};
	if (HasTextRange()) { // plage de texte : ses segments et leurs paragraphes
		for (NodeId t : RangeTexts()) {
			add(m.texts, t);
			add(m.paragraphs, m_document->Find(t)->Parent());
		}
		return m;
	}
	for (NodeId id : m_selection.Nodes()) {
		const Node *n = m_document->Find(id);
		if (!n)
			continue;
		if (n->Type() == NodeType::Text) {
			add(m.texts, id);
			add(m.paragraphs, n->Parent());
			continue;
		}
		m_document->Visit(id, [&](const Node &d) {
			if (d.Type() == NodeType::Text)
				add(m.texts, d.Id());
			else if (d.Type() == NodeType::Paragraph)
				add(m.paragraphs, d.Id());
		});
	}
	return m;
}

const SelectionContext &Editor::Context() const { return Derived().context; }
const std::vector<NodeId> &Editor::TextTargets() const { return Derived().texts; }
const std::vector<NodeId> &Editor::ParagraphTargets() const { return Derived().paragraphs; }

bool Editor::TextFlag(PropertyId flag) const {
	const Memo &m = Derived();
	if (const auto it = std::ranges::find(m.flags, flag, &std::pair<PropertyId, bool>::first); it != m.flags.end())
		return it->second;
	const std::vector<NodeId> &targets = m.texts.empty() ? m.paragraphs : m.texts;
	const bool all = !targets.empty() &&
					 std::ranges::all_of(targets, [&](NodeId id) { return std::get<bool>(m_document->Resolve(id, flag).value); });
	m_memo.flags.emplace_back(flag, all);
	return all;
}

Status Editor::ToggleText(PropertyId flag) {
	if (Info(flag).type != PropertyType::Bool)
		return Failure(ErrorCode::TypeMismatch, "propriété non booléenne");
	return SetText(flag, !TextFlag(flag));
}

Status Editor::SetText(PropertyId property, PropertyValue value) {
	if (HasTextRange()) { // plage de texte : segments coupés aux bornes, mise en forme de la plage
		auto styled = text::ApplyStyle(*m_history, m_anchor, m_caret, property, std::move(value));
		Changed();
		if (!styled)
			return Err(styled.Error());
		const bool forward = Order().Compare(m_anchor, m_caret) <= 0;
		m_anchor = forward ? styled.Value().first : styled.Value().second;
		m_caret = forward ? styled.Value().second : styled.Value().first;
		++m_caretRevision;
		return Success();
	}
	// Copie : les commandes exécutées ci-dessous invalident les valeurs mémorisées.
	std::vector<NodeId> targets = TextTargets();
	if (targets.empty()) // paragraphes vides : la valeur sera héritée par leur futur texte
		targets = ParagraphTargets();
	if (targets.empty())
		return Failure(ErrorCode::Nothing, "aucun texte sélectionné");
	return Transaction(std::format("Texte : {}", Info(property).name), [&]() -> Status {
		for (NodeId id : targets)
			if (auto s = m_history->Execute(doc::SetProperty(id, property, value)); !s)
				return s;
		return Success();
	});
}

Status Editor::SetParagraph(PropertyId property, PropertyValue value) {
	const std::vector<NodeId> targets = ParagraphTargets(); // copie (voir SetText)
	if (targets.empty())
		return Failure(ErrorCode::Nothing, "aucun paragraphe sélectionné");
	return Transaction(std::format("Paragraphe : {}", Info(property).name), [&]() -> Status {
		for (NodeId id : targets)
			if (auto s = m_history->Execute(doc::SetProperty(id, property, value)); !s)
				return s;
		return Success();
	});
}

Status Editor::EditText(NodeId text, std::string value) { return Run(doc::SetProperty(text, props::Text, value)); }

// ── Insertion ──

NodeId Editor::InsertionPoint(size_t &position, bool bodyOnly) const {
	for (NodeId id = m_selection.Primary(); id != kNoNode;) {
		const Node *n = m_document->Find(id);
		if (!n)
			break;
		const Node *parent = m_document->Find(n->Parent());
		if (parent && IsContainer(parent->Type()) && (!bodyOnly || parent->Type() == NodeType::Body)) {
			position = parent->IndexOfChild(id) + 1;
			return parent->Id();
		}
		id = n->Parent();
	}
	const NodeId body = m_document->Body(m_document->Sections().front());
	position = kAppend;
	return body;
}

NodeId Editor::NewParagraph(NodeId parent, size_t position, std::string_view text) {
	const NodeId p = Create(doc::CreateNode(NodeType::Paragraph, parent, position));
	if (p == kNoNode)
		return kNoNode;
	const NodeId t = Create(doc::CreateNode(NodeType::Text, p));
	if (t != kNoNode && !text.empty())
		(void)m_history->Execute(doc::SetProperty(t, props::Text, text));
	return t;
}

Status Editor::InsertParagraph(std::string_view text) {
	size_t position = 0;
	const NodeId parent = InsertionPoint(position);
	NodeId created = kNoNode;
	auto s = Transaction("Insérer un paragraphe", [&]() -> Status {
		created = NewParagraph(parent, position, text);
		return created == kNoNode ? Failure(ErrorCode::InvalidParent, "insertion impossible ici") : Success();
	});
	if (s) { // `created` est le segment de texte du paragraphe : frappe directe dedans
		m_selection.Select(*m_document, created);
		PlaceCaret({created, text.size()});
	}
	return s;
}

Status Editor::InsertTable(int rows, int columns) {
	if (rows < 1 || columns < 1)
		return Failure(ErrorCode::InvalidValue, "dimensions de tableau invalides");
	size_t position = 0;
	const NodeId parent = InsertionPoint(position);
	PropertyMap cell; // marges intérieures et quadrillage fin
	for (PropertyId p : {PropertyId::PaddingTop, PropertyId::PaddingRight, PropertyId::PaddingBottom, PropertyId::PaddingLeft})
		cell.Set(p, kCellPadding);
	cell.Set(PropertyId::BorderWidth, kCellBorder);
	cell.Set(PropertyId::BorderColor, kGridColor);
	auto created = table::Create(*m_history, parent, position, size_t(rows), size_t(columns), cell);
	Changed();
	if (!created)
		return Err(created.Error());
	const TableGrid g = TableGrid::Of(*m_document, created.Value());
	SelectCell(g.rows.front(), 0);
	if (const NodeId t = m_selection.Primary(); m_document->Find(t)->Type() == NodeType::Text)
		PlaceCaret({t, 0}); // frappe directe dans la première cellule
	return Success();
}

Status Editor::InsertImage(ImageResource image) {
	if (image.width == 0 || image.height == 0 || image.rgba.size() != size_t(image.width) * image.height * 4)
		return Failure(ErrorCode::InvalidValue, "image invalide");
	// La ressource n'est pas annulable : elle reste dans le document (non référencée) si
	// l'insertion est annulée.
	const ResourceId res = m_document->AddResource(std::move(image));
	size_t position = 0;
	double offset = 0;
	const NodeId parent = FreeInsertionPoint(position, offset); // zone de dessin : placement libre
	NodeId created = kNoNode;
	auto s = Transaction("Insérer une image", [&]() -> Status {
		created = Create(doc::CreateNode(NodeType::Image, parent, position));
		if (created == kNoNode)
			return Failure(ErrorCode::InvalidParent, "une image ne peut pas être insérée ici");
		std::vector<CommandPtr> props;
		props.push_back(doc::SetProperty(created, props::Resource, int64_t(res)));
		PlaceFreely(created, offset, props);
		for (CommandPtr &c : props)
			if (auto r = m_history->Execute(std::move(c)); !r)
				return r;
		return Success();
	});
	if (s)
		m_selection.Select(*m_document, created);
	return s;
}

Status Editor::InsertImageFile(const std::filesystem::path &path) {
	auto surface = sdl3::Surface::CreateFromFile(path.string().c_str());
	if (!surface)
		return Failure(ErrorCode::Io, std::format("image illisible : {}", path.string()));
	ImageResource img;
	img.width = uint32_t(surface.Value().GetWidth());
	img.height = uint32_t(surface.Value().GetHeight());
	img.rgba = surface.Value().ToRgba8();
	return InsertImage(std::move(img));
}

NodeId Editor::DrawingOfSelection() const {
	for (NodeId id = m_selection.Primary(); id != kNoNode;) {
		const Node *n = m_document->Find(id);
		if (!n)
			break;
		if (n->Type() == NodeType::Drawing)
			return id;
		id = n->Parent();
	}
	return kNoNode;
}

namespace {

/// Taille d'une nouvelle forme : trait, forme « carrée » (étoile, polygones réguliers, cœur…),
/// flèche verticale ou horizontale, rectangle.
std::pair<double, double> DefaultShapeSize(ShapeKind kind) {
	switch (kind) {
	case ShapeKind::Line:
	case ShapeKind::ArrowLine: return {160, 0};
	case ShapeKind::Pentagon:
	case ShapeKind::Hexagon:
	case ShapeKind::Octagon:
	case ShapeKind::Star5:
	case ShapeKind::Star6:
	case ShapeKind::Cross:
	case ShapeKind::Heart:
	case ShapeKind::Diamond:
	case ShapeKind::Triangle:
	case ShapeKind::RightTriangle: return {100, 100};
	case ShapeKind::ArrowUp:
	case ShapeKind::ArrowDown:
	case ShapeKind::Lightning: return {70, 120};
	case ShapeKind::ArrowRight:
	case ShapeKind::ArrowLeft:
	case ShapeKind::DoubleArrow:
	case ShapeKind::Chevron: return {140, 70};
	default: return {160, 80};
	}
}

} // namespace

/// Parent d'un nouvel élément placé librement (forme, image, zone de texte) : la zone de dessin
/// de la sélection, sinon le flux. `offset` : position dans la zone (décalée d'un élément à
/// l'autre), négatif dans le flux.
NodeId Editor::FreeInsertionPoint(size_t &position, double &offset) const {
	offset = -1;
	if (const NodeId drawing = DrawingOfSelection(); drawing != kNoNode) {
		position = kAppend;
		offset = 20 + 16 * double(m_document->Find(drawing)->Children().size() % 8);
		return drawing;
	}
	return InsertionPoint(position);
}

/// Élément d'une zone de dessin : alignement « aucun », position `offset`.
void Editor::PlaceFreely(NodeId created, double offset, std::vector<CommandPtr> &props) const {
	if (offset < 0)
		return;
	props.push_back(doc::SetProperty(created, props::HAlign, HorizontalAlign::None));
	props.push_back(doc::SetProperty(created, props::PositionX, offset));
	props.push_back(doc::SetProperty(created, props::PositionY, offset));
}

Status Editor::InsertShape(ShapeKind kind) {
	NodeId created = kNoNode;
	auto s = Transaction("Insérer une forme", [&]() -> Status {
		size_t position = 0;
		double offset = 0;
		const NodeId parent = FreeInsertionPoint(position, offset);
		created = Create(doc::CreateNode(NodeType::Shape, parent, position));
		if (created == kNoNode)
			return Failure(ErrorCode::InvalidParent, "une forme ne peut pas être insérée ici");
		const auto [w, h] = DefaultShapeSize(kind);
		std::vector<CommandPtr> props;
		PlaceFreely(created, offset, props);
		props.push_back(doc::SetProperty(created, props::Shape, kind));
		props.push_back(doc::SetProperty(created, props::Width, w));
		props.push_back(doc::SetProperty(created, props::Height, h));
		props.push_back(doc::SetProperty(created, props::BorderWidth, 1.0));
		props.push_back(doc::SetProperty(created, props::BorderColor, kShapeStroke));
		if (!IsLineShape(kind))
			props.push_back(doc::SetProperty(created, props::BackgroundColor, kShapeFill));
		for (CommandPtr &c : props)
			if (auto r = m_history->Execute(std::move(c)); !r)
				return r;
		return Success();
	});
	if (s)
		m_selection.Select(*m_document, created);
	return s;
}

Status Editor::InsertDrawing() {
	size_t position = 0;
	const NodeId parent = InsertionPoint(position);
	NodeId created = kNoNode;
	auto s = Transaction("Insérer une zone de dessin", [&]() -> Status {
		created = Create(doc::CreateNode(NodeType::Drawing, parent, position));
		if (created == kNoNode)
			return Failure(ErrorCode::InvalidParent, "une zone de dessin ne peut pas être insérée ici");
		std::vector<CommandPtr> props;
		props.push_back(doc::SetProperty(created, props::Height, 200.0));
		props.push_back(doc::SetProperty(created, props::BorderWidth, 0.75));
		props.push_back(doc::SetProperty(created, props::BorderColor, kDrawingBorder));
		for (CommandPtr &c : props)
			if (auto r = m_history->Execute(std::move(c)); !r)
				return r;
		return Success();
	});
	if (s)
		m_selection.Select(*m_document, created);
	return s;
}

Status Editor::InsertTextBox() {
	NodeId created = kNoNode, text = kNoNode;
	auto s = Transaction("Insérer une zone de texte", [&]() -> Status {
		size_t position = 0;
		double offset = 0;
		const NodeId parent = FreeInsertionPoint(position, offset);
		created = Create(doc::CreateNode(NodeType::Shape, parent, position));
		if (created == kNoNode)
			return Failure(ErrorCode::InvalidParent, "une zone de texte ne peut pas être insérée ici");
		std::vector<CommandPtr> props;
		PlaceFreely(created, offset, props);
		props.push_back(doc::SetProperty(created, props::Width, 160.0));
		props.push_back(doc::SetProperty(created, props::Height, 0.0)); // hauteur du texte
		props.push_back(doc::SetProperty(created, props::BorderWidth, 0.0));
		for (PropertyId side : {PropertyId::PaddingTop, PropertyId::PaddingRight, PropertyId::PaddingBottom, PropertyId::PaddingLeft})
			props.push_back(doc::SetProperty(created, side, 4.0));
		for (CommandPtr &c : props)
			if (auto r = m_history->Execute(std::move(c)); !r)
				return r;
		text = NewParagraph(created, kAppend, {});
		return text == kNoNode ? Failure(ErrorCode::InvalidParent, "texte impossible dans la forme") : Success();
	});
	if (s) {
		m_selection.Select(*m_document, text);
		PlaceCaret({text, 0});
	}
	return s;
}

Status Editor::EditShapeText(NodeId shape) {
	const Node *n = m_document->Find(shape);
	if (!n || n->Type() != NodeType::Shape)
		return Failure(ErrorCode::InvalidValue, "sélectionnez une forme");
	NodeId text = kNoNode;
	for (NodeId p : n->Children()) // dernier segment de texte de la forme
		if (const Node *para = m_document->Find(p))
			for (NodeId t : para->Children())
				if (m_document->Find(t)->Type() == NodeType::Text)
					text = t;
	if (text == kNoNode) { // premier texte : centré dans la forme
		auto s = Transaction("Texte de la forme", [&]() -> Status {
			text = NewParagraph(shape, kAppend, {});
			if (text == kNoNode)
				return Failure(ErrorCode::InvalidParent, "texte impossible dans la forme");
			if (auto r = m_history->Execute(doc::SetProperty(m_document->Find(text)->Parent(), props::HAlign, HorizontalAlign::Center)); !r)
				return r;
			return m_history->Execute(doc::SetProperty(shape, props::VAlign, VerticalAlign::Middle));
		});
		if (!s)
			return s;
	}
	m_selection.Select(*m_document, text);
	PlaceCaret({text, m_document->Get(text, props::Text).size()});
	return Success();
}

Status Editor::InsertPageBreak() {
	size_t position = 0;
	const NodeId parent = InsertionPoint(position, true);
	const NodeId created = Create(doc::CreateNode(NodeType::PageBreak, parent, position));
	Changed();
	if (created == kNoNode)
		return Failure(ErrorCode::InvalidParent, "saut de page impossible ici");
	m_selection.Select(*m_document, created); // l'insertion suivante vient après le saut
	return Success();
}

Status Editor::InsertPageNumber() {
	NodeId section = m_document->AncestorOfType(m_selection.Primary(), NodeType::Section);
	if (section == kNoNode)
		section = m_document->Sections().front();
	NodeId footer = kNoNode;
	for (NodeId c : m_document->Find(section)->Children())
		if (m_document->Find(c)->Type() == NodeType::Footer && m_document->Get(c, props::Variant) == HeaderFooterVariant::Default)
			footer = c;
	return Transaction("Insérer un numéro de page", [&]() -> Status {
		if (footer == kNoNode)
			footer = Create(doc::CreateNode(NodeType::Footer, section));
		if (footer == kNoNode)
			return Failure(ErrorCode::InvalidParent, "pied de page impossible");
		const NodeId p = Create(doc::CreateNode(NodeType::Paragraph, footer));
		if (p == kNoNode)
			return Failure(ErrorCode::InvalidParent, "paragraphe impossible");
		(void)m_history->Execute(doc::SetProperty(p, props::HAlign, HorizontalAlign::Center));
		const auto text = [&](std::string_view s) {
			const NodeId t = Create(doc::CreateNode(NodeType::Text, p));
			(void)m_history->Execute(doc::SetProperty(t, props::Text, s));
		};
		const auto field = [&](FieldKind k) {
			const NodeId t = Create(doc::CreateNode(NodeType::Text, p));
			(void)m_history->Execute(doc::SetProperty(t, props::Field, k));
		};
		text("Page ");
		field(FieldKind::PageNumber);
		text(" / ");
		field(FieldKind::PageCount);
		return Success();
	});
}

// ── Mise en page ──

Status Editor::SetPage(PropertyId property, PropertyValue value) {
	std::vector<NodeId> targets;
	for (NodeId id : m_selection.Nodes())
		if (m_document->Find(id)->Type() == NodeType::Page)
			targets.push_back(id);
	if (targets.empty()) {
		for (NodeId id : m_selection.Nodes())
			if (const NodeId s = m_document->Find(id)->Type() == NodeType::Section ? id : m_document->AncestorOfType(id, NodeType::Section);
				s != kNoNode && std::ranges::find(targets, s) == targets.end())
				targets.push_back(s);
	}
	if (targets.empty())
		targets = m_document->Sections();
	return Transaction(std::format("Mise en page : {}", Info(property).name), [&]() -> Status {
		for (NodeId id : targets)
			if (auto s = m_history->Execute(doc::SetProperty(id, property, value)); !s)
				return s;
		return Success();
	});
}

// ── Tableau ──

namespace {

struct TableCursor {
	NodeId table = kNoNode, row = kNoNode, cell = kNoNode;
	size_t rowIndex = 0, column = 0;
};

TableCursor Locate(const doc::Document &d, NodeId primary) {
	TableCursor c;
	const Node *n = d.Find(primary);
	if (!n)
		return c;
	c.cell = n->Type() == NodeType::TableCell ? primary : d.AncestorOfType(primary, NodeType::TableCell);
	c.table = n->Type() == NodeType::Table ? primary : d.AncestorOfType(primary, NodeType::Table);
	if (c.cell != kNoNode) {
		c.row = d.Find(c.cell)->Parent();
		c.table = d.Find(c.row)->Parent();
		c.column = d.Find(c.row)->IndexOfChild(c.cell);
	} else if (c.table != kNoNode && !d.Find(c.table)->Children().empty()) {
		c.row = d.Find(c.table)->Children().back();
	}
	if (c.row != kNoNode)
		c.rowIndex = d.Find(c.table)->IndexOfChild(c.row);
	return c;
}

} // namespace

Status Editor::AddRow(bool below) {
	const TableCursor c = Locate(*m_document, m_selection.Primary());
	if (c.row == kNoNode)
		return Failure(ErrorCode::Nothing, "aucune ligne de tableau sélectionnée");
	const std::vector<NodeId> sourceCells(m_document->Find(c.row)->Children().begin(), m_document->Find(c.row)->Children().end());
	return Transaction("Insérer une ligne", [&]() -> Status {
		const NodeId row = Create(doc::CreateNode(NodeType::TableRow, c.table, c.rowIndex + (below ? 1 : 0)));
		if (row == kNoNode)
			return Failure(ErrorCode::InvalidParent, "ligne impossible");
		for (NodeId source : sourceCells) {
			const NodeId cell = Create(doc::CreateNode(NodeType::TableCell, row));
			for (const auto &[id, value] : m_document->Find(source)->Properties()) // mêmes bordures et marges
				(void)m_history->Execute(doc::SetProperty(cell, id, value));
			NewParagraph(cell, kAppend, {});
		}
		return Success();
	});
}

Status Editor::AddColumn(bool right) {
	const TableCursor c = Locate(*m_document, m_selection.Primary());
	if (c.cell == kNoNode)
		return Failure(ErrorCode::Nothing, "aucune cellule sélectionnée");
	const std::vector<NodeId> rows(m_document->Find(c.table)->Children().begin(), m_document->Find(c.table)->Children().end());
	return Transaction("Insérer une colonne", [&]() -> Status {
		for (NodeId row : rows) {
			const auto cells = m_document->Find(row)->Children();
			const size_t at = std::min(c.column + (right ? 1 : 0), cells.size());
			const NodeId source = cells.empty() ? kNoNode : cells[std::min(c.column, cells.size() - 1)];
			const NodeId cell = Create(doc::CreateNode(NodeType::TableCell, row, at));
			if (cell == kNoNode)
				return Failure(ErrorCode::InvalidParent, "cellule impossible");
			if (source != kNoNode)
				for (const auto &[id, value] : m_document->Find(source)->Properties())
					if (id != PropertyId::Width) // la largeur de la colonne d'origine n'est pas recopiée
						(void)m_history->Execute(doc::SetProperty(cell, id, value));
			NewParagraph(cell, kAppend, {});
		}
		return Success();
	});
}

Status Editor::DeleteRow() {
	const TableCursor c = Locate(*m_document, m_selection.Primary());
	if (c.row == kNoNode)
		return Failure(ErrorCode::Nothing, "aucune ligne de tableau sélectionnée");
	const bool last = m_document->Find(c.table)->Children().size() <= 1;
	auto s = Run(doc::DeleteNode(last ? c.table : c.row)); // dernière ligne : le tableau entier
	if (s && !last) { // la ligne suivante (ou précédente) prend la sélection
		const auto rows = m_document->Find(c.table)->Children();
		SelectCell(rows[std::min(c.rowIndex, rows.size() - 1)], c.column);
	}
	return s;
}

Status Editor::DeleteColumn() {
	const TableCursor c = Locate(*m_document, m_selection.Primary());
	if (c.cell == kNoNode)
		return Failure(ErrorCode::Nothing, "aucune cellule sélectionnée");
	std::vector<NodeId> cells;
	bool emptyAfter = true;
	for (NodeId row : m_document->Find(c.table)->Children()) {
		const auto rc = m_document->Find(row)->Children();
		if (c.column < rc.size())
			cells.push_back(rc[c.column]);
		emptyAfter = emptyAfter && rc.size() <= 1;
	}
	if (emptyAfter)
		return Run(doc::DeleteNode(c.table));
	auto s = Transaction("Supprimer la colonne", [&]() -> Status {
		for (NodeId cell : cells)
			if (auto r = m_history->Execute(doc::DeleteNode(cell)); !r)
				return r;
		return Success();
	});
	if (s) // la cellule voisine de la même ligne prend la sélection
		SelectCell(c.row, c.column == 0 ? 0 : c.column - 1);
	return s;
}

void Editor::SelectCell(NodeId row, size_t column) {
	const Node *r = m_document->Find(row);
	if (!r || r->Children().empty())
		return;
	const NodeId cell = r->Children()[std::min(column, r->Children().size() - 1)];
	NodeId target = cell; // premier segment de texte de la cellule, sinon la cellule
	m_document->Visit(cell, [&](const Node &n) {
		if (target == cell && n.Type() == NodeType::Text)
			target = n.Id();
	});
	m_selection.Select(*m_document, target);
}

Status Editor::SetCells(PropertyId property, PropertyValue value) {
	std::vector<NodeId> cells;
	for (NodeId id : m_selection.Nodes()) {
		const NodeId cell = m_document->Find(id)->Type() == NodeType::TableCell ? id : m_document->AncestorOfType(id, NodeType::TableCell);
		if (cell != kNoNode && std::ranges::find(cells, cell) == cells.end())
			cells.push_back(cell);
	}
	if (cells.empty())
		return Failure(ErrorCode::Nothing, "aucune cellule sélectionnée");
	return Transaction(std::format("Cellules : {}", Info(property).name), [&]() -> Status {
		for (NodeId cell : cells)
			if (auto s = m_history->Execute(doc::SetProperty(cell, property, value)); !s)
				return s;
		return Success();
	});
}

// ── Disposition ──

Status Editor::GroupSelection() {
	std::vector<NodeId> nodes;
	for (NodeId id : m_selection.Nodes())
		if (const NodeType t = m_document->Find(id)->Type(); t != NodeType::Group && t != NodeType::Section)
			nodes.push_back(id);
	if (nodes.empty())
		return Failure(ErrorCode::Nothing, "rien à grouper");
	auto command = doc::GroupNodes(std::format("Groupe {}", ++m_groupCounter), nodes);
	Command *raw = command.get();
	auto s = Run(std::move(command));
	if (s)
		m_selection.Select(*m_document, raw->Created(), SelectMode::Replace, true);
	return s;
}

Status Editor::UngroupSelection() {
	std::vector<NodeId> groups;
	for (NodeId id : m_selection.Nodes())
		if (m_document->Find(id)->Type() == NodeType::Group)
			groups.push_back(id);
	if (!groups.empty())
		return Transaction("Dissocier", [&]() -> Status {
			for (NodeId g : groups)
				if (auto s = m_history->Execute(doc::Ungroup(g)); !s)
					return s;
			return Success();
		});
	// Sinon : les nœuds sélectionnés quittent leurs groupes (hors groupes automatiques de pages).
	return Transaction("Retirer des groupes", [&]() -> Status {
		bool any = false;
		for (NodeId id : m_selection.Nodes()) {
			const auto memberships = m_document->Find(id)->Groups();
			const std::vector<Membership> copy(memberships.begin(), memberships.end());
			for (const Membership &m : copy)
				if (!m.automatic) {
					if (auto s = m_history->Execute(doc::RemoveFromGroup(id, m.group)); !s)
						return s;
					any = true;
				}
		}
		return any ? Success() : Failure(ErrorCode::Nothing, "aucun groupe à dissocier");
	});
}

Status Editor::DuplicateSelection() {
	const std::vector<NodeId> nodes(m_selection.Nodes().begin(), m_selection.Nodes().end());
	if (nodes.empty())
		return Failure(ErrorCode::Nothing, "rien à dupliquer");
	std::vector<NodeId> copies;
	auto s = Transaction("Dupliquer", [&]() -> Status {
		for (NodeId id : nodes) {
			const NodeId copy = Create(doc::Duplicate(id));
			if (copy == kNoNode)
				return Failure(ErrorCode::Protected, "duplication impossible");
			copies.push_back(copy);
		}
		return Success();
	});
	if (s)
		m_selection.SelectIds(*m_document, copies, SelectMode::Replace, true);
	return s;
}

Status Editor::DeleteSelection() {
	// Les descendants d'un nœud déjà supprimé sont ignorés.
	std::vector<NodeId> nodes;
	for (NodeId id : m_selection.Nodes())
		if (std::ranges::none_of(m_selection.Nodes(), [&](NodeId other) { return m_document->IsAncestor(other, id); }))
			nodes.push_back(id);
	if (nodes.empty())
		return Failure(ErrorCode::Nothing, "rien à supprimer");
	auto s = Transaction("Supprimer", [&]() -> Status {
		for (NodeId id : nodes)
			if (auto r = m_history->Execute(doc::DeleteNode(id)); !r)
				return r;
		return Success();
	});
	if (s)
		m_selection.Clear();
	return s;
}

Status Editor::ArrangeSelection(Arrange arrange) {
	const auto isObject = [&](NodeId id) {
		const Node *n = m_document->Find(id);
		return n && (n->Type() == NodeType::Image || n->Type() == NodeType::Shape);
	};
	const NodeId primary = m_selection.Primary();
	if (!isObject(primary))
		return Failure(ErrorCode::Nothing, "sélectionner une image ou une forme");
	// Pile : images et formes de la page de l'objet (ordre de dessin), sinon ses voisines.
	std::vector<NodeId> stack;
	size_t page = 0;
	if (BoxOf(primary, &page))
		for (const LayoutBox &b : Current()->layout.pages[page].boxes)
			if (isObject(b.node) && std::ranges::find(stack, b.node) == stack.end())
				stack.push_back(b.node);
	if (stack.empty())
		for (NodeId c : m_document->Find(m_document->Find(primary)->Parent())->Children())
			if (isObject(c))
				stack.push_back(c);
	std::ranges::stable_sort(stack, {}, [&](NodeId id) { return m_document->Get(id, props::ZIndex); });
	std::vector<NodeId> moved;
	for (NodeId id : stack)
		if (m_selection.Contains(id))
			moved.push_back(id);
	std::vector<NodeId> order = stack;
	switch (arrange) {
	case Arrange::Front:
	case Arrange::Back: {
		std::erase_if(order, [&](NodeId id) { return m_selection.Contains(id); });
		order.insert(arrange == Arrange::Front ? order.end() : order.begin(), moved.begin(), moved.end());
		break;
	}
	case Arrange::Forward: // chaque objet passe au-dessus de son voisin du dessus (du haut vers le bas)
		for (size_t i = order.size(); i-- > 1;)
			if (m_selection.Contains(order[i - 1]) && !m_selection.Contains(order[i]))
				std::swap(order[i - 1], order[i]);
		break;
	case Arrange::Backward:
		for (size_t i = 1; i < order.size(); ++i)
			if (m_selection.Contains(order[i]) && !m_selection.Contains(order[i - 1]))
				std::swap(order[i - 1], order[i]);
		break;
	}
	bool changed = order != stack;
	for (size_t i = 0; i < order.size() && !changed; ++i)
		changed = m_document->Get(order[i], props::ZIndex) != int64_t(i + 1);
	if (order == stack && (arrange == Arrange::Front || arrange == Arrange::Forward ? stack.back() == moved.back() : stack.front() == moved.front()))
		return Failure(ErrorCode::Nothing, arrange == Arrange::Front || arrange == Arrange::Forward ? "déjà au premier plan" : "déjà au dernier plan");
	if (!changed)
		return Failure(ErrorCode::Nothing, "ordre inchangé");
	static constexpr const char *kNames[] = {"Premier plan", "Dernier plan", "Avancer", "Reculer"};
	return Transaction(kNames[size_t(arrange)], [&]() -> Status {
		for (size_t i = 0; i < order.size(); ++i)
			if (m_document->Get(order[i], props::ZIndex) != int64_t(i + 1))
				if (auto s = m_history->Execute(doc::SetProperty(order[i], props::ZIndex, int64_t(i + 1))); !s)
					return s;
		return Success();
	});
}

Status Editor::SetNodes(const std::vector<NodeId> &nodes, PropertyId property, PropertyValue value) {
	auto s = table::SetProperty(*m_history, nodes, property, std::move(value));
	Changed();
	return s;
}

Status Editor::ToggleLocked() {
	if (m_selection.Empty())
		return Failure(ErrorCode::Nothing, "rien de sélectionné");
	const bool all = std::ranges::all_of(m_selection.Nodes(), [&](NodeId id) {
		const auto *v = m_document->Explicit(id, PropertyId::Locked);
		return v && std::get<bool>(*v);
	});
	return Transaction(all ? "Déverrouiller" : "Verrouiller", [&]() -> Status {
		for (NodeId id : m_selection.Nodes())
			if (auto s = m_history->Execute(doc::SetProperty(id, props::Locked, !all)); !s)
				return s;
		return Success();
	});
}

Status Editor::ToggleHidden() {
	std::vector<NodeId> nodes;
	for (NodeId id : m_selection.Nodes())
		if (AppliesTo(PropertyId::Visible, m_document->Find(id)->Type()))
			nodes.push_back(id);
	if (nodes.empty())
		return Failure(ErrorCode::Nothing, "rien à masquer");
	const bool allHidden = std::ranges::all_of(nodes, [&](NodeId id) { return !m_document->Get(id, props::Visible); });
	return Transaction(allHidden ? "Afficher" : "Masquer", [&]() -> Status {
		for (NodeId id : nodes)
			if (auto s = m_history->Execute(doc::SetProperty(id, props::Visible, allHidden)); !s)
				return s;
		return Success();
	});
}

Status Editor::SetSelected(PropertyId property, PropertyValue value) {
	std::vector<NodeId> nodes;
	for (NodeId id : m_selection.Nodes())
		if (const NodeType t = m_document->Find(id)->Type(); AppliesTo(property, t) || t == NodeType::Group)
			nodes.push_back(id);
	if (nodes.empty())
		return Failure(ErrorCode::Nothing, "propriété inapplicable à la sélection");
	return Transaction(std::format("Propriété {}", Info(property).name), [&]() -> Status {
		for (NodeId id : nodes)
			if (auto s = m_history->Execute(doc::SetProperty(id, property, value)); !s)
				return s;
		return Success();
	});
}

Status Editor::ClearSelected(PropertyId property) {
	std::vector<NodeId> nodes;
	for (NodeId id : m_selection.Nodes())
		if (m_document->Explicit(id, property))
			nodes.push_back(id);
	if (nodes.empty())
		return Failure(ErrorCode::Nothing, "aucune valeur explicite");
	return Transaction(std::format("Hériter {}", Info(property).name), [&]() -> Status {
		for (NodeId id : nodes)
			if (auto s = m_history->Execute(doc::ClearProperty(id, property)); !s)
				return s;
		return Success();
	});
}

// ── Sélection ──

void Editor::SelectAt(size_t page, double x, double y, bool add) {
	LeaveText(); // sélection de nœuds : le curseur de texte disparaît
	const Presentation *p = Current();
	const NodeId hit = p ? p->layout.HitTest(page, x, y) : kNoNode;
	if (hit == kNoNode) {
		if (!add) {
			// Clic sur une page hors du contenu : la page elle-même (si elle existe).
			const NodeId pageNode = p && page < p->layout.pages.size() ? p->layout.pages[page].page : kNoNode;
			if (pageNode != kNoNode && m_document->Contains(pageNode))
				m_selection.Select(*m_document, pageNode);
			else
				m_selection.Clear();
		}
		return;
	}
	m_selection.Select(*m_document, hit, add ? SelectMode::Toggle : SelectMode::Replace);
}

void Editor::SelectArea(size_t page, const Rect &area, bool add) {
	LeaveText();
	if (const Presentation *p = Current())
		m_selection.SelectArea(*m_document, p->layout, page, area, add ? SelectMode::Add : SelectMode::Replace);
}

void Editor::SelectNode(NodeId id, bool add) {
	LeaveText();
	m_selection.Select(*m_document, id, add ? SelectMode::Toggle : SelectMode::Replace, true);
}

void Editor::SelectAll() {
	LeaveText();
	std::vector<NodeId> blocks;
	for (NodeId s : m_document->Sections())
		for (NodeId c : m_document->Find(m_document->Body(s))->Children())
			blocks.push_back(c);
	m_selection.SelectIds(*m_document, blocks);
}

void Editor::ClearSelection() {
	LeaveText();
	m_selection.Clear();
}

// ── Polices ──

void Editor::SetFonts(std::shared_ptr<const FontLibrary> fonts) {
	m_fonts = std::move(fonts);
	RequestLayout();
}

void Editor::ScanFontsAsync(std::vector<std::filesystem::path> directories) {
	m_fontScan.Request(m_pool, [dirs = std::move(directories)](std::stop_token) -> std::shared_ptr<const FontLibrary> {
		return FontLibrary::Scan(dirs);
	}, jobs::Priority::Low);
}

// ── Édition du texte ──

const TextOrder &Editor::Order() const {
	if (!m_order || m_orderRevision != m_document->Revision() || m_orderStamp != m_document->Stamp()) {
		m_order = std::make_unique<TextOrder>(*m_document);
		m_orderRevision = m_document->Revision();
		m_orderStamp = m_document->Stamp();
	}
	return *m_order;
}

std::vector<NodeId> Editor::RangeTexts() const {
	std::vector<NodeId> out;
	if (!HasTextRange())
		return out;
	const TextOrder &order = Order();
	TextPosition a = m_anchor, b = m_caret;
	if (order.Compare(a, b) > 0)
		std::swap(a, b);
	bool inside = false;
	m_document->Visit(m_document->Root(), [&](const Node &n) {
		if (n.Type() != NodeType::Text || !order.Contains(n.Id()))
			return;
		if (n.Id() == a.text)
			inside = true;
		if (inside && !(n.Id() == b.text && b.offset == 0 && a.text != b.text))
			out.push_back(n.Id());
		if (n.Id() == b.text)
			inside = false;
	});
	return out;
}

void Editor::PlaceCaret(TextPosition position, bool extend) {
	position = Order().Normalize(position);
	if (!position.Valid())
		return;
	m_caret = position;
	if (!extend || !m_anchor.Valid())
		m_anchor = position;
	m_caretX = -1;
	++m_caretRevision;
	m_selection.Select(*m_document, position.text, SelectMode::Replace, true); // le panneau suit le segment
}

std::optional<TextPosition> Editor::PositionAt(size_t page, double x, double y) const {
	const Presentation *p = Current();
	if (!p)
		return std::nullopt;
	auto pos = text::PositionAt(*p->snapshot, p->layout, page, x, y);
	if (!pos || !m_document->Contains(pos->text))
		return std::nullopt;
	return Order().Normalize(*pos);
}

bool Editor::PlaceCaretAt(size_t page, double x, double y, bool extend) {
	const auto pos = PositionAt(page, x, y);
	if (!pos)
		return false;
	PlaceCaret(*pos, extend);
	return true;
}

void Editor::SelectWordAt(size_t page, double x, double y) {
	if (const auto pos = PositionAt(page, x, y)) {
		const auto [a, b] = Order().WordAt(*pos);
		PlaceCaret(a);
		PlaceCaret(b, true);
	}
}

void Editor::SelectParagraphAt(size_t page, double x, double y) {
	if (const auto pos = PositionAt(page, x, y)) {
		const auto [a, b] = Order().ParagraphRange(*pos);
		PlaceCaret(a);
		PlaceCaret(b, true);
	}
}

void Editor::SelectStory() {
	if (!Editing())
		return;
	const auto [a, b] = Order().StoryRange(m_caret);
	PlaceCaret(a);
	PlaceCaret(b, true);
}

void Editor::LeaveText() {
	m_caret = m_anchor = {};
	++m_caretRevision;
}

std::optional<CaretBox> Editor::CaretGeometry(TextPosition position) const {
	const Presentation *p = Current();
	if (!p || !position.Valid())
		return std::nullopt;
	return text::Caret(*p->snapshot, p->layout, position);
}

std::vector<std::pair<size_t, Rect>> Editor::RangeGeometry() const {
	const Presentation *p = Current();
	if (!p || !HasTextRange())
		return {};
	// Ordre de lecture de la vue affichée, gardé tant que la présentation ne change pas (appelé à
	// chaque image) ; la clé contient l'adresse de la vue : l'ordre ne survit pas à sa vue.
	const DocumentView *view = &*p->snapshot;
	if (!m_viewOrder || m_viewOrderOf != view || m_viewOrderRevision != view->Revision()) {
		m_viewOrder = std::make_unique<TextOrder>(*view);
		m_viewOrderOf = view;
		m_viewOrderRevision = view->Revision();
	}
	return text::RangeRects(p->layout, *m_viewOrder, m_anchor, m_caret);
}

void Editor::MoveCaret(Motion motion, bool extend) {
	if (!Editing())
		return;
	const TextOrder &order = Order();
	// Plage sans extension : gauche / droite réduisent la plage à sa borne.
	if (!extend && HasTextRange() && (motion == Motion::Left || motion == Motion::Right)) {
		const bool forward = order.Compare(m_anchor, m_caret) <= 0;
		const TextPosition edge = (motion == Motion::Left) == forward ? m_anchor : m_caret;
		PlaceCaret(edge);
		return;
	}
	const double keepX = m_caretX;
	TextPosition next = m_caret;
	const Presentation *p = Current();
	switch (motion) {
	case Motion::Left: next = order.Previous(m_caret); break;
	case Motion::Right: next = order.Next(m_caret); break;
	case Motion::WordLeft: next = order.PreviousWord(m_caret); break;
	case Motion::WordRight: next = order.NextWord(m_caret); break;
	case Motion::StoryStart: next = order.StoryRange(m_caret).first; break;
	case Motion::StoryEnd: next = order.StoryRange(m_caret).second; break;
	case Motion::LineStart:
	case Motion::LineEnd:
		if (p)
			if (auto edge = text::LineEdge(*p->snapshot, p->layout, m_caret, motion == Motion::LineEnd))
				next = *edge;
		break;
	case Motion::Up:
	case Motion::Down:
		if (p) {
			double x = keepX;
			if (x < 0)
				if (auto c = text::Caret(*p->snapshot, p->layout, m_caret))
					x = c->rect.x;
			if (auto moved = text::LineMove(*p->snapshot, p->layout, m_caret, motion == Motion::Up ? -1 : 1, x)) {
				PlaceCaret(*moved, extend);
				m_caretX = x; // colonne conservée sur plusieurs lignes
				return;
			}
		}
		break;
	}
	PlaceCaret(next, extend);
}

Status Editor::TypeText(std::string_view utf8) {
	if (!Editing())
		return Failure(ErrorCode::Nothing, "aucun curseur de texte");
	if (utf8.empty())
		return Success();
	if (HasTextRange()) { // la frappe remplace la plage (une seule entrée d'historique)
		m_history->BeginTransaction("Saisie");
		auto erased = text::Erase(*m_history, m_anchor, m_caret);
		auto inserted = erased ? text::Insert(*m_history, erased.Value(), utf8) : DocResult<TextPosition>(Err(erased.Error()));
		if (!inserted) {
			(void)m_history->Rollback();
			Changed();
			return Err(inserted.Error());
		}
		(void)m_history->Commit();
		Changed();
		PlaceCaret(inserted.Value());
		return Success();
	}
	auto inserted = text::Insert(*m_history, m_caret, utf8);
	Changed();
	if (!inserted)
		return Err(inserted.Error());
	PlaceCaret(inserted.Value());
	return Success();
}

Status Editor::Backspace(bool word) {
	if (!Editing())
		return Failure(ErrorCode::Nothing, "aucun curseur de texte");
	const TextOrder &order = Order();
	const TextPosition from = HasTextRange() ? m_anchor : (word ? order.PreviousWord(m_caret) : order.Previous(m_caret));
	if (from == m_caret)
		return Failure(ErrorCode::Nothing, "début du texte");
	auto erased = text::Erase(*m_history, from, m_caret);
	Changed();
	if (!erased)
		return Err(erased.Error());
	PlaceCaret(erased.Value());
	return Success();
}

Status Editor::DeleteForward(bool word) {
	if (!Editing())
		return Failure(ErrorCode::Nothing, "aucun curseur de texte");
	const TextOrder &order = Order();
	const TextPosition to = HasTextRange() ? m_anchor : (word ? order.NextWord(m_caret) : order.Next(m_caret));
	if (to == m_caret)
		return Failure(ErrorCode::Nothing, "fin du texte");
	auto erased = text::Erase(*m_history, m_caret, to);
	Changed();
	if (!erased)
		return Err(erased.Error());
	PlaceCaret(erased.Value());
	return Success();
}

Status Editor::Enter() {
	if (!Editing())
		return Failure(ErrorCode::Nothing, "aucun curseur de texte");
	m_history->BeginTransaction("Nouveau paragraphe");
	TextPosition at = m_caret;
	if (HasTextRange()) {
		auto erased = text::Erase(*m_history, m_anchor, m_caret);
		if (!erased) {
			(void)m_history->Rollback();
			return Err(erased.Error());
		}
		at = erased.Value();
	}
	auto split = text::Split(*m_history, at);
	if (!split) {
		(void)m_history->Rollback();
		Changed();
		return Err(split.Error());
	}
	(void)m_history->Commit();
	Changed();
	PlaceCaret(split.Value());
	return Success();
}

Status Editor::LineBreak() {
	if (!Editing())
		return Failure(ErrorCode::Nothing, "aucun curseur de texte");
	m_history->BeginTransaction("Saut de ligne");
	TextPosition at = m_caret;
	if (HasTextRange()) { // le saut remplace la plage (une seule entrée d'historique)
		auto erased = text::Erase(*m_history, m_anchor, m_caret);
		if (!erased) {
			(void)m_history->Rollback();
			Changed();
			return Err(erased.Error());
		}
		at = erased.Value();
	}
	auto inserted = text::InsertLineBreak(*m_history, at);
	if (!inserted) {
		(void)m_history->Rollback();
		Changed();
		return Err(inserted.Error());
	}
	(void)m_history->Commit();
	Changed();
	PlaceCaret(inserted.Value());
	return Success();
}

Status Editor::PageBreakBelow() {
	if (!Editing())
		return Failure(ErrorCode::Nothing, "aucun curseur de texte");
	const Node *paragraph = m_document->Find(m_document->AncestorOfType(m_caret.text, NodeType::Paragraph));
	const Node *container = paragraph ? m_document->Find(paragraph->Parent()) : nullptr;
	if (!container || !doc::CanContain(container->Type(), NodeType::PageBreak))
		return Failure(ErrorCode::InvalidParent, "saut de page : seulement dans le corps du document (pas dans un tableau, une forme, un en-tête)");
	m_history->BeginTransaction("Saut de page");
	const auto fail = [&](const doc::Error &e) -> Status {
		(void)m_history->Rollback();
		Changed();
		return Err(e);
	};
	TextPosition at = m_caret;
	if (HasTextRange()) {
		auto erased = text::Erase(*m_history, m_anchor, m_caret);
		if (!erased)
			return fail(erased.Error());
		at = erased.Value();
	}
	auto split = text::Split(*m_history, at);
	if (!split)
		return fail(split.Error());
	// Saut de page entre le paragraphe et la suite (nouveau paragraphe du même style).
	const NodeId next = m_document->AncestorOfType(split.Value().text, NodeType::Paragraph);
	const NodeId parent = m_document->Find(next)->Parent();
	if (auto s = m_history->Execute(doc::CreateNode(NodeType::PageBreak, parent, m_document->Find(parent)->IndexOfChild(next))); !s)
		return fail(s.Error());
	(void)m_history->Commit();
	Changed();
	PlaceCaret(split.Value());
	return Success();
}

bool Editor::InTextRange(TextPosition position) const {
	if (!HasTextRange() || !position.Valid())
		return false;
	const TextOrder &order = Order();
	TextPosition a = m_anchor, b = m_caret;
	if (order.Compare(a, b) > 0)
		std::swap(a, b);
	return order.Compare(a, position) <= 0 && order.Compare(position, b) <= 0;
}

Status Editor::DropText(TextPosition target, bool copy) {
	if (!HasTextRange())
		return Failure(ErrorCode::Nothing, "aucune plage de texte");
	auto moved = text::MoveRange(*m_history, m_anchor, m_caret, target, copy);
	Changed();
	if (!moved)
		return Err(moved.Error());
	PlaceCaret(moved.Value().first);
	PlaceCaret(moved.Value().second, true);
	return Success();
}

// ── Presse-papiers ──

Clip Editor::Copy() const {
	Clip clip;
	if (HasTextRange()) {
		clip.text = text::Copy(*m_document, Order(), m_anchor, m_caret);
		clip.plain = clip.text.PlainText();
		return clip;
	}
	if (Editing())
		return clip; // curseur sans plage : rien à copier
	const std::vector<NodeId> ids(m_selection.Nodes().begin(), m_selection.Nodes().end());
	clip.nodes = CopyNodes(*m_document, ids);
	clip.isNodes = true;
	clip.plain = clip.nodes.PlainText();
	return clip;
}

Status Editor::Cut(Clip &clip) {
	clip = Copy();
	if (clip.Empty())
		return Failure(ErrorCode::Nothing, "rien à couper");
	if (!clip.isNodes) {
		auto erased = text::Erase(*m_history, m_anchor, m_caret);
		Changed();
		if (!erased)
			return Err(erased.Error());
		PlaceCaret(erased.Value());
		return Success();
	}
	return DeleteSelection();
}

Status Editor::Paste(const Clip &clip) {
	if (clip.Empty() && clip.plain.empty())
		return Failure(ErrorCode::Nothing, "presse-papiers vide");
	if (Editing() && !clip.isNodes) { // texte : remplace la plage
		const TextFragment fragment = clip.text.paragraphs.empty() ? TextFragment::FromPlainText(clip.plain) : clip.text;
		m_history->BeginTransaction("Coller");
		TextPosition at = m_caret;
		if (HasTextRange()) {
			auto erased = text::Erase(*m_history, m_anchor, m_caret);
			if (!erased) {
				(void)m_history->Rollback();
				return Err(erased.Error());
			}
			at = erased.Value();
		}
		auto pasted = text::Paste(*m_history, at, fragment);
		if (!pasted) {
			(void)m_history->Rollback();
			Changed();
			return Err(pasted.Error());
		}
		(void)m_history->Commit();
		Changed();
		PlaceCaret(pasted.Value());
		return Success();
	}
	// Nœuds (ou texte hors édition : paragraphes) après le bloc courant.
	NodeFragment nodes = clip.nodes;
	if (!clip.isNodes) {
		const TextFragment f = clip.text.paragraphs.empty() ? TextFragment::FromPlainText(clip.plain) : clip.text;
		for (const auto &para : f.paragraphs) {
			NodeFragment::Item p{NodeType::Paragraph, para.properties, {}, {}};
			for (const auto &run : para.runs) {
				NodeFragment::Item t{NodeType::Text, run.properties, {}, {}};
				t.properties.Set(PropertyId::Text, run.text);
				p.children.push_back(std::move(t));
			}
			if (p.children.empty())
				p.children.push_back({NodeType::Text, {}, {}, {}});
			nodes.roots.push_back(std::move(p));
		}
	}
	size_t position = 0;
	const NodeId parent = InsertionPoint(position);
	auto pasted = PasteNodes(*m_history, nodes, parent, position);
	if (!pasted && Editing()) { // un bloc ne va pas dans ce conteneur : après le bloc du corps
		const NodeId body = InsertionPoint(position, true);
		pasted = PasteNodes(*m_history, nodes, body, position);
	}
	Changed();
	if (!pasted)
		return Err(pasted.Error());
	LeaveText();
	m_selection.SelectIds(*m_document, pasted.Value(), SelectMode::Replace, true);
	return Success();
}

Status Editor::MoveBlock(NodeId block, NodeId parent, size_t position) {
	auto s = Run(doc::MoveNode(block, parent, position));
	if (s)
		m_selection.Select(*m_document, block, SelectMode::Replace, true);
	return s;
}

// ── Formes et images ──

const LayoutBox *Editor::BoxOf(NodeId id, size_t *page) const {
	const Presentation *p = Current();
	if (!p)
		return nullptr;
	for (size_t i = 0; i < p->layout.pages.size(); ++i)
		for (const LayoutBox &b : p->layout.pages[i].boxes)
			if (b.node == id) {
				if (page)
					*page = i;
				return &b;
			}
	return nullptr;
}

/// Placement manuel d'une forme ou d'une image : alignement « aucun », position décalée de
/// (dx, dy) ; le décalage d'alignement de sa place est reporté dans sa position.
Status Editor::PlaceManually(NodeId id, const LayoutBox &box, double dx, double dy) {
	double x = m_document->Get(id, props::PositionX) + dx;
	if (m_document->Get(id, props::HAlign) != HorizontalAlign::None) {
		x += box.alignX;
		if (auto s = m_history->Execute(doc::SetProperty(id, props::HAlign, HorizontalAlign::None)); !s)
			return s;
	}
	if (x != m_document->Get(id, props::PositionX))
		if (auto s = m_history->Execute(doc::SetProperty(id, props::PositionX, x)); !s)
			return s;
	if (dy != 0)
		if (auto s = m_history->Execute(doc::SetProperty(id, props::PositionY, m_document->Get(id, props::PositionY) + dy)); !s)
			return s;
	return Success();
}

Status Editor::ResizeShape(NodeId id, const Rect &rect) {
	const LayoutBox *box = BoxOf(id);
	if (!box)
		return Failure(ErrorCode::NotFound, "forme absente de la mise en page");
	const LayoutBox before = *box; // la mise en page peut être remplacée pendant la transaction
	const double w = std::max(1.0, rect.w), h = std::max(1.0, rect.h);
	return Transaction("Redimensionner", [&]() -> Status {
		for (auto [p, v] : {std::pair{PropertyId::Width, w}, std::pair{PropertyId::Height, h}})
			if (auto s = m_history->Execute(doc::SetProperty(id, p, v)); !s)
				return s;
		if (before.type == NodeType::Drawing) // bloc du flux : sa taille seulement
			return Success();
		// Bord gauche / haut déplacé : la position suit (même décalage dans tout repère).
		return PlaceManually(id, before, rect.x - before.rect.x, rect.y - before.rect.y);
	});
}

Status Editor::MoveShape(NodeId id, double dx, double dy) {
	const LayoutBox *box = BoxOf(id);
	if (!box)
		return Failure(ErrorCode::NotFound, "forme absente de la mise en page");
	if (box->type == NodeType::Drawing)
		return Failure(ErrorCode::InvalidValue, "une zone de dessin se déplace dans le flux (poignée du bloc)");
	const LayoutBox before = *box;
	return Transaction("Déplacer", [&]() -> Status { return PlaceManually(id, before, dx, dy); });
}

Status Editor::AlignShapes(const std::vector<NodeId> &ids, HorizontalAlign align) {
	const Presentation *p = Current();
	return Transaction("Alignement horizontal", [&]() -> Status {
		for (NodeId id : ids) {
			const Node *n = m_document->Find(id);
			if (!n || (n->Type() != NodeType::Shape && n->Type() != NodeType::Image && n->Type() != NodeType::Drawing))
				continue;
			if (auto s = m_history->Execute(doc::SetProperty(id, props::HAlign, align)); !s)
				return s;
			if (align == HorizontalAlign::None || n->Type() == NodeType::Drawing)
				continue; // aucun : la forme reste où elle est ; zone de dessin : alignée par le moteur
			size_t page = 0;
			const LayoutBox *box = BoxOf(id, &page);
			const Node *parent = m_document->Find(n->Parent());
			const bool inDrawing = parent && parent->Type() == NodeType::Drawing;
			const bool absolute = m_document->Get(id, props::Position) == Positioning::Absolute;
			PositionBase base = m_document->Get(id, props::PositionReference);
			if (base == PositionBase::Auto)
				base = absolute ? PositionBase::Page : PositionBase::Node;
			if (!box || !p || (!inDrawing && !absolute && base == PositionBase::Node)) {
				// Dans le flux : le moteur aligne la place, la position revient à zéro.
				if (auto s = m_history->Execute(doc::SetProperty(id, props::PositionX, 0.0)); !s)
					return s;
				continue;
			}
			// Placement libre : aligné dans la zone de dessin, sinon dans la zone de contenu de la page.
			Rect area = p->layout.pages[page].content;
			if (inDrawing)
				if (const LayoutBox *d = BoxOf(parent->Id())) {
					const double border = m_document->Get(parent->Id(), props::BorderWidth);
					const double left = border + m_document->Get(parent->Id(), props::PaddingLeft);
					area = Rect(d->rect.x + left, d->rect.y, d->rect.w - left - border - m_document->Get(parent->Id(), props::PaddingRight), d->rect.h);
				}
			double x = area.x;
			if (align == HorizontalAlign::Center)
				x += (area.w - box->rect.w) / 2;
			else if (align == HorizontalAlign::Right)
				x += area.w - box->rect.w;
			if (auto s = m_history->Execute(doc::SetProperty(id, props::PositionX, m_document->Get(id, props::PositionX) + x - box->rect.x)); !s)
				return s;
		}
		return Success();
	});
}

Status Editor::RotateShape(NodeId id, double degrees) {
	double d = std::fmod(degrees, 360.0);
	if (d > 180)
		d -= 360;
	if (d < -180)
		d += 360;
	return Run(doc::SetProperty(id, props::Rotation, d));
}

Status Editor::SetRotationOrigin(NodeId id, double pageX, double pageY) {
	const LayoutBox *box = BoxOf(id);
	if (!box)
		return Failure(ErrorCode::NotFound, "forme absente de la mise en page");
	double x = pageX, y = pageY;
	switch (m_document->Get(id, props::RotationOriginReference)) {
	case OriginBase::Node:
		x -= box->rect.x + box->rect.w / 2;
		y -= box->rect.y + box->rect.h / 2;
		break;
	case OriginBase::Parent:
		x -= box->parentX;
		y -= box->parentY;
		break;
	case OriginBase::Page: break;
	}
	return Transaction("Centre de rotation", [&]() -> Status {
		if (auto s = m_history->Execute(doc::SetProperty(id, props::RotationOriginX, x)); !s)
			return s;
		return m_history->Execute(doc::SetProperty(id, props::RotationOriginY, y));
	});
}

Status Editor::SetPositionBase(NodeId id, PositionBase base) {
	const LayoutBox *box = BoxOf(id);
	if (!box)
		return Run(doc::SetProperty(id, props::PositionReference, base));
	PositionBase effective = base;
	if (effective == PositionBase::Auto)
		effective = m_document->Get(id, props::Position) == Positioning::Absolute ? PositionBase::Page : PositionBase::Node;
	double ox = 0, oy = 0;
	if (effective == PositionBase::Node) {
		ox = box->slotX;
		oy = box->slotY;
	} else if (effective == PositionBase::Parent) {
		ox = box->parentX;
		oy = box->parentY;
	}
	const double x = box->rect.x - ox, y = box->rect.y - oy;
	return Transaction("Repère de la position", [&]() -> Status {
		if (auto s = m_history->Execute(doc::SetProperty(id, props::PositionReference, base)); !s)
			return s;
		if (auto s = m_history->Execute(doc::SetProperty(id, props::PositionX, x)); !s)
			return s;
		return m_history->Execute(doc::SetProperty(id, props::PositionY, y));
	});
}

Status Editor::SetOriginBase(NodeId id, OriginBase base) {
	const LayoutBox *box = BoxOf(id);
	if (!box)
		return Run(doc::SetProperty(id, props::RotationOriginReference, base));
	double x = box->pivotX, y = box->pivotY;
	if (base == OriginBase::Node) {
		x -= box->rect.x + box->rect.w / 2;
		y -= box->rect.y + box->rect.h / 2;
	} else if (base == OriginBase::Parent) {
		x -= box->parentX;
		y -= box->parentY;
	}
	return Transaction("Repère du centre de rotation", [&]() -> Status {
		if (auto s = m_history->Execute(doc::SetProperty(id, props::RotationOriginReference, base)); !s)
			return s;
		if (auto s = m_history->Execute(doc::SetProperty(id, props::RotationOriginX, x)); !s)
			return s;
		return m_history->Execute(doc::SetProperty(id, props::RotationOriginY, y));
	});
}

} // namespace scribe
