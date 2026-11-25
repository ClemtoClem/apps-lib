#include "script/script.hpp"
#include "heap.hpp"
#include "internal.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <random>
#include <stop_token>
#include <thread>

#include <core/robin_map.hpp>
#include <core/string.hpp>

namespace script {


// ════════════════════════════════════════════════════════════════════════
// Implémentation de l'interpréteur
// ════════════════════════════════════════════════════════════════════════
namespace {
/// État d'exécution par fil : les fonctions `async` tournent chacune sur leur
/// propre fil, avec leur propre erreur/valeur de retour/profondeur/budget.
struct ExecState {
	ScriptError error;
	bool failed = false;
	Value ret;
	int depth = 0;
	uint64_t steps = 0;
	const Program *current = nullptr;
	std::shared_ptr<const void> currentProgram;
	std::stop_token stop; ///< tâche `async` : arrêt demandé à la destruction de l'interpréteur
};
thread_local ExecState t_exec;

/// Verrou global de l'interpréteur (« GIL ») : un seul fil exécute du script à la fois.
/// Les listes, tables, instances et environnements sont partagés par référence ; sans ce
/// verrou, une fonction `async` et le fil principal les modifieraient en concurrence.
/// Réentrant pour le fil propriétaire (natives qui rappellent l'interpréteur) ; les attentes
/// (`await`, `.wait()`, `sleep`) le relâchent entièrement pour laisser tourner les autres fils.
class Gil {
public:
	void Lock() {
		const auto me = std::this_thread::get_id();
		if (m_owner.load(std::memory_order_relaxed) == me) { // seul ce fil a pu écrire son propre id
			++m_depth;
			return;
		}
		m_mutex.lock();
		m_owner.store(me, std::memory_order_relaxed);
		m_depth = 1;
	}
	void Unlock() {
		if (--m_depth == 0) {
			m_owner.store({}, std::memory_order_relaxed);
			m_mutex.unlock();
		}
	}
	/// Relâche entièrement le verrou s'il est tenu par ce fil ; rend la profondeur à restaurer.
	[[nodiscard]] int ReleaseAll() {
		if (m_owner.load(std::memory_order_relaxed) != std::this_thread::get_id())
			return 0;
		const int depth = std::exchange(m_depth, 0);
		m_owner.store({}, std::memory_order_relaxed);
		m_mutex.unlock();
		return depth;
	}
	/// Profondeur de réentrance si ce fil tient le verrou, 0 sinon.
	[[nodiscard]] int OwnedDepth() const {
		return m_owner.load(std::memory_order_relaxed) == std::this_thread::get_id() ? m_depth : 0;
	}
	void Reacquire(int depth) {
		if (depth == 0)
			return;
		m_mutex.lock();
		m_owner.store(std::this_thread::get_id(), std::memory_order_relaxed);
		m_depth = depth;
	}

private:
	std::mutex m_mutex;
	std::atomic<std::thread::id> m_owner{};
	int m_depth = 0; ///< modifié uniquement par le fil propriétaire
};

class GilLock {
public:
	explicit GilLock(Gil &gil) : m_gil(gil) { m_gil.Lock(); }
	~GilLock() { m_gil.Unlock(); }
	GilLock(const GilLock &) = delete;
	GilLock &operator=(const GilLock &) = delete;

private:
	Gil &m_gil;
};

/// Relâche le GIL le temps d'une attente bloquante, puis le reprend.
class GilUnlocked {
public:
	explicit GilUnlocked(Gil &gil) : m_gil(gil), m_depth(gil.ReleaseAll()) {}
	~GilUnlocked() { m_gil.Reacquire(m_depth); }
	GilUnlocked(const GilUnlocked &) = delete;
	GilUnlocked &operator=(const GilUnlocked &) = delete;

private:
	Gil &m_gil;
	int m_depth;
};

/// Fil d'une fonction `async` (ou d'un `.then`) : possédé par l'interpréteur, arrêté et
/// joint à sa destruction (plus de fil détaché survivant à l'interpréteur).
struct AsyncTask {
	std::shared_ptr<std::atomic<bool>> done;
	std::jthread thread;
};
} // namespace

struct Interpreter::Impl {
	Interpreter &self;
	core::RobinMap<std::string, uint32_t, core::Hasher<std::string>> symbolIds;
	std::vector<std::string> symbolNames;
	core::RobinMap<uint32_t, Value> globals;
	std::vector<std::shared_ptr<Program>> programs;
	std::mt19937_64 rng{0x5eed};
	std::map<std::string, Value, std::less<>> modules; ///< modules importés (nil : en cours de chargement)
	std::vector<uint32_t> classOrder;				   ///< classes déclarées (ordre de `Classes()`)
	Gil gil;   ///< un seul fil exécute du script à la fois (voir `Gil`)
	Heap heap; ///< collecteur des cycles de références (voir `heap.hpp`)
	/// Fils `async` en cours. Déclaré en dernier : détruit (arrêt + jointure) en premier,
	/// pendant que le reste de l'interpréteur est encore valide.
	std::vector<AsyncTask> tasks;

	explicit Impl(Interpreter &s) : self(s) {}
	~Impl() {
		tasks.clear(); // arrête et joint les fils `async` avant de démonter le reste
		GilLock lock(gil);
		globals.Clear();
		modules.clear();
		heap.Collect(); // les cycles qui ne tenaient que par les globales
	}
	Impl(const Impl &) = delete;
	Impl &operator=(const Impl &) = delete;

	/// Collecte des cycles à l'entrée la plus externe de l'API (verrou tenu une seule fois
	/// par ce fil) : aucune évaluation n'est alors en cours sur ce fil.
	void MaybeCollect() {
		if (gil.OwnedDepth() == 1 && heap.Due())
			heap.Collect();
	}

	uint32_t Intern(std::string_view name) {
		GilLock lock(gil);
		if (const uint32_t *id = symbolIds.Find(name))
			return *id;
		const auto id = static_cast<uint32_t>(symbolNames.size());
		symbolNames.emplace_back(name);
		symbolIds.InsertOrAssign(std::string(name), id);
		return id;
	}

	bool Fail(const Node *at, std::string msg) {
		if (!t_exec.failed) {
			t_exec.failed = true;
			t_exec.error = {std::move(msg), t_exec.current ? t_exec.current->file : "", at ? at->line : 0, at ? at->col : 0};
		}
		return false;
	}


	// ── Évaluation ─────────────────────────────────────────────────────────
	enum class Flow : uint8_t { Normal, Return, Break, Continue, Error };

	/// Lit une variable : locale (portée) d'abord, puis globale (copie, verrou).
	bool Lookup(uint32_t sym, const EnvPtr &env, Value &out) {
		if (env)
			if (Value *v = env->Find(sym)) {
				out = *v;
				return true;
			}
		GilLock lock(gil);
		if (const Value *v = globals.Find(sym)) {
			out = *v;
			return true;
		}
		return false;
	}
	/// Écrit une variable : locale (portée) ou globale (verrou).
	void Store(uint32_t sym, const EnvPtr &env, Value v) {
		if (env)
			if (Value *slot = env->Find(sym)) {
				*slot = std::move(v);
				return;
			}
		GilLock lock(gil);
		globals.InsertOrAssign(sym, std::move(v));
	}
	/// Déclare / remplace une variable GLOBALE (verrou).
	void SetGlobal(uint32_t sym, Value v) {
		GilLock lock(gil);
		globals.InsertOrAssign(sym, std::move(v));
	}

	bool Step(const Node &n) {
		if (self.m_stepLimit && ++t_exec.steps > self.m_stepLimit)
			return Fail(&n, "limite d'instructions dépassée (boucle infinie ?)");
		if (t_exec.stop.stop_requested())
			return Fail(&n, "tâche async interrompue (interpréteur détruit)");
		return true;
	}

	/// Lance `body` sur un fil possédé par l'interpréteur ; `body` s'exécute sous le GIL.
	/// Appelé sous le GIL : les fils terminés sont joints au passage.
	template <typename Body>
	void Spawn(Body body) {
		std::erase_if(tasks, [](const AsyncTask &t) { return t.done->load(std::memory_order_acquire); });
		auto done = std::make_shared<std::atomic<bool>>(false);
		tasks.push_back(AsyncTask{done, std::jthread([this, done, body = std::move(body)](std::stop_token stop) mutable {
			t_exec.stop = std::move(stop);
			{
				GilLock lock(gil);
				body();
			}
			done->store(true, std::memory_order_release);
		})});
	}

	/// Attend la fin d'un future en relâchant le GIL (interrompue si l'interpréteur est détruit).
	bool WaitFuture(const Node *at, Future &f, Value &out) {
		bool stopped = false;
		bool failed = false;
		std::string error;
		{
			GilUnlocked unlocked(gil); // jamais f.mutex tenu pendant la reprise du GIL
			std::unique_lock<std::mutex> lock(f.mutex);
			if (!f.ready.wait(lock, t_exec.stop, [&] { return f.state != Future::State::Pending; }))
				stopped = true;
			else {
				f.observed = true;
				failed = f.state == Future::State::Failed;
				if (failed)
					error = f.error;
				else
					out = f.value;
			}
		}
		if (stopped)
			return Fail(at, "tâche async interrompue (interpréteur détruit)");
		if (failed)
			return Fail(at, std::move(error));
		return true;
	}

	static bool ToKey(const Value &v, std::string &out) {
		if (v.IsString()) {
			out = std::string(v.AsString());
			return true;
		}
		if (v.IsNumber() || v.Type() == ValueType::Bool) {
			out = v.ToString();
			return true;
		}
		return false;
	}

	bool Arith(const Node &n, std::string_view op, const Value &a, const Value &b, Value &out) {
		if (op == "..") {
			out = Value(a.ToString() + b.ToString());
			return true;
		}
		if (op == "is" || op == "as") { // test de type / transtypage contrôlé (classe ou interface)
			const auto *cls = std::get_if<std::shared_ptr<ClassDef>>(&b.Raw());
			const bool ok = cls && a.IsInstance() && a.AsInstance()->cls->Derives((*cls)->name);
			if (op == "is") {
				out = Value(ok);
				return true;
			}
			if (!ok)
				return Fail(&n, std::format("transtypage impossible : {} n'est pas {}", a.TypeName(), b.ToString()));
			out = a;
			return true;
		}
		if (op == "==" || op == "!=") {
			bool eq = a.Equals(b);
			out = Value(op == "==" ? eq : !eq);
			return true;
		}
		if (op == "+" && (a.IsString() || b.IsString())) {
			out = Value(a.ToString() + b.ToString());
			return true;
		}
		if (op == "+" && a.IsList() && b.IsList()) {
			List l = *a.AsList();
			l.insert(l.end(), b.AsList()->begin(), b.AsList()->end());
			out = Value(std::move(l));
			return true;
		}
		if (a.IsString() && b.IsString() && (op == "<" || op == "<=" || op == ">" || op == ">=")) {
			const int c = a.AsString().compare(b.AsString());
			out = Value(op == "<" ? c < 0 : op == "<=" ? c <= 0 : op == ">" ? c > 0 : c >= 0);
			return true;
		}
		if (!a.IsNumber() || !b.IsNumber())
			return Fail(&n, std::format("opérateur '{}' impossible entre {} et {}", op, a.TypeName(), b.TypeName()));
		const double x = a.AsNumber(), y = b.AsNumber();
		switch (op[0]) {
		case '+': out = Value(x + y); return true;
		case '-': out = Value(x - y); return true;
		case '*': out = Value(x * y); return true;
		case '/': out = Value(x / y); return true;
		case '%': out = Value(y == 0 ? 0.0 : x - std::floor(x / y) * y); return true;
		case '^': out = Value(std::pow(x, y)); return true;
		case '<': out = Value(op.size() == 1 ? x < y : x <= y); return true;
		case '>': out = Value(op.size() == 1 ? x > y : x >= y); return true;
		default: return Fail(&n, std::format("opérateur inconnu '{}'", op));
		}
	}

	bool IndexGet(const Node &n, const Value &obj, const Value &key, Value &out) {
		if (List *l = obj.AsList()) {
			if (!key.IsNumber())
				return Fail(&n, "index de liste non numérique");
			const double d = key.AsNumber();
			const auto idx = static_cast<int64_t>(d < 0 ? static_cast<double>(l->size()) + d : d);
			out = (idx >= 0 && static_cast<size_t>(idx) < l->size()) ? (*l)[static_cast<size_t>(idx)] : Value();
			return true;
		}
		if (Map *m = obj.AsMap()) {
			std::string k;
			if (!ToKey(key, k))
				return Fail(&n, "clé de table invalide");
			auto it = m->find(k);
			out = it == m->end() ? Value() : it->second;
			return true;
		}
		if (Instance *o = obj.AsInstance()) {
			std::string k;
			if (!ToKey(key, k))
				return Fail(&n, "nom de champ invalide");
			auto it = o->fields.find(k);
			out = it == o->fields.end() ? Value() : it->second;
			return true;
		}
		if (obj.IsString() && key.IsNumber()) {
			const std::string_view s = obj.AsString();
			const auto idx = static_cast<size_t>(key.AsNumber());
			out = idx < s.size() ? Value(std::string(1, s[idx])) : Value();
			return true;
		}
		if (obj.IsNil())
			return Fail(&n, "indexation de nil");
		return Fail(&n, std::format("{} non indexable", obj.TypeName()));
	}

	bool IndexSet(const Node &n, const Value &obj, const Value &key, Value v) {
		if (List *l = obj.AsList()) {
			const auto idx = static_cast<int64_t>(key.AsNumber(-1));
			if (idx < 0)
				return Fail(&n, "index de liste invalide");
			if (static_cast<size_t>(idx) >= l->size())
				l->resize(static_cast<size_t>(idx) + 1);
			(*l)[static_cast<size_t>(idx)] = std::move(v);
			return true;
		}
		if (Map *m = obj.AsMap()) {
			std::string k;
			if (!ToKey(key, k))
				return Fail(&n, "clé de table invalide");
			if (v.IsNil())
				m->erase(k);
			else
				m->insert_or_assign(std::move(k), std::move(v));
			return true;
		}
		if (Instance *o = obj.AsInstance()) {
			std::string k;
			if (!ToKey(key, k))
				return Fail(&n, "nom de champ invalide");
			o->fields.insert_or_assign(std::move(k), std::move(v));
			return true;
		}
		return Fail(&n, std::format("affectation indexée impossible sur {}", obj.TypeName()));
	}

	bool Assign(const Node &target, const EnvPtr &env, Value v) {
		if (target.kind == NK::Ident) {
			Store(target.sym, env, std::move(v)); // locale ou globale (verrou)
			return true;
		}
		Value obj;
		if (!Eval(*target.kids[0], env, obj))
			return false;
		if (target.kind == NK::Member)
			return IndexSet(target, obj, Value(target.text), std::move(v));
		Value key;
		return Eval(*target.kids[1], env, key) && IndexSet(target, obj, key, std::move(v));
	}

	// ── Classes ────────────────────────────────────────────────────────────
	struct Found {
		Value fn;					  ///< méthode de script
		const ClassDef *owner = nullptr; ///< classe qui la définit (pour `super`)
		const NativeMethod *native = nullptr;
		[[nodiscard]] bool Ok() const noexcept { return native || fn.IsCallable(); }
	};
	/// Méthode : classe, parents de script, puis bases natives (classe et ancêtres).
	static Found FindMethod(const ClassDef &cls, std::string_view name) {
		for (const ClassDef *c = &cls; c; c = c->parent.get())
			if (auto it = c->methods.find(name); it != c->methods.end())
				return {it->second, c, nullptr};
		for (const ClassDef *n : cls.AllNatives())
			if (auto it = n->nativeMethods.find(name); it != n->nativeMethods.end())
				return {Value(), n, &it->second};
		return {};
	}
	bool CallMethod(const Node *at, const std::shared_ptr<Instance> &inst, const Found &m, std::span<const Value> args, Value &out) {
		if (m.native) {
			auto r = (*m.native)(self, *inst, args);
			if (t_exec.failed)
				return false;
			if (!r)
				return Fail(at, std::format("{}.{}", m.owner ? m.owner->name : "", r.Error()));
			out = std::move(r.Value());
			return true;
		}
		const auto *clo = std::get_if<std::shared_ptr<Closure>>(&m.fn.Raw());
		if (!clo)
			return CallValue(at, m.fn, args, out);
		return CallClosure(at, **clo, args, out, Value(inst), m.owner);
	}
	bool Instantiate(const Node *at, const std::shared_ptr<ClassDef> &cls, std::span<const Value> args, const std::function<void(Instance &)> *prepare,
					 Value &out) {
		if (cls->native && !cls->makeState)
			return Fail(at, std::format("la classe native {} ne s'instancie pas seule", cls->name));
		if (cls->abstract)
			return Fail(at, std::format("la classe abstraite {} ne s'instancie pas (dérivez-la)", cls->name));
		auto inst = std::make_shared<Instance>();
		heap.Track(inst);
		inst->cls = cls;
		for (const ClassDef *n : cls->AllNatives())
			inst->states.emplace_back(n, n->makeState ? n->makeState() : nullptr);
		// Champs : des ancêtres vers la classe, chacun évalué dans la portée de sa déclaration.
		std::vector<const ClassDef *> chain;
		for (const ClassDef *c = cls.get(); c; c = c->parent.get())
			chain.push_back(c);
		for (auto c = chain.rbegin(); c != chain.rend(); ++c)
			for (const auto &[name, raw] : (*c)->fields) {
				const Node *field = static_cast<const Node *>(raw);
				Value v;
				if (!field->kids.empty() && !Eval(*field->kids[0], std::static_pointer_cast<Environment>((*c)->scope), v))
					return false;
				inst->fields.insert_or_assign(name, std::move(v));
			}
		if (prepare && *prepare)
			(*prepare)(*inst);
		if (const Found init = FindMethod(*cls, "init"); init.Ok()) {
			Value ignored;
			if (!CallMethod(at, inst, init, args, ignored))
				return false;
		}
		out = Value(inst);
		if (self.m_instanceHook)
			self.m_instanceHook(out);
		return true;
	}

	bool CallValue(const Node *at, const Value &callee, std::span<const Value> args, Value &out) {
		if (const auto *cls = std::get_if<std::shared_ptr<ClassDef>>(&callee.Raw()))
			return Instantiate(at, *cls, args, nullptr, out);
		if (const auto *nat = std::get_if<std::shared_ptr<Native>>(&callee.Raw())) {
			auto r = (*nat)->fn(self, args);
			if (t_exec.failed)
				return false;
			if (!r)
				return Fail(at, std::format("{}: {}", (*nat)->name, r.Error()));
			out = std::move(r.Value());
			return true;
		}
		const auto *clo = std::get_if<std::shared_ptr<Closure>>(&callee.Raw());
		if (!clo)
			return Fail(at, std::format("appel d'une valeur non appelable ({})", callee.TypeName()));
		if ((*clo)->def->async) { // `async fn` : tourne sur un fil, rend un future
			out = SpawnAsync(at, **clo, args);
			return true;
		}
		return CallClosure(at, **clo, args, out, Value(), nullptr);
	}

	/// Lance une fonction `async` sur un fil dédié et rend son `future`.
	Value SpawnAsync(const Node *at, const Closure &clo, std::span<const Value> args) {
		auto future = std::make_shared<Future>();
		auto closure = std::make_shared<Closure>(clo); // copie : le fil est indépendant
		heap.Track(closure);
		std::vector<Value> copied(args.begin(), args.end());
		Spawn([this, at, closure, copied = std::move(copied), future] {
			t_exec.currentProgram = closure->keepAlive; // garde l'AST en vie
			t_exec.current = static_cast<const Program *>(closure->keepAlive.get());
			Value out;
			if (CallClosure(at, *closure, copied, out, Value(), nullptr))
				future->Complete(std::move(out));
			else
				future->Fail(t_exec.error.message);
		});
		return Value(std::move(future));
	}

	/// Fermeture ; méthode si `thisValue` est une instance (`this`, et `super` = parent de `owner`).
	bool CallClosure(const Node *at, const Closure &clo, std::span<const Value> args, Value &out, const Value &thisValue, const ClassDef *owner) {
		if (t_exec.depth > 256)
			return Fail(at, "récursion trop profonde");
		const FunctionDef &def = *clo.def;
		auto env = std::make_shared<Environment>();
		env->parent = clo.env;
		env->vars.reserve(def.params.size() + 4);
		if (thisValue.IsInstance()) {
			env->vars.emplace_back(Intern("this"), thisValue);
			env->vars.emplace_back(Intern("super"), owner && owner->parent ? Value(owner->parent) : Value());
		}
		for (size_t p = 0; p < def.params.size(); ++p)
			env->vars.emplace_back(def.params[p], p < args.size() ? args[p] : Value());
		++t_exec.depth;
		const Flow f = ExecBlockIn(*def.body, env);
		--t_exec.depth;
		if (f == Flow::Error)
			return false;
		// Capture la valeur de retour dans un temporaire : `out` peut être la même
		// référence que `t_exec.ret` (appel depuis `return expr`), un auto-déplacement
		// viderait la valeur.
		Value result = f == Flow::Return ? std::move(t_exec.ret) : Value();
		t_exec.ret = Value();
		out = std::move(result);
		return true;
	}

	bool Builtin(const Node &n, Value &obj, std::string_view name, std::span<const Value> a, Value &out);

	bool Eval(const Node &n, const EnvPtr &env, Value &out) {
		switch (n.kind) {
		case NK::Nil: out = Value(); return true;
		case NK::True: out = Value(true); return true;
		case NK::False: out = Value(false); return true;
		case NK::Number: out = Value(n.number); return true;
		case NK::String: out = Value(n.text); return true;
		case NK::Ident: {
			if (Lookup(n.sym, env, out))
				return true;
			return Fail(&n, std::format("variable inconnue '{}'", n.text));
		}
		case NK::ListLit: {
			List l;
			l.reserve(n.kids.size());
			for (const auto &k : n.kids) {
				Value v;
				if (!Eval(*k, env, v))
					return false;
				l.push_back(std::move(v));
			}
			auto list = std::make_shared<List>(std::move(l));
			heap.Track(list);
			out = Value(std::move(list));
			return true;
		}
		case NK::MapLit: {
			Map m;
			for (size_t i = 0; i < n.kids.size(); ++i) {
				Value v;
				if (!Eval(*n.kids[i], env, v))
					return false;
				m.insert_or_assign(n.keys[i], std::move(v));
			}
			auto map = std::make_shared<Map>(std::move(m));
			heap.Track(map);
			out = Value(std::move(map));
			return true;
		}
		case NK::Lambda: {
			auto c = std::make_shared<Closure>();
			heap.Track(c);
			c->def = n.fn;
			c->env = env;
			c->keepAlive = t_exec.currentProgram;
			out = Value(std::move(c));
			return true;
		}
		default: break;
		}
		return EvalComplex(n, env, out);
	}

	bool EvalComplex(const Node &n, const EnvPtr &env, Value &out);
	bool Import(const Node &n, Value &out);
	Flow Exec(const Node &n, const std::shared_ptr<Environment> &env);
	Flow ExecBlockIn(const Node &block, const std::shared_ptr<Environment> &env);
};

bool Interpreter::Impl::EvalComplex(const Node &n, const EnvPtr &env, Value &out) {
	if (!Step(n))
		return false;
	switch (n.kind) {
	case NK::Unary: {
		Value v;
		if (!Eval(*n.kids[0], env, v))
			return false;
		if (n.text == "not") {
			out = Value(!v.Truthy());
			return true;
		}
		if (!v.IsNumber())
			return Fail(&n, std::format("'-' impossible sur {}", v.TypeName()));
		out = Value(-v.AsNumber());
		return true;
	}
	case NK::And:
	case NK::Or: {
		Value l;
		if (!Eval(*n.kids[0], env, l))
			return false;
		if (n.kind == NK::And ? !l.Truthy() : l.Truthy()) {
			out = std::move(l);
			return true;
		}
		return Eval(*n.kids[1], env, out);
	}
	case NK::Binary: {
		Value a, b;
		if (!Eval(*n.kids[0], env, a) || !Eval(*n.kids[1], env, b))
			return false;
		return Arith(n, n.text, a, b, out);
	}
	case NK::Index: {
		Value obj, key;
		if (!Eval(*n.kids[0], env, obj) || !Eval(*n.kids[1], env, key))
			return false;
		return IndexGet(n, obj, key, out);
	}
	case NK::Member: {
		Value obj;
		if (!Eval(*n.kids[0], env, obj))
			return false;
		if (obj.IsMap())
			return IndexGet(n, obj, Value(n.text), out);
		if (const ClassDef *cls = obj.AsClass()) { // membre `static` d'une classe
			if (auto it = cls->statics.find(n.text); it != cls->statics.end()) {
				out = it->second;
				return true;
			}
		}
		if (Future *f = obj.AsFuture()) { // propriétés d'un future `async`
			if (n.text == "done") {
				out = Value(f->GetState() != Future::State::Pending);
				return true;
			}
			if (n.text == "failed") {
				out = Value(f->GetState() == Future::State::Failed);
				return true;
			}
			if (n.text == "value") {
				std::lock_guard<std::mutex> fl(f->mutex);
				out = f->state == Future::State::Done ? f->value : Value();
				return true;
			}
			if (n.text == "error") {
				std::lock_guard<std::mutex> fl(f->mutex);
				f->observed = true;
				out = f->state == Future::State::Failed ? Value(f->error) : Value();
				return true;
			}
		}
		if (const std::shared_ptr<Instance> inst = obj.InstancePtr()) {
			if (auto it = inst->fields.find(n.text); it != inst->fields.end()) {
				out = it->second;
				return true;
			}
			const Found m = FindMethod(*inst->cls, n.text);
			if (!m.Ok()) {
				out = Value(); // champ jamais affecté : nil
				return true;
			}
			// Méthode détachée (`let f = this.m`) : `this` reste lié.
			out = Value::MakeNative(n.text, [inst, m](Interpreter &vm, std::span<const Value> a) -> Result<Value, std::string> {
				Value r;
				if (!vm.m_impl->CallMethod(nullptr, inst, m, a, r))
					return Err(t_exec.error.message);
				return Ok(std::move(r));
			});
			return true;
		}
		if (n.text == "length" || n.text == "len" || n.text == "size") {
			if (const List *l = obj.AsList()) {
				out = Value(static_cast<double>(l->size()));
				return true;
			}
			if (obj.IsString()) {
				out = Value(static_cast<double>(obj.AsString().size()));
				return true;
			}
		}
		if (obj.IsNil())
			return Fail(&n, std::format("accès au membre '{}' de nil", n.text));
		return Fail(&n, std::format("{} n'a pas de membre '{}'", obj.TypeName(), n.text));
	}
	case NK::Call: {
		Value callee;
		if (!Eval(*n.kids[0], env, callee))
			return false;
		std::vector<Value> args;
		args.reserve(n.kids.size() - 1);
		for (size_t i = 1; i < n.kids.size(); ++i) {
			Value v;
			if (!Eval(*n.kids[i], env, v))
				return false;
			args.push_back(std::move(v));
		}
		if (!callee.IsCallable() && n.kids[0]->kind == NK::Ident)
			return Fail(&n, std::format("'{}' n'est pas une fonction", n.kids[0]->text));
		return CallValue(&n, callee, args, out);
	}
	case NK::MethodCall: {
		const bool isSuper = n.kids[0]->kind == NK::Ident && n.kids[0]->text == "super";
		Value obj;
		if (!Eval(*n.kids[0], env, obj))
			return false;
		std::vector<Value> args;
		args.reserve(n.kids.size() - 1);
		for (size_t i = 1; i < n.kids.size(); ++i) {
			Value v;
			if (!Eval(*n.kids[i], env, v))
				return false;
			args.push_back(std::move(v));
		}
		if (isSuper) { // méthode de la classe parente, sur la même instance
			Value thisValue;
			if (!Lookup(Intern("this"), env, thisValue) || !thisValue.IsInstance())
				return Fail(&n, "super hors d'une méthode");
			if (const ClassDef *parent = obj.AsClass())
				if (const Found m = FindMethod(*parent, n.text); m.Ok())
					return CallMethod(&n, thisValue.InstancePtr(), m, args, out);
			if (n.text == "init") { // pas de constructeur parent : sans effet
				out = Value();
				return true;
			}
			return Fail(&n, std::format("la classe parente n'a pas de méthode '{}'", n.text));
		}
		if (const ClassDef *cls = obj.AsClass()) { // méthode `static` / `factory`
			if (auto it = cls->statics.find(n.text); it != cls->statics.end() && it->second.IsCallable()) {
				const Value fn = it->second;
				return CallValue(&n, fn, args, out);
			}
		}
		if (const std::shared_ptr<Instance> inst = obj.InstancePtr()) {
			if (auto it = inst->fields.find(n.text); it != inst->fields.end() && it->second.IsCallable()) {
				const Value fn = it->second;
				return CallValue(&n, fn, args, out);
			}
			if (const Found m = FindMethod(*inst->cls, n.text); m.Ok())
				return CallMethod(&n, inst, m, args, out);
			return Fail(&n, std::format("{} n'a pas de méthode '{}'", inst->cls->name, n.text));
		}
		if (Map *m = obj.AsMap()) {
			if (auto it = m->find(n.text); it != m->end() && it->second.IsCallable()) {
				const Value fn = it->second;
				return CallValue(&n, fn, args, out);
			}
		}
		if (Future *f = obj.AsFuture()) { // `.wait()` / `.then(fn)` d'un future `async`
			if (n.text == "wait")
				return WaitFuture(&n, *f, out);
			if (n.text == "then" && !args.empty()) {
				const Value fn = args[0];
				auto next = std::make_shared<Future>();
				auto src = obj.FuturePtr();
				Spawn([this, src, fn, next] {
					Value result;
					if (!WaitFuture(nullptr, *src, result)) {
						next->Fail(t_exec.error.message);
						return;
					}
					Value r;
					const Value a[] = {result};
					if (CallValue(nullptr, fn, a, r))
						next->Complete(std::move(r));
					else
						next->Fail(t_exec.error.message);
				});
				out = Value(std::move(next));
				return true;
			}
		}
		return Builtin(n, obj, n.text, args, out);
	}
	case NK::Assign: {
		Value v;
		if (!Eval(*n.kids[1], env, v))
			return false;
		if (!n.text.empty()) {
			Value cur;
			if (!Eval(*n.kids[0], env, cur))
				return false;
			Value combined;
			if (!Arith(n, n.text, cur, v, combined))
				return false;
			v = std::move(combined);
		}
		out = v;
		return Assign(*n.kids[0], env, std::move(v));
	}
	case NK::Import: {
		if (!self.m_loader)
			return Fail(&n, std::format("import \"{}\" : aucun chargeur de modules", n.text));
		return Import(n, out);
	}
	case NK::Await: { // attend la fin d'un future (une valeur non-future passe telle quelle)
		Value v;
		if (!Eval(*n.kids[0], env, v))
			return false;
		if (!v.IsFuture()) {
			out = std::move(v);
			return true;
		}
		const std::shared_ptr<Future> f = v.FuturePtr();
		return WaitFuture(&n, *f, out);
	}
	default: return Fail(&n, "expression invalide");
	}
}

bool Interpreter::Impl::Import(const Node &n, Value &out) {
	if (const auto it = modules.find(n.text); it != modules.end()) {
		if (it->second.IsNil())
			return Fail(&n, std::format("import circulaire de \"{}\"", n.text));
		out = it->second;
		return true;
	}
	auto source = self.m_loader(n.text);
	if (!source)
		return Fail(&n, std::format("import \"{}\" : {}", n.text, source.Error()));
	auto prog = std::make_shared<Program>();
	prog->file = n.text;
	std::vector<Tok> toks;
	ScriptError err;
	if (!Lexer(source.Value()).Run(toks, err)) {
		err.file = prog->file;
		t_exec.failed = true;
		t_exec.error = std::move(err);
		return false;
	}
	Parser parser{[this](std::string_view s) { return Intern(s); }, std::move(toks), *prog};
	if (!parser.ParseProgram()) {
		t_exec.failed = true;
		t_exec.error = std::move(parser.err);
		return false;
	}
	programs.push_back(prog);
	modules.insert_or_assign(n.text, Value()); // en cours : détecte les imports circulaires
	const Program *savedCur = t_exec.current;
	auto savedProg = t_exec.currentProgram;
	t_exec.current = prog.get();
	t_exec.currentProgram = prog;
	auto scope = std::make_shared<Environment>(); // portée propre au module
	bool ok = true;
	for (const auto &st : prog->statements)
		if (Exec(*st, scope) == Flow::Error) {
			ok = false;
			break;
		}
	t_exec.current = savedCur;
	t_exec.currentProgram = savedProg;
	if (!ok) {
		modules.erase(n.text);
		return false;
	}
	Map table;
	for (const auto &[sym, v] : scope->vars) // les dernières définitions l'emportent
		table.insert_or_assign(symbolNames[sym], v);
	out = Value(std::move(table));
	modules.insert_or_assign(n.text, out);
	return true;
}

Interpreter::Impl::Flow Interpreter::Impl::ExecBlockIn(const Node &block, const EnvPtr &env) {
	for (const auto &st : block.kids) {
		const Flow f = Exec(*st, env);
		if (f != Flow::Normal)
			return f;
	}
	return Flow::Normal;
}

Interpreter::Impl::Flow Interpreter::Impl::Exec(const Node &n, const EnvPtr &env) {
	switch (n.kind) {
	case NK::ExprStmt: {
		Value v;
		return Eval(*n.kids[0], env, v) ? Flow::Normal : Flow::Error;
	}
	case NK::Let: {
		Value v;
		if (!n.kids.empty() && !Eval(*n.kids[0], env, v))
			return Flow::Error;
		if (env)
			env->vars.emplace_back(n.sym, std::move(v));
		else
			SetGlobal(n.sym, std::move(v));
		return Flow::Normal;
	}
	case NK::ClassDecl:
	case NK::InterfaceDecl: {
		auto cls = std::make_shared<ClassDef>();
		heap.Track(cls);
		cls->name = n.text;
		cls->abstract = n.flag;
		cls->isInterface = n.kind == NK::InterfaceDecl;
		const size_t bases = size_t(n.number);
		const size_t ifaces = size_t(n.mod);
		for (size_t k = 0; k < bases; ++k) {
			Value b;
			if (!Eval(*n.kids[k], env, b))
				return Flow::Error;
			const auto *base = std::get_if<std::shared_ptr<ClassDef>>(&b.Raw());
			if (!base) {
				Fail(n.kids[k].get(), std::format("{} : '{}' n'est pas une classe", n.text, b.ToString()));
				return Flow::Error;
			}
			if ((*base)->native)
				cls->natives.push_back(*base);
			else if (cls->parent) {
				Fail(n.kids[k].get(), std::format("{} : une seule classe parente de script ({} et {})", n.text, cls->parent->name, (*base)->name));
				return Flow::Error;
			} else
				cls->parent = *base;
		}
		for (size_t k = bases; k < bases + ifaces; ++k) { // interfaces `implements` / super-interfaces
			Value b;
			if (!Eval(*n.kids[k], env, b))
				return Flow::Error;
			const auto *iface = std::get_if<std::shared_ptr<ClassDef>>(&b.Raw());
			if (!iface || !(*iface)->isInterface) {
				Fail(n.kids[k].get(), std::format("{} : '{}' n'est pas une interface", n.text, b.ToString()));
				return Flow::Error;
			}
			cls->interfaces.push_back(*iface);
		}
		for (size_t k = bases + ifaces; k < n.kids.size(); ++k) {
			const Node &m = *n.kids[k];
			if (m.kind == NK::Let) {
				if (m.mod & 1) { // `static let` : évalué dans la portée de la classe
					Value v;
					if (!m.kids.empty() && !Eval(*m.kids[0], env, v))
						return Flow::Error;
					cls->statics.insert_or_assign(symbolNames[m.sym], std::move(v));
				} else {
					cls->fields.emplace_back(symbolNames[m.sym], &m);
					if (m.flag)
						cls->exported.push_back(symbolNames[m.sym]);
				}
			} else if (m.kind == NK::FnDecl) {
				auto c = std::make_shared<Closure>();
				heap.Track(c);
				c->def = m.fn;
				c->env = env;
				c->keepAlive = t_exec.currentProgram;
				if (m.mod & 1 || m.mod & 2) // `static` / `factory` : membre de classe
					cls->statics.insert_or_assign(m.fn->name, Value(std::move(c)));
				else
					cls->methods.insert_or_assign(m.fn->name, Value(std::move(c)));
			}
		}
		cls->scope = std::static_pointer_cast<void>(env);
		cls->keepAlive = t_exec.currentProgram;
		if (env)
			env->vars.emplace_back(n.sym, Value(cls));
		else
			SetGlobal(n.sym, Value(cls));
		classOrder.push_back(n.sym);
		return Flow::Normal;
	}
	case NK::EnumDecl: {
		auto cls = std::make_shared<ClassDef>();
		heap.Track(cls);
		cls->name = n.text;
		List values;
		double next = 0.0;
		for (size_t k = 0; k < n.kids.size(); ++k) {
			const Node &v = *n.kids[k];
			double value = next;
			if (!v.kids.empty()) {
				Value vv;
				if (!Eval(*v.kids[0], env, vv))
					return Flow::Error;
				value = vv.AsNumber();
			}
			auto inst = std::make_shared<Instance>();
			heap.Track(inst);
			inst->cls = cls;
			inst->fields.insert_or_assign("name", Value(v.text));
			inst->fields.insert_or_assign("value", Value(value));
			inst->fields.insert_or_assign("ordinal", Value(double(k)));
			cls->statics.insert_or_assign(v.text, Value(inst));
			values.push_back(Value(std::move(inst)));
			next = value + 1.0;
		}
		auto list = std::make_shared<List>(std::move(values));
		heap.Track(list);
		cls->statics.insert_or_assign("values", Value(std::move(list)));
		cls->statics.insert_or_assign("from", Value::MakeNative("from", [weak = std::weak_ptr<ClassDef>(cls)](Interpreter &, std::span<const Value> a) -> Result<Value, std::string> {
			const std::shared_ptr<ClassDef> owner = weak.lock(); // faible : la classe possède déjà cette native
			if (!owner)
				return Ok(Value());
			const double want = a.empty() ? 0.0 : a[0].AsNumber();
			for (const auto &[name, v] : owner->statics)
				if (v.IsInstance() && v.AsInstance()->fields.at("value").AsNumber() == want)
					return Ok(v);
			return Ok(Value());
		}));
		cls->statics.insert_or_assign("from_name", Value::MakeNative("from_name", [weak = std::weak_ptr<ClassDef>(cls)](Interpreter &, std::span<const Value> a) -> Result<Value, std::string> {
			const std::shared_ptr<ClassDef> owner = weak.lock(); // faible : la classe possède déjà cette native
			if (!owner)
				return Ok(Value());
			const std::string_view want = a.empty() ? "" : a[0].AsString();
			for (const auto &[name, v] : owner->statics)
				if (v.IsInstance() && v.AsInstance()->fields.at("name").AsString() == want)
					return Ok(v);
			return Ok(Value());
		}));
		if (env)
			env->vars.emplace_back(n.sym, Value(cls));
		else
			SetGlobal(n.sym, Value(cls));
		classOrder.push_back(n.sym);
		return Flow::Normal;
	}
	case NK::NamespaceDecl: {
		// `namespace Nom { … }` : portée nommée ré-ouvrable (les membres sont ses variables).
		Value existing;
		Lookup(n.sym, env, existing);
		auto scope = std::make_shared<Environment>();
		scope->parent = env;
		Map table;
		if (Map *m = existing.AsMap())
			table = *m;
		for (const auto &st : n.kids[0]->kids)
			if (Exec(*st, scope) == Flow::Error)
				return Flow::Error;
		for (const auto &[sym, v] : scope->vars)
			table.insert_or_assign(symbolNames[sym], v);
		if (env)
			env->vars.emplace_back(n.sym, Value(std::move(table)));
		else {
			GilLock lock(gil);
			SetGlobal(n.sym, Value(std::move(table)));
		}
		return Flow::Normal;
	}
	case NK::ImportStmt: {
		if (!self.m_loader)
			return Flow::Normal; // compatibilité : les fichiers d'un mod partagent leur environnement
		Value table;
		if (!Import(n, table))
			return Flow::Error;
		for (const auto &[name, v] : *table.AsMap()) {
			const uint32_t sym = Intern(name);
			if (env)
				env->vars.emplace_back(sym, v);
			else
				SetGlobal(sym, v);
		}
		return Flow::Normal;
	}
	case NK::FnDecl: {
		auto c = std::make_shared<Closure>();
		heap.Track(c);
		c->def = n.fn;
		c->env = env;
		c->keepAlive = t_exec.currentProgram;
		if (env)
			env->vars.emplace_back(n.sym, Value(std::move(c)));
		else
			SetGlobal(n.sym, Value(std::move(c)));
		return Flow::Normal;
	}
	case NK::Block: {
		if (!n.flag)
			return ExecBlockIn(n, env);
		auto scope = std::make_shared<Environment>();
		scope->parent = env;
		return ExecBlockIn(n, scope);
	}
	case NK::If: {
		Value c;
		if (!Eval(*n.kids[0], env, c))
			return Flow::Error;
		if (c.Truthy())
			return Exec(*n.kids[1], env);
		if (n.kids.size() > 2)
			return Exec(*n.kids[2], env);
		return Flow::Normal;
	}
	case NK::While: {
		for (;;) {
			Value c;
			if (!Eval(*n.kids[0], env, c))
				return Flow::Error;
			if (!c.Truthy())
				return Flow::Normal;
			const Flow f = Exec(*n.kids[1], env);
			if (f == Flow::Break)
				return Flow::Normal;
			if (f == Flow::Return || f == Flow::Error)
				return f;
			if (!Step(n))
				return Flow::Error;
		}
	}
	case NK::DoWhile: {
		for (;;) {
			const Flow f = Exec(*n.kids[0], env); // corps d'abord
			if (f == Flow::Break)
				return Flow::Normal;
			if (f == Flow::Return || f == Flow::Error)
				return f;
			Value c;
			if (!Eval(*n.kids[1], env, c))
				return Flow::Error;
			if (!c.Truthy())
				return Flow::Normal;
			if (!Step(n))
				return Flow::Error;
		}
	}
	case NK::For: {
		Value seq;
		if (!Eval(*n.kids[0], env, seq))
			return Flow::Error;
		auto scope = std::make_shared<Environment>();
		scope->parent = env;
		scope->vars.emplace_back(n.sym, Value());
		auto body = [&](Value item) {
			scope->vars[0].second = std::move(item);
			scope->vars.resize(1);
			return Exec(*n.kids[1], scope);
		};
		std::vector<Value> items;
		if (const List *l = seq.AsList()) {
			items = *l; // copie : la boucle tolère les modifications de la liste
		} else if (const Map *m = seq.AsMap()) {
			for (const auto &[k, v] : *m)
				items.emplace_back(k);
		} else if (seq.IsString()) {
			for (char ch : seq.AsString())
				items.emplace_back(std::string(1, ch));
		} else if (seq.IsNumber()) {
			for (int i = 0; i < seq.AsInt(); ++i)
				items.emplace_back(i);
		} else if (!seq.IsNil()) {
			Fail(&n, std::format("impossible d'itérer sur {}", seq.TypeName()));
			return Flow::Error;
		}
		for (Value &it : items) {
			const Flow f = body(std::move(it));
			if (f == Flow::Break)
				break;
			if (f == Flow::Return || f == Flow::Error)
				return f;
			if (!Step(n))
				return Flow::Error;
		}
		return Flow::Normal;
	}
	case NK::Return:
		if (!n.kids.empty() && !Eval(*n.kids[0], env, t_exec.ret))
			return Flow::Error;
		if (n.kids.empty())
			t_exec.ret = Value();
		return Flow::Return;
	case NK::Break: return Flow::Break;
	case NK::Continue: return Flow::Continue;
	case NK::Nil: return Flow::Normal;
	default: {
		Value v;
		return Eval(n, env, v) ? Flow::Normal : Flow::Error;
	}
	}
}

// ── Méthodes intégrées des chaînes, listes et tables ─────────────────────
bool Interpreter::Impl::Builtin(const Node &n, Value &obj, std::string_view name, std::span<const Value> a, Value &out) {
	auto arg = [&](size_t i) -> const Value & {
		static const Value nil;
		return i < a.size() ? a[i] : nil;
	};
	if (obj.IsString()) {
		const std::string s(obj.AsString());
		const StringView sv(s);
		if (name == "len" || name == "length") { out = Value(static_cast<double>(s.size())); return true; }
		if (name == "upper") { out = Value(String(s).ToUpper()); return true; }
		if (name == "lower") { out = Value(String(s).ToLower()); return true; }
		if (name == "trim") { out = Value(std::string(sv.Trim())); return true; }
		if (name == "starts_with") { out = Value(sv.starts_with(arg(0).AsString())); return true; }
		if (name == "ends_with") { out = Value(sv.ends_with(arg(0).AsString())); return true; }
		if (name == "contains") { out = Value(sv.find(arg(0).ToString()) != std::string::npos); return true; }
		if (name == "index_of") {
			const size_t p = sv.find(arg(0).ToString());
			out = Value(p == std::string::npos ? -1.0 : static_cast<double>(p));
			return true;
		}
		if (name == "sub" || name == "slice" || name == "substr") {
			const auto len = static_cast<int64_t>(s.size());
			int64_t b = static_cast<int64_t>(arg(0).AsNumber(0)), e = a.size() > 1 ? static_cast<int64_t>(arg(1).AsNumber()) : len;
			if (b < 0) b += len;
			if (e < 0) e += len;
			b = std::clamp<int64_t>(b, 0, len);
			e = std::clamp<int64_t>(e, b, len);
			out = Value(s.substr(static_cast<size_t>(b), static_cast<size_t>(e - b)));
			return true;
		}
		if (name == "split") {
			List l;
			for (auto &part : String(s).Split(a.empty() ? std::string_view(" ") : arg(0).AsString()))
				l.emplace_back(std::string(part));
			out = Value(std::move(l));
			return true;
		}
		if (name == "replace") { out = Value(String(s).Replace(arg(0).ToString(), arg(1).ToString())); return true; }
		if (name == "format") { out = Value(FormatValues(s, a)); return true; }
		if (name == "repeat") { out = Value(String(s).Repeat(static_cast<size_t>(std::max(0, arg(0).AsInt())))); return true; }
		return Fail(&n, std::format("méthode de chaîne inconnue '{}'", name));
	}
	if (List *l = obj.AsList()) {
		if (name == "push" || name == "append") {
			for (const Value &v : a)
				l->push_back(v);
			out = obj;
			return true;
		}
		if (name == "pop") {
			if (l->empty()) { out = Value(); return true; }
			out = std::move(l->back());
			l->pop_back();
			return true;
		}
		if (name == "len" || name == "length" || name == "size") { out = Value(static_cast<double>(l->size())); return true; }
		if (name == "contains") { out = Value(std::ranges::any_of(*l, [&](const Value &v) { return v.Equals(arg(0)); })); return true; }
		if (name == "index_of") {
			auto it = std::ranges::find_if(*l, [&](const Value &v) { return v.Equals(arg(0)); });
			out = Value(it == l->end() ? -1.0 : static_cast<double>(it - l->begin()));
			return true;
		}
		if (name == "remove") {
			const auto i = static_cast<size_t>(arg(0).AsNumber(-1));
			if (i < l->size()) {
				out = (*l)[i];
				l->erase(l->begin() + static_cast<std::ptrdiff_t>(i));
			}
			return true;
		}
		if (name == "insert") {
			const size_t i = std::min(static_cast<size_t>(std::max(0.0, arg(0).AsNumber())), l->size());
			l->insert(l->begin() + static_cast<std::ptrdiff_t>(i), arg(1));
			return true;
		}
		if (name == "join") {
			std::string r;
			for (size_t i = 0; i < l->size(); ++i)
				r += (i ? std::string(arg(0).AsString()) : "") + (*l)[i].ToString();
			out = Value(std::move(r));
			return true;
		}
		if (name == "slice") {
			const auto len = static_cast<int64_t>(l->size());
			int64_t b = static_cast<int64_t>(arg(0).AsNumber(0)), e = a.size() > 1 ? static_cast<int64_t>(arg(1).AsNumber()) : len;
			if (b < 0) b += len;
			if (e < 0) e += len;
			b = std::clamp<int64_t>(b, 0, len);
			e = std::clamp<int64_t>(e, b, len);
			out = Value(List(l->begin() + b, l->begin() + e));
			return true;
		}
		if (name == "sort") {
			std::ranges::stable_sort(*l, [](const Value &x, const Value &y) {
				if (x.IsNumber() && y.IsNumber())
					return x.AsNumber() < y.AsNumber();
				return x.ToString() < y.ToString();
			});
			out = obj;
			return true;
		}
		if (name == "reverse") {
			std::ranges::reverse(*l);
			out = obj;
			return true;
		}
		return Fail(&n, std::format("méthode de liste inconnue '{}'", name));
	}
	if (Map *m = obj.AsMap()) {
		if (name == "has" || name == "contains") { out = Value(m->contains(arg(0).ToString())); return true; }
		if (name == "keys") {
			List l;
			for (const auto &[k, v] : *m)
				l.emplace_back(k);
			out = Value(std::move(l));
			return true;
		}
		if (name == "values") {
			List l;
			for (const auto &[k, v] : *m)
				l.push_back(v);
			out = Value(std::move(l));
			return true;
		}
		if (name == "len" || name == "size") { out = Value(static_cast<double>(m->size())); return true; }
		if (name == "remove") { out = Value(m->erase(arg(0).ToString()) > 0); return true; }
		if (name == "get") {
			auto it = m->find(arg(0).ToString());
			out = it == m->end() ? arg(1) : it->second;
			return true;
		}
		return Fail(&n, std::format("fonction inconnue '{}' dans la table", name));
	}
	if (obj.IsNil())
		return Fail(&n, std::format("appel de '{}' sur nil", name));
	return Fail(&n, std::format("{} n'a pas de méthode '{}'", obj.TypeName(), name));
}

// ════════════════════════════════════════════════════════════════════════
// API publique
// ════════════════════════════════════════════════════════════════════════
namespace {
Result<Value, std::string> Need(std::span<const Value> a, size_t n, std::string_view fn) {
	if (a.size() < n)
		return Err(std::format("{} attend {} argument(s)", fn, n));
	return Ok(Value());
}
} // namespace

Interpreter::Interpreter() : m_impl(std::make_unique<Impl>(*this)) {
	using A = std::span<const Value>;
	using R = Result<Value, std::string>;
	auto num1 = [this](std::string_view name, double (*f)(double)) {
		RegisterFunction(name, [f](Interpreter &, A a) -> R { return Ok(Value(f(a.empty() ? 0.0 : a[0].AsNumber()))); });
	};
	RegisterFunction("print", [](Interpreter &vm, A a) -> R {
		std::string line;
		for (size_t i = 0; i < a.size(); ++i)
			line += (i ? " " : "") + a[i].ToString();
		vm.Print(line);
		return Ok(Value());
	});
	RegisterFunction("str", [](Interpreter &, A a) -> R { return Ok(Value(a.empty() ? std::string() : a[0].ToString())); });
	RegisterFunction("num", [](Interpreter &, A a) -> R { return Ok(a.empty() ? Value() : Value(a[0].AsNumber())); });
	RegisterFunction("bool", [](Interpreter &, A a) -> R { return Ok(Value(!a.empty() && a[0].Truthy())); });
	RegisterFunction("type", [](Interpreter &, A a) -> R { return Ok(Value(a.empty() ? "nil" : a[0].TypeName())); });
	RegisterFunction("len", [](Interpreter &, A a) -> R {
		if (a.empty()) return Ok(Value(0));
		if (const List *l = a[0].AsList()) return Ok(Value(static_cast<double>(l->size())));
		if (const Map *m = a[0].AsMap()) return Ok(Value(static_cast<double>(m->size())));
		return Ok(Value(static_cast<double>(a[0].AsString().size())));
	});
	RegisterFunction("push", [](Interpreter &, A a) -> R {
		if (auto r = Need(a, 2, "push"); !r) return r;
		if (List *l = a[0].AsList()) l->push_back(a[1]);
		return Ok(a[0]);
	});
	RegisterFunction("pop", [](Interpreter &, A a) -> R {
		List *l = a.empty() ? nullptr : a[0].AsList();
		if (!l || l->empty()) return Ok(Value());
		Value v = l->back();
		l->pop_back();
		return Ok(std::move(v));
	});
	RegisterFunction("keys", [](Interpreter &, A a) -> R {
		List l;
		if (!a.empty())
			if (const Map *m = a[0].AsMap())
				for (const auto &[k, v] : *m) l.emplace_back(k);
		return Ok(Value(std::move(l)));
	});
	RegisterFunction("values", [](Interpreter &, A a) -> R {
		List l;
		if (!a.empty())
			if (const Map *m = a[0].AsMap())
				for (const auto &[k, v] : *m) l.push_back(v);
		return Ok(Value(std::move(l)));
	});
	RegisterFunction("has", [](Interpreter &, A a) -> R {
		if (a.size() < 2) return Ok(Value(false));
		if (const Map *m = a[0].AsMap()) return Ok(Value(m->contains(a[1].ToString())));
		if (const List *l = a[0].AsList()) return Ok(Value(std::ranges::any_of(*l, [&](const Value &v) { return v.Equals(a[1]); })));
		return Ok(Value(a[0].AsString().find(a[1].ToString()) != std::string_view::npos));
	});
	RegisterFunction("range", [](Interpreter &, A a) -> R {
		double b = 0, e = 0, st = 1;
		if (a.size() == 1) e = a[0].AsNumber();
		else if (a.size() >= 2) { b = a[0].AsNumber(); e = a[1].AsNumber(); }
		if (a.size() >= 3) st = a[2].AsNumber();
		if (st == 0) return Err(std::string("range : pas nul"));
		List l;
		for (double v = b; st > 0 ? v < e : v > e; v += st) {
			l.emplace_back(v);
			if (l.size() > 10'000'000) return Err(std::string("range trop grand"));
		}
		return Ok(Value(std::move(l)));
	});
	RegisterFunction("format", [](Interpreter &, A a) -> R {
		if (a.empty()) return Ok(Value(""));
		return Ok(Value(FormatValues(a[0].AsString(), a.subspan(1))));
	});
	RegisterFunction("join", [](Interpreter &, A a) -> R {
		std::string r;
		if (!a.empty())
			if (const List *l = a[0].AsList())
				for (size_t i = 0; i < l->size(); ++i)
					r += (i && a.size() > 1 ? std::string(a[1].AsString()) : "") + (*l)[i].ToString();
		return Ok(Value(std::move(r)));
	});
	RegisterFunction("split", [](Interpreter &, A a) -> R {
		List l;
		if (!a.empty())
			for (auto &p : String(std::string(a[0].AsString())).Split(a.size() > 1 ? a[1].AsString() : std::string_view(" ")))
				l.emplace_back(std::string(p));
		return Ok(Value(std::move(l)));
	});
	RegisterFunction("trim", [](Interpreter &, A a) -> R { return Ok(Value(std::string(StringView(a.empty() ? "" : a[0].AsString()).Trim()))); });
	RegisterFunction("upper", [](Interpreter &, A a) -> R { return Ok(Value(String(a.empty() ? "" : std::string(a[0].AsString())).ToUpper())); });
	RegisterFunction("lower", [](Interpreter &, A a) -> R { return Ok(Value(String(a.empty() ? "" : std::string(a[0].AsString())).ToLower())); });
	RegisterFunction("index_of", [](Interpreter &, A a) -> R {
		if (a.size() < 2) return Ok(Value(-1));
		if (const List *l = a[0].AsList()) {
			auto it = std::ranges::find_if(*l, [&](const Value &v) { return v.Equals(a[1]); });
			return Ok(Value(it == l->end() ? -1.0 : static_cast<double>(it - l->begin())));
		}
		const size_t p = a[0].AsString().find(a[1].ToString());
		return Ok(Value(p == std::string_view::npos ? -1.0 : static_cast<double>(p)));
	});
	num1("abs", [](double x) { return std::fabs(x); });
	num1("floor", [](double x) { return std::floor(x); });
	num1("ceil", [](double x) { return std::ceil(x); });
	num1("round", [](double x) { return std::round(x); });
	num1("sqrt", [](double x) { return std::sqrt(x); });
	num1("sin", [](double x) { return std::sin(x); });
	num1("cos", [](double x) { return std::cos(x); });
	num1("tan", [](double x) { return std::tan(x); });
	auto minmax = [](bool isMax) {
		return [isMax](Interpreter &, A a) -> R {
			std::span<const Value> src = a;
			if (a.size() == 1 && a[0].IsList()) src = *a[0].AsList();
			if (src.empty()) return Ok(Value());
			double r = src[0].AsNumber();
			for (const Value &v : src.subspan(1)) r = isMax ? std::max(r, v.AsNumber()) : std::min(r, v.AsNumber());
			return Ok(Value(r));
		};
	};
	RegisterFunction("min", minmax(false));
	RegisterFunction("max", minmax(true));
	RegisterFunction("pow", [](Interpreter &, A a) -> R { return Ok(Value(std::pow(a.size() > 0 ? a[0].AsNumber() : 0, a.size() > 1 ? a[1].AsNumber() : 1))); });
	RegisterFunction("clamp", [](Interpreter &, A a) -> R {
		if (a.size() < 3) return Err(std::string("clamp(v, min, max)"));
		return Ok(Value(std::clamp(a[0].AsNumber(), a[1].AsNumber(), std::max(a[1].AsNumber(), a[2].AsNumber()))));
	});
	RegisterFunction("lerp", [](Interpreter &, A a) -> R {
		if (a.size() < 3) return Err(std::string("lerp(a, b, t)"));
		return Ok(Value(a[0].AsNumber() + (a[1].AsNumber() - a[0].AsNumber()) * a[2].AsNumber()));
	});
	RegisterFunction("random", [](Interpreter &vm, A a) -> R {
		std::uniform_real_distribution<double> u(0.0, 1.0);
		const double r = u(vm.m_impl->rng);
		if (a.size() >= 2) return Ok(Value(std::floor(a[0].AsNumber() + r * (a[1].AsNumber() - a[0].AsNumber() + 1))));
		if (a.size() == 1) return Ok(Value(std::floor(1 + r * a[0].AsNumber())));
		return Ok(Value(r));
	});
	RegisterFunction("seed", [](Interpreter &vm, A a) -> R {
		vm.m_impl->rng.seed(static_cast<uint64_t>(a.empty() ? 0.0 : a[0].AsNumber()));
		return Ok(Value());
	});
	RegisterFunction("sleep", [](Interpreter &vm, A a) -> R {
		// Suspension : attend `ms` millisecondes en relâchant le GIL (les autres fils
		// avancent), par tranches de 2 ms pour rester interruptible.
		const double ms = std::clamp(a.empty() ? 0.0 : a[0].AsNumber(), 0.0, 86'400'000.0);
		const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(static_cast<int64_t>(ms * 1000.0));
		GilUnlocked unlocked(vm.m_impl->gil);
		while (std::chrono::steady_clock::now() < until && !t_exec.stop.stop_requested())
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		return Ok(Value());
	});
	// Table `math` : mêmes fonctions + constantes.
	Map math;
	for (const char *name : {"abs", "floor", "ceil", "round", "sqrt", "sin", "cos", "tan", "min", "max", "pow", "clamp", "lerp", "random", "seed"})
		math.emplace(name, GetGlobal(name));
	math.emplace("atan2", Value::MakeNative("atan2", [](Interpreter &, A a) -> R {
		return Ok(Value(std::atan2(a.size() > 0 ? a[0].AsNumber() : 0, a.size() > 1 ? a[1].AsNumber() : 1)));
	}));
	math.emplace("sign", Value::MakeNative("sign", [](Interpreter &, A a) -> R {
		const double x = a.empty() ? 0 : a[0].AsNumber();
		return Ok(Value(x > 0 ? 1.0 : x < 0 ? -1.0 : 0.0));
	}));
	math.emplace("pi", Value(3.14159265358979323846));
	// Entier (troncature vers zéro), horloge du système.
	RegisterFunction("i64", [](Interpreter &, A a) -> R { return Ok(Value(a.empty() ? 0.0 : std::trunc(a[0].AsNumber()))); });
	RegisterFunction("i32", [](Interpreter &, A a) -> R { return Ok(Value(a.empty() ? 0.0 : double(int32_t(std::trunc(a[0].AsNumber()))))); });
	Map os;
	os.emplace("time", Value::MakeNative("std.os.time", [](Interpreter &, A) -> R {
		return Ok(Value(double(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) / 1000.0));
	}));
	Map stdlib;
	stdlib.emplace("os", Value(std::move(os)));
	SetGlobal("std", Value(std::move(stdlib)));
	math.emplace("huge", Value(1e308));
	SetGlobal("math", Value(std::move(math)));
}

Interpreter::~Interpreter() = default;

void Interpreter::Print(std::string_view s) const {
	if (m_print)
		m_print(s);
	else
		std::fprintf(stdout, "%.*s\n", static_cast<int>(s.size()), s.data());
}

Result<Value, ScriptError> Interpreter::Run(std::string_view source, std::string_view fileName) {
	GilLock lock(m_impl->gil);
	m_impl->MaybeCollect();
	Impl &i = *m_impl;
	auto prog = std::make_shared<Program>();
	prog->file = std::string(fileName);
	std::vector<Tok> toks;
	ScriptError err;
	if (!Lexer(source).Run(toks, err)) {
		err.file = prog->file;
		return Err(std::move(err));
	}
	Parser parser{[&i](std::string_view s) { return i.Intern(s); }, std::move(toks), *prog};
	if (!parser.ParseProgram())
		return Err(std::move(parser.err));
	i.programs.push_back(prog);
	const Program *savedCur = t_exec.current;
	auto savedProg = t_exec.currentProgram;
	t_exec.current = prog.get();
	t_exec.currentProgram = prog;
	t_exec.failed = false;
	t_exec.steps = 0;
	Value last;
	for (const auto &st : prog->statements) {
		if (st->kind == NK::ExprStmt) { // valeur de la dernière expression (console)
			if (!i.Eval(*st->kids[0], nullptr, last))
				break;
			continue;
		}
		last = Value();
		const Impl::Flow f = i.Exec(*st, nullptr);
		if (f == Impl::Flow::Error)
			break;
		if (f == Impl::Flow::Return) {
			last = std::move(t_exec.ret);
			break;
		}
	}
	t_exec.current = savedCur;
	t_exec.currentProgram = savedProg;
	if (t_exec.failed) {
		t_exec.failed = false;
		ScriptError e = std::move(t_exec.error);
		if (e.file.empty())
			e.file = prog->file;
		return Err(std::move(e));
	}
	return Ok(std::move(last));
}

Result<bool, ScriptError> Interpreter::Check(std::string_view source, std::string_view fileName) {
	GilLock lock(m_impl->gil);
	Program prog;
	prog.file = std::string(fileName);
	std::vector<Tok> toks;
	ScriptError err;
	if (!Lexer(source).Run(toks, err)) {
		err.file = prog.file;
		return Err(std::move(err));
	}
	Parser parser{[this](std::string_view s) { return m_impl->Intern(s); }, std::move(toks), prog};
	if (!parser.ParseProgram())
		return Err(std::move(parser.err));
	return Ok(true);
}

Result<Value, ScriptError> Interpreter::RunFile(const std::string &path) {
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return Err(ScriptError{"fichier introuvable", path, 0, 0});
	std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	return Run(src, path);
}

Result<Value, ScriptError> Interpreter::Call(const Value &fn, std::span<const Value> args) {
	GilLock lock(m_impl->gil);
	m_impl->MaybeCollect();
	Impl &i = *m_impl;
	const bool nested = t_exec.depth > 0;
	if (!nested) {
		t_exec.failed = false;
		t_exec.steps = 0;
	}
	Value out;
	const Program *saved = t_exec.current;
	if (const auto *clo = std::get_if<std::shared_ptr<Closure>>(&fn.Raw()))
		t_exec.current = static_cast<const Program *>((*clo)->keepAlive.get());
	const bool ok = i.CallValue(nullptr, fn, args, out);
	t_exec.current = saved;
	if (!ok && !nested) {
		t_exec.failed = false;
		return Err(std::move(t_exec.error));
	}
	if (!ok)
		return Err(t_exec.error);
	return Ok(std::move(out));
}

void Interpreter::SetGlobal(std::string_view name, Value v) {
	GilLock lock(m_impl->gil);
	m_impl->globals.InsertOrAssign(m_impl->Intern(name), std::move(v));
}

Value Interpreter::GetGlobal(std::string_view name) const {
	GilLock lock(m_impl->gil);
	const uint32_t *id = m_impl->symbolIds.Find(name);
	if (!id)
		return {};
	const Value *v = m_impl->globals.Find(*id);
	return v ? *v : Value();
}

std::vector<std::string> Interpreter::GlobalNames() const {
	GilLock lock(m_impl->gil);
	std::vector<std::string> out;
	out.reserve(m_impl->globals.Size());
	for (const auto &e : m_impl->globals)
		if (e.key < m_impl->symbolNames.size())
			out.push_back(m_impl->symbolNames[e.key]);
	std::ranges::sort(out);
	return out;
}

std::vector<std::string> Interpreter::ParamNames(const Value &fn) const {
	GilLock lock(m_impl->gil);
	std::vector<std::string> out;
	if (const auto *c = std::get_if<std::shared_ptr<Closure>>(&fn.Raw()); c && *c && (*c)->def)
		for (uint32_t sym : (*c)->def->params)
			out.push_back(sym < m_impl->symbolNames.size() ? m_impl->symbolNames[sym] : std::string());
	return out;
}

Result<Value, ScriptError> Interpreter::CallNamed(const Value &fn, const Value &payload) {
	if (const Map *m = payload.AsMap()) {
		const std::vector<std::string> names = ParamNames(fn);
		bool any = false;
		for (const std::string &n : names)
			any = any || m->contains(n);
		if (any) {
			std::vector<Value> args;
			args.reserve(names.size());
			for (const std::string &n : names) {
				const auto it = m->find(n);
				args.push_back(it == m->end() ? Value() : it->second);
			}
			return Call(fn, args);
		}
	}
	const Value args[] = {payload};
	return Call(fn, args);
}

std::span<const std::string_view> Keywords() noexcept {
	static constexpr std::string_view kKeywords[] = {"let",	  "var",	 "const",   "fn",	   "func",	 "if",	   "else",	  "while",
													 "do",	  "for",	 "in",	   "return", "break",	 "continue", "and",	  "or",
													 "not",	  "nil",	 "true",	"false",  "null",	 "class",	"interface", "enum",
													 "extends", "implements", "override", "this",	 "super",	 "new",	  "is",
													 "as",	  "static",	 "namespace", "factory", "operator", "abstract", "import",
													 "export", "async",	 "await"};
	return kKeywords;
}

void Interpreter::RegisterClass(const std::shared_ptr<ClassDef> &cls, std::string_view module) {
	GilLock lock(m_impl->gil);
	if (module.empty()) {
		SetGlobal(cls->name, Value(cls));
		return;
	}
	Value table = GetGlobal(module);
	if (!table.IsMap())
		table = Value(Map{});
	table.AsMap()->insert_or_assign(cls->name, Value(cls));
	SetGlobal(module, std::move(table));
}

Result<Value, ScriptError> Interpreter::Instantiate(const Value &cls, std::span<const Value> args, const std::function<void(Instance &)> &prepare) {
	GilLock lock(m_impl->gil);
	m_impl->MaybeCollect();
	Impl &i = *m_impl;
	const auto *def = std::get_if<std::shared_ptr<ClassDef>>(&cls.Raw());
	if (!def)
		return Err(ScriptError{std::format("Instantiate : {} n'est pas une classe", cls.TypeName()), "", 0, 0});
	const bool nested = t_exec.depth > 0;
	if (!nested) {
		t_exec.failed = false;
		t_exec.steps = 0;
	}
	Value out;
	const bool ok = i.Instantiate(nullptr, *def, args, &prepare, out);
	if (!ok && !nested) {
		t_exec.failed = false;
		return Err(std::move(t_exec.error));
	}
	if (!ok)
		return Err(t_exec.error);
	return Ok(std::move(out));
}

Value Interpreter::Method(const Value &instance, std::string_view name) const {
	const Instance *o = instance.AsInstance();
	if (!o)
		return {};
	const Impl::Found m = Impl::FindMethod(*o->cls, name);
	if (!m.Ok())
		return {};
	std::shared_ptr<Instance> inst = instance.InstancePtr();
	return Value::MakeNative(std::string(name), [inst, m](Interpreter &vm, std::span<const Value> a) -> Result<Value, std::string> {
		Value r;
		if (!vm.m_impl->CallMethod(nullptr, inst, m, a, r))
			return Err(t_exec.error.message);
		return Ok(std::move(r));
	});
}

Result<Value, ScriptError> Interpreter::CallMethod(const Value &instance, std::string_view name, std::span<const Value> args) {
	const Value fn = Method(instance, name);
	if (fn.IsNil())
		return Ok(Value());
	return Call(fn, args);
}

Result<std::vector<std::pair<std::string, Value>>, ScriptError> Interpreter::Exports(const Value &cls) {
	GilLock lock(m_impl->gil);
	Impl &i = *m_impl;
	const ClassDef *def = cls.AsClass();
	if (!def)
		return Err(ScriptError{"Exports : classe attendue", "", 0, 0});
	std::vector<const ClassDef *> chain; // des ancêtres vers la classe : la dérivée redéfinit
	for (const ClassDef *c = def; c; c = c->parent.get())
		chain.push_back(c);
	std::vector<std::pair<std::string, Value>> out;
	t_exec.failed = false;
	for (auto c = chain.rbegin(); c != chain.rend(); ++c)
		for (const auto &[name, raw] : (*c)->fields) {
			if (std::ranges::find((*c)->exported, name) == (*c)->exported.end())
				continue;
			const auto *field = static_cast<const Node *>(raw);
			Value v;
			if (!field->kids.empty() && !i.Eval(*field->kids[0], std::static_pointer_cast<Environment>((*c)->scope), v)) {
				t_exec.failed = false;
				return Err(std::move(t_exec.error));
			}
			if (auto it = std::ranges::find(out, name, &std::pair<std::string, Value>::first); it != out.end())
				it->second = std::move(v);
			else
				out.emplace_back(name, std::move(v));
		}
	return Ok(std::move(out));
}

std::vector<Value> Interpreter::Classes() const {
	GilLock lock(m_impl->gil);
	std::vector<Value> out;
	for (const uint32_t sym : m_impl->classOrder)
		if (const Value *v = m_impl->globals.Find(sym); v && v->IsClass() && std::ranges::none_of(out, [&](const Value &x) { return x.Equals(*v); }))
			out.push_back(*v);
	return out;
}

std::size_t Interpreter::CollectGarbage() {
	GilLock lock(m_impl->gil);
	if (m_impl->gil.OwnedDepth() != 1) // appelé depuis une native : des évaluations sont en cours
		return 0;
	return m_impl->heap.Collect();
}

void Interpreter::RegisterFunction(std::string_view name, NativeFn fn) { SetGlobal(name, Value::MakeNative(std::string(name), std::move(fn))); }

void Interpreter::RegisterModule(std::string_view name, std::vector<std::pair<std::string, NativeFn>> functions) {
	GilLock lock(m_impl->gil);
	Value table = GetGlobal(name);
	if (!table.IsMap())
		table = Value(Map{});
	Map &m = *table.AsMap();
	for (auto &[fname, fn] : functions)
		m.insert_or_assign(fname, Value::MakeNative(std::format("{}.{}", name, fname), std::move(fn)));
	SetGlobal(name, std::move(table));
}

} // namespace script
