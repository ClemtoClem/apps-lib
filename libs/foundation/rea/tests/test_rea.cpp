#include <cpp-maker/test.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <rea/rea.hpp>

// ── Désassemblage x86-64 ─────────────────────────────────────────────────────

TEST(Rea_Disasm_Basic) {
	// mov eax, 0x2A
	{
		const uint8_t code[] = {0xB8, 0x2A, 0x00, 0x00, 0x00};
		auto r = rea::DecodeInstruction(code, sizeof(code), 0x1000);
		CHECK(r.IsOk());
		CHECK_EQ(r.Value().mnemonic, std::string("mov"));
		CHECK_EQ(r.Value().length, uint8_t(5));
		CHECK(r.Value().operands.find("eax") != std::string::npos);
		CHECK(r.Value().operands.find("0x2") != std::string::npos); // immédiat hexadécimal
	}

	// ret
	{
		const uint8_t code[] = {0xC3};
		auto r = rea::DecodeInstruction(code, sizeof(code), 0x1000);
		CHECK(r.IsOk());
		CHECK(r.Value().isReturn);
		CHECK_EQ(r.Value().length, uint8_t(1));
	}

	// mov rax, rbx (REX.W)
	{
		const uint8_t code[] = {0x48, 0x89, 0xD8};
		auto r = rea::DecodeInstruction(code, sizeof(code), 0x1000);
		CHECK(r.IsOk());
		CHECK_EQ(r.Value().mnemonic, std::string("mov"));
		CHECK(r.Value().operands.find("rax") != std::string::npos);
		CHECK(r.Value().operands.find("rbx") != std::string::npos);
	}

	// lea rax, [rbp-0x8]
	{
		const uint8_t code[] = {0x48, 0x8D, 0x45, 0xF8};
		auto r = rea::DecodeInstruction(code, sizeof(code), 0x1000);
		CHECK(r.IsOk());
		CHECK_EQ(r.Value().mnemonic, std::string("lea"));
		CHECK(r.Value().operands.find("rbp") != std::string::npos);
	}

	// jmp -2 (boucle sur elle-même)
	{
		const uint8_t code[] = {0xEB, 0xFE};
		auto r = rea::DecodeInstruction(code, sizeof(code), 0x1000);
		CHECK(r.IsOk());
		CHECK_EQ(r.Value().mnemonic, std::string("jmp"));
		CHECK(r.Value().isBranch);
		CHECK_FALSE(r.Value().isConditional);
		CHECK(r.Value().hasTarget);
		CHECK_EQ(r.Value().target, uint64_t(0x1000));
	}

	// call +0 (rel32 nul) → cible = adresse + 5
	{
		const uint8_t code[] = {0xE8, 0x00, 0x00, 0x00, 0x00};
		auto r = rea::DecodeInstruction(code, sizeof(code), 0x1000);
		CHECK(r.IsOk());
		CHECK(r.Value().isCall);
		CHECK(r.Value().hasTarget);
		CHECK_EQ(r.Value().target, uint64_t(0x1005));
	}

	// jne +5 (jcc conditionnel)
	{
		const uint8_t code[] = {0x75, 0x05};
		auto r = rea::DecodeInstruction(code, sizeof(code), 0x1000);
		CHECK(r.IsOk());
		CHECK(r.Value().isBranch);
		CHECK(r.Value().isConditional);
		CHECK(r.Value().hasTarget);
	}

	// octet invalide
	{
		const uint8_t code[] = {0x0F}; // préfixe 0F tronqué
		auto r = rea::DecodeInstruction(code, sizeof(code), 0x1000);
		CHECK(r.IsError());
	}
}

TEST(Rea_Disasm_Linear) {
	// push rbp ; mov rbp, rsp ; pop rbp ; ret
	const uint8_t code[] = {0x55, 0x48, 0x89, 0xE5, 0x5D, 0xC3};
	auto insns = rea::Disassemble(code, sizeof(code), 0x2000);
	CHECK_EQ(insns.size(), size_t(4));
	CHECK_EQ(insns[0].mnemonic, std::string("push"));
	CHECK_EQ(insns[0].address, uint64_t(0x2000));
	CHECK_EQ(insns[1].mnemonic, std::string("mov"));
	CHECK_EQ(insns[2].mnemonic, std::string("pop"));
	CHECK(insns[3].isReturn);
}

// ── Analyse ELF64 (binaire compilé sur place) ────────────────────────────────

namespace {

/// Compile un petit programme C et rend son chemin (vide si échec).
std::string CompileFixture() {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "rea-test";
	std::filesystem::create_directories(dir);
	const auto src = dir / "fixture.c";
	const auto bin = dir / "fixture";
	{
		std::ofstream f(src);
		f << "#include <stdio.h>\n"
			 "int add(int a, int b) { return a + b; }\n"
			 "int mul(int a, int b) { return a * b; }\n"
			 "int sum_array(const int *arr, int n) { int t = 0; for (int i = 0; i < n; i++) t += arr[i]; return t; }\n"
			 "int main(void) { printf(\"somme=%d\\n\", sum_array((const int[]){1,2,3}, 3)); return add(2, 3) + mul(4, 5); }\n";
	}
	const std::string cmd = std::format("gcc -O0 -o {} {}", bin.string(), src.string());
	if (std::system(cmd.c_str()) != 0)
		return {};
	return bin.string();
}

} // namespace

TEST(Rea_Elf_Compiled) {
	const std::string path = CompileFixture();
	CHECK_FALSE(path.empty()); // gcc doit être disponible
	if (path.empty())
		return;

	auto elf = rea::Elf::Load(path);
	CHECK(elf.IsOk());
	if (elf.IsError())
		return;

	CHECK_EQ(elf.Value().Machine(), uint16_t(62)); // EM_X86_64
	CHECK_NE(elf.Value().EntryPoint(), uint64_t(0));

	const rea::ElfSection *text = elf.Value().FindSection(".text");
	CHECK(text != nullptr);
	if (text)
		CHECK(text->IsExecutable());

	const rea::ElfSymbol *add = elf.Value().FindSymbol("add");
	CHECK(add != nullptr);
	if (!add)
		return;
	CHECK(add->IsFunction());
	CHECK_GT(add->size, uint64_t(0));

	// Désassemblage du corps de `add` via la traduction d'adresse.
	uint64_t off = 0;
	CHECK(elf.Value().AddressToOffset(add->value, off));
	const auto &raw = elf.Value().Raw();
	auto insns = rea::Disassemble(raw.data() + off, add->size, add->value);
	CHECK_FALSE(insns.empty());
	// Vérifie que le corps contient bien une addition (a + b) et un retour,
	// sans dépendre du prologue exact (endbr64 / push selon le compilateur).
	bool hasAdd = false, hasRet = false;
	for (const rea::Instruction &i : insns) {
		hasAdd = hasAdd || i.mnemonic == "add";
		hasRet = hasRet || i.isReturn;
	}
	CHECK(hasAdd);
	CHECK(hasRet);
}

// ── Chaînes et graphe d'appels ───────────────────────────────────────────────

TEST(Rea_Analysis) {
	const std::string path = CompileFixture();
	CHECK_FALSE(path.empty());
	if (path.empty())
		return;

	auto elf = rea::Elf::Load(path);
	CHECK(elf.IsOk());
	if (elf.IsError())
		return;

	// Chaînes ASCII : les noms de fonctions sont dans .strtab.
	const auto strings = rea::ExtractStrings(elf.Value(), 3);
	CHECK_FALSE(strings.empty());
	bool hasAdd = false;
	for (const rea::StringRef &s : strings)
		hasAdd = hasAdd || s.text == "add";
	CHECK(hasAdd);

	// Graphe d'appels : `main` appelle `add` et `mul`.
	const auto funcs = rea::AnalyzeFunctions(elf.Value());
	CHECK_FALSE(funcs.empty());
	const rea::FunctionAnalysis *mainFn = nullptr;
	for (const rea::FunctionAnalysis &f : funcs)
		if (f.symbol->name == "main")
			mainFn = &f;
	CHECK(mainFn != nullptr);
	if (mainFn) {
		bool callsAdd = false, callsMul = false;
		for (const std::string &n : mainFn->callNames) {
			callsAdd = callsAdd || n == "add";
			callsMul = callsMul || n == "mul";
		}
		CHECK(callsAdd);
		CHECK(callsMul);
	}

	// Références croisées inverses : `add` est appelée par `main`.
	const rea::FunctionAnalysis *addFn = nullptr;
	for (const rea::FunctionAnalysis &f : funcs)
		if (f.symbol->name == "add")
			addFn = &f;
	CHECK(addFn != nullptr);
	if (addFn) {
		CHECK_EQ(addFn->xrefs, size_t(1));
		bool hasMainCaller = false;
		for (const std::string &n : addFn->callerNames)
			hasMainCaller = hasMainCaller || n == "main";
		CHECK(hasMainCaller);
	}
}

// ── Lifting C ────────────────────────────────────────────────────────────────

TEST(Rea_Lift) {
	rea::Function fn;
	fn.name = "add";
	fn.address = 0x1000;
	fn.instructions = {
		{0x1000, 5, {0xB8, 0x2A, 0, 0, 0}, "mov", "eax, 0x2A", "", false, false, false, false, false, 0},
		{0x1005, 3, {0x83, 0xC0, 0x05}, "add", "eax, 0x5", "", false, false, false, false, false, 0},
		{0x1008, 1, {0xC3}, "ret", "", "ret", false, false, false, true, false, 0},
	};

	const auto body = rea::LiftFunctionBody(fn);
	CHECK(body.find("eax = 0x2A") != std::string::npos);
	CHECK(body.find("eax += 0x5") != std::string::npos);
	CHECK(body.find("return;") != std::string::npos);
	CHECK(body.find("uint64_t eax") != std::string::npos); // registre déclaré

	const auto full = rea::LiftFunction(fn);
	CHECK(full.starts_with("int add(void)")); // eax écrit → retour int, aucun paramètre
	CHECK(full.find("return eax;") != std::string::npos);
}

// ── Signature (convention System V AMD64) ────────────────────────────────────

TEST(Rea_Signature) {
	// int add(int a, int b) { return a + b; } compilé en -O0 :
	// push rbp ; mov rbp,rsp ; mov [rbp-4],edi ; mov [rbp-8],esi ;
	// mov eax,[rbp-4] ; mov edx,[rbp-8] ; add eax,edx ; pop rbp ; ret
	rea::Function fn;
	fn.name = "add";
	fn.address = 0x1000;
	fn.instructions = {
		{0x1000, 1, {0x55}, "push", "rbp", "", false, false, false, false, false, 0},
		{0x1001, 3, {0x48, 0x89, 0xE5}, "mov", "rbp, rsp", "", false, false, false, false, false, 0},
		{0x1004, 3, {0x89, 0x7D, 0xFC}, "mov", "[rbp-0x04], edi", "", false, false, false, false, false, 0},
		{0x1007, 3, {0x89, 0x75, 0xF8}, "mov", "[rbp-0x08], esi", "", false, false, false, false, false, 0},
		{0x100A, 3, {0x8B, 0x45, 0xFC}, "mov", "eax, [rbp-0x04]", "", false, false, false, false, false, 0},
		{0x100D, 3, {0x8B, 0x55, 0xF8}, "mov", "edx, [rbp-0x08]", "", false, false, false, false, false, 0},
		{0x1010, 2, {0x01, 0xD0}, "add", "eax, edx", "", false, false, false, false, false, 0},
		{0x1012, 1, {0x5D}, "pop", "rbp", "", false, false, false, false, false, 0},
		{0x1013, 1, {0xC3}, "ret", "", "ret", false, false, false, true, false, 0},
	};

	const rea::Signature sig = rea::InferSignature(fn);
	CHECK_EQ(sig.returnType, std::string("int"));
	CHECK_EQ(sig.returnRegister, std::string("eax"));
	CHECK_EQ(sig.paramNames.size(), size_t(2));
	if (sig.paramNames.size() == 2) {
		CHECK_EQ(sig.paramNames[0], std::string("a"));
		CHECK_EQ(sig.paramNames[1], std::string("b"));
		CHECK_EQ(sig.paramTypes[0], std::string("int"));
		CHECK_EQ(sig.paramTypes[1], std::string("int"));
		CHECK_EQ(sig.Params(), std::string("int a, int b"));
	}

	const auto full = rea::LiftFunction(fn);
	CHECK(full.starts_with("int add(int a, int b)"));
}

TEST(Rea_Signature_Pointer) {
	// int first(int *arr) { return arr[0]; } — rdi utilisé comme base mémoire.
	rea::Function fn;
	fn.name = "first";
	fn.address = 0x1000;
	fn.instructions = {
		{0x1000, 2, {0x8B, 0x07}, "mov", "eax, [rdi]", "", false, false, false, false, false, 0},
		{0x1002, 1, {0xC3}, "ret", "", "ret", false, false, false, true, false, 0},
	};

	const rea::Signature sig = rea::InferSignature(fn);
	CHECK_EQ(sig.returnType, std::string("int"));
	CHECK_EQ(sig.paramNames.size(), size_t(1));
	if (sig.paramNames.size() == 1)
		CHECK_EQ(sig.paramTypes[0], std::string("void*")); // pointeur détecté

	const auto full = rea::LiftFunction(fn);
	CHECK(full.starts_with("int first(void* a)"));
}

// ── Lifting : contrôle de flux (branches, appels) ────────────────────────────

TEST(Rea_Lift_ControlFlow) {
	rea::Function fn;
	fn.name = "loop";
	fn.address = 0x2000;
	fn.instructions = {
		{0x2000, 5, {0xB8, 0, 0, 0, 0}, "mov", "eax, 0", "", false, false, false, false, false, 0},
		{0x2005, 3, {0x83, 0xC0, 0x01}, "add", "eax, 0x1", "", false, false, false, false, false, 0},
		{0x2008, 2, {0x75, 0xFB}, "jne", "0x2005", "jcc", true, true, false, false, true, 0x2005},
		{0x200A, 1, {0xC3}, "ret", "", "ret", false, false, false, true, false, 0},
	};

	const auto body = rea::LiftFunctionBody(fn);
	CHECK(body.find("eax = 0") != std::string::npos);
	CHECK(body.find("eax += 0x1") != std::string::npos);
	CHECK(body.find("L_2005:") != std::string::npos); // étiquette au point de retour de boucle
	CHECK(body.find("return;") != std::string::npos);
}

TEST(Rea_Lift_Call) {
	rea::Function fn;
	fn.name = "caller";
	fn.address = 0x3000;
	fn.instructions = {
		{0x3000, 5, {0xE8, 0, 0, 0, 0}, "call", "0x3010", "call", true, false, true, false, true, 0x3010},
		{0x3005, 1, {0xC3}, "ret", "", "ret", false, false, false, true, false, 0},
	};

	const auto body = rea::LiftFunctionBody(fn);
	CHECK(body.find("sub_3010();") != std::string::npos);
	CHECK(body.find("return;") != std::string::npos);
}

// ── Blocs de base (graphe de flot de contrôle) ───────────────────────────────

TEST(Rea_Blocks) {
	// Boucle : mov eax,0 ; add eax,1 ; jne 0x2005 ; ret
	std::vector<rea::Instruction> insns = {
		{0x2000, 5, {0xB8, 0, 0, 0, 0}, "mov", "eax, 0", "", false, false, false, false, false, 0},
		{0x2005, 3, {0x83, 0xC0, 0x01}, "add", "eax, 0x1", "", false, false, false, false, false, 0},
		{0x2008, 2, {0x75, 0x05}, "jne", "0x2005", "jcc", true, true, false, false, true, 0x2005},
		{0x200A, 1, {0xC3}, "ret", "", "ret", false, false, false, true, false, 0},
	};
	// (BuildBlocks est interne ; on passe par AnalyzeFunctions d'un Elf compilé,
	//  donc on teste plutôt sur le binaire de la fixture.)
	(void)insns;
	const std::string path = CompileFixture();
	CHECK_FALSE(path.empty());
	if (path.empty())
		return;
	auto elf = rea::Elf::Load(path);
	CHECK(elf.IsOk());
	if (elf.IsError())
		return;
	auto funcs = rea::AnalyzeFunctions(elf.Value());
	const rea::FunctionAnalysis *sum = nullptr;
	for (const rea::FunctionAnalysis &f : funcs)
		if (f.symbol->name == "sum_array")
			sum = &f;
	CHECK(sum != nullptr);
	if (!sum)
		return;
	// sum_array contient une boucle : au moins 2 blocs et des successeurs.
	CHECK_GE(sum->blocks.size(), size_t(2));
	size_t edges = 0;
	for (const rea::BasicBlock &b : sum->blocks)
		edges += b.successors.size();
	CHECK_GE(edges, size_t(2));
}

// ── Diagnostic de sécurité, notes et importations ────────────────────────────

TEST(Rea_Security) {
	const std::string path = CompileFixture();
	CHECK_FALSE(path.empty());
	if (path.empty())
		return;
	auto elf = rea::Elf::Load(path);
	CHECK(elf.IsOk());
	if (elf.IsError())
		return;

	const rea::SecurityReport sec = elf.Value().Security();
	CHECK(sec.nx);		 // pile non exécutable par défaut
	CHECK_FALSE(sec.buildId.empty()); // gcc émet un build-id par défaut

	// Importations : le programme appelle printf (libc).
	const auto imports = elf.Value().Imports();
	CHECK_FALSE(imports.empty());
	bool hasPrintf = false;
	for (const rea::ElfSymbol *s : imports)
		hasPrintf = hasPrintf || s->name == "printf";
	CHECK(hasPrintf);

	// Notes : au moins la note GNU (build-id / ABI-tag).
	const auto notes = elf.Value().Notes();
	CHECK_FALSE(notes.empty());
}

// ── Références de données (chaînes / globales) ───────────────────────────────

TEST(Rea_DataRefs) {
	const std::string path = CompileFixture();
	CHECK_FALSE(path.empty());
	if (path.empty())
		return;
	auto elf = rea::Elf::Load(path);
	CHECK(elf.IsOk());
	if (elf.IsError())
		return;

	auto funcs = rea::AnalyzeFunctions(elf.Value());
	const rea::FunctionAnalysis *mainFn = nullptr;
	for (const rea::FunctionAnalysis &f : funcs)
		if (f.symbol->name == "main")
			mainFn = &f;
	CHECK(mainFn != nullptr);
	if (!mainFn)
		return;

	// main appelle printf("somme=%d\n", …) : il référence la chaîne via lea [rip+…].
	bool foundSomme = false;
	for (const rea::DataRef &d : mainFn->dataRefs)
		foundSomme = foundSomme || d.what.find("somme") != std::string::npos;
	CHECK(foundSomme);
}

// ── Identité du binaire (langage / chaîne d'outils / liaison) ────────────────

TEST(Rea_Identity) {
	// C compilé avec gcc.
	{
		const std::string path = CompileFixture();
		CHECK_FALSE(path.empty());
		if (path.empty())
			return;
		auto elf = rea::Elf::Load(path);
		CHECK(elf.IsOk());
		if (elf.IsError())
			return;
		const rea::BinaryIdentity id = rea::DetectIdentity(elf.Value());
		CHECK_EQ(id.language, std::string("C"));
		CHECK(id.toolchain.starts_with("GCC"));
		CHECK_FALSE(id.stripped);
		CHECK_FALSE(id.staticallyLinked);
	}

	// Rust compilé avec rustc.
	{
		const auto dir = std::filesystem::temp_directory_path() / "rea-test";
		std::filesystem::create_directories(dir);
		const auto src = dir / "fixture.rs";
		const auto bin = dir / "fixture-rs";
		{
			std::ofstream f(src);
			f << "fn main() { println!(\"bonjour\"); }\n";
		}
		if (std::system(std::format("rustc -o {} {}", bin.string(), src.string()).c_str()) != 0)
			return; // rustc indisponible
		auto elf = rea::Elf::Load(bin.string());
		CHECK(elf.IsOk());
		if (elf.IsError())
			return;
		const rea::BinaryIdentity id = rea::DetectIdentity(elf.Value());
		CHECK_EQ(id.language, std::string("Rust"));
		CHECK(id.toolchain.starts_with("rustc"));
	}

	// Statique puis « stripé ».
	{
		const auto dir = std::filesystem::temp_directory_path() / "rea-test";
		const auto src = dir / "static.c";
		const auto bin = dir / "static-bin";
		{
			std::ofstream f(src);
			f << "int main(void) { return 0; }\n";
		}
		if (std::system(std::format("gcc -static -O0 -o {} {}", bin.string(), src.string()).c_str()) != 0)
			return;
		if (std::system(std::format("strip {}", bin.string()).c_str()) != 0)
			return;
		auto elf = rea::Elf::Load(bin.string());
		CHECK(elf.IsOk());
		if (elf.IsError())
			return;
		const rea::BinaryIdentity id = rea::DetectIdentity(elf.Value());
		CHECK(id.staticallyLinked);
		CHECK(id.stripped);
	}
}

// ── Export du C reconstruit ──────────────────────────────────────────────────

TEST(Rea_ExportC) {
	const std::string path = CompileFixture();
	CHECK_FALSE(path.empty());
	if (path.empty())
		return;
	auto elf = rea::Elf::Load(path);
	CHECK(elf.IsOk());
	if (elf.IsError())
		return;

	const auto funcs = rea::AnalyzeFunctions(elf.Value());
	const auto dir = std::filesystem::temp_directory_path() / "rea-test" / "export";
	std::filesystem::remove_all(dir);
	const auto result = rea::ExportC(funcs, dir);
	CHECK(result.IsOk());
	if (result.IsError())
		return;
	CHECK_GT(result.Value(), size_t(0));

	// Le fichier main.c existe et contient la fonction main exportée.
	std::ifstream in(dir / "main.c");
	CHECK(bool(in));
	std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	CHECK(content.find("#include <stdint.h>") != std::string::npos);
	CHECK(content.find("main(") != std::string::npos);

	// add.c contient la signature inférée de add.
	std::ifstream addIn(dir / "add.c");
	CHECK(bool(addIn));
	std::string addContent((std::istreambuf_iterator<char>(addIn)), std::istreambuf_iterator<char>());
	CHECK(addContent.find("int add(int a, int b)") != std::string::npos);
}

// ── Projet (création / sauvegarde / réouverture) ─────────────────────────────

TEST(Rea_Project) {
	rea::Project p;
	p.binaryPath = "/tmp/opencode/rea_demo";
	p.arch = rea::Arch::Mips32;
	p.Comment(0x1000, "boucle principale");
	p.Rename(0x1000, "main_loop");
	p.Comment(0x2000, "entrée");

	const auto path = std::filesystem::temp_directory_path() / "rea-test" / "projet.json";
	CHECK(p.Save(path));

	const auto loaded = rea::Project::Load(path);
	CHECK(loaded.IsOk());
	if (loaded.IsError())
		return;
	CHECK_EQ(loaded.Value().binaryPath, p.binaryPath);
	CHECK(loaded.Value().arch == rea::Arch::Mips32);
	CHECK_EQ(loaded.Value().annotations.size(), size_t(2));

	bool found = false;
	for (const rea::Annotation &a : loaded.Value().annotations)
		if (a.address == 0x1000) {
			CHECK_EQ(a.comment, std::string("boucle principale"));
			CHECK_EQ(a.name, std::string("main_loop"));
			found = true;
		}
	CHECK(found);

	// Le format est un JSON lisible.
	std::ifstream in(path);
	CHECK(bool(in));
	std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	CHECK(content.find("rea-project") != std::string::npos);
	CHECK(content.find("mips32") != std::string::npos);
}

// ── Recherche de motifs (signatures d'algorithmes) ───────────────────────────

TEST(Rea_Signatures) {
	const auto sigs = rea::BuiltinSignatures();
	CHECK_EQ(sigs.size(), size_t(3));
	CHECK_EQ(sigs[0].bytes.size(), size_t(256)); // boîte S AES
	CHECK_EQ(sigs[1].bytes.size(), size_t(32));  // IV SHA-256
	CHECK_EQ(sigs[2].bytes.size(), size_t(16));  // IV MD5

	// Vecteur MD5 (01 23 45 67 …) planté à l'offset 1.
	const uint8_t buf[] = {0xAA, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10, 0xBB};
	const auto matches = rea::SearchSignatures(buf, sizeof(buf), sigs);
	CHECK_EQ(matches.size(), size_t(1));
	if (!matches.empty()) {
		CHECK_EQ(matches[0].name, std::string("MD5"));
		CHECK_EQ(matches[0].offset, uint64_t(1));
	}
}
