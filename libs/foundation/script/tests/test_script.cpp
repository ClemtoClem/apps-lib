#include <cpp-maker/test.hpp>

#include <algorithm>
#include <string>

#include <script/script.hpp>

namespace {
std::string Eval(script::Interpreter &vm, std::string_view src) {
	auto r = vm.Run(src, "test");
	if (!r)
		return "ERR " + r.Error().Format();
	return r.Value().ToString();
}
} // namespace

TEST(Script_ArithmeticAndStrings) {
	script::Interpreter vm;
	CHECK_EQ(Eval(vm, "1 + 2 * 3"), std::string("7"));
	CHECK_EQ(Eval(vm, "2 ^ 10"), std::string("1024"));
	CHECK_EQ(Eval(vm, "-7 % 3"), std::string("2"));
	CHECK_EQ(Eval(vm, "\"a\" .. 1 .. true"), std::string("a1true"));
	CHECK_EQ(Eval(vm, "format(\"{} + {}\", 1, \"x\")"), std::string("1 + x"));
	CHECK_EQ(Eval(vm, "\"a,b,c\".split(\",\")[1]"), std::string("b"));
	CHECK_EQ(Eval(vm, "\"Hello\".sub(1, 3).upper()"), std::string("EL"));
}

TEST(Script_FunctionsClosuresControlFlow) {
	script::Interpreter vm;
	REQUIRE(vm.Run(R"(
		fn counter() {
			let n = 0
			return fn() { n += 1; return n }
		}
		let c = counter()
		c(); c()
		let total = 0
		for (i in range(10)) {
			if (i == 3) { continue }
			if (i == 8) { break }
			total += i
		}
		let m = {a: 1, "b c": 2}
		m.z = 26
		let keysJoined = join(keys(m), ",")
		let fib = fn(n) { if (n < 2) { return n } else { return fib(n - 1) + fib(n - 2) } }
		let w = 0
		while (w < 5) { w += 1 }
	)", "prog").IsOk());
	CHECK_EQ(Eval(vm, "c()"), std::string("3"));
	CHECK_EQ(Eval(vm, "total"), std::string("25"));
	CHECK_EQ(Eval(vm, "keysJoined"), std::string("a,b c,z"));
	CHECK_EQ(Eval(vm, "fib(15)"), std::string("610"));
	CHECK_EQ(Eval(vm, "w"), std::string("5"));
	CHECK_EQ(Eval(vm, "nil == nil and not false"), std::string("true"));
}

TEST(Script_HostBindingsAndErrors) {
	script::Interpreter vm;
	int calls = 0;
	vm.RegisterModule("world", {{"get", [&](script::Interpreter &, std::span<const script::Value> a) -> Result<script::Value, std::string> {
									 ++calls;
									 return Ok(script::Value(a.size() == 3 ? "core:stone" : "core:air"));
								 }}});
	CHECK_EQ(Eval(vm, "world.get(1, 2, 3)"), std::string("core:stone"));
	CHECK_EQ(calls, 1);
	REQUIRE(vm.Run("fn hook(x) { return x * 2 }", "mod").IsOk());
	script::Value args[] = {script::Value(21)};
	auto r = vm.Call(vm.GetGlobal("hook"), args);
	REQUIRE(r.IsOk());
	CHECK_EQ(r.Value().AsInt(), 42);
	auto bad = vm.Run("let x = 1\nlet y = x.foo.bar", "bad.script");
	REQUIRE(bad.IsError());
	CHECK_EQ(bad.Error().line, 2);
	vm.SetStepLimit(10000);
	CHECK(vm.Run("while (true) { }", "loop").IsError());
}

#include <filesystem>
#include <fstream>
#include <iterator>

TEST(Script_ParseAllVoxeliaMods) {
	namespace fs = std::filesystem;
	fs::path root = fs::current_path();
	while (!root.empty() && !fs::exists(root / "games/voxelia/mods") && root != root.parent_path())
		root = root.parent_path();
	if (!fs::exists(root / "games/voxelia/mods"))
		return;
	script::Interpreter vm;
	size_t files = 0;
	for (const auto &e : fs::recursive_directory_iterator(root / "games/voxelia/mods")) {
		if (e.path().extension() != ".script")
			continue;
		std::ifstream f(e.path());
		std::string src((std::istreambuf_iterator<char>(f)), {});
		auto r = vm.Check(src, e.path().string());
		if (!r)
			std::printf("    %s\n", r.Error().Format().c_str());
		CHECK(r.IsOk());
		++files;
	}
	std::printf("    %zu scripts de mods analysés\n", files);
}

TEST(Script_GlobalNamesForCompletion) {
	script::Interpreter vm;
	vm.RegisterModule("voxel", {{"on", [](script::Interpreter &, std::span<const script::Value>) -> Result<script::Value, std::string> {
									 return Ok(script::Value());
								 }}});
	REQUIRE(vm.Run("let speed = 3\nfn helper() { return 1 }").IsOk());
	const auto names = vm.GlobalNames();
	CHECK(std::ranges::find(names, "voxel") != names.end());
	CHECK(std::ranges::find(names, "speed") != names.end());
	CHECK(std::ranges::find(names, "helper") != names.end());
	CHECK(std::ranges::is_sorted(names));
	const script::Value mod = vm.GetGlobal("voxel");
	REQUIRE(mod.IsMap());
	CHECK(mod.AsMap()->contains("on"));
}

TEST(Script_TypedBindings) {
	script::Interpreter in;
	double moved = 0.0;
	std::string named;
	script::Binder(in, "obj")
		.Fn("move", [&](double dx, float dy, int dz) { moved = dx + double(dy) + dz; })
		.Fn("name", [&] { return std::string("caisse"); })
		.Fn("rename", [&](std::string_view n) { named = n; })
		.Fn("scale", [](double v, std::optional<double> k) { return v * k.value_or(2.0); })
		.Fn("pos", [] { return std::vector<double>{1.0, 2.0, 3.0}; })
		.Fn("check", [](bool ok) -> Result<double, std::string> {
			if (!ok)
				return Err(std::string("refusé"));
			return Ok(1.0);
		});
	script::Binder(in, "").Fn("twice", [](double v) { return 2.0 * v; });
	auto r = in.Run(R"(
		obj.move(1, 2, 3)
		obj.rename(obj.name() .. "!")
		let a = obj.scale(5)
		let b = obj.scale(5, 3)
		let p = obj.pos()
		let t = twice(p[2])
		let c = obj.check(true)
	)");
	REQUIRE(r.IsOk());
	CHECK_EQ(moved, 6.0);
	CHECK(named == "caisse!");
	CHECK_EQ(in.GetGlobal("a").AsNumber(), 10.0);
	CHECK_EQ(in.GetGlobal("b").AsNumber(), 15.0);
	CHECK_EQ(in.GetGlobal("t").AsNumber(), 6.0);
	CHECK_EQ(in.GetGlobal("c").AsNumber(), 1.0);
	// Erreurs positionnées : argument manquant, mauvais type, Result en erreur.
	auto missing = in.Run("obj.move(1, 2)");
	REQUIRE(missing.IsError());
	CHECK(missing.Error().message.find("argument 3 manquant") != std::string::npos);
	auto wrong = in.Run("obj.rename(42)");
	REQUIRE(wrong.IsError());
	CHECK(wrong.Error().message.find("texte attendu") != std::string::npos);
	auto refused = in.Run("obj.check(false)");
	REQUIRE(refused.IsError());
	CHECK(refused.Error().message.find("refusé") != std::string::npos);
	CHECK(std::ranges::find(script::Keywords(), std::string_view("fn")) != script::Keywords().end());
}

TEST(Script, ClassesInheritanceAndSuper) {
	script::Interpreter vm;
	auto r = vm.Run(R"(
		class Animal {
			let nom = "?"
			let pattes = 4
			fn init(nom) { this.nom = nom }
			fn cri() { return "..." }
			fn presenter() { return this.nom .. " (" .. this.pattes .. ") : " .. this.cri() }
		}
		class Oiseau extends Animal {
			let pattes = 2
			fn init(nom, chant) {
				super.init(nom)
				this.chant = chant
			}
			fn cri() { return this.chant }
			fn presenter() { return "oiseau " .. super.presenter() }
		}
		let o = Oiseau("Merle", "tuit")
		let f = o.cri   # méthode détachée : `this` reste lié
		let r = [o.presenter(), Animal("Rex").presenter(), f(), o.pattes, o.inconnu]
		r
	)");
	REQUIRE(r.IsOk());
	const script::List &l = *r.Value().AsList();
	CHECK(l[0].AsString() == "oiseau Merle (2) : tuit");
	CHECK(l[1].AsString() == "Rex (4) : ...");
	CHECK(l[2].AsString() == "tuit");
	CHECK_EQ(l[3].AsInt(), 2);
	CHECK(l[4].IsNil());
	const auto classes = vm.Classes();
	REQUIRE(classes.size() == 2u);
	CHECK(classes[1].AsClass()->Derives("Animal"));
	CHECK(vm.Run("class A {}\nclass B {}\nclass C extends A, B {}").IsError()); // deux parents de script
	CHECK(vm.Run("let x = 3\nclass D extends x {}").IsError());
	CHECK(vm.Run("class E { if (true) {} }").IsError()); // seuls champs et méthodes
	REQUIRE(script::Keywords().size() > 20u);
}

namespace {
struct Corps {
	double masse = 1.0;
	std::vector<std::string> *journal = nullptr;
};
struct Lampe {
	bool allumee = false;
};
} // namespace

TEST(Script, NativeClassesAndModules) {
	script::Interpreter vm;
	std::vector<std::string> journal;
	const auto corps = script::ClassBinder<Corps>(vm, "PhysicsBody")
						   .Method("push", [](Corps &c, double v) {
							   if (c.journal)
								   c.journal->push_back("push " + std::to_string(int(v / c.masse)));
						   })
						   .Method("mass", [](Corps &c) { return c.masse; })
						   .Class();
	const auto lampe = script::ClassBinder<Lampe>(vm, "Light3D").Method("light", [](Lampe &l, std::optional<bool> on) { l.allumee = on.value_or(true); }).Class();
	// Modules : chargés par l'hôte, mis en cache, une portée chacun.
	std::map<std::string, std::string, std::less<>> files = {
		{"base", "const LOURD = 10\nclass Caisse extends PhysicsBody {\n\tfn lancer() { this.push(LOURD * 3) }\n}\n"},
		{"boucle", "import \"boucle\"\n"},
	};
	vm.SetModuleLoader([&](std::string_view name) -> Result<std::string, std::string> {
		const auto it = files.find(name);
		if (it == files.end())
			return Err(std::string("introuvable"));
		return Ok(it->second);
	});
	auto r = vm.Run(R"(
		const b = import "base"
		class Lanterne extends b.Caisse, Light3D {
			let lancers = 0
			fn on_use() {
				this.lancer()
				this.light(false)
				this.lancers += 1
				return this.mass()
			}
		}
	)");
	REQUIRE(r.IsOk());
	const script::Value cls = vm.GetGlobal("Lanterne");
	REQUIRE(cls.IsClass());
	CHECK(cls.AsClass()->Derives("PhysicsBody"));
	CHECK(cls.AsClass()->Derives("Light3D"));
	CHECK_EQ(cls.AsClass()->AllNatives().size(), 2u);
	// L'hôte prépare l'état natif avant `init`.
	auto object = vm.Instantiate(cls, {}, [&](script::Instance &o) {
		auto state = std::make_shared<Corps>();
		state->masse = 2.0;
		state->journal = &journal;
		o.SetState(corps.get(), state);
	});
	REQUIRE(object.IsOk());
	auto mass = vm.CallMethod(object.Value(), "on_use", {});
	REQUIRE(mass.IsOk());
	CHECK_NEAR(mass.Value().AsNumber(), 2.0, 1e-9);
	REQUIRE(journal.size() == 1u);
	CHECK(journal[0] == "push 15");
	CHECK(script::StateOf<Lampe>(object.Value(), *lampe) != nullptr);
	CHECK(!script::StateOf<Lampe>(object.Value(), *lampe)->allumee);
	CHECK_EQ(object.Value().Get("lancers").AsInt(), 0); // Get : tables seulement
	CHECK(vm.CallMethod(object.Value(), "absente", {}).Value().IsNil());
	CHECK(vm.Method(object.Value(), "on_use").IsCallable());
	// Erreurs : module introuvable, import circulaire, classe native seule sans état.
	CHECK(vm.Run("const x = import \"nulle-part\"").IsError());
	CHECK(vm.Run("import \"boucle\"").IsError());
	script::Interpreter bare;
	CHECK(bare.Run("import \"rien\"\nlet ok = 1").IsOk()); // sans chargeur : sans effet (mods)
}

TEST(Script, ExportedParameters) {
	script::Interpreter vm;
	auto r = vm.Run(R"(
		const BASE = 2
		class Vehicule {
			export let vitesse = BASE * 2
			export let nom = "?"
			let interne = 1
		}
		class Bolide extends Vehicule {
			export let vitesse = 9   # redéfinie
			export let turbo = true
			fn init() { this.depart = this.vitesse }
		}
	)");
	REQUIRE(r.IsOk());
	auto params = vm.Exports(vm.GetGlobal("Bolide"));
	REQUIRE(params.IsOk());
	REQUIRE(params.Value().size() == 3u); // vitesse, nom, turbo — pas « interne »
	CHECK(params.Value()[0].first == "vitesse");
	CHECK_EQ(params.Value()[0].second.AsInt(), 9);
	CHECK(params.Value()[1].second.AsString() == "?");
	CHECK(params.Value()[2].second.Truthy());
	// L'hôte pose la valeur réglée avant init.
	auto o = vm.Instantiate(vm.GetGlobal("Bolide"), {}, [](script::Instance &inst) { inst.fields.insert_or_assign("vitesse", script::Value(12.0)); });
	REQUIRE(o.IsOk());
	CHECK_EQ(o.Value().AsInstance()->fields["depart"].AsInt(), 12);
	CHECK(vm.Run("class X { export fn f() {} }").IsError());
}

TEST(Script, AbstractClasses) {
	script::Interpreter vm;
	REQUIRE(vm.Run("abstract class Forme { fn aire() { return 0 } }\nclass Carre extends Forme { let c = 2\n fn aire() { return this.c * this.c } }").IsOk());
	CHECK(vm.Run("Forme()").IsError());
	auto r = vm.Run("Carre().aire()");
	REQUIRE(r.IsOk());
	CHECK_EQ(r.Value().AsInt(), 4);
	CHECK(vm.GetGlobal("Forme").AsClass()->abstract);
}
