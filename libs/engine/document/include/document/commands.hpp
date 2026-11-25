#pragma once
/**
 * @file commands.hpp
 * @brief Commandes réversibles, transactions et historique (annuler / rétablir).
 *
 * Toute modification venant de l'interface passe par une commande exécutée par `History`.
 * Les commandes référencent les nœuds par identifiant (jamais par pointeur) : une suppression
 * annulée restaure le sous-arbre avec les mêmes identifiants, positions et appartenances, et une
 * création rétablie réutilise l'identifiant d'origine. Les commandes suivantes de l'historique
 * restent donc valides.
 *
 *   doc::History history(document);
 *   history.Execute(doc::SetProperty(id, doc::props::Bold, true));
 *   history.BeginTransaction("Insérer un tableau");
 *   …                                            // plusieurs commandes, une seule entrée
 *   history.Commit();
 *   history.Undo();
 */
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <events/events.hpp>

#include "document/document.hpp"

namespace doc {

class Command {
public:
	virtual ~Command() = default;
	/// Libellé affiché dans l'historique.
	[[nodiscard]] virtual std::string Name() const = 0;
	/// Exécute (première fois) ou rétablit.
	virtual Status Apply(Document &document) = 0;
	/// Annule (état exact d'avant `Apply`).
	virtual Status Revert(Document &document) = 0;
	/// Absorbe la commande suivante (frappe continue dans un même texte) ; faux par défaut.
	virtual bool Merge(const Command &) { return false; }
	/// Nœud créé par la dernière exécution (commandes de création) ; kNoNode sinon.
	[[nodiscard]] virtual NodeId Created() const { return kNoNode; }
};

using CommandPtr = std::unique_ptr<Command>;

// ── Fabriques ──

CommandPtr CreateNode(NodeType type, NodeId parent, size_t position = kAppend);
CommandPtr DeleteNode(NodeId id);
CommandPtr MoveNode(NodeId id, NodeId parent, size_t position = kAppend);
CommandPtr Duplicate(NodeId id, NodeId parent = kNoNode, size_t position = kAppend); ///< parent par défaut : celui de l'original, juste après
CommandPtr SetProperty(NodeId id, PropertyId property, PropertyValue value);
template <typename T> CommandPtr SetProperty(NodeId id, Prop<T> p, const T &value) { return SetProperty(id, p.id, Prop<T>::ToValue(value)); }
inline CommandPtr SetProperty(NodeId id, Prop<std::string> p, std::string_view value) { return SetProperty(id, p.id, std::string(value)); }
CommandPtr ClearProperty(NodeId id, PropertyId property);
CommandPtr CreateGroup(std::string name, int64_t priority = 0, NodeId parentGroup = kNoNode);
CommandPtr AddToGroup(NodeId node, NodeId group);
CommandPtr RemoveFromGroup(NodeId node, NodeId group);
CommandPtr SetResolution(GroupResolution resolution);
/// Plusieurs commandes formant une seule opération (annulée d'un bloc).
CommandPtr Composite(std::string name, std::vector<CommandPtr> commands);
/// Regroupe des nœuds dans un nouveau groupe (opération de groupe, une entrée d'historique).
CommandPtr GroupNodes(std::string name, std::vector<NodeId> nodes, int64_t priority = 0);
/// Dissocie : retire tous les membres du groupe puis supprime le groupe.
CommandPtr Ungroup(NodeId group);

/// Identifiant produit par la dernière exécution d'une commande de création (CreateNode,
/// Duplicate, CreateGroup, GroupNodes) ; kNoNode sinon.
[[nodiscard]] inline NodeId CreatedId(const Command &command) { return command.Created(); }

class History {
public:
	struct Entry {
		std::string name;
		uint64_t id = 0; ///< identifiant unique de l'entrée
	};

	explicit History(Document &document) : m_document(document) {}
	/// Document modifié par cet historique (lecture de l'état avant de composer des commandes).
	[[nodiscard]] Document &Target() noexcept { return m_document; }

	/// Exécute la commande et l'inscrit (fusion avec la précédente si possible) ; vide le
	/// « rétablir ». En transaction, elle rejoint la transaction en cours.
	Status Execute(CommandPtr command);
	Status Undo();
	Status Redo();
	/// Annule ou rétablit jusqu'à ce que `Position() == position`.
	Status JumpTo(size_t position);
	[[nodiscard]] bool CanUndo() const noexcept { return m_position > 0 && !m_transaction; }
	[[nodiscard]] bool CanRedo() const noexcept { return m_position < m_entries.size() && !m_transaction; }
	[[nodiscard]] std::vector<Entry> Entries() const;
	/// Nombre d'entrées appliquées (les suivantes peuvent être rétablies).
	[[nodiscard]] size_t Position() const noexcept { return m_position; }
	[[nodiscard]] std::string UndoName() const;
	[[nodiscard]] std::string RedoName() const;

	void BeginTransaction(std::string name);
	/// Inscrit la transaction comme une seule entrée (rien si elle est vide).
	Status Commit();
	/// Annule les commandes de la transaction en cours.
	Status Rollback();
	[[nodiscard]] bool InTransaction() const noexcept { return m_transaction != nullptr; }

	/// Nombre maximal d'entrées conservées (les plus anciennes sont oubliées).
	void SetLimit(size_t limit);
	void Clear();
	/// Mémorise l'état courant comme enregistré.
	void MarkClean() noexcept { m_cleanId = TopId(); }
	/// Identifiant de l'état courant (à mémoriser au lancement d'une sauvegarde de fond).
	[[nodiscard]] uint64_t StateId() const noexcept { return TopId(); }
	/// Marque comme enregistré l'état `stateId` (fin d'une sauvegarde de fond lancée sur cet état ;
	/// si l'utilisateur a continué d'éditer, le document reste « modifié »).
	void MarkCleanAt(uint64_t stateId) noexcept { m_cleanId = stateId; }
	[[nodiscard]] bool IsDirty() const noexcept { return m_cleanId != TopId(); }
	/// Fusion des frappes successives dans un même texte (désactivable).
	void SetMergeEnabled(bool enabled) noexcept { m_merge = enabled; }
	/// Interrompt la fusion : la prochaine commande crée une nouvelle entrée.
	void BreakMerge() noexcept { m_mergeable = false; }

	/// Émis après chaque exécution, annulation, rétablissement ou transaction.
	[[nodiscard]] events::Signal<> &Changed() noexcept { return m_changed; }

private:
	struct Stored {
		CommandPtr command;
		uint64_t id;
	};
	[[nodiscard]] uint64_t TopId() const noexcept { return m_position ? m_entries[m_position - 1].id : 0; }

	Document &m_document;
	std::vector<Stored> m_entries;
	size_t m_position = 0;
	size_t m_limit = 1000;
	uint64_t m_nextId = 1, m_cleanId = 0;
	bool m_merge = true, m_mergeable = false;
	std::unique_ptr<std::vector<CommandPtr>> m_transaction;
	std::string m_transactionName;
	events::Signal<> m_changed;
};

/// Copie de nœuds indépendante des identifiants (presse-papiers des blocs : paragraphes,
/// tableaux, images, formes…) : types, propriétés explicites, appartenances, descendants.
struct NodeFragment {
	struct Item {
		NodeType type = NodeType::Paragraph;
		PropertyMap properties;
		std::vector<NodeId> groups; ///< groupes (non automatiques) du nœud copié
		std::vector<Item> children;
	};
	std::vector<Item> roots;

	[[nodiscard]] bool Empty() const noexcept { return roots.empty(); }
	/// Texte des segments (paragraphes séparés par « \n ») pour le presse-papiers du système.
	[[nodiscard]] std::string PlainText() const;
};

/// Copie des nœuds (un nœud dont un ancêtre est aussi copié n'est copié qu'une fois ; les nœuds
/// structurels, sections, pages et groupes sont ignorés).
[[nodiscard]] NodeFragment CopyNodes(const DocumentView &view, std::span<const NodeId> ids);
/// Colle les nœuds sous `parent` à partir de `position` (une transaction) ; les appartenances
/// à des groupes disparus sont ignorées. Rend les nœuds racines créés.
DocResult<std::vector<NodeId>> PasteNodes(History &history, const NodeFragment &fragment, NodeId parent, size_t position = kAppend);

} // namespace doc
