#!/usr/bin/env python3
"""
Renomme les identifiants d'un projet C++23 selon les conventions du dépôt :

- PascalCase  : enum, typedef / using, class, struct, union, fonction, méthode (et modèles)
- camelCase   : variable, paramètre, espace de noms, membre non privé
- m_camelCase : membre privé de classe (préfixe m_), données statiques comprises

Analyse sémantique par libclang (liaisons Python `clang.cindex`) : chaque référence est
rattachée à sa déclaration (USR), si bien que deux entités homonymes ne sont jamais
confondues. Les options de compilation viennent de `compile_commands.json`
(`cpp-maker export compdb`).

Par défaut, l'outil ne modifie rien : il affiche le plan (et l'écrit en diff avec --diff).
`--apply` écrit les fichiers, après copie des originaux dans --backup.

Non renommés : `operator…`, `main`, `begin`/`end` et variantes (exigés par le langage),
méthodes qui redéfinissent une méthode déclarée hors du périmètre, entités de l'espace
`std`, déclarations `extern "C"`, noms déjà conformes (les acronymes sont conservés :
`IOStream` reste tel quel), les constantes avec --keep-constants, et tout renommage qui créerait un conflit dans une même portée
ou un mot-clé. Les identifiants écrits dans les arguments ou le corps d'une macro sont
renommés à l'endroit où ils sont écrits ; une entité dont une occurrence ne peut pas être
située (collage de jetons `##`) ou dont le corps de macro désigne selon l'appel des entités
renommées différemment n'est pas renommée. Les appels non résolus dans le code générique
(membres de types dépendants d'un modèle) sont signalés : à vérifier en recompilant.

Exemple :
    cpp-maker export compdb
    source ~/venv/bin/activate
    python tools/rename_identifiers.py --scope libs/engine/consoles --diff /tmp/plan.diff
    python tools/rename_identifiers.py --scope libs/engine/consoles --apply --backup /tmp/sauvegarde
"""

from __future__ import annotations

import argparse
import ctypes
import difflib
import glob
import json
import keyword
import os
import re
import shutil
import sys
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass, field
from fnmatch import fnmatch

# ---------------------------------------------------------------------------
# Conversion de casse (sans dépendance à libclang : testable seule)
# ---------------------------------------------------------------------------

_IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_PASCAL = re.compile(r"[A-Z][A-Za-z0-9]*")
_CAMEL = re.compile(r"[a-z][A-Za-z0-9]*")
_WORD = re.compile(r"[A-Z]+(?=[A-Z][a-z])|[A-Z]?[a-z]+|[A-Z]+|\d+")


def split_identifier(name: str) -> list[str]:
    """Mots d'un identifiant : snake_case, SCREAMING_CASE, camelCase, PascalCase, chiffres."""
    words: list[str] = []
    for part in re.split(r"_+", name):
        words.extend(_WORD.findall(part))
    return words


def _capitalize(word: str) -> str:
    # Un mot tout en capitales (acronyme d'un nom snake_case) devient « Rom » ; sinon on
    # ne touche qu'à la première lettre (« rgb5 » → « Rgb5 »).
    if word.isupper() and len(word) > 1:
        return word[0] + word[1:].lower()
    return word[:1].upper() + word[1:]


def to_pascal_case(name: str) -> str:
    core = name.strip("_")
    if not core:
        return name
    if _PASCAL.fullmatch(core):
        return core  # déjà conforme : acronymes conservés
    if _CAMEL.fullmatch(core):
        return core[0].upper() + core[1:]
    words = split_identifier(core)
    return "".join(_capitalize(w) for w in words) if words else name


def to_camel_case(name: str) -> str:
    core = name.strip("_")
    if not core:
        return name
    if _CAMEL.fullmatch(core):
        return core
    words = split_identifier(core)
    if not words:
        return name
    return words[0].lower() + "".join(_capitalize(w) for w in words[1:])


def to_m_camel_case(name: str) -> str:
    base = name[2:] if name.startswith("m_") else name
    camel = to_camel_case(base)
    return "m_" + camel if camel.strip("_") else name


CPP_KEYWORDS = frozenset(
    """alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t
    char16_t char32_t class compl concept const consteval constexpr constinit const_cast
    continue co_await co_return co_yield decltype default delete do double dynamic_cast else
    enum explicit export extern false float for friend goto if inline int long mutable
    namespace new noexcept not not_eq nullptr operator or or_eq private protected public
    register reinterpret_cast requires return short signed sizeof static static_assert
    static_cast struct switch template this thread_local throw true try typedef typeid
    typename union unsigned using virtual void volatile wchar_t while xor xor_eq final
    override import module""".split()
)

DEFAULT_KEEP = frozenset({"main", "begin", "end", "cbegin", "cend", "rbegin", "rend", "crbegin", "crend"})
# Types membres imposés par la bibliothèque standard (itérateurs, conteneurs, comparateurs).
STD_MEMBER_TYPES = frozenset(
    """value_type size_type difference_type reference const_reference pointer const_pointer iterator
    const_iterator reverse_iterator const_reverse_iterator iterator_category iterator_concept
    element_type key_type mapped_type allocator_type hasher key_equal is_transparent char_type
    traits_type""".split()
)
CATEGORIES = ("types", "functions", "namespaces", "variables", "parameters", "fields")

# ---------------------------------------------------------------------------
# Analyse d'une unité de traduction (exécutée dans un processus de travail)
# ---------------------------------------------------------------------------


@dataclass
class Config:
    root: str
    scopes: list[str]
    excludes: list[str]
    keep: frozenset
    categories: frozenset
    keep_constants: bool
    libclang: str | None
    resource_dir: str | None


@dataclass
class TuResult:
    file: str
    decls: dict = field(default_factory=dict)  # usr -> (old, new, category, scopeUsr, file, line, reason)
    occurrences: list = field(default_factory=list)  # (file, offset, targetUsr, oldText)
    overrides: list = field(default_factory=list)  # (usr, baseUsr)
    unresolved: list = field(default_factory=list)  # (file, line, column, spelling)
    external: set = field(default_factory=set)  # entités déclarées d'abord hors du périmètre
    unsafe: set = field(default_factory=set)  # entités dont une occurrence n'est pas situable
    classlike: set = field(default_factory=set)  # classes, structures, unions vues
    defined: set = field(default_factory=set)  # … dont la définition est dans le périmètre
    errors: list = field(default_factory=list)


_ci = None  # module clang.cindex, chargé dans chaque processus
_config: Config | None = None


def _init_worker(config: Config) -> None:
    global _ci, _config
    import clang.cindex as ci

    if config.libclang:
        ci.Config.set_library_file(config.libclang)
    lib = ci.conf.lib
    # Fonction absente des liaisons Python : déclarée ici.
    lib.clang_getOverriddenCursors.argtypes = [ci.Cursor, ctypes.POINTER(ctypes.POINTER(ci.Cursor)), ctypes.POINTER(ctypes.c_uint)]
    lib.clang_getOverriddenCursors.restype = None
    lib.clang_disposeOverriddenCursors.argtypes = [ctypes.POINTER(ci.Cursor)]
    lib.clang_disposeOverriddenCursors.restype = None
    lib.clang_getSpellingLocation.argtypes = [ci.SourceLocation, ctypes.POINTER(ci.c_object_p), ctypes.POINTER(ctypes.c_uint),
                                              ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint)]
    lib.clang_getSpellingLocation.restype = None
    _ci = ci
    _config = config


def _kind(cursor):
    """Type du nœud, ou None s'il est inconnu des liaisons (libclang plus récente)."""
    try:
        return cursor.kind
    except ValueError:
        return None


class _Analyzer:
    def __init__(self, config: Config):
        ci = _ci
        K = ci.CursorKind
        self.ci = ci
        self.config = config
        self.path_cache: dict[str, tuple[bool, bool]] = {}
        self.class_kinds = {K.CLASS_DECL, K.STRUCT_DECL, K.UNION_DECL, K.CLASS_TEMPLATE, K.CLASS_TEMPLATE_PARTIAL_SPECIALIZATION}
        self.type_kinds = self.class_kinds | {K.ENUM_DECL, K.TYPEDEF_DECL, K.TYPE_ALIAS_DECL, K.TYPE_ALIAS_TEMPLATE_DECL}
        self.function_kinds = {K.FUNCTION_DECL, K.CXX_METHOD, K.FUNCTION_TEMPLATE}
        self.ref_kinds = {K.MEMBER_REF_EXPR, K.DECL_REF_EXPR, K.OVERLOADED_DECL_REF}
        # Nœuds qui nomment une entité à leur emplacement (les enveloppes d'expression,
        # UNEXPOSED_EXPR ou CALL_EXPR, partagent la position d'un autre identifiant).
        self.naming_kinds = self.ref_kinds | {K.TYPE_REF, K.TEMPLATE_REF, K.NAMESPACE_REF, K.MEMBER_REF, K.VARIABLE_REF}
        self.function_scopes = {K.CXX_METHOD, K.CONSTRUCTOR, K.DESTRUCTOR, K.FUNCTION_TEMPLATE, K.CONVERSION_FUNCTION}
        self.K = K
        self.classified: dict[str, tuple | None] = {}
        self.texts: dict[str, bytes] = {}

    def text_at(self, path: str, offset: int, length: int) -> bytes:
        if path not in self.texts:
            try:
                with open(path, "rb") as f:
                    self.texts[path] = f.read()
            except OSError:
                self.texts[path] = b""
        return self.texts[path][offset:offset + length]

    # Fichiers : (dans la racine, dans le périmètre des déclarations à renommer).
    def where(self, path: str | None) -> tuple[bool, bool]:
        if not path:
            return (False, False)
        cached = self.path_cache.get(path)
        if cached is not None:
            return cached
        real = os.path.realpath(path)
        rel = os.path.relpath(real, self.config.root)
        in_root = not rel.startswith("..") and not any(fnmatch(rel, pattern) for pattern in self.config.excludes)
        in_scope = in_root and any(real == s or real.startswith(s + os.sep) for s in self.config.scopes)
        self.path_cache[path] = (in_root, in_scope)
        return self.path_cache[path]

    def spelling(self, location) -> tuple[str | None, int]:
        """Position où le texte est écrit (dans l'argument d'une macro plutôt qu'à son appel)."""
        ci = self.ci
        f, line, column, offset = ci.c_object_p(), ctypes.c_uint(), ctypes.c_uint(), ctypes.c_uint()
        ci.conf.lib.clang_getSpellingLocation(location, ctypes.byref(f), ctypes.byref(line), ctypes.byref(column), ctypes.byref(offset))
        if not f:
            return None, 0
        return os.path.realpath(ci.File(f).name), offset.value

    def file_of(self, cursor) -> str | None:
        f = cursor.location.file
        return os.path.realpath(f.name) if f else None

    def in_std(self, cursor) -> bool:
        parent = cursor.semantic_parent
        while parent is not None and _kind(parent) not in (None, self.K.TRANSLATION_UNIT):
            if _kind(parent) == self.K.NAMESPACE and parent.spelling == "std":
                return True
            parent = parent.semantic_parent
        return False

    def extern_c(self, cursor) -> bool:
        parent = cursor.lexical_parent
        while parent is not None and _kind(parent) not in (None, self.K.TRANSLATION_UNIT):
            if _kind(parent) == self.K.LINKAGE_SPEC:
                return True
            parent = parent.lexical_parent
        return False

    def is_constant(self, cursor) -> bool:
        """Variable const / constexpr (tableaux compris : éléments constants)."""
        t = cursor.type
        while t.kind in (self.ci.TypeKind.CONSTANTARRAY, self.ci.TypeKind.INCOMPLETEARRAY):
            t = t.get_array_element_type()
        return t.is_const_qualified()

    def member_names(self, cls, depth: int = 0) -> set:
        """Noms (actuels et prévus) des membres d'une classe et de ses bases."""
        K = self.K
        names: set = set()
        for child in cls.get_children():
            kind = _kind(child)
            if kind in (K.FIELD_DECL, K.VAR_DECL):
                names.add(child.spelling)
                private = child.access_specifier == self.ci.AccessSpecifier.PRIVATE
                names.add(to_m_camel_case(child.spelling) if private else to_camel_case(child.spelling))
            elif kind in (K.CXX_METHOD, K.FUNCTION_TEMPLATE):
                names.update((child.spelling, to_pascal_case(child.spelling)))
            elif kind == K.CXX_BASE_SPECIFIER and depth < 8:
                base = child.referenced
                base = base.get_definition() if base is not None else None
                if base is not None:
                    names |= self.member_names(base, depth + 1)
        return names

    def shadows_member(self, decl, new: str) -> bool:
        """Vrai si le nouveau nom d'un paramètre ou d'une variable locale masquerait un membre."""
        function = decl.semantic_parent
        if function is None or _kind(function) not in self.function_scopes:
            return False
        cls = function.semantic_parent
        if cls is None or _kind(cls) not in self.class_kinds:
            return False
        definition = cls.get_definition() or cls
        return new in self.member_names(definition)

    def overridden(self, cursor) -> list:
        lib = self.ci.conf.lib
        out = ctypes.POINTER(self.ci.Cursor)()
        count = ctypes.c_uint(0)
        lib.clang_getOverriddenCursors(cursor, ctypes.byref(out), ctypes.byref(count))
        result = [out[i] for i in range(count.value)]
        if count.value:
            lib.clang_disposeOverriddenCursors(out)
        return result

    def classify(self, decl, result: TuResult):
        """(ancien nom, nouveau nom, catégorie, raison d'exclusion) d'une déclaration du
        périmètre, ou None si elle n'est pas concernée."""
        usr = decl.get_usr()
        if not usr:
            return None
        if usr in self.classified:
            return self.classified[usr]
        self.classified[usr] = None  # garde contre la récursion
        info = self._classify(decl, usr, result)
        self.classified[usr] = info
        return info

    def _classify(self, decl, usr: str, result: TuResult):
        K = self.K
        kind = _kind(decl)
        name = decl.spelling
        path = self.file_of(decl)
        if kind is None or not _IDENTIFIER.fullmatch(name) or name.startswith("__") or not self.where(path)[1]:
            return None  # déclarations implicites (__begin1, guides de déduction…) ou hors périmètre
        if self.text_at(path, decl.location.offset, len(name.encode())) != name.encode():
            return None  # nom produit par une macro : absent tel quel du texte
        if self.in_std(decl):
            return None  # spécialisations de std::hash, std::formatter…
        if kind in self.type_kinds or kind in self.function_kinds:
            canonical = decl.canonical
            if canonical is not None and not self.where(self.file_of(canonical))[1]:
                result.external.add(usr)  # malloc, types C redéclarés…
                return None
        if kind in self.class_kinds:
            result.classlike.add(usr)
            definition = decl.get_definition()
            if definition is not None and self.where(self.file_of(definition))[1]:
                result.defined.add(usr)
        reason = ""
        if kind in self.type_kinds:
            if name in STD_MEMBER_TYPES:
                return None
            category, new = "types", to_pascal_case(name)
        elif kind in self.function_kinds:
            if name.startswith("operator"):
                return None
            category, new = "functions", to_pascal_case(name)
            if name in self.config.keep:
                reason = "nom exigé par le langage ou conservé (--keep)"
            elif self.extern_c(decl):
                reason = 'déclaration extern "C"'
            elif kind == K.CXX_METHOD:
                for base in self.overridden(decl):
                    base_usr = base.get_usr()
                    if not self.where(self.file_of(base))[1]:
                        reason = "redéfinit une méthode déclarée hors du périmètre"
                    elif base_usr:
                        result.overrides.append((usr, base_usr))
        elif kind == K.NAMESPACE:
            if name == "std":
                return None
            category, new = "namespaces", to_camel_case(name)
        elif kind in (K.FIELD_DECL, K.VAR_DECL) and self.config.keep_constants and self.is_constant(decl):
            return None
        elif kind == K.FIELD_DECL or (kind == K.VAR_DECL and _kind(decl.semantic_parent) in self.class_kinds):
            category = "fields"
            new = to_m_camel_case(name) if decl.access_specifier == self.ci.AccessSpecifier.PRIVATE else to_camel_case(name)
        elif kind == K.VAR_DECL:
            category, new = "variables", to_camel_case(name)
            if self.extern_c(decl):
                reason = 'déclaration extern "C"'
            elif new != name and self.shadows_member(decl, new):
                reason = f"« {new} » masquerait un membre de la classe"
        elif kind == K.PARM_DECL:
            category, new = "parameters", to_camel_case(name)
            if new != name and self.shadows_member(decl, new):
                reason = f"« {new} » masquerait un membre de la classe"
        else:
            return None
        if not reason and new in CPP_KEYWORDS:
            reason = f"« {new} » est un mot-clé"
        parent = decl.semantic_parent
        scope = parent.get_usr() if parent is not None and _kind(parent) != K.TRANSLATION_UNIT else "<global>"
        if kind == K.PARM_DECL:  # portée : la fonction (paramètres et variables locales)
            scope = parent.get_usr() if parent is not None else "<global>"
        result.decls[usr] = (name, new, category, scope or "<global>", self.file_of(decl), decl.location.line, reason)
        return (name, new, category, reason)

    def target(self, cursor, result: TuResult):
        """Déclaration visée par un nœud et texte attendu à son emplacement."""
        K = self.K
        decl = cursor.referenced
        kind = _kind(cursor)
        if decl is None and kind == K.OVERLOADED_DECL_REF:
            lib = self.ci.conf.lib
            if lib.clang_getNumOverloadedDecls(cursor) > 0:
                decl = lib.clang_getOverloadedDecl(cursor, 0)
        if decl is None:
            if kind in self.ref_kinds and cursor.spelling:
                result.unresolved.append((self.file_of(cursor), cursor.location.line, cursor.location.column, cursor.spelling))
            return None
        dkind = _kind(decl)
        if dkind in (K.CONSTRUCTOR, K.DESTRUCTOR):  # suivent le nom de leur classe
            decl = decl.semantic_parent
            if decl is None:
                return None
        if self.classify(decl, result) is None:
            return None
        return decl.get_usr(), decl.spelling

    def run(self, entry: dict) -> TuResult:
        ci = self.ci
        result = TuResult(entry["file"])
        args = compile_arguments(entry, self.config.resource_dir)
        try:
            os.chdir(entry.get("directory", self.config.root))
            tu = ci.Index.create().parse(entry["file"], args=args)
        except ci.TranslationUnitLoadError as error:
            result.errors.append(f"analyse impossible : {error}")
            return result
        for diag in tu.diagnostics:
            if diag.severity >= ci.Diagnostic.Error:
                result.errors.append(str(diag))
        seen: set[tuple[str, int, str]] = set()

        def record(node) -> None:
            try:
                found = self.target(node, result)
            except ValueError:
                return
            if found is None:
                return
            path, offset = self.spelling(node.location)
            if path is None or not self.where(path)[0]:
                result.unsafe.add(found[0])  # collage de jetons, en-tête externe… : pas réécrivable
                return
            if (path, offset, found[0]) not in seen:
                seen.add((path, offset, found[0]))
                result.occurrences.append((path, offset, found[0], found[1]))

        K = self.K
        stack = list(tu.cursor.get_children())
        while stack:
            cursor = stack.pop()
            path = self.file_of(cursor)
            if path is not None and not self.where(path)[0]:
                continue  # en-têtes du système et fichiers exclus : sous-arbre ignoré
            children = list(cursor.get_children())
            stack.extend(children)
            kind = _kind(cursor)
            if kind == K.CALL_EXPR:
                # Conversion implicite (`if (ptr)` → operator bool) : libclang n'expose pas
                # l'expression convertie ; ses identifiants sont retrouvés par leur position.
                ref = cursor.referenced
                if ref is not None and _kind(ref) == K.CONVERSION_FUNCTION:
                    for token in cursor.get_tokens():
                        if token.kind == ci.TokenKind.IDENTIFIER:
                            inner = ci.Cursor.from_location(tu, token.location)
                            if inner is not None and _kind(inner) in self.naming_kinds:
                                record(inner)
                continue
            if kind is None or not (kind in self.naming_kinds or kind.is_declaration()):
                continue
            record(cursor)
        return result


def _analyze(entry: dict) -> TuResult:
    return _Analyzer(_config).run(entry)


# ---------------------------------------------------------------------------
# Base de compilation
# ---------------------------------------------------------------------------

_DROP_WITH_VALUE = {"-o", "-MF", "-MT", "-MQ"}
_DROP = {"-c", "-MMD", "-MD", "-MP"}


def compile_arguments(entry: dict, resource_dir: str | None) -> list[str]:
    """Options de compilation d'une entrée, sans le compilateur (ni ccache), ni sortie."""
    raw = entry.get("arguments")
    if raw is None:
        import shlex

        raw = shlex.split(entry["command"])
    start = next((i for i, a in enumerate(raw) if a.startswith("-")), len(raw))
    out: list[str] = []
    skip = False
    source = os.path.realpath(os.path.join(entry.get("directory", "."), entry["file"]))
    for arg in raw[start:]:
        if skip:
            skip = False
            continue
        if arg in _DROP_WITH_VALUE:
            skip = True
            continue
        if arg in _DROP or os.path.realpath(os.path.join(entry.get("directory", "."), arg)) == source:
            continue
        out.append(arg)
    out += ["-w", "-Wno-unknown-warning-option", "-Wno-unused-command-line-argument"]
    if resource_dir:
        out += ["-resource-dir", resource_dir]
    return out


def find_libclang() -> str | None:
    env = os.environ.get("LIBCLANG")
    if env:
        return env

    def version(path: str) -> int:
        match = re.search(r"llvm-(\d+)", path)
        return int(match.group(1)) if match else 0

    candidates = sorted(glob.glob("/usr/lib/llvm-*/lib/libclang-*.so.1") + glob.glob("/usr/lib/llvm-*/lib/libclang.so.1"), key=version)
    return candidates[-1] if candidates else None


def find_resource_dir(libclang: str | None) -> str | None:
    if libclang:
        dirs = sorted(glob.glob(os.path.join(os.path.dirname(libclang), "clang", "*")))
        if dirs:
            return dirs[-1]
    return None


# ---------------------------------------------------------------------------
# Plan de renommage
# ---------------------------------------------------------------------------


@dataclass
class Plan:
    renames: dict  # usr -> nouveau nom
    decls: dict  # usr -> (old, new, category, scope, file, line, reason)
    edits: dict  # fichier -> [(offset, ancien, nouveau)]
    mismatches: list  # (fichier, offset, attendu)
    unresolved: list  # (fichier, ligne, colonne, nom)
    errors: dict  # unité de traduction -> erreurs


def build_plan(results: list[TuResult], categories: frozenset) -> Plan:
    decls: dict = {}
    overrides: list = []
    for result in results:
        for usr, info in result.decls.items():
            if usr not in decls or (info[6] and not decls[usr][6]):
                decls[usr] = info  # une exclusion vue dans une unité l'emporte
        overrides.extend(result.overrides)

    external = set().union(*(r.external for r in results))
    classlike = set().union(*(r.classlike for r in results))
    defined = set().union(*(r.defined for r in results))
    reasons = {usr: info[6] for usr, info in decls.items()}
    unsafe = set().union(*(r.unsafe for r in results))
    # Une même position qui désigne plusieurs entités renommées différemment (corps de
    # macro générique) : aucune d'elles n'est renommée.
    targets: dict = defaultdict(set)
    for result in results:
        for path, offset, usr, _old in result.occurrences:
            if usr in decls:
                targets[(path, offset)].add(usr)
    for usrs in targets.values():
        if len({decls[u][1] for u in usrs}) > 1:
            unsafe |= usrs
    for usr in decls:
        if usr in unsafe:
            reasons[usr] = "occurrence non réécrivable (macro ?)"
        elif usr in external:
            reasons[usr] = "entité déclarée hors du périmètre"
        elif usr in classlike and usr not in defined:
            reasons[usr] = "type sans définition dans le projet (type externe ?)"
    for usr, info in decls.items():
        if not reasons[usr] and info[2] not in categories:
            reasons[usr] = "catégorie non retenue"

    # Conflits : deux noms différents qui aboutissent au même nom dans une même portée.
    by_scope: dict = defaultdict(lambda: defaultdict(set))
    for usr, (old, new, _category, scope, *_rest) in decls.items():
        final = new if not reasons[usr] else old
        by_scope[scope][final].add((old, usr))
    for names in by_scope.values():
        for final, members in names.items():
            olds = {old for old, _usr in members}
            if len(olds) > 1:
                for old, usr in members:
                    if old != final and not reasons[usr]:
                        reasons[usr] = f"conflit : {', '.join(sorted(olds))} → {final}"

    # Redéfinitions : une méthode et celles qu'elle redéfinit changent ensemble ou pas du tout.
    parent: dict = {}

    def find(x):
        while parent.get(x, x) != x:
            parent[x] = parent.get(parent[x], parent[x])
            x = parent[x]
        return x

    for usr, base in overrides:
        parent[find(usr)] = find(base)
    groups: dict = defaultdict(list)
    for usr in {u for pair in overrides for u in pair}:
        groups[find(usr)].append(usr)
    for members in groups.values():
        blocked = [u for u in members if u in decls and reasons[u]]
        if blocked:
            for u in members:
                if u in decls and not reasons[u]:
                    reasons[u] = "redéfinition liée à une méthode non renommée"

    renames = {usr: info[1] for usr, info in decls.items() if not reasons[usr] and info[1] != info[0]}
    final_decls = {usr: (*info[:6], reasons[usr]) for usr, info in decls.items()}

    edits: dict = defaultdict(dict)
    for result in results:
        for path, offset, usr, old in result.occurrences:
            if usr in renames:
                edits[path][offset] = (old, renames[usr], usr)
    old_names = {decls[usr][0] for usr in renames}
    unresolved = sorted({u for result in results for u in result.unresolved if u[3] in old_names and u[0]})
    errors = {result.file: result.errors for result in results if result.errors}
    return Plan(renames, final_decls, {p: sorted((o, *v) for o, v in e.items()) for p, e in edits.items()}, [], unresolved, errors)


def unsafe_occurrences(path: str, edits: list) -> list:
    """Occurrences dont le texte n'est pas l'ancien nom (expansion de macro…) : (offset, usr)."""
    with open(path, "rb") as f:
        data = f.read()
    bad = []
    for offset, old, _new, usr in edits:
        start = _name_start(data, offset)
        end = start + len(old.encode())
        if data[start:end] != old.encode() or (end < len(data) and (chr(data[end]).isalnum() or data[end:end + 1] == b"_")):
            bad.append((offset, usr))
    return bad


def _name_start(data: bytes, offset: int) -> int:
    start = offset
    if data[start:start + 1] == b"~":  # destructeur : le nom suit le tilde
        start += 1
        while data[start:start + 1] in (b" ", b"\t"):
            start += 1
    return start


def drop_unsafe(plan: Plan) -> None:
    """Retire du plan toute entité dont une occurrence ne peut pas être réécrite : le
    renommage partiel casserait la compilation (appel depuis le corps d'une macro…)."""
    for path, edits in plan.edits.items():
        for offset, usr in unsafe_occurrences(path, edits):
            plan.mismatches.append((path, offset, plan.decls[usr][0]))
            if usr in plan.renames:
                del plan.renames[usr]
                plan.decls[usr] = (*plan.decls[usr][:6], "occurrence non réécrivable (macro ?)")
    plan.edits = {p: [e for e in edits if e[3] in plan.renames] for p, edits in plan.edits.items()}


def rewrite(path: str, edits: list, plan: Plan) -> bytes | None:
    """Contenu réécrit d'un fichier (None si inchangé)."""
    with open(path, "rb") as f:
        data = f.read()
    out = data
    changed = False
    for offset, old, new, _usr in sorted(edits, reverse=True):
        start = _name_start(out, offset)
        out = out[:start] + new.encode() + out[start + len(old.encode()):]
        changed = True
    return out if changed else None


# ---------------------------------------------------------------------------
# Programme
# ---------------------------------------------------------------------------


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=".", help="racine du projet (défaut : dossier courant)")
    parser.add_argument("--compdb", default=None, help="compile_commands.json (défaut : <racine>/compile_commands.json)")
    parser.add_argument("--scope", action="append", default=[], help="dossier dont les déclarations sont renommées (répétable ; défaut : la racine)")
    parser.add_argument("--tu", action="append", default=[], help="n'analyser que les unités de traduction de ce dossier (répétable)")
    parser.add_argument("--exclude", action="append", default=["build/*", ".git/*", "*/third_party/*", "*/external/*"], help="motif exclu, relatif à la racine")
    parser.add_argument("--keep", action="append", default=[], help="nom de fonction à ne jamais renommer (répétable)")
    parser.add_argument("--only", default=",".join(CATEGORIES), help=f"catégories à renommer, parmi : {', '.join(CATEGORIES)}")
    parser.add_argument("--keep-constants", action="store_true", help="ne pas renommer les variables et membres const / constexpr")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    parser.add_argument("--libclang", default=None, help="bibliothèque libclang (défaut : la plus récente de /usr/lib/llvm-*, ou $LIBCLANG)")
    parser.add_argument("--resource-dir", default=None, help="en-têtes intégrés de clang (défaut : déduit de --libclang)")
    parser.add_argument("--diff", default=None, help="écrit le plan sous forme de diff unifié")
    parser.add_argument("--apply", action="store_true", help="écrit les fichiers (sinon : simulation)")
    parser.add_argument("--backup", default=None, help="avec --apply : dossier où copier les originaux")
    parser.add_argument("--list", type=int, default=40, help="nombre de renommages affichés en exemple")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    root = os.path.realpath(args.root)
    categories = frozenset(c.strip() for c in args.only.split(",") if c.strip())
    unknown = categories - set(CATEGORIES)
    if unknown:
        print(f"catégories inconnues : {', '.join(sorted(unknown))}", file=sys.stderr)
        return 2
    compdb = args.compdb or os.path.join(root, "compile_commands.json")
    try:
        with open(compdb, encoding="utf-8") as f:
            entries = json.load(f)
    except OSError as error:
        print(f"base de compilation illisible ({error}) : lancez « cpp-maker export compdb »", file=sys.stderr)
        return 2
    libclang = args.libclang or find_libclang()
    config = Config(
        root=root,
        scopes=[os.path.realpath(os.path.join(root, s)) for s in (args.scope or ["."])],
        excludes=args.exclude,
        keep=DEFAULT_KEEP | frozenset(args.keep),
        categories=categories,
        keep_constants=args.keep_constants,
        libclang=libclang,
        resource_dir=args.resource_dir or find_resource_dir(libclang),
    )
    tu_dirs = [os.path.realpath(os.path.join(root, d)) for d in args.tu]
    selected = []
    for entry in entries:
        path = os.path.realpath(os.path.join(entry.get("directory", root), entry["file"]))
        rel = os.path.relpath(path, root)
        if rel.startswith("..") or any(fnmatch(rel, p) for p in config.excludes):
            continue
        if tu_dirs and not any(path.startswith(d + os.sep) for d in tu_dirs):
            continue
        selected.append({**entry, "file": path})
    if not selected:
        print("aucune unité de traduction à analyser", file=sys.stderr)
        return 2

    print(f"libclang : {libclang or 'celle des liaisons'} · {len(selected)} unité(s) de traduction · {args.jobs} processus")
    results: list[TuResult] = []
    with ProcessPoolExecutor(max_workers=args.jobs, initializer=_init_worker, initargs=(config,)) as pool:
        for i, result in enumerate(pool.map(_analyze, selected, chunksize=1), 1):
            results.append(result)
            if i % 20 == 0 or i == len(selected):
                print(f"  analysé {i}/{len(selected)}", flush=True)

    plan = build_plan(results, categories)
    drop_unsafe(plan)
    outputs: dict[str, bytes] = {}
    for path, edits in plan.edits.items():
        content = rewrite(path, edits, plan)
        if content is not None:
            outputs[path] = content
    report(plan, outputs, root, args.list)

    if args.diff:
        with open(args.diff, "w", encoding="utf-8") as out:
            for path, content in sorted(outputs.items()):
                with open(path, encoding="utf-8", errors="replace") as f:
                    before = f.read().splitlines(keepends=True)
                after = content.decode("utf-8", errors="replace").splitlines(keepends=True)
                rel = os.path.relpath(path, root)
                out.writelines(difflib.unified_diff(before, after, f"a/{rel}", f"b/{rel}"))
        print(f"diff écrit : {args.diff}")

    if not args.apply:
        print("\nSimulation : aucun fichier modifié (--apply pour écrire).")
        return 0
    if args.backup:
        for path in outputs:
            target = os.path.join(args.backup, os.path.relpath(path, root))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copy2(path, target)
        print(f"originaux copiés dans {args.backup}")
    for path, content in outputs.items():
        with open(path, "wb") as f:
            f.write(content)
    print(f"{len(outputs)} fichier(s) écrit(s). Recompilez pour vérifier (cas signalés ci-dessus).")
    return 0


def report(plan: Plan, outputs: dict, root: str, limit: int) -> None:
    rel = lambda p: os.path.relpath(p, root) if p else "?"  # noqa: E731
    counts: dict = defaultdict(int)
    for usr in plan.renames:
        counts[plan.decls[usr][2]] += 1
    print(f"\n{len(plan.renames)} déclaration(s) à renommer, {sum(len(e) for e in plan.edits.values())} occurrence(s), {len(outputs)} fichier(s)")
    for category in CATEGORIES:
        if counts[category]:
            print(f"  {category:<11} {counts[category]}")
    shown = sorted({(info[2], info[0], info[1]) for usr, info in plan.decls.items() if usr in plan.renames})
    for category, old, new in shown[:limit]:
        print(f"    [{category}] {old} → {new}")
    if len(shown) > limit:
        print(f"    … {len(shown) - limit} autre(s)")
    skipped = [(info[6], info[0], rel(info[4]), info[5]) for info in plan.decls.values() if info[6] and info[6] != "catégorie non retenue" and info[1] != info[0]]
    if skipped:
        print(f"\nNon renommés ({len(skipped)}) :")
        for reason, old, path, line in sorted(skipped)[:limit]:
            print(f"  {path}:{line} {old} — {reason}")
    if plan.mismatches:
        print(f"\nOccurrences non réécrivables (macro…) : {len(plan.mismatches)} — leurs entités ne sont pas renommées")
        for path, offset, old in plan.mismatches[:limit]:
            print(f"  {rel(path)} (octet {offset}) {old}")
    if plan.unresolved:
        print(f"\nRéférences non résolues portant un ancien nom (code générique : à vérifier) : {len(plan.unresolved)}")
        for path, line, column, name in plan.unresolved[:limit]:
            print(f"  {rel(path)}:{line}:{column} {name}")
    if plan.errors:
        print(f"\nUnités de traduction analysées avec des erreurs (références peut-être manquées) : {len(plan.errors)}")
        for path, errors in list(plan.errors.items())[:10]:
            print(f"  {rel(path)} : {errors[0]}")


if __name__ == "__main__":
    sys.exit(main())
