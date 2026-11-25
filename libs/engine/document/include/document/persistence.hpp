#pragma once
/**
 * @file persistence.hpp
 * @brief Format `.docdb` : base `foundation.sql` (tables meta, nodes, properties, memberships,
 *        resources) sérialisée en instantané avec somme CRC-32, écriture atomique, contrôle
 *        d'intégrité complet au chargement, version du schéma et migrations.
 *
 * Les types de nœuds et les propriétés sont enregistrés par **nom** (identifiants stables du
 * catalogue), jamais par valeur d'énumération : réordonner une énumération ne casse pas les
 * fichiers existants.
 *
 * L'enregistrement lit une vue en lecture seule : il peut s'exécuter sur un `Snapshot`, sur un
 * fil de fond, pendant que l'utilisateur continue d'éditer.
 *
 *   auto saved = doc::SaveDocument(*document.TakeSnapshot(), "rapport.docdb");
 *   auto loaded = doc::LoadDocument("rapport.docdb");     // DocResult<Document>
 */
#include <filesystem>
#include <span>
#include <string_view>

#include "document/document.hpp"

namespace sql {
class Database;
}

namespace doc {

/// Version du schéma écrite par ce moteur.
inline constexpr int kSchemaVersion = 1;
inline constexpr std::string_view kFormatName = "apps-lib.docdb";

/// Migration d'un fichier de la version `from` vers `from + 1` (modifie la base en place).
struct Migration {
	int from = 0;
	Status (*apply)(sql::Database &db) = nullptr;
};

/// Migrations intégrées (vide en version 1 : aucune version antérieure publiée).
[[nodiscard]] std::span<const Migration> BuiltinMigrations() noexcept;

/// Remplit une base vide avec le contenu de la vue.
Status WriteDatabase(const DocumentView &view, sql::Database &db);
/// Construit un document depuis une base : migrations jusqu'à `targetVersion`, puis contrôle
/// d'intégrité (parents, enfants admis, cycles, nœuds orphelins, unicité, propriétés connues
/// et valides, appartenances, ressources). Toute incohérence rend `ErrorCode::Corrupt`.
[[nodiscard]] DocResult<Document> ReadDatabase(sql::Database &db, std::span<const Migration> migrations = BuiltinMigrations(),
											   int targetVersion = kSchemaVersion);

/// Enregistre la vue dans `path` (fichier temporaire puis renommage : un échec laisse l'ancien
/// fichier intact).
Status SaveDocument(const DocumentView &view, const std::filesystem::path &path);
[[nodiscard]] DocResult<Document> LoadDocument(const std::filesystem::path &path);

} // namespace doc
