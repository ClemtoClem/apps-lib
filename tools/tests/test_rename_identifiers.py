"""Tests de tools/rename_identifiers.py : conversions de casse, puis renommage complet d'un
petit projet C++23 (compilé avant et après avec g++ pour vérifier le résultat).

    source ~/venv/bin/activate
    python -m unittest discover -s tools/tests -v
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import rename_identifiers as ri  # noqa: E402

try:
    import clang.cindex  # noqa: F401

    HAVE_CLANG = ri.find_libclang() is not None
except ImportError:
    HAVE_CLANG = False


class CaseConversionTest(unittest.TestCase):
    def test_split(self):
        self.assertEqual(ri.split_identifier("rgb5ToRgb8"), ["rgb", "5", "To", "Rgb", "8"])
        self.assertEqual(ri.split_identifier("IOStream"), ["IO", "Stream"])
        self.assertEqual(ri.split_identifier("read_rom_header"), ["read", "rom", "header"])
        self.assertEqual(ri.split_identifier("ROM_GBA"), ["ROM", "GBA"])

    def test_pascal(self):
        self.assertEqual(ri.to_pascal_case("runFrame"), "RunFrame")
        self.assertEqual(ri.to_pascal_case("getPC"), "GetPC")  # acronyme conservé
        self.assertEqual(ri.to_pascal_case("IOStream"), "IOStream")  # déjà conforme
        self.assertEqual(ri.to_pascal_case("read_rom"), "ReadRom")
        self.assertEqual(ri.to_pascal_case("_Polygon"), "Polygon")
        self.assertEqual(ri.to_pascal_case("MAX_SIZE"), "MaxSize")

    def test_camel(self):
        self.assertEqual(ri.to_camel_case("FrameCount"), "frameCount")
        self.assertEqual(ri.to_camel_case("frame_count"), "frameCount")
        self.assertEqual(ri.to_camel_case("URLPath"), "urlPath")
        self.assertEqual(ri.to_camel_case("kFrameTicks"), "kFrameTicks")  # déjà conforme
        self.assertEqual(ri.to_camel_case("ID"), "id")
        self.assertEqual(ri.to_camel_case("ROM_GBA"), "romGba")

    def test_member(self):
        self.assertEqual(ri.to_m_camel_case("frameTicks"), "m_frameTicks")
        self.assertEqual(ri.to_m_camel_case("m_frameTicks"), "m_frameTicks")
        self.assertEqual(ri.to_m_camel_case("frame_ticks_"), "m_frameTicks")
        self.assertEqual(ri.to_m_camel_case("m_Count"), "m_count")

    def test_compile_arguments(self):
        entry = {"directory": "/p", "file": "/p/a.cpp", "arguments": ["ccache", "g++", "-std=c++23", "-I/p/inc", "-MMD", "-MP", "-MF", "a.d", "-c", "/p/a.cpp", "-o", "a.o"]}
        args = ri.compile_arguments(entry, "/res")
        self.assertEqual(args[:2], ["-std=c++23", "-I/p/inc"])
        for dropped in ("-c", "-o", "a.o", "-MF", "a.d", "/p/a.cpp", "g++", "ccache"):
            self.assertNotIn(dropped, args)
        self.assertEqual(args[-2:], ["-resource-dir", "/res"])


HEADER = """\
#pragma once
#include <cstdint>
#include <ctime>
#include <functional>
struct tm;                        // type C redéclaré : jamais renommé
struct opaque_handle;             // type sans définition (bibliothèque externe) : jamais renommé
extern "C" int c_style_api(int);  // liaison C : jamais renommée
#include <memory>
#include <vector>

namespace Demo_Space {

enum class color_kind { Red, Green };
using byte_list = std::vector<uint8_t>;

struct shape_info {
    int side_count = 0;
};

class Base_Device {
public:
    virtual ~Base_Device() = default;
    virtual int run_cycle(int cycle_count) = 0;
};

class cpu_core : public Base_Device {
public:
    explicit cpu_core(int start_value);
    ~cpu_core() override;
    int run_cycle(int cycle_count) override;
    int get_value() const { return counter_value; }
    void reset() { buffer.reset(); counter_value = 0; }   // std::unique_ptr::reset ne bouge pas
    static int make_id();
    static constexpr int MAX_UNITS = 4;
    bool operator==(const cpu_core &other) const { return counter_value == other.counter_value; }
    int public_field = 1;
    void set_field(int Public_Field) { public_field = Public_Field; }   // publicField masquerait le membre
    bool has_buffer() const { return buffer ? true : false; }           // conversion implicite (operator bool)

private:
    int counter_value;
    std::unique_ptr<int> buffer;
    static int instance_count;
};

int free_helper(int Input_Value);

#define TWICE(x) ((x) * 2)
inline int doubled_counter(int Some_Count) { return TWICE(Some_Count); }   // argument de macro : renommé

#define MAKE_GETTER(name) inline int get_##name() { return 7; }
MAKE_GETTER(seven)

template <typename T> T twice_value(T value) { return value + value; }

} // namespace Demo_Space

template <> struct std::hash<Demo_Space::shape_info> {
    size_t operator()(const Demo_Space::shape_info &s) const noexcept { return size_t(s.side_count); }
};

inline int macro_target(int v) { return v; }
#define CALL_TARGET(x) macro_target(x)
"""

SOURCE = """\
#include "demo.hpp"

namespace Demo_Space {

int cpu_core::instance_count = 0;

cpu_core::cpu_core(int start_value) : counter_value(start_value) { ++instance_count; }
cpu_core::~cpu_core() { --instance_count; }

int cpu_core::run_cycle(int cycle_count) {
    int Local_Total = counter_value + cycle_count + MAX_UNITS;
    for (int Item : {1, 2}) Local_Total += Item;
    counter_value = Local_Total;
    return twice_value(Local_Total);
}

int cpu_core::make_id() { return instance_count; }

int free_helper(int Input_Value) {
    cpu_core core_object(Input_Value);
    Base_Device &device_ref = core_object;
    shape_info info{3};
    byte_list bytes{1, 2};
    color_kind kind = color_kind::Green;
    (void)kind;
    core_object.reset();
    return device_ref.run_cycle(info.side_count) + core_object.get_value() + cpu_core::make_id() + int(bytes.size());
}

} // namespace Demo_Space

extern "C" int c_style_api(int v) { return v; }

int main() {
    tm *none = nullptr;
    opaque_handle *handle = nullptr;
    (void)none;
    (void)handle;
    return CALL_TARGET(0) + Demo_Space::free_helper(2) + Demo_Space::get_seven() + c_style_api(0) + int(std::hash<Demo_Space::shape_info>{}({1})) > 0 ? 0 : 1;
}
"""


@unittest.skipUnless(HAVE_CLANG and shutil.which("g++"), "liaisons clang.cindex, libclang ou g++ absents")
class EndToEndTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="rename_identifiers_")
        with open(os.path.join(self.dir, "demo.hpp"), "w") as f:
            f.write(HEADER)
        with open(os.path.join(self.dir, "main.cpp"), "w") as f:
            f.write(SOURCE)
        entry = {"directory": self.dir, "file": os.path.join(self.dir, "main.cpp"),
                 "arguments": ["g++", "-std=c++23", "-I" + self.dir, "-c", "main.cpp", "-o", "main.o"]}
        with open(os.path.join(self.dir, "compile_commands.json"), "w") as f:
            json.dump([entry], f)
        self.compile()  # le projet de départ compile

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def compile(self):
        result = subprocess.run(["g++", "-std=c++23", "-I" + self.dir, "main.cpp", "-o", "demo"], cwd=self.dir, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(subprocess.run([os.path.join(self.dir, "demo")]).returncode, 0)

    def read(self, name):
        with open(os.path.join(self.dir, name)) as f:
            return f.read()

    def run_tool(self, *extra):
        cwd = os.getcwd()
        try:
            code = ri.main(["--root", self.dir, "--jobs", "1", *extra])
        finally:
            os.chdir(cwd)
        self.assertEqual(code, 0)

    def test_dry_run_changes_nothing(self):
        diff = os.path.join(self.dir, "plan.diff")
        self.run_tool("--diff", diff)
        self.assertEqual(self.read("demo.hpp"), HEADER)
        self.assertEqual(self.read("main.cpp"), SOURCE)
        self.assertIn("+class CpuCore", self.read("plan.diff"))

    def test_apply_renames_and_still_compiles(self):
        backup = os.path.join(self.dir, "backup")
        self.run_tool("--apply", "--backup", backup)
        header, source = self.read("demo.hpp"), self.read("main.cpp")
        # PascalCase : types, fonctions, méthodes, modèles ; constructeur et destructeur suivent.
        for expected in ("enum class ColorKind", "using ByteList", "struct ShapeInfo", "class BaseDevice", "class CpuCore : public BaseDevice",
                         "explicit CpuCore(int startValue)", "~CpuCore() override", "int RunCycle(int cycleCount) override",
                         "int GetValue() const", "static int MakeId()", "int FreeHelper(int inputValue)", "T TwiceValue(T value)"):
            self.assertIn(expected, header)
        # camelCase : espace de noms, membres publics ; m_camelCase : membres privés.
        self.assertIn("namespace demoSpace", header)
        self.assertIn("int sideCount = 0;", header)
        self.assertIn("int publicField = 1;", header)
        self.assertIn("int m_counterValue;", header)
        self.assertIn("std::unique_ptr<int> m_buffer;", header)
        self.assertIn("static int m_instanceCount;", header)
        # Inchangés : méthode homonyme d'une méthode std, opérateur, main, énumérateurs.
        self.assertIn("m_buffer.reset();", header)
        self.assertIn("void SetField(int Public_Field) { publicField = Public_Field; }", header)
        self.assertIn("return m_buffer ? true : false;", header)
        self.assertIn("void Reset()", header)
        self.assertIn("operator==", header)
        self.assertIn("int main()", source)
        self.assertIn("struct tm;", header)
        self.assertIn("struct opaque_handle;", header)
        self.assertIn("opaque_handle *handle = nullptr;", source)
        self.assertIn('extern "C" int c_style_api(int);', header)
        self.assertIn("MAKE_GETTER(seven)", header)
        # Appel écrit dans le corps d'une macro : renommé là aussi (position d'écriture).
        self.assertIn("inline int MacroTarget(int v)", header)
        self.assertIn("#define CALL_TARGET(x) MacroTarget(x)", header)
        self.assertIn("inline int DoubledCounter(int someCount) { return TWICE(someCount); }", header)
        self.assertIn("Demo_Space::get_seven()".replace("Demo_Space", "demoSpace"), source)
        self.assertIn("struct std::hash<demoSpace::ShapeInfo>", header)
        self.assertIn("return size_t(s.sideCount);", header)
        self.assertIn("ColorKind::Green", source)
        # Définitions hors classe, variables locales, appels qualifiés.
        for expected in ("int CpuCore::m_instanceCount = 0;", "CpuCore::CpuCore(int startValue) : m_counterValue(startValue)",
                         "CpuCore::~CpuCore()", "int localTotal = m_counterValue + cycleCount + maxUnits;", "CpuCore coreObject(inputValue);",
                         "coreObject.Reset();", "deviceRef.RunCycle(info.sideCount)", "CpuCore::MakeId()", "demoSpace::FreeHelper(2)"):
            self.assertIn(expected, source)
        self.assertTrue(os.path.exists(os.path.join(backup, "demo.hpp")))
        self.compile()  # le projet renommé compile et s'exécute

    def test_constants(self):
        self.run_tool("--apply")
        self.assertIn("static constexpr int maxUnits = 4;", self.read("demo.hpp"))  # membre public : camelCase
        self.assertIn("cycleCount + maxUnits;", self.read("main.cpp"))
        self.assertNotIn("__begin", self.read("main.cpp"))
        self.assertIn("for (int item : {1, 2}) localTotal += item;", self.read("main.cpp"))
        self.compile()

    def test_keep_constants(self):
        self.run_tool("--apply", "--keep-constants")
        self.assertIn("static constexpr int MAX_UNITS = 4;", self.read("demo.hpp"))
        self.assertIn("int m_counterValue;", self.read("demo.hpp"))
        self.assertIn("cycleCount + MAX_UNITS;", self.read("main.cpp"))
        self.compile()

    def test_only_functions_category(self):
        self.run_tool("--apply", "--only", "functions")
        header = self.read("demo.hpp")
        self.assertIn("int GetValue() const", header)
        self.assertIn("class cpu_core", header)  # types non retenus
        self.assertIn("int counter_value;", header)
        self.compile()

    def test_conflict_is_reported_and_skipped(self):
        with open(os.path.join(self.dir, "demo.hpp"), "a") as f:
            f.write("namespace Demo_Space { inline int Value_Of() { return 1; } inline int value_of() { return 2; } }\n")
        self.run_tool("--apply", "--only", "functions")
        header = self.read("demo.hpp")
        self.assertIn("int Value_Of()", header)  # les deux deviendraient ValueOf : laissés
        self.assertIn("int value_of()", header)
        self.compile()

    def test_override_of_external_method_is_kept(self):
        # Seul le fichier dérivé est dans le périmètre : sa redéfinition garde le nom de la base.
        os.makedirs(os.path.join(self.dir, "derived"))
        with open(os.path.join(self.dir, "derived", "derived.hpp"), "w") as f:
            f.write('#pragma once\n#include "demo.hpp"\nstruct derived_device : Demo_Space::Base_Device {\n'
                    "    int run_cycle(int cycle_count) override { return cycle_count; }\n    int extra_call() { return 1; }\n};\n")
        with open(os.path.join(self.dir, "main.cpp"), "a") as f:
            f.write('#include "derived/derived.hpp"\nint use_derived() { derived_device d; return d.run_cycle(1) + d.extra_call(); }\n')
        self.run_tool("--apply", "--scope", "derived")
        derived = self.read("derived/derived.hpp")
        self.assertIn("int run_cycle(int cycleCount) override", derived)
        self.assertIn("struct DerivedDevice", derived)
        self.assertIn("int ExtraCall()", derived)
        self.assertIn("DerivedDevice d; return d.run_cycle(1) + d.ExtraCall();", self.read("main.cpp"))
        self.assertEqual(self.read("demo.hpp"), HEADER)  # hors périmètre : intact
        self.compile()


if __name__ == "__main__":
    unittest.main()
