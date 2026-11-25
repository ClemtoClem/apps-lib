#include "pdf/reader.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <format>

#include <archive/archive_deflate.hpp>
#include <core/string.hpp>

namespace pdf {

namespace {

constexpr int kMaxResolveDepth = 32;
constexpr int kMaxPageDepth = 64;

std::string_view AsText(std::span<const uint8_t> bytes) { return {reinterpret_cast<const char *>(bytes.data()), bytes.size()}; }

/// Dernière occurrence de `needle` dans `data` (SIZE_MAX si absente).
size_t FindLast(std::span<const uint8_t> data, std::string_view needle) {
	const size_t pos = AsText(data).rfind(needle);
	return pos == std::string_view::npos ? SIZE_MAX : pos;
}

Err<Error> Fail(std::string message) { return Err(Error{std::move(message)}); }

// ── Filtres ──

PdfResult<Bytes> AsciiHex(std::span<const uint8_t> in) {
	Bytes out;
	int pending = -1;
	for (const uint8_t c : in) {
		if (c == '>')
			break;
		int v = -1;
		if (c >= '0' && c <= '9')
			v = c - '0';
		else if (c >= 'a' && c <= 'f')
			v = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			v = c - 'A' + 10;
		else if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == 0)
			continue;
		else
			return Fail("ASCIIHexDecode : caractère invalide");
		if (pending < 0) {
			pending = v;
		} else {
			out.push_back(uint8_t(pending * 16 + v));
			pending = -1;
		}
	}
	if (pending >= 0)
		out.push_back(uint8_t(pending * 16));
	return Ok(std::move(out));
}

PdfResult<Bytes> Ascii85(std::span<const uint8_t> in) {
	Bytes out;
	uint32_t tuple = 0;
	int count = 0;
	for (size_t i = 0; i < in.size(); ++i) {
		const uint8_t c = in[i];
		if (c == '~')
			break;
		if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == 0)
			continue;
		if (c == 'z' && count == 0) {
			out.insert(out.end(), 4, 0);
			continue;
		}
		if (c < '!' || c > 'u')
			return Fail("ASCII85Decode : caractère invalide");
		tuple = tuple * 85 + uint32_t(c - '!');
		if (++count == 5) {
			for (int s = 24; s >= 0; s -= 8)
				out.push_back(uint8_t(tuple >> s));
			tuple = 0;
			count = 0;
		}
	}
	if (count == 1)
		return Fail("ASCII85Decode : groupe final incomplet");
	if (count > 1) {
		for (int k = count; k < 5; ++k)
			tuple = tuple * 85 + 84;
		for (int k = 0; k < count - 1; ++k)
			out.push_back(uint8_t(tuple >> (24 - 8 * k)));
	}
	return Ok(std::move(out));
}

/// Prédicteurs PNG (Predictor ≥ 10) d'un flux FlateDecode.
PdfResult<Bytes> PngPredictor(const Bytes &in, int colors, int bits, int columns) {
	const size_t bpp = std::max<size_t>(1, size_t(colors * bits) / 8);
	const size_t row = (size_t(colors) * size_t(bits) * size_t(columns) + 7) / 8;
	if (row == 0)
		return Fail("prédicteur : largeur nulle");
	Bytes out;
	out.reserve(in.size());
	Bytes previous(row, 0), current(row);
	for (size_t pos = 0; pos + 1 + row <= in.size(); pos += row + 1) {
		const uint8_t type = in[pos];
		for (size_t i = 0; i < row; ++i) {
			const uint8_t raw = in[pos + 1 + i];
			const uint8_t left = i >= bpp ? current[i - bpp] : 0, up = previous[i], upLeft = i >= bpp ? previous[i - bpp] : 0;
			uint8_t v = raw;
			switch (type) {
			case 0: break;
			case 1: v = uint8_t(raw + left); break;
			case 2: v = uint8_t(raw + up); break;
			case 3: v = uint8_t(raw + (left + up) / 2); break;
			case 4: {
				const int p = left + up - upLeft;
				const int pa = std::abs(p - left), pb = std::abs(p - up), pc = std::abs(p - upLeft);
				v = uint8_t(raw + (pa <= pb && pa <= pc ? left : pb <= pc ? up : upLeft));
				break;
			}
			default: return Fail(std::format("prédicteur PNG inconnu {}", type));
			}
			current[i] = v;
		}
		out.insert(out.end(), current.begin(), current.end());
		std::swap(previous, current);
	}
	return Ok(std::move(out));
}

int IntOr(const Object *o, int fallback) { return o && o->kind == ObjectKind::Integer ? int(o->integer) : fallback; }

std::string DecodeTextString(const std::string &bytes) {
	if (bytes.size() >= 2 && uint8_t(bytes[0]) == 0xFE && uint8_t(bytes[1]) == 0xFF) {
		std::u16string u;
		for (size_t i = 2; i + 1 < bytes.size(); i += 2)
			u.push_back(char16_t(uint8_t(bytes[i]) << 8 | uint8_t(bytes[i + 1])));
		return unicode::FromUtf16(u);
	}
	return DecodeWinAnsi(bytes); // PDFDocEncoding : identique à WinAnsi pour l'essentiel
}

} // namespace

// ── Ouverture ──

PdfResult<Reader> Reader::Load(const std::filesystem::path &path) {
	std::FILE *f = std::fopen(path.string().c_str(), "rb");
	if (!f)
		return Fail(std::format("impossible d'ouvrir {}", path.string()));
	Bytes data;
	uint8_t buffer[65536];
	for (size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;)
		data.insert(data.end(), buffer, buffer + n);
	std::fclose(f);
	return Open(std::move(data));
}

PdfResult<Reader> Reader::Open(Bytes data) {
	Reader r;
	r.m_data = std::move(data);
	const std::string_view text = AsText(r.m_data);
	const size_t header = text.substr(0, 1024).find("%PDF-");
	if (header == std::string_view::npos)
		return Fail("en-tête %PDF- absent");
	size_t end = header + 5;
	while (end < text.size() && end < header + 9 && (std::isdigit(uint8_t(text[end])) || text[end] == '.'))
		++end;
	r.m_version = std::string(text.substr(header + 5, end - header - 5));

	bool ok = false;
	const size_t startxref = FindLast(r.m_data, "startxref");
	if (startxref != SIZE_MAX) {
		Parser p(r.m_data, startxref + 9);
		auto offset = p.ParseObject();
		if (offset && offset.Value().kind == ObjectKind::Integer && offset.Value().integer >= 0) {
			auto read = r.ReadXref(size_t(offset.Value().integer));
			ok = read.IsOk() && r.m_trailer.Get("Root") != nullptr;
		}
	}
	if (ok)
		ok = r.CollectPages().IsOk();
	if (!ok) {
		r.Repair();
		if (auto pages = r.CollectPages(); !pages)
			return Err(pages.Error());
	}
	return Ok(std::move(r));
}

PdfResult<bool> Reader::ReadXref(size_t offset) {
	std::vector<size_t> visited;
	bool first = true;
	while (offset < m_data.size() && std::ranges::find(visited, offset) == visited.end()) {
		visited.push_back(offset);
		Parser p(m_data, offset);
		Object trailer;
		if (p.Expect("xref")) {
			auto table = ReadXrefTable(p);
			if (!table)
				return table;
			auto t = p.ParseObject();
			if (!t || !t.Value().IsDictionary())
				return Fail("dictionnaire « trailer » invalide");
			trailer = std::move(t.Value());
			// Fichier hybride : flux de références complémentaire.
			if (const Object *stm = trailer.Get("XRefStm"); stm && stm->kind == ObjectKind::Integer) {
				Parser s(m_data, size_t(stm->integer));
				(void)ReadXrefStream(s);
			}
		} else {
			auto obj = p.ParseIndirect();
			if (!obj || obj.Value().second.kind != ObjectKind::Stream)
				return Fail(std::format("table de références introuvable à {}", offset));
			Parser s(m_data, offset);
			auto stream = ReadXrefStream(s);
			if (!stream)
				return stream;
			trailer = std::move(obj.Value().second);
			trailer.kind = ObjectKind::Dictionary;
			trailer.data.clear();
		}
		if (first) {
			m_trailer = trailer;
			first = false;
		}
		const Object *prev = trailer.Get("Prev");
		if (!prev || prev->kind != ObjectKind::Integer || prev->integer < 0)
			break;
		offset = size_t(prev->integer);
	}
	return Ok(true);
}

PdfResult<bool> Reader::ReadXrefTable(Parser &p) {
	for (;;) {
		if (p.Expect("trailer"))
			return Ok(true);
		auto start = p.ParseObject();
		auto count = p.ParseObject();
		if (!start || !count || start.Value().kind != ObjectKind::Integer || count.Value().kind != ObjectKind::Integer ||
			start.Value().integer < 0 || count.Value().integer < 0 || count.Value().integer > 10000000)
			return Fail("sous-section de la table de références invalide");
		for (int64_t i = 0; i < count.Value().integer; ++i) {
			auto offset = p.ParseObject();
			auto gen = p.ParseObject();
			if (!offset || !gen || offset.Value().kind != ObjectKind::Integer)
				return Fail("entrée de la table de références invalide");
			const bool used = p.Expect("n");
			if (!used && !p.Expect("f"))
				return Fail("entrée de la table de références sans « n » ni « f »");
			const auto number = uint32_t(start.Value().integer + i);
			if (used && number != 0 && !m_entries.Contains(number))
				m_entries.InsertOrAssign(number, Entry{1, uint64_t(offset.Value().integer), 0});
		}
	}
}

PdfResult<bool> Reader::ReadXrefStream(Parser &p) {
	auto obj = p.ParseIndirect();
	if (!obj || obj.Value().second.kind != ObjectKind::Stream)
		return Fail("flux de références invalide");
	const Object &stream = obj.Value().second;
	auto bytes = DecodeStream(stream);
	if (!bytes)
		return Err(bytes.Error());
	const Object *w = stream.Get("W");
	if (!w || w->kind != ObjectKind::Array || w->items.size() != 3)
		return Fail("flux de références sans /W");
	int widths[3];
	for (int k = 0; k < 3; ++k) {
		widths[k] = int(w->items[size_t(k)].integer);
		if (widths[k] < 0 || widths[k] > 8)
			return Fail("largeur /W invalide");
	}
	std::vector<std::pair<int64_t, int64_t>> ranges;
	if (const Object *index = stream.Get("Index"); index && index->kind == ObjectKind::Array) {
		for (size_t k = 0; k + 1 < index->items.size(); k += 2)
			ranges.emplace_back(index->items[k].integer, index->items[k + 1].integer);
	} else {
		ranges.emplace_back(0, IntOr(stream.Get("Size"), 0));
	}
	const Bytes &data = bytes.Value();
	const size_t entrySize = size_t(widths[0] + widths[1] + widths[2]);
	if (entrySize == 0)
		return Fail("flux de références vide");
	size_t pos = 0;
	const auto field = [&](int width, uint64_t fallback) {
		if (width == 0)
			return fallback;
		uint64_t v = 0;
		for (int k = 0; k < width; ++k)
			v = v << 8 | data[pos++];
		return v;
	};
	for (const auto &[first, count] : ranges)
		for (int64_t i = 0; i < count && pos + entrySize <= data.size(); ++i) {
			const uint64_t type = field(widths[0], 1), f1 = field(widths[1], 0), f2 = field(widths[2], 0);
			const auto number = uint32_t(first + i);
			if (number == 0 || m_entries.Contains(number) || (type != 1 && type != 2))
				continue;
			m_entries.InsertOrAssign(number, Entry{uint8_t(type), f1, uint32_t(f2)});
		}
	return Ok(true);
}

void Reader::Repair() {
	m_repaired = true;
	m_entries.Clear();
	m_cache.Clear();
	m_objectStreams.Clear();
	const std::string_view text = AsText(m_data);
	const auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
	const auto isSpace = [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == 0; };
	// « n g obj » : la dernière définition d'un numéro l'emporte (mises à jour incrémentales).
	for (size_t pos = text.find("obj"); pos != std::string_view::npos; pos = text.find("obj", pos + 3)) {
		if (pos + 3 < text.size() && !isSpace(text[pos + 3]) && text[pos + 3] != '<' && text[pos + 3] != '[' && text[pos + 3] != '/')
			continue;
		size_t i = pos;
		const auto skipSpaces = [&] {
			while (i > 0 && isSpace(text[i - 1]))
				--i;
		};
		const auto digits = [&] {
			const size_t end = i;
			while (i > 0 && isDigit(text[i - 1]))
				--i;
			return end - i;
		};
		skipSpaces();
		if (digits() == 0)
			continue;
		skipSpaces();
		const size_t numberEnd = i;
		if (digits() == 0)
			continue;
		if (i > 0 && !isSpace(text[i - 1]) && text[i - 1] != '>' && text[i - 1] != ']')
			continue;
		uint32_t number = 0;
		for (size_t k = i; k < numberEnd; ++k)
			number = number * 10 + uint32_t(text[k] - '0');
		if (number != 0)
			m_entries.InsertOrAssign(number, Entry{1, i, 0});
	}
	// Flux d'objets : leurs objets complètent la table.
	std::vector<uint32_t> numbers;
	for (const auto &[number, entry] : m_entries)
		numbers.push_back(number);
	std::ranges::sort(numbers);
	for (const uint32_t n : numbers) {
		auto obj = Get(n);
		if (!obj || obj.Value().kind != ObjectKind::Stream)
			continue;
		const Object &o = obj.Value();
		if (o.Get("Type") && o.Get("Type")->IsName("ObjStm")) {
			auto decoded = DecodeStream(o);
			if (!decoded)
				continue;
			Parser header(decoded.Value());
			const int count = IntOr(o.Get("N"), 0);
			for (int k = 0; k < count; ++k) {
				auto num = header.ParseObject();
				auto off = header.ParseObject();
				if (!num || !off || num.Value().kind != ObjectKind::Integer)
					break;
				const auto objNumber = uint32_t(num.Value().integer);
				if (!m_entries.Contains(objNumber))
					m_entries.InsertOrAssign(objNumber, Entry{2, n, uint32_t(k)});
			}
		}
		if (!m_trailer.Get("Root") && o.Get("Type") && o.Get("Type")->IsName("XRef") && o.Get("Root")) {
			m_trailer = o;
			m_trailer.kind = ObjectKind::Dictionary;
			m_trailer.data.clear();
		}
	}
	// En-queue : dernier « trailer », sinon recherche du catalogue.
	if (!m_trailer.Get("Root")) {
		if (const size_t t = FindLast(m_data, "trailer"); t != SIZE_MAX) {
			Parser p(m_data, t + 7);
			if (auto dict = p.ParseObject(); dict && dict.Value().IsDictionary())
				m_trailer = std::move(dict.Value());
		}
	}
	if (!m_trailer.Get("Root")) {
		numbers.clear();
		for (const auto &[number, entry] : m_entries)
			numbers.push_back(number);
		std::ranges::sort(numbers);
		for (const uint32_t n : numbers)
			if (auto obj = Get(n); obj && obj.Value().IsDictionary() && obj.Value().Get("Type") && obj.Value().Get("Type")->IsName("Catalog")) {
				m_trailer = Object{};
				m_trailer.kind = ObjectKind::Dictionary;
				m_trailer.Set("Root", Object::MakeReference({n, 0}));
				break;
			}
	}
}

// ── Objets ──

std::optional<int64_t> Reader::LengthOf(Reference ref) const {
	const Entry *e = m_entries.Find(ref.number);
	if (!e || e->type != 1 || m_cache.Contains(ref.number))
		return m_cache.Contains(ref.number) && m_cache.Find(ref.number)->kind == ObjectKind::Integer
				   ? std::optional<int64_t>(m_cache.Find(ref.number)->integer)
				   : std::nullopt;
	Parser p(m_data, size_t(e->offset));
	auto obj = p.ParseIndirect(); // un entier : pas de flux, pas de récursion
	if (!obj || obj.Value().second.kind != ObjectKind::Integer)
		return std::nullopt;
	return obj.Value().second.integer;
}

PdfResult<Object> Reader::Get(uint32_t number) const {
	if (const Object *cached = m_cache.Find(number))
		return Ok(*cached);
	const Entry *e = m_entries.Find(number);
	if (!e)
		return Ok(Object{}); // objet absent : null (ISO 32000-1 §7.3.10)
	Object result;
	if (e->type == 2) {
		auto loaded = LoadFromObjectStream(uint32_t(e->offset), e->index);
		if (!loaded)
			return loaded;
		result = std::move(loaded.Value());
	} else {
		if (e->offset >= m_data.size())
			return Fail(std::format("objet {} : position hors du fichier", number));
		Parser p(m_data, size_t(e->offset));
		p.SetLengthResolver([this](Reference r) { return LengthOf(r); });
		auto obj = p.ParseIndirect();
		if (!obj)
			return Fail(std::format("objet {} : {}", number, obj.Error()));
		if (obj.Value().first.number != number)
			return Fail(std::format("objet {} attendu, {} trouvé", number, obj.Value().first.number));
		result = std::move(obj.Value().second);
	}
	m_cache.InsertOrAssign(number, result);
	return Ok(std::move(result));
}

PdfResult<Object> Reader::LoadFromObjectStream(uint32_t stream, uint32_t index) const {
	if (const auto *objects = m_objectStreams.Find(stream))
		return index < objects->size() ? PdfResult<Object>(Ok((*objects)[index])) : Fail("rang hors du flux d'objets");
	const Entry *e = m_entries.Find(stream);
	if (!e || e->type != 1)
		return Fail(std::format("flux d'objets {} introuvable", stream));
	auto obj = Get(stream);
	if (!obj || obj.Value().kind != ObjectKind::Stream)
		return Fail(std::format("flux d'objets {} invalide", stream));
	auto decoded = DecodeStream(obj.Value());
	if (!decoded)
		return Err(decoded.Error());
	const int count = IntOr(obj.Value().Get("N"), 0), first = IntOr(obj.Value().Get("First"), 0);
	if (count < 0 || first < 0 || size_t(first) > decoded.Value().size())
		return Fail("en-tête de flux d'objets invalide");
	Parser header(decoded.Value());
	std::vector<Object> objects;
	for (int k = 0; k < count; ++k) {
		auto num = header.ParseObject();
		auto off = header.ParseObject();
		if (!num || !off || off.Value().kind != ObjectKind::Integer)
			return Fail("en-tête de flux d'objets tronqué");
		Parser body(decoded.Value(), size_t(first) + size_t(std::max<int64_t>(0, off.Value().integer)));
		auto value = body.ParseObject();
		objects.push_back(value ? std::move(value.Value()) : Object{});
	}
	m_objectStreams.InsertOrAssign(stream, std::move(objects));
	return LoadFromObjectStream(stream, index);
}

PdfResult<Object> Reader::Resolve(const Object &object) const {
	Object current = object;
	for (int depth = 0; current.kind == ObjectKind::Reference; ++depth) {
		if (depth >= kMaxResolveDepth)
			return Fail("chaîne de références trop longue");
		auto next = Get(current.ref.number);
		if (!next)
			return next;
		current = std::move(next.Value());
	}
	return Ok(std::move(current));
}

// ── Pages ──

PdfResult<bool> Reader::CollectPages() {
	m_pages.clear();
	const Object *rootRef = m_trailer.Get("Root");
	if (!rootRef)
		return Fail("catalogue (/Root) absent");
	auto root = Resolve(*rootRef);
	if (!root || !root.Value().IsDictionary() || !root.Value().Get("Pages"))
		return Fail("catalogue invalide");
	std::vector<uint32_t> visited;
	struct Inherited {
		const Object *resources = nullptr, *mediaBox = nullptr, *rotate = nullptr;
	};
	std::function<PdfResult<bool>(const Object &, Reference, Inherited, int)> walk = [&](const Object &nodeRef, Reference ref, Inherited inherited,
																						   int depth) -> PdfResult<bool> {
		if (depth > kMaxPageDepth)
			return Fail("arbre des pages trop profond");
		if (nodeRef.kind == ObjectKind::Reference) {
			if (std::ranges::find(visited, nodeRef.ref.number) != visited.end())
				return Fail("cycle dans l'arbre des pages");
			visited.push_back(nodeRef.ref.number);
			ref = nodeRef.ref;
		}
		auto node = Resolve(nodeRef);
		if (!node)
			return Err(node.Error());
		const Object &n = node.Value();
		if (!n.IsDictionary())
			return Ok(true); // nœud invalide ignoré
		if (const Object *r = n.Get("Resources"))
			inherited.resources = r;
		if (const Object *m = n.Get("MediaBox"))
			inherited.mediaBox = m;
		if (const Object *r = n.Get("Rotate"))
			inherited.rotate = r;
		const Object *kids = n.Get("Kids");
		const bool isTree = (n.Get("Type") && n.Get("Type")->IsName("Pages")) || kids;
		if (isTree) {
			auto kidsArray = kids ? Resolve(*kids) : PdfResult<Object>(Ok(Object{}));
			if (!kidsArray)
				return Err(kidsArray.Error());
			for (const Object &kid : kidsArray.Value().items)
				if (auto r = walk(kid, {}, inherited, depth + 1); !r)
					return r;
			return Ok(true);
		}
		PageInfo page;
		page.ref = ref;
		page.dictionary = n;
		if (inherited.resources && !n.Get("Resources"))
			page.dictionary.Set("Resources", *inherited.resources);
		if (inherited.mediaBox) {
			auto box = Resolve(*inherited.mediaBox);
			if (box && box.Value().kind == ObjectKind::Array && box.Value().items.size() == 4) {
				double v[4];
				for (size_t k = 0; k < 4; ++k) {
					auto item = Resolve(box.Value().items[k]);
					v[k] = item ? item.Value().Number() : 0;
				}
				page.width = std::abs(v[2] - v[0]);
				page.height = std::abs(v[3] - v[1]);
				page.dictionary.Set("MediaBox", box.Value());
			}
		}
		if (page.width <= 0 || page.height <= 0) { // MediaBox obligatoire ; Letter par défaut
			page.width = 612;
			page.height = 792;
		}
		if (inherited.rotate) {
			auto rot = Resolve(*inherited.rotate);
			page.rotate = rot ? int(((rot.Value().integer % 360) + 360) % 360) : 0;
		}
		m_pages.push_back(std::move(page));
		return Ok(true);
	};
	Object pagesRef = *root.Value().Get("Pages");
	return walk(pagesRef, {}, {}, 0);
}

// ── Flux ──

PdfResult<Bytes> Reader::DecodeStream(const Object &stream) const {
	if (stream.kind != ObjectKind::Stream)
		return Fail("objet non flux");
	std::vector<Object> filters, parms;
	if (const Object *f = stream.Get("Filter")) {
		auto resolved = Resolve(*f);
		if (resolved)
			filters = resolved.Value().kind == ObjectKind::Array ? resolved.Value().items : std::vector<Object>{resolved.Value()};
	}
	if (const Object *d = stream.Get("DecodeParms")) {
		auto resolved = Resolve(*d);
		if (resolved)
			parms = resolved.Value().kind == ObjectKind::Array ? resolved.Value().items : std::vector<Object>{resolved.Value()};
	}
	Bytes data = stream.data;
	for (size_t i = 0; i < filters.size(); ++i) {
		const std::string &name = filters[i].text;
		const Object *parm = i < parms.size() && parms[i].IsDictionary() ? &parms[i] : nullptr;
		if (name == "FlateDecode" || name == "Fl") {
			auto inflated = archive::ZlibDecompress(data);
			Bytes out;
			if (inflated) {
				out = std::move(inflated.Value());
			} else if (data.size() > 2) { // somme Adler-32 fausse ou en-tête absent : DEFLATE brut
				auto raw = archive::Inflate(std::span<const uint8_t>(data).subspan(2));
				if (!raw)
					raw = archive::Inflate(data);
				if (!raw)
					return Fail(std::format("FlateDecode : {}", inflated.Error()));
				out = std::move(raw.Value().bytes);
			} else {
				return Fail("FlateDecode : flux vide");
			}
			const int predictor = parm ? IntOr(parm->Get("Predictor"), 1) : 1;
			if (predictor >= 10) {
				auto unpredicted = PngPredictor(out, IntOr(parm->Get("Colors"), 1), IntOr(parm->Get("BitsPerComponent"), 8),
												IntOr(parm->Get("Columns"), 1));
				if (!unpredicted)
					return unpredicted;
				out = std::move(unpredicted.Value());
			} else if (predictor == 2) {
				return Fail("prédicteur TIFF non pris en charge");
			}
			data = std::move(out);
		} else if (name == "ASCIIHexDecode" || name == "AHx") {
			auto out = AsciiHex(data);
			if (!out)
				return out;
			data = std::move(out.Value());
		} else if (name == "ASCII85Decode" || name == "A85") {
			auto out = Ascii85(data);
			if (!out)
				return out;
			data = std::move(out.Value());
		} else {
			return Fail(std::format("filtre {} non pris en charge", name));
		}
	}
	return Ok(std::move(data));
}

PdfResult<Bytes> Reader::PageContent(size_t index) const {
	if (index >= m_pages.size())
		return Fail("page inexistante");
	const Object *contents = m_pages[index].dictionary.Get("Contents");
	if (!contents)
		return Ok(Bytes{});
	auto resolved = Resolve(*contents);
	if (!resolved)
		return Err(resolved.Error());
	std::vector<Object> parts =
		resolved.Value().kind == ObjectKind::Array ? resolved.Value().items : std::vector<Object>{resolved.Value()};
	Bytes out;
	for (const Object &part : parts) {
		auto stream = Resolve(part);
		if (!stream)
			return Err(stream.Error());
		auto decoded = DecodeStream(stream.Value());
		if (!decoded)
			return decoded;
		out.insert(out.end(), decoded.Value().begin(), decoded.Value().end());
		out.push_back('\n');
	}
	return Ok(std::move(out));
}

// ── Texte ──

namespace {

struct TextFont {
	StandardFont metrics = StandardFont::Helvetica;
	int firstChar = 0;
	std::vector<double> widths; ///< /Widths du fichier (1/1000 du corps)

	[[nodiscard]] double Width(uint8_t b) const {
		if (b >= firstChar && size_t(b - firstChar) < widths.size())
			return widths[size_t(b - firstChar)];
		return ByteWidth(metrics, b, 1000.0);
	}
};

StandardFont FontFromBaseName(std::string name) {
	if (const size_t plus = name.find('+'); plus == 6)
		name.erase(0, 7); // sous-ensemble « ABCDEF+Nom »
	for (size_t f = 0; f < kStandardFontCount; ++f)
		if (Metrics(StandardFont(f)).baseName == name)
			return StandardFont(f);
	const bool bold = name.find("Bold") != std::string::npos;
	const bool italic = name.find("Italic") != std::string::npos || name.find("Oblique") != std::string::npos;
	return MatchFont(name, bold, italic);
}

} // namespace

PdfResult<std::string> Reader::ExtractText(size_t index) const {
	auto content = PageContent(index);
	if (!content)
		return Err(content.Error());
	// Polices de la page.
	core::RobinMap<std::string, TextFont> fonts;
	if (const Object *res = m_pages[index].dictionary.Get("Resources"))
		if (auto resources = Resolve(*res); resources && resources.Value().IsDictionary())
			if (const Object *f = resources.Value().Get("Font"))
				if (auto dict = Resolve(*f); dict && dict.Value().IsDictionary())
					for (size_t k = 0; k < dict.Value().keys.size(); ++k) {
						auto font = Resolve(dict.Value().items[k]);
						if (!font || !font.Value().IsDictionary())
							continue;
						TextFont tf;
						if (const Object *base = font.Value().Get("BaseFont"))
							tf.metrics = FontFromBaseName(base->text);
						tf.firstChar = IntOr(font.Value().Get("FirstChar"), 0);
						if (const Object *w = font.Value().Get("Widths"))
							if (auto widths = Resolve(*w); widths && widths.Value().kind == ObjectKind::Array)
								for (const Object &v : widths.Value().items)
									tf.widths.push_back(v.Number());
						fonts.InsertOrAssign(dict.Value().keys[k], std::move(tf));
					}

	std::string out;
	double tm[6] = {1, 0, 0, 1, 0, 0}, tlm[6] = {1, 0, 0, 1, 0, 0};
	double size = 12, leading = 0, charSpacing = 0, wordSpacing = 0;
	const TextFont fallback;
	const TextFont *font = &fallback;
	double lastX = 0, lastY = 0;
	bool any = false, spacePending = false;
	std::vector<Object> operands;

	const auto translate = [&](double tx, double ty) {
		tlm[4] += tx * tlm[0] + ty * tlm[2];
		tlm[5] += tx * tlm[1] + ty * tlm[3];
		std::copy(std::begin(tlm), std::end(tlm), std::begin(tm));
	};
	const auto show = [&](const std::string &bytes) {
		const double scale = std::hypot(tm[2], tm[3]) > 0 ? std::hypot(tm[2], tm[3]) : 1.0;
		const double effective = size * scale;
		const double x = tm[4], y = tm[5];
		if (any) {
			if (std::abs(y - lastY) > effective * 0.5) {
				out.push_back('\n');
			} else if ((spacePending || x - lastX > effective * 0.2) && !out.empty() && out.back() != ' ' && out.back() != '\n') {
				out.push_back(' ');
			}
		}
		out += DecodeWinAnsi(bytes);
		double advance = 0;
		for (const char c : bytes) {
			const auto b = uint8_t(c);
			advance += font->Width(b) / 1000.0 * size + charSpacing + (b == ' ' ? wordSpacing : 0);
		}
		tm[4] += advance * tm[0];
		tm[5] += advance * tm[1];
		lastX = tm[4];
		lastY = tm[5];
		any = true;
		spacePending = false;
	};
	const auto num = [&](size_t i) { return i < operands.size() ? operands[i].Number() : 0.0; };

	Parser parser(content.Value());
	while (!parser.AtEnd()) {
		auto token = parser.NextToken();
		if (!token)
			break; // flux tronqué : on garde le texte déjà extrait
		if (!token.Value().keyword) {
			operands.push_back(std::move(token.Value().value));
			continue;
		}
		const std::string &op = token.Value().value.text;
		if (op == "BT") {
			const double id[6] = {1, 0, 0, 1, 0, 0};
			std::copy(std::begin(id), std::end(id), std::begin(tm));
			std::copy(std::begin(id), std::end(id), std::begin(tlm));
		} else if (op == "Tf" && operands.size() >= 2) {
			const TextFont *found = fonts.Find(operands[0].text);
			font = found ? found : &fallback;
			size = operands[1].Number();
		} else if (op == "TL") {
			leading = num(0);
		} else if (op == "Tc") {
			charSpacing = num(0);
		} else if (op == "Tw") {
			wordSpacing = num(0);
		} else if (op == "Td") {
			translate(num(0), num(1));
		} else if (op == "TD") {
			leading = -num(1);
			translate(num(0), num(1));
		} else if (op == "Tm" && operands.size() >= 6) {
			for (size_t k = 0; k < 6; ++k)
				tm[k] = tlm[k] = num(k);
		} else if (op == "T*") {
			translate(0, -leading);
		} else if (op == "Tj" && !operands.empty()) {
			show(operands.back().text);
		} else if (op == "'" && !operands.empty()) {
			translate(0, -leading);
			show(operands.back().text);
		} else if (op == "\"" && operands.size() >= 3) {
			wordSpacing = num(0);
			charSpacing = num(1);
			translate(0, -leading);
			show(operands[2].text);
		} else if (op == "TJ" && !operands.empty() && operands.back().kind == ObjectKind::Array) {
			for (const Object &item : operands.back().items) {
				if (item.kind == ObjectKind::String) {
					show(item.text);
				} else if (item.IsNumber()) {
					const double shift = -item.Number() / 1000.0 * size;
					tm[4] += shift * tm[0];
					tm[5] += shift * tm[1];
					if (item.Number() < -250)
						spacePending = true;
				}
			}
		}
		operands.clear();
	}
	return Ok(std::move(out));
}

std::optional<std::string> Reader::InfoString(std::string_view key) const {
	const Object *infoRef = m_trailer.Get("Info");
	if (!infoRef)
		return std::nullopt;
	auto info = Resolve(*infoRef);
	if (!info || !info.Value().IsDictionary())
		return std::nullopt;
	const Object *value = info.Value().Get(key);
	if (!value)
		return std::nullopt;
	auto resolved = Resolve(*value);
	if (!resolved || resolved.Value().kind != ObjectKind::String)
		return std::nullopt;
	return DecodeTextString(resolved.Value().text);
}

} // namespace pdf
