#include "heap.hpp"

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace script {

namespace {

enum class Kind : uint8_t { Closure, Instance, Class, List, Map, Env };

struct GcNode {
	Kind kind;
	void *ptr;
	std::shared_ptr<void> hold; ///< garde l'objet en vie pendant la collecte (+1 à `use_count`)
	long refs = 0;				///< références extérieures au graphe suivi
	bool reachable = false;
};

/// Graphe des objets suivis, plus les environnements qu'ils référencent.
class Graph {
public:
	template <typename T>
	void AddTracked(std::vector<std::weak_ptr<T>> &tracked, Kind kind) {
		for (const std::weak_ptr<T> &w : tracked)
			if (std::shared_ptr<T> p = w.lock()) {
				T *raw = p.get(); // avant le déplacement : ordre d'évaluation des arguments non spécifié
				Add(kind, raw, std::move(p));
			}
	}

	/// Ajoute tout ce qui est atteignable depuis les objets déjà présents : environnements,
	/// mais aussi listes, tables, instances, classes et fermetures non enregistrées à leur
	/// création (table d'export d'un module, résultat d'une fonction intégrée…). Sans cela, un
	/// conteneur non suivi au milieu d'un cycle compterait comme référence extérieure et
	/// garderait le cycle en vie. Ce que seul l'hôte référence n'est jamais atteint ici.
	void Discover() {
		for (std::size_t i = 0; i < m_nodes.size(); ++i) {
			const Kind kind = m_nodes[i].kind; // copies : `Add` peut réallouer `m_nodes`
			void *const ptr = m_nodes[i].ptr;
			Children(kind, ptr, [this](Kind k, std::shared_ptr<void> child) {
				void *raw = child.get();
				Add(k, raw, std::move(child));
			});
		}
	}

	std::size_t Collect() {
		for (GcNode &n : m_nodes)
			n.refs = n.hold.use_count() - 1;
		for (GcNode &n : m_nodes) // retranche les références internes au graphe
			Edges(n, [&](GcNode &target) { --target.refs; });
		std::vector<GcNode *> work;
		for (GcNode &n : m_nodes)
			if (n.refs > 0) {
				n.reachable = true;
				work.push_back(&n);
			}
		while (!work.empty()) {
			GcNode *n = work.back();
			work.pop_back();
			Edges(*n, [&](GcNode &target) {
				if (!target.reachable) {
					target.reachable = true;
					work.push_back(&target);
				}
			});
		}
		// Vide les objets inaccessibles. Leur contenu part dans `grave`, détruit après la
		// libération des `hold` : aucun objet ne disparaît pendant qu'on le parcourt.
		Grave grave;
		std::size_t freed = 0;
		for (GcNode &n : m_nodes)
			if (!n.reachable) {
				Clear(n, grave);
				++freed;
			}
		m_nodes.clear();
		m_index.clear();
		return freed;
	}

private:
	struct Grave {
		std::vector<Value> values;
		std::vector<std::shared_ptr<void>> objects;
	};

	void Add(Kind kind, void *ptr, std::shared_ptr<void> hold) {
		if (m_index.contains(ptr))
			return;
		m_index.emplace(ptr, m_nodes.size());
		m_nodes.push_back(GcNode{kind, ptr, std::move(hold)});
	}

	/// Énumère les références fortes d'un objet vers d'autres objets du graphe possible.
	template <typename F>
	static void ChildValue(const Value &v, F &f) {
		std::visit(
			[&]<typename T>(const T &x) {
				constexpr Kind kNone = Kind::Env; // sentinelle : type sans enfant suivi
				constexpr Kind kind = std::is_same_v<T, std::shared_ptr<List>>		 ? Kind::List
									: std::is_same_v<T, std::shared_ptr<Map>>		 ? Kind::Map
									: std::is_same_v<T, std::shared_ptr<Closure>>	 ? Kind::Closure
									: std::is_same_v<T, std::shared_ptr<ClassDef>> ? Kind::Class
									: std::is_same_v<T, std::shared_ptr<Instance>> ? Kind::Instance
																				   : kNone;
				if constexpr (kind != kNone) {
					if (x)
						f(kind, x);
				}
			},
			v.Raw());
	}

	template <typename F>
	static void Children(Kind kind, void *ptr, F &&f) {
		switch (kind) {
		case Kind::Closure:
			if (const auto &env = static_cast<Closure *>(ptr)->env)
				f(Kind::Env, env);
			break;
		case Kind::Instance: {
			const auto *inst = static_cast<Instance *>(ptr);
			if (inst->cls)
				f(Kind::Class, inst->cls);
			for (const auto &[k, v] : inst->fields)
				ChildValue(v, f);
			break;
		}
		case Kind::Class: {
			const auto *cls = static_cast<ClassDef *>(ptr);
			if (cls->parent)
				f(Kind::Class, cls->parent);
			for (const auto &c : cls->natives)
				if (c)
					f(Kind::Class, c);
			for (const auto &c : cls->interfaces)
				if (c)
					f(Kind::Class, c);
			for (const auto &[k, v] : cls->methods)
				ChildValue(v, f);
			for (const auto &[k, v] : cls->statics)
				ChildValue(v, f);
			if (cls->scope)
				f(Kind::Env, cls->scope);
			break;
		}
		case Kind::List:
			for (const Value &v : *static_cast<List *>(ptr))
				ChildValue(v, f);
			break;
		case Kind::Map:
			for (const auto &[k, v] : *static_cast<Map *>(ptr))
				ChildValue(v, f);
			break;
		case Kind::Env: {
			const auto *env = static_cast<Environment *>(ptr);
			for (const auto &[sym, v] : env->vars)
				ChildValue(v, f);
			if (env->parent)
				f(Kind::Env, env->parent);
			break;
		}
		}
	}

	GcNode *Find(const void *p) {
		if (!p)
			return nullptr;
		const auto it = m_index.find(p);
		return it == m_index.end() ? nullptr : &m_nodes[it->second];
	}

	template <typename F>
	void Ref(const void *p, F &f) {
		if (GcNode *t = Find(p))
			f(*t);
	}

	template <typename F>
	void RefValue(const Value &v, F &f) {
		std::visit(
			[&]<typename T>(const T &x) {
				if constexpr (std::is_same_v<T, std::shared_ptr<List>> || std::is_same_v<T, std::shared_ptr<Map>> ||
							  std::is_same_v<T, std::shared_ptr<Closure>> || std::is_same_v<T, std::shared_ptr<ClassDef>> ||
							  std::is_same_v<T, std::shared_ptr<Instance>>)
					Ref(x.get(), f);
			},
			v.Raw());
	}

	template <typename F>
	void Edges(const GcNode &n, F &&f) {
		switch (n.kind) {
		case Kind::Closure: Ref(static_cast<Closure *>(n.ptr)->env.get(), f); break;
		case Kind::Instance: {
			const auto *inst = static_cast<Instance *>(n.ptr);
			Ref(inst->cls.get(), f);
			for (const auto &[k, v] : inst->fields)
				RefValue(v, f);
			break;
		}
		case Kind::Class: {
			const auto *cls = static_cast<ClassDef *>(n.ptr);
			Ref(cls->parent.get(), f);
			for (const auto &c : cls->natives)
				Ref(c.get(), f);
			for (const auto &c : cls->interfaces)
				Ref(c.get(), f);
			for (const auto &[k, v] : cls->methods)
				RefValue(v, f);
			for (const auto &[k, v] : cls->statics)
				RefValue(v, f);
			Ref(cls->scope.get(), f);
			break;
		}
		case Kind::List:
			for (const Value &v : *static_cast<List *>(n.ptr))
				RefValue(v, f);
			break;
		case Kind::Map:
			for (const auto &[k, v] : *static_cast<Map *>(n.ptr))
				RefValue(v, f);
			break;
		case Kind::Env: {
			const auto *env = static_cast<Environment *>(n.ptr);
			for (const auto &[sym, v] : env->vars)
				RefValue(v, f);
			Ref(env->parent.get(), f);
			break;
		}
		}
	}

	static void Clear(GcNode &n, Grave &grave) {
		switch (n.kind) {
		case Kind::Closure: grave.objects.push_back(std::move(static_cast<Closure *>(n.ptr)->env)); break;
		case Kind::Instance: {
			auto *inst = static_cast<Instance *>(n.ptr);
			for (auto &[k, v] : inst->fields)
				grave.values.push_back(std::move(v));
			inst->fields.clear();
			grave.objects.push_back(std::move(inst->cls));
			break;
		}
		case Kind::Class: {
			auto *cls = static_cast<ClassDef *>(n.ptr);
			for (auto &[k, v] : cls->methods)
				grave.values.push_back(std::move(v));
			for (auto &[k, v] : cls->statics)
				grave.values.push_back(std::move(v));
			cls->methods.clear();
			cls->statics.clear();
			grave.objects.push_back(std::move(cls->parent));
			for (auto &c : cls->natives)
				grave.objects.push_back(std::move(c));
			for (auto &c : cls->interfaces)
				grave.objects.push_back(std::move(c));
			cls->natives.clear();
			cls->interfaces.clear();
			grave.objects.push_back(std::move(cls->scope));
			break;
		}
		case Kind::List: {
			auto *list = static_cast<List *>(n.ptr);
			std::ranges::move(*list, std::back_inserter(grave.values));
			list->clear();
			break;
		}
		case Kind::Map: {
			auto *map = static_cast<Map *>(n.ptr);
			for (auto &[k, v] : *map)
				grave.values.push_back(std::move(v));
			map->clear();
			break;
		}
		case Kind::Env: {
			auto *env = static_cast<Environment *>(n.ptr);
			for (auto &[sym, v] : env->vars)
				grave.values.push_back(std::move(v));
			env->vars.clear();
			grave.objects.push_back(std::move(env->parent));
			break;
		}
		}
	}

	std::vector<GcNode> m_nodes;
	std::unordered_map<const void *, std::size_t> m_index;
};

template <typename T>
void Prune(std::vector<std::weak_ptr<T>> &list) {
	std::erase_if(list, [](const std::weak_ptr<T> &w) { return w.expired(); });
}

} // namespace

std::size_t Heap::Collect() {
	std::size_t freed = 0;
	{
		Graph graph;
		graph.AddTracked(m_closures, Kind::Closure);
		graph.AddTracked(m_instances, Kind::Instance);
		graph.AddTracked(m_classes, Kind::Class);
		graph.AddTracked(m_lists, Kind::List);
		graph.AddTracked(m_maps, Kind::Map);
		graph.Discover();
		freed = graph.Collect();
	} // les objets vidés sont détruits ici
	Prune(m_closures);
	Prune(m_instances);
	Prune(m_classes);
	Prune(m_lists);
	Prune(m_maps);
	const std::size_t live = m_closures.size() + m_instances.size() + m_classes.size() + m_lists.size() + m_maps.size();
	m_threshold = std::max<std::size_t>(4096, live * 2);
	m_allocations = 0;
	return freed;
}

} // namespace script
