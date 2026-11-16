#pragma once
/**
 * @file script.hpp
 * @brief Langage de script des mods : valeurs, interpréteur, liaisons hôte.
 *
 * Langage
 * - commentaires `#`, `//` et blocs ; points-virgules facultatifs ;
 * - `nil`, `true`/`false`, nombres, chaînes `"…"`/`'…'`, listes `[1, 2]`,
 *   tables `{clé: valeur, "autre": v}`, fonctions de première classe et fermetures ;
 * - `let`/`var`/`const`, `fn nom(a, b) { … }`, `fn(x) { … }` ;
 * - `if / else if / else`, `while`, `for (x in liste|table|range(n))`, `break`,
 *   `continue`, `return` ;
 * - classes : `class Nom extends Parent, BaseNative { let champ = 0  fn init(a) { … }  fn m() { … } }`,
 *   `export let vitesse = 4` : paramètre réglable par l'hôte (valeur posée avant `init`),
 *   au plus une classe parente de script et des bases natives (C++, `ClassBinder`) ; `Nom(args)`
 *   crée une instance (champs initialisés, puis `init`) ; `this`, `super.m(…)` dans les méthodes ;
 * - modules : `const m = import "nom"` (table de ses définitions) ou `import "nom"` (ses
 *   définitions dans la portée courante), chargés par `SetModuleLoader` ;
 * - opérateurs `+ - * / % ^`, `..` (concaténation), `== != < <= > >=`,
 *   `and or not`, affectations `= += -= *= /= %=` sur variables, index et membres.
 *
 * Exécution : arbre syntaxique interprété, symboles interneés en entiers (aucune
 * comparaison de chaînes pour résoudre une variable), environnements chaînés,
 * aucune exception (erreurs positionnées via `Result`).
 */
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <type_traits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <core/result.hpp>

namespace script {

class Interpreter;
struct FunctionDef;
struct Environment;
class Value;
struct ClassDef;
struct Instance;

using List = std::vector<Value>;
using Map = std::map<std::string, Value, std::less<>>;
using NativeFn = std::function<Result<Value, std::string>(Interpreter &, std::span<const Value> args)>;

struct Closure {
	const FunctionDef *def = nullptr;
	std::shared_ptr<Environment> env;
	std::shared_ptr<const void> keepAlive; // maintient l'AST du fichier source en vie
};
struct Native {
	std::string name;
	NativeFn fn;
};

enum class ValueType : uint8_t { Nil, Bool, Number, Text, Array, Table, Function, Builtin, Class, Object };

class Value {
public:
	using Storage = std::variant<std::monostate, bool, double, std::string, std::shared_ptr<List>, std::shared_ptr<Map>,
								 std::shared_ptr<Closure>, std::shared_ptr<Native>, std::shared_ptr<ClassDef>, std::shared_ptr<Instance>>;

	Value() noexcept = default;
	Value(std::nullptr_t) noexcept {}
	Value(bool b) noexcept : m_v(b) {}
	Value(double d) noexcept : m_v(d) {}
	Value(int i) noexcept : m_v(static_cast<double>(i)) {}
	Value(int64_t i) noexcept : m_v(static_cast<double>(i)) {}
	Value(float f) noexcept : m_v(static_cast<double>(f)) {}
	Value(const char *s) : m_v(std::string(s ? s : "")) {}
	Value(std::string s) noexcept : m_v(std::move(s)) {}
	Value(std::string_view s) : m_v(std::string(s)) {}
	Value(List l) : m_v(std::make_shared<List>(std::move(l))) {}
	Value(Map m) : m_v(std::make_shared<Map>(std::move(m))) {}
	Value(std::shared_ptr<List> l) noexcept : m_v(std::move(l)) {}
	Value(std::shared_ptr<Map> m) noexcept : m_v(std::move(m)) {}
	Value(std::shared_ptr<Closure> c) noexcept : m_v(std::move(c)) {}
	Value(std::shared_ptr<Native> n) noexcept : m_v(std::move(n)) {}
	Value(std::shared_ptr<ClassDef> c) noexcept : m_v(std::move(c)) {}
	Value(std::shared_ptr<Instance> o) noexcept : m_v(std::move(o)) {}

	[[nodiscard]] static Value MakeNative(std::string name, NativeFn fn) {
		return Value(std::make_shared<Native>(Native{std::move(name), std::move(fn)}));
	}

	[[nodiscard]] ValueType Type() const noexcept { return static_cast<ValueType>(m_v.index()); }
	[[nodiscard]] bool IsNil() const noexcept { return m_v.index() == 0; }
	[[nodiscard]] bool IsNumber() const noexcept { return m_v.index() == 2; }
	[[nodiscard]] bool IsString() const noexcept { return m_v.index() == 3; }
	[[nodiscard]] bool IsList() const noexcept { return m_v.index() == 4; }
	[[nodiscard]] bool IsMap() const noexcept { return m_v.index() == 5; }
	/// Fonction, fonction native ou classe (appeler une classe crée une instance).
	[[nodiscard]] bool IsCallable() const noexcept { return m_v.index() >= 6 && m_v.index() <= 8; }
	[[nodiscard]] bool IsClass() const noexcept { return m_v.index() == 8; }
	[[nodiscard]] bool IsInstance() const noexcept { return m_v.index() == 9; }
	[[nodiscard]] ClassDef *AsClass() const noexcept;
	[[nodiscard]] Instance *AsInstance() const noexcept;
	[[nodiscard]] std::shared_ptr<Instance> InstancePtr() const noexcept;

	[[nodiscard]] bool Truthy() const noexcept;
	[[nodiscard]] double AsNumber(double def = 0.0) const noexcept;
	[[nodiscard]] int AsInt(int def = 0) const noexcept { return static_cast<int>(AsNumber(def)); }
	[[nodiscard]] bool AsBool() const noexcept { return Truthy(); }
	[[nodiscard]] std::string_view AsString() const noexcept;
	[[nodiscard]] List *AsList() const noexcept;
	[[nodiscard]] Map *AsMap() const noexcept;
	/// Membre d'une table (nil si absent).
	[[nodiscard]] Value Get(std::string_view key) const;

	/// Représentation lisible (`print`, `str`, concaténation).
	[[nodiscard]] std::string ToString() const;
	[[nodiscard]] std::string_view TypeName() const noexcept;
	[[nodiscard]] bool Equals(const Value &o) const noexcept;

	[[nodiscard]] const Storage &Raw() const noexcept { return m_v; }

private:
	Storage m_v;
};

/// Méthode d'une classe native : l'instance de script et ses arguments.
using NativeMethod = std::function<Result<Value, std::string>(Interpreter &, Instance &self, std::span<const Value> args)>;

/// Classe : de script (`class` du langage) ou native (déclarée en C++ par `ClassBinder`).
struct ClassDef {
	std::string name;
	std::shared_ptr<ClassDef> parent;				 ///< classe de script parente (une au plus)
	std::vector<std::shared_ptr<ClassDef>> natives;	 ///< bases natives directes, dans l'ordre de `extends`
	std::map<std::string, Value, std::less<>> methods; ///< méthodes de script (fermetures)
	// Classe native.
	std::map<std::string, NativeMethod, std::less<>> nativeMethods;
	std::function<std::shared_ptr<void>()> makeState; ///< état C++ de chaque instance
	bool native = false;
	bool abstract = false; ///< `abstract class` : ne s'instancie pas directement (seulement ses dérivées)
	// Champs `let` d'une classe de script (interne à l'interpréteur).
	std::vector<std::pair<std::string, const void *>> fields;
	std::vector<std::string> exported; ///< champs `export let` : paramètres réglables par l'hôte
	std::shared_ptr<void> scope;			   ///< environnement de déclaration
	std::shared_ptr<const void> keepAlive;	   ///< programme source

	/// La classe est `name`, ou en dérive (parents de script et bases natives).
	[[nodiscard]] bool Derives(std::string_view name) const;
	/// Toutes les bases natives, de la classe et de ses ancêtres, sans doublon.
	[[nodiscard]] std::vector<const ClassDef *> AllNatives() const;
};

/// Instance d'une classe : champs de script et état C++ de chaque base native.
struct Instance {
	std::shared_ptr<ClassDef> cls;
	Map fields;
	std::vector<std::pair<const ClassDef *, std::shared_ptr<void>>> states;
	[[nodiscard]] void *StateOf(const ClassDef *native) const noexcept {
		for (const auto &[c, s] : states)
			if (c == native)
				return s.get();
		return nullptr;
	}
	/// Remplace l'état d'une base native (préparé par l'hôte avant `init`).
	void SetState(const ClassDef *native, std::shared_ptr<void> state);
};

struct ScriptError {
	std::string message;
	std::string file;
	int line = 0;
	int column = 0;
	[[nodiscard]] std::string Format() const;
};

class Interpreter {
public:
	Interpreter();
	~Interpreter();
	Interpreter(const Interpreter &) = delete;
	Interpreter &operator=(const Interpreter &) = delete;

	/// Exécute un fichier source (les définitions vont dans l'environnement global).
	[[nodiscard]] Result<Value, ScriptError> Run(std::string_view source, std::string_view fileName = "<script>");
	[[nodiscard]] Result<Value, ScriptError> RunFile(const std::string &path);
	/// Analyse seulement (aucune exécution) : diagnostics d'éditeur, validation de mods.
	[[nodiscard]] Result<bool, ScriptError> Check(std::string_view source, std::string_view fileName = "<script>");
	/// Appelle une valeur appelable (fermeture ou fonction native).
	[[nodiscard]] Result<Value, ScriptError> Call(const Value &fn, std::span<const Value> args);

	void SetGlobal(std::string_view name, Value v);
	[[nodiscard]] Value GetGlobal(std::string_view name) const;
	/// Noms des variables globales définies (fonctions, modules…), pour la complétion.
	[[nodiscard]] std::vector<std::string> GlobalNames() const;
	/// Noms des paramètres d'une fermeture (vide pour une fonction native).
	[[nodiscard]] std::vector<std::string> ParamNames(const Value &fn) const;
	/// Appelle `fn` en liant ses paramètres par nom aux clés de la table `payload`
	/// (`fn(x, y, z, block)` reçoit `payload.x`…) ; si aucun nom ne correspond, ou si
	/// `payload` n'est pas une table, `fn` reçoit `payload` tel quel.
	[[nodiscard]] Result<Value, ScriptError> CallNamed(const Value &fn, const Value &payload);
	void RegisterFunction(std::string_view name, NativeFn fn);
	/// Classe native globale (ou membre d'une table `module`) ; voir `ClassBinder`.
	void RegisterClass(const std::shared_ptr<ClassDef> &cls, std::string_view module = {});
	/// Crée une instance de `cls` (classe de script ou native) : états natifs, champs, puis
	/// `prepare` (l'hôte y attache ses données aux bases natives), puis `init(args)`.
	[[nodiscard]] Result<Value, ScriptError> Instantiate(const Value &cls, std::span<const Value> args,
														 const std::function<void(Instance &)> &prepare = {});
	/// Méthode d'une instance (`this` lié) ; nil si elle n'existe pas.
	[[nodiscard]] Value Method(const Value &instance, std::string_view name) const;
	/// Appelle la méthode `name` de l'instance ; nil sans erreur si elle n'existe pas.
	[[nodiscard]] Result<Value, ScriptError> CallMethod(const Value &instance, std::string_view name, std::span<const Value> args);
	/// Paramètres d'une classe (`export let nom = défaut`, ancêtres compris) et leurs valeurs par
	/// défaut, évaluées sans créer d'instance.
	[[nodiscard]] Result<std::vector<std::pair<std::string, Value>>, ScriptError> Exports(const Value &cls);
	/// Classes définies dans l'environnement global (après `Run`), dans l'ordre de déclaration.
	[[nodiscard]] std::vector<Value> Classes() const;
	/// Appelée après chaque instance créée (par le script ou l'hôte), son `init` passé : l'hôte peut
	/// suivre les objets qu'un script fabrique (comportements attachés à la volée).
	void SetInstanceHook(std::function<void(const Value &)> hook) { m_instanceHook = std::move(hook); }
	/// Source d'un module (`import "nom"`) ; sans chargeur, `import` est sans effet.
	void SetModuleLoader(std::function<Result<std::string, std::string>(std::string_view name)> loader) { m_loader = std::move(loader); }
	/// Crée/complète une table globale de fonctions (`world.get`, `game.say`…).
	void RegisterModule(std::string_view name, std::vector<std::pair<std::string, NativeFn>> functions);

	void SetPrint(std::function<void(std::string_view)> print) { m_print = std::move(print); }
	void Print(std::string_view s) const;
	/// Limite d'instructions par appel hôte (0 = illimité) : protège des boucles infinies.
	void SetStepLimit(uint64_t steps) noexcept { m_stepLimit = steps; }

	struct Impl;

private:
	friend struct Impl;
	std::unique_ptr<Impl> m_impl;
	std::function<void(std::string_view)> m_print;
	std::function<Result<std::string, std::string>(std::string_view)> m_loader;
	std::function<void(const Value &)> m_instanceHook;
	uint64_t m_stepLimit = 50'000'000;
};

/// Formatage `format("x = {}, y = {}", x, y)` utilisé par la bibliothèque standard.
[[nodiscard]] std::string FormatValues(std::string_view fmt, std::span<const Value> args);

/// Mots-clés du langage (coloration syntaxique, complétion).
[[nodiscard]] std::span<const std::string_view> Keywords() noexcept;

// ── Liaisons typées ─────────────────────────────────────────────────────────
//
// Une fonction C++ ordinaire devient une fonction du script : ses arguments sont convertis
// depuis les valeurs du script (nombre → double/float/int, texte → std::string /
// std::string_view, bool, Value, std::optional<T> pour un argument facultatif), son résultat
// vers une valeur (void → nil, nombres, texte, bool, std::vector<double> → liste, Value,
// Result<T, std::string> → erreur du script). Un argument manquant ou d'un mauvais type est
// une erreur positionnée, sans exception.
//
//   script::Binder(interp, "self")
//       .Fn("move", [&](double dx, double dy, double dz) { … })
//       .Fn("name", [&] { return name; });

namespace detail {

template <typename T> struct Callable : Callable<decltype(&T::operator())> {};
template <typename C, typename R, typename... A> struct Callable<R (C::*)(A...) const> {
	using Ret = R;
	using Args = std::tuple<A...>;
};
template <typename C, typename R, typename... A> struct Callable<R (C::*)(A...)> : Callable<R (C::*)(A...) const> {};
template <typename R, typename... A> struct Callable<R (*)(A...)> {
	using Ret = R;
	using Args = std::tuple<A...>;
};

template <typename T> struct IsOptional : std::false_type {};
template <typename T> struct IsOptional<std::optional<T>> : std::true_type {};
template <typename T> struct IsResult : std::false_type {};
template <typename T, typename E> struct IsResult<Result<T, E>> : std::true_type {};

/// Valeur du script → type C++ (`v` nul : argument absent).
template <typename T> Result<T, std::string> FromValue(const Value *v, size_t index) {
	const auto missing = [index] { return std::format("argument {} manquant", index + 1); };
	if constexpr (IsOptional<T>::value) {
		if (!v || v->IsNil())
			return Ok(T{});
		auto inner = FromValue<typename T::value_type>(v, index);
		if (inner.IsError())
			return Err(inner.Error());
		return Ok(T{std::move(inner.Value())});
	} else if constexpr (std::is_same_v<T, Value>) {
		return Ok(v ? *v : Value());
	} else if constexpr (std::is_same_v<T, bool>) {
		if (!v)
			return Err(missing());
		return Ok(v->Truthy());
	} else if constexpr (std::is_arithmetic_v<T>) {
		if (!v)
			return Err(missing());
		if (!v->IsNumber())
			return Err(std::format("argument {} : nombre attendu, {} reçu", index + 1, v->TypeName()));
		return Ok(static_cast<T>(v->AsNumber()));
	} else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>) {
		if (!v)
			return Err(missing());
		if (!v->IsString())
			return Err(std::format("argument {} : texte attendu, {} reçu", index + 1, v->TypeName()));
		return Ok(T(v->AsString()));
	} else {
		static_assert(sizeof(T) == 0, "type d'argument non pris en charge par script::Binder");
	}
}

template <typename R> Result<Value, std::string> ToValue(R &&r) {
	using D = std::remove_cvref_t<R>;
	if constexpr (IsResult<D>::value) {
		if (r.IsError())
			return Err(std::string(r.Error()));
		if constexpr (std::is_void_v<typename D::ValueType>)
			return Ok(Value());
		else
			return ToValue(std::move(r.Value()));
	} else if constexpr (std::is_same_v<D, Value>) {
		return Ok(std::forward<R>(r));
	} else if constexpr (std::is_same_v<D, std::vector<double>> || std::is_same_v<D, std::vector<float>>) {
		List list;
		for (const auto x : r)
			list.emplace_back(static_cast<double>(x));
		return Ok(Value(std::move(list)));
	} else if constexpr (std::is_same_v<D, bool>) {
		return Ok(Value(bool(r)));
	} else if constexpr (std::is_arithmetic_v<D>) {
		return Ok(Value(static_cast<double>(r)));
	} else {
		return Ok(Value(std::forward<R>(r)));
	}
}

template <typename F, typename... A, size_t... I>
Result<Value, std::string> Invoke(F &fn, std::span<const Value> args, std::tuple<A...> *, std::index_sequence<I...>) {
	std::tuple<Result<std::remove_cvref_t<A>, std::string>...> conv{FromValue<std::remove_cvref_t<A>>(I < args.size() ? &args[I] : nullptr, I)...};
	std::string error;
	((error.empty() && std::get<I>(conv).IsError() ? (void)(error = std::get<I>(conv).Error()) : (void)0), ...);
	if (!error.empty())
		return Err(std::move(error));
	using R = std::invoke_result_t<F &, A...>;
	if constexpr (std::is_void_v<R>) {
		fn(std::move(std::get<I>(conv).Value())...);
		return Ok(Value());
	} else {
		return ToValue(fn(std::move(std::get<I>(conv).Value())...));
	}
}

} // namespace detail

/// Fonction native typée (voir les conversions ci-dessus).
template <typename F> [[nodiscard]] NativeFn Bind(F fn) {
	return [fn = std::move(fn)](Interpreter &, std::span<const Value> args) mutable -> Result<Value, std::string> {
		using Args = typename detail::Callable<std::decay_t<F>>::Args;
		return detail::Invoke(fn, args, static_cast<Args *>(nullptr), std::make_index_sequence<std::tuple_size_v<Args>>{});
	};
}

/// Enregistre des fonctions typées dans une table globale (`module`, complétée si elle existe)
/// ou, si `module` est vide, comme fonctions globales.
class Binder {
public:
	Binder(Interpreter &interp, std::string_view module) : m_interp(interp), m_module(module) {}
	template <typename F> Binder &Fn(std::string_view name, F fn) {
		if (m_module.empty())
			m_interp.RegisterFunction(name, Bind(std::move(fn)));
		else
			m_interp.RegisterModule(m_module, {{std::string(name), Bind(std::move(fn))}});
		return *this;
	}

private:
	Interpreter &m_interp;
	std::string m_module;
};

/**
 * Classe native : l'état C++ `T` de chaque instance et ses méthodes typées (premier paramètre
 * `T&`, les suivants convertis comme pour `Bind`). Une classe de script en hérite par `extends` :
 *
 *   struct Corps { GameSession *game = nullptr; EntityId id = 0; };
 *   script::ClassBinder<Corps>(interp, "PhysicsBody")
 *       .Method("push", [](Corps &c, float vx, float vy, float vz) { c.game->Push(c.id, {vx, vy, vz}); })
 *       .Method("mass", [](Corps &c) { return c.game->MassOf(c.id); });
 *
 *   class Caisse extends PhysicsBody { fn on_use() { this.push(0, 3, 0) } }
 *
 * `T` est construit par défaut pour chaque instance ; l'hôte le remplace dans le `prepare`
 * d'`Interpreter::Instantiate` (`StateOf<T>`, `Instance::SetState`).
 */
template <typename T> class ClassBinder {
public:
	ClassBinder(Interpreter &interp, std::string_view name, std::string_view module = {}) : m_class(std::make_shared<ClassDef>()) {
		m_class->name = std::string(name);
		m_class->native = true;
		if constexpr (std::is_default_constructible_v<T>)
			m_class->makeState = [] { return std::static_pointer_cast<void>(std::make_shared<T>()); };
		interp.RegisterClass(m_class, module);
	}
	template <typename F> ClassBinder &Method(std::string_view name, F fn) {
		const ClassDef *self = m_class.get();
		const std::string label = std::format("{}.{}", m_class->name, name);
		m_class->nativeMethods.insert_or_assign(std::string(name), [fn = std::move(fn), self, label](Interpreter &, Instance &inst,
																										  std::span<const Value> args) mutable -> Result<Value, std::string> {
			T *state = static_cast<T *>(inst.StateOf(self));
			if (!state)
				return Err(std::format("{} : instance sans base {}", label, self->name));
			using Args = typename detail::Callable<std::decay_t<F>>::Args;
			return InvokeOn(fn, *state, args, static_cast<Args *>(nullptr));
		});
		return *this;
	}
	[[nodiscard]] const std::shared_ptr<ClassDef> &Class() const noexcept { return m_class; }

private:
	template <typename F, typename First, typename... A>
	static Result<Value, std::string> InvokeOn(F &fn, T &state, std::span<const Value> args, std::tuple<First, A...> *) {
		static_assert(std::is_same_v<std::remove_cvref_t<First>, T>, "le premier paramètre d'une méthode native est T&");
		auto bound = [&](A... rest) { return fn(state, std::forward<A>(rest)...); };
		return detail::Invoke(bound, args, static_cast<std::tuple<A...> *>(nullptr), std::index_sequence_for<A...>{});
	}
	std::shared_ptr<ClassDef> m_class;
};

/// État C++ de la base native `native` d'une instance (nullptr si elle n'en dérive pas).
template <typename T> [[nodiscard]] T *StateOf(const Value &instance, const ClassDef &native) noexcept {
	const Instance *o = instance.AsInstance();
	return o ? static_cast<T *>(o->StateOf(&native)) : nullptr;
}

} // namespace script
