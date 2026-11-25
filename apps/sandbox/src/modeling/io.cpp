// Définitions de modeling/io.hpp
#include <sandbox/modeling/io.hpp>
#include <sandbox/modeling/operations.hpp>

#include <dataio/documents/gltf.hpp>
#include <dataio/documents/json.hpp>
#include <dataio/mesh_export.hpp>

#include <cstdio>
#include <map>

namespace modeling {

namespace {

using dataio::Node;
using dataio::NodePtr;

NodePtr Floats(std::initializer_list<float> values) {
	NodePtr a = Node::MakeArray();
	for (float v : values)
		a->Push(Node::MakeFloat(double(v)));
	return a;
}

NodePtr Ints(const std::vector<VertexId> &values) {
	NodePtr a = Node::MakeArray();
	for (VertexId v : values)
		a->Push(Node::MakeInt(int64_t(v)));
	return a;
}

double Number(const NodePtr &n, double fallback = 0.0) {
	if (!n)
		return fallback;
	if (n->IsInt())
		return double(n->intValue);
	if (n->IsFloat())
		return n->floatValue;
	return fallback;
}

int64_t Int(const NodePtr &n, int64_t fallback = -1) {
	if (!n)
		return fallback;
	if (n->IsInt())
		return n->intValue;
	if (n->IsFloat())
		return int64_t(n->floatValue);
	return fallback;
}

String Str(const NodePtr &n) { return n && n->IsString() ? n->stringValue : String(); }

Result<Loop, String> ReadLoop(const NodePtr &n, size_t vertexCount) {
	Loop loop;
	if (!n || !n->IsArray())
		return Err(String("contour manquant"));
	for (size_t i = 0; i < n->GetSize(); ++i) {
		const int64_t v = Int(n->At(i));
		if (v < 0 || size_t(v) >= vertexCount)
			return Err(String::Format("indice de sommet invalide : {}", static_cast<long long>(v)));
		loop.push_back(VertexId(v));
	}
	return Ok(std::move(loop));
}

Option<String> WriteText(const String &path, const String &text) {
	std::FILE *file = std::fopen(path.CStr(), "w");
	if (!file)
		return Some(String::Format("impossible d'écrire {}", path.CStr()));
	const size_t written = std::fwrite(text.CStr(), 1, text.GetSize(), file);
	const bool ok = std::fclose(file) == 0 && written == text.GetSize();
	return ok ? Option<String>(NONE) : Some(String::Format("écriture incomplète de {}", path.CStr()));
}

} // namespace

NodePtr ToJson(const Model &source) {
	Model model = source;
	model.Compact();
	NodePtr root = Node::MakeObject();
	root->Set("format", Node::MakeString(String(MODEL_FORMAT)));
	root->Set("version", Node::MakeInt(MODEL_VERSION));

	NodePtr materials = Node::MakeArray();
	for (const Material &m : model.Materials()) {
		NodePtr o = Node::MakeObject();
		o->Set("name", Node::MakeString(m.name));
		o->Set("color", Floats({m.color.r, m.color.g, m.color.b, m.color.a}));
		o->Set("texture_scale", Node::MakeFloat(double(m.textureScale)));
		if (m.kind != MaterialKind::Plastic)
			o->Set("kind", Node::MakeString(String(MaterialKindName(m.kind))));
		o->Set("roughness", Node::MakeFloat(double(m.roughness)));
		o->Set("strength", Node::MakeFloat(double(m.strength)));
		materials->Push(o);
	}
	root->Set("materials", materials);

	NodePtr layers = Node::MakeArray();
	for (const Layer &l : model.Layers()) {
		NodePtr o = Node::MakeObject();
		o->Set("name", Node::MakeString(l.name));
		o->Set("visible", Node::MakeBool(l.visible));
		layers->Push(o);
	}
	root->Set("layers", layers);

	// Sommets en tableau plat x, y, z : compact et lisible.
	NodePtr vertices = Node::MakeArray();
	for (const Vertex &v : model.Vertices())
		for (float c : {v.position.x, v.position.y, v.position.z})
			vertices->Push(Node::MakeFloat(double(c)));
	root->Set("vertices", vertices);

	// Arêtes : [a, b, drapeaux (1 adoucie, 2 lisse), courbe (-1 = aucune), calque].
	NodePtr edges = Node::MakeArray();
	for (const Edge &e : model.Edges()) {
		NodePtr a = Node::MakeArray();
		a->Push(Node::MakeInt(e.a));
		a->Push(Node::MakeInt(e.b));
		a->Push(Node::MakeInt((e.soft ? 1 : 0) | (e.smooth ? 2 : 0)));
		a->Push(Node::MakeInt(e.curve == INVALID_ID ? -1 : int64_t(e.curve)));
		a->Push(Node::MakeInt(e.layer));
		edges->Push(a);
	}
	root->Set("edges", edges);

	NodePtr faces = Node::MakeArray();
	for (const Face &f : model.Faces()) {
		NodePtr o = Node::MakeObject();
		o->Set("outer", Ints(f.outer));
		if (!f.holes.empty()) {
			NodePtr holes = Node::MakeArray();
			for (const Loop &h : f.holes)
				holes->Push(Ints(h));
			o->Set("holes", holes);
		}
		o->Set("material", Node::MakeInt(f.material == INVALID_ID ? -1 : int64_t(f.material)));
		if (f.layer)
			o->Set("layer", Node::MakeInt(f.layer));
		faces->Push(o);
	}
	root->Set("faces", faces);
	return root;
}

Result<Model, String> FromJson(const NodePtr &root) {
	if (!root || !root->IsObject())
		return Err(String("modèle : document JSON attendu"));
	if (const String format = Str(root->Get("format")); format != MODEL_FORMAT && format != "modeling.model")
		return Err(String::Format("modèle : format « {} » attendu", MODEL_FORMAT));
	if (Int(root->Get("version"), 0) > MODEL_VERSION)
		return Err(String("modèle : version plus récente que ce programme"));
	Model model;
	if (NodePtr materials = root->Get("materials"); materials && materials->IsArray())
		for (size_t i = 0; i < materials->GetSize(); ++i) {
			const NodePtr m = materials->At(i);
			const NodePtr c = m ? m->Get("color") : nullptr;
			Color color;
			if (c && c->IsArray() && c->GetSize() >= 3)
				color = {float(Number(c->At(0))), float(Number(c->At(1))), float(Number(c->At(2))),
						 float(Number(c->At(3), 1.0))};
			Material mat{Str(m ? m->Get("name") : nullptr), color, float(Number(m ? m->Get("texture_scale") : nullptr, 1.0))};
			mat.kind = MaterialKindFromName(std::string_view(Str(m ? m->Get("kind") : nullptr)));
			mat.roughness = float(Number(m ? m->Get("roughness") : nullptr, 0.85));
			mat.strength = float(Number(m ? m->Get("strength") : nullptr, 4.0));
			model.Materials().push_back(std::move(mat));
		}
	if (NodePtr layers = root->Get("layers"); layers && layers->IsArray())
		for (size_t i = 0; i < layers->GetSize(); ++i) {
			const NodePtr l = layers->At(i);
			const NodePtr visible = l ? l->Get("visible") : nullptr;
			model.Layers().push_back(Layer{Str(l ? l->Get("name") : nullptr), !visible || !visible->IsBool() || visible->boolValue});
		}
	const NodePtr vertices = root->Get("vertices");
	if (!vertices || !vertices->IsArray() || vertices->GetSize() % 3 != 0)
		return Err(String("modèle : tableau de sommets invalide"));
	for (size_t i = 0; i + 2 < vertices->GetSize(); i += 3)
		model.Vertices().push_back(Vertex{{float(Number(vertices->At(i))), float(Number(vertices->At(i + 1))),
										   float(Number(vertices->At(i + 2)))},
										  true});
	const size_t vertexCount = model.Vertices().size();
	uint32_t maxCurve = 0;
	if (NodePtr edges = root->Get("edges"); edges && edges->IsArray())
		for (size_t i = 0; i < edges->GetSize(); ++i) {
			const NodePtr e = edges->At(i);
			if (!e || !e->IsArray() || e->GetSize() < 2)
				return Err(String("modèle : arête invalide"));
			const int64_t a = Int(e->At(0)), b = Int(e->At(1));
			if (a < 0 || b < 0 || size_t(a) >= vertexCount || size_t(b) >= vertexCount)
				return Err(String("modèle : arête hors des sommets"));
			Edge edge;
			edge.a = VertexId(a);
			edge.b = VertexId(b);
			const int64_t flags = Int(e->At(2), 0), curve = Int(e->At(3), -1);
			edge.soft = (flags & 1) != 0;
			edge.smooth = (flags & 2) != 0;
			edge.curve = curve < 0 ? INVALID_ID : uint32_t(curve);
			edge.layer = uint32_t(Int(e->At(4), 0));
			if (edge.curve != INVALID_ID)
				maxCurve = math::Max(maxCurve, edge.curve + 1);
			model.Edges().push_back(edge);
		}
	model.ReserveCurveIds(maxCurve);
	if (NodePtr faces = root->Get("faces"); faces && faces->IsArray())
		for (size_t i = 0; i < faces->GetSize(); ++i) {
			const NodePtr f = faces->At(i);
			Result<Loop, String> outer = ReadLoop(f ? f->Get("outer") : nullptr, vertexCount);
			if (outer.IsError())
				return Err(String::Format("modèle : face {} : {}", i, outer.Error().CStr()));
			Face face;
			face.outer = std::move(outer).Unwrap();
			if (NodePtr holes = f->Get("holes"); holes && holes->IsArray())
				for (size_t h = 0; h < holes->GetSize(); ++h) {
					Result<Loop, String> hole = ReadLoop(holes->At(h), vertexCount);
					if (hole.IsError())
						return Err(String::Format("modèle : trou de la face {} : {}", i, hole.Error().CStr()));
					face.holes.push_back(std::move(hole).Unwrap());
				}
			const int64_t material = Int(f->Get("material"), -1);
			face.material = material < 0 || size_t(material) >= model.Materials().size() ? INVALID_ID : uint32_t(material);
			face.layer = uint32_t(Int(f->Get("layer"), 0));
			if (face.outer.size() < 3)
				continue;
			model.Faces().push_back(std::move(face));
			model.UpdateFaceNormal(FaceId(model.Faces().size() - 1));
		}
	model.InvalidateIndices();
	return Ok(std::move(model));
}

String Serialize(const Model &model) {
	dataio::JsonDocument doc;
	doc.SetRoot(ToJson(model));
	return doc.EncodeStr();
}

Result<Model, String> Deserialize(const String &json) {
	dataio::JsonDocument doc;
	if (Option<dataio::ParseError> err = doc.DecodeStr(json); err.IsSome())
		return Err(String::Format("modèle : JSON invalide ({})", err.Value().Format().CStr()));
	return FromJson(doc.GetRoot());
}

Option<String> SaveModel(const String &path, const Model &model) { return WriteText(path, Serialize(model)); }

Result<Model, String> LoadModel(const String &path) {
	std::FILE *file = std::fopen(path.CStr(), "rb");
	if (!file)
		return Err(String::Format("impossible de lire {}", path.CStr()));
	String text;
	char buffer[16384];
	size_t n = 0;
	while ((n = std::fread(buffer, 1, sizeof buffer, file)) > 0)
		text += String(buffer, n);
	std::fclose(file);
	Result<Model, String> model = Deserialize(text);
	if (model.IsError())
		return Err(String::Format("{} : {}", path.CStr(), model.Error().CStr()));
	return model;
}

Option<String> ExportModel(const String &path, const Model &model) {
	const MeshData mesh = BuildMesh(model);
	std::vector<float> positions, normals, uvs;
	std::vector<uint8_t> colors;
	for (size_t i = 0; i < mesh.positions.size(); ++i) {
		positions.insert(positions.end(), {mesh.positions[i].x, mesh.positions[i].y, mesh.positions[i].z});
		normals.insert(normals.end(), {mesh.normals[i].x, mesh.normals[i].y, mesh.normals[i].z});
		uvs.insert(uvs.end(), {mesh.uvs[i].x, mesh.uvs[i].y});
		colors.insert(colors.end(), {uint8_t(math::Clamp(mesh.colors[i].r, 0.f, 1.f) * 255.f),
									 uint8_t(math::Clamp(mesh.colors[i].g, 0.f, 1.f) * 255.f),
									 uint8_t(math::Clamp(mesh.colors[i].b, 0.f, 1.f) * 255.f)});
	}
	std::vector<dataio::MeshMaterialGroup> groups;
	for (const MeshGroup &g : mesh.groups) {
		dataio::MeshMaterialGroup out;
		if (g.material < model.Materials().size()) {
			const Material &m = model.Materials()[g.material];
			out.name = m.name.IsEmpty() ? String::Format("materiau_{}", g.material) : m.name.Replace(' ', '_');
			out.r = m.color.r;
			out.g = m.color.g;
			out.b = m.color.b;
			out.a = m.color.a;
		} else {
			out.name = String("defaut");
		}
		out.firstIndex = g.start;
		out.indexCount = g.count;
		groups.push_back(std::move(out));
	}
	dataio::TriangleMeshView view;
	view.positions = positions;
	view.normals = normals;
	view.uvs = uvs;
	view.colors = colors;
	view.indices = mesh.indices;
	view.materials = groups;
	return dataio::SaveMesh(path, view, String("Exporté par modeling:: (SDL3_Wrapper)"));
}

Model FromTriangles(std::span<const Vec3> positions, std::span<const uint32_t> indices, const ImportOptions &options) {
	Model model;
	std::vector<VertexId> ids;
	ids.reserve(positions.size());
	for (const Vec3 &p : positions)
		ids.push_back(model.AddVertex(p));
	std::vector<FaceId> faces;
	for (size_t i = 0; i + 2 < indices.size(); i += 3) {
		if (indices[i] >= ids.size() || indices[i + 1] >= ids.size() || indices[i + 2] >= ids.size())
			continue;
		Option<FaceId> f = model.AddFaceFromVertices({ids[indices[i]], ids[indices[i + 1]], ids[indices[i + 2]]}, {},
													 FaceOptions{options.material});
		if (f.IsSome())
			faces.push_back(f.Unwrap());
	}
	if (options.mergeCoplanar)
		for (FaceId f : faces)
			if (model.IsAlive(f))
				(void)model.MergeCoplanar(f);
	(void)SoftenEdges(model, options.softenAngle);
	return model;
}

namespace {

/// Matrice locale d'un nœud glTF : translation · rotation (quaternion) · échelle.
math::FMatrix4 NodeMatrix(const dataio::gltf::Node &n) {
	const math::FQuaternion q{n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]};
	math::FMatrix4 r = math::FMatrix4::Identity();
	const math::FVector3 axes[3] = {q.Rotate({1.f, 0.f, 0.f}), q.Rotate({0.f, 1.f, 0.f}), q.Rotate({0.f, 0.f, 1.f})};
	for (int c = 0; c < 3; ++c) {
		r.m[c][0] = axes[c].x;
		r.m[c][1] = axes[c].y;
		r.m[c][2] = axes[c].z;
	}
	return math::FMatrix4::Translation({n.translation[0], n.translation[1], n.translation[2]}) * r *
		   math::FMatrix4::Scale({n.scale[0], n.scale[1], n.scale[2]});
}

} // namespace

Result<Model, String> ImportGltf(const String &path, std::vector<Material> &palette, const ImportOptions &options) {
	auto loaded = dataio::gltf::LoadFile(path, true);
	if (loaded.IsError())
		return Err(loaded.Error());
	const dataio::gltf::Document &g = loaded.Value();
	// Matériau glTF → index de la palette (par nom ; ajouté s'il manque).
	std::vector<uint32_t> materialOf(g.materials.size(), options.material);
	for (size_t i = 0; i < g.materials.size(); ++i) {
		const dataio::gltf::Material &m = g.materials[i];
		const String name = m.name.IsEmpty() ? String::Format("glTF {}", i + 1) : m.name;
		const auto it = std::ranges::find(palette, name, &Material::name);
		if (it != palette.end())
			materialOf[i] = uint32_t(it - palette.begin());
		else {
			materialOf[i] = uint32_t(palette.size());
			palette.push_back({name, {m.baseColor[0], m.baseColor[1], m.baseColor[2], m.baseColor[3]}, 1.f});
		}
	}
	// Triangles du monde, regroupés par matériau.
	struct Batch {
		std::vector<Vec3> positions;
		std::vector<uint32_t> indices;
	};
	std::map<uint32_t, Batch> batches;
	String failure;
	const auto addMesh = [&](int meshIndex, const math::FMatrix4 &world) {
		if (meshIndex < 0 || size_t(meshIndex) >= g.meshes.size())
			return;
		for (const dataio::gltf::Primitive &prim : g.meshes[size_t(meshIndex)].primitives) {
			const int position = prim.Find("POSITION");
			if (prim.mode != 4 || position < 0)
				continue; // seuls les triangles portent des faces
			auto floats = g.ReadFloats(position);
			if (floats.IsError()) {
				failure = floats.Error();
				continue;
			}
			const std::vector<float> &f = floats.Value();
			const uint32_t material = prim.material >= 0 && size_t(prim.material) < materialOf.size() ? materialOf[size_t(prim.material)] : options.material;
			Batch &batch = batches[material];
			const uint32_t base = uint32_t(batch.positions.size());
			for (size_t i = 0; i + 2 < f.size(); i += 3)
				batch.positions.push_back(world.TransformPoint({f[i], f[i + 1], f[i + 2]}));
			const uint32_t count = uint32_t(f.size() / 3);
			if (prim.indices >= 0) {
				auto indices = g.ReadIndices(prim.indices);
				if (indices.IsError()) {
					failure = indices.Error();
					continue;
				}
				for (const uint32_t i : indices.Value())
					if (i < count)
						batch.indices.push_back(base + i);
			} else {
				for (uint32_t i = 0; i < count; ++i)
					batch.indices.push_back(base + i);
			}
		}
	};
	const auto visit = [&](auto &&self, int nodeIndex, const math::FMatrix4 &parent, int depth) -> void {
		if (nodeIndex < 0 || size_t(nodeIndex) >= g.nodes.size() || depth > 64)
			return; // hiérarchie invalide ou cyclique
		const dataio::gltf::Node &n = g.nodes[size_t(nodeIndex)];
		const math::FMatrix4 world = parent * NodeMatrix(n);
		addMesh(n.mesh, world);
		for (const int child : n.children)
			self(self, child, world, depth + 1);
	};
	if (!g.scenes.empty()) {
		const size_t scene = g.defaultScene >= 0 && size_t(g.defaultScene) < g.scenes.size() ? size_t(g.defaultScene) : 0;
		for (const int root : g.scenes[scene].nodes)
			visit(visit, root, math::FMatrix4::Identity(), 0);
	} else if (!g.nodes.empty()) {
		std::vector<bool> child(g.nodes.size(), false);
		for (const dataio::gltf::Node &n : g.nodes)
			for (const int c : n.children)
				if (c >= 0 && size_t(c) < child.size())
					child[size_t(c)] = true;
		for (size_t i = 0; i < g.nodes.size(); ++i)
			if (!child[i])
				visit(visit, int(i), math::FMatrix4::Identity(), 0);
	} else {
		for (size_t i = 0; i < g.meshes.size(); ++i)
			addMesh(int(i), math::FMatrix4::Identity());
	}
	if (batches.empty())
		return Err(failure.IsEmpty() ? String::Format("glTF : aucun triangle dans {}", path) : failure);
	Model model;
	model.Materials() = palette;
	for (const auto &[material, batch] : batches) {
		ImportOptions part = options;
		part.material = material;
		Model piece = FromTriangles(batch.positions, batch.indices, part);
		piece.Materials() = palette; // même palette : les index de matériaux sont conservés
		(void)model.Append(piece);
	}
	model.Touch();
	return Ok(std::move(model));
}

} // namespace modeling
