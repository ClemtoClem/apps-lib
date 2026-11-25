// Commandes réversibles et historique.
#include "document/commands.hpp"

#include <algorithm>
#include <format>

namespace doc {

namespace {

std::string NodeLabel(NodeType type) { return std::string(NodeTypeName(type)); }

/// Base des commandes qui créent un sous-arbre : l'annulation le retire en le conservant, le
/// rétablissement le réinsère (mêmes identifiants).
class Creation : public Command {
public:
	Status Revert(Document &d) override {
		auto removed = d.RemoveSubtree(m_created);
		if (!removed)
			return Err(removed.Error());
		m_saved = std::move(removed.Value());
		return Success();
	}
	[[nodiscard]] NodeId Created() const override { return m_created; }

protected:
	/// Rétablissement : réinsertion du sous-arbre conservé.
	Status Reinsert(Document &d) { return d.InsertSubtree(*m_saved); }
	[[nodiscard]] bool FirstRun() const noexcept { return !m_saved.has_value(); }

	NodeId m_created = kNoNode;
	std::optional<Subtree> m_saved;
};

class CreateNodeCommand final : public Creation {
public:
	CreateNodeCommand(NodeType type, NodeId parent, size_t position) : m_type(type), m_parent(parent), m_position(position) {}
	[[nodiscard]] std::string Name() const override { return "Insérer " + NodeLabel(m_type); }
	Status Apply(Document &d) override {
		if (!FirstRun())
			return Reinsert(d);
		auto created = d.CreateNode(m_type, m_parent, m_position);
		if (!created)
			return Err(created.Error());
		m_created = created.Value();
		return Success();
	}

private:
	NodeType m_type;
	NodeId m_parent;
	size_t m_position;
};

class DuplicateCommand final : public Creation {
public:
	DuplicateCommand(NodeId id, NodeId parent, size_t position) : m_id(id), m_parent(parent), m_position(position) {}
	[[nodiscard]] std::string Name() const override { return "Dupliquer"; }
	Status Apply(Document &d) override {
		if (!FirstRun())
			return Reinsert(d);
		NodeId parent = m_parent;
		size_t position = m_position;
		if (parent == kNoNode) { // juste après l'original
			const Node *n = d.Find(m_id);
			if (!n)
				return Failure(ErrorCode::NotFound, std::format("nœud {} introuvable", m_id));
			parent = n->Parent();
			const Node *p = d.Find(parent);
			position = p ? p->IndexOfChild(m_id) + 1 : kAppend;
		}
		auto created = d.Duplicate(m_id, parent, position);
		if (!created)
			return Err(created.Error());
		m_created = created.Value();
		return Success();
	}

private:
	NodeId m_id, m_parent;
	size_t m_position;
};

class CreateGroupCommand final : public Creation {
public:
	CreateGroupCommand(std::string name, int64_t priority, NodeId parent, std::vector<NodeId> members = {})
		: m_name(std::move(name)), m_priority(priority), m_parent(parent), m_members(std::move(members)) {}
	[[nodiscard]] std::string Name() const override { return m_members.empty() ? "Créer le groupe « " + m_name + " »" : "Grouper"; }
	Status Apply(Document &d) override {
		if (!FirstRun())
			return Reinsert(d); // le groupe revient avec ses appartenances
		auto created = d.CreateGroup(m_name, m_priority, m_parent);
		if (!created)
			return Err(created.Error());
		m_created = created.Value();
		for (NodeId member : m_members)
			if (auto added = d.AddToGroup(member, m_created); !added) {
				(void)d.RemoveSubtree(m_created); // retour à l'état initial
				m_created = kNoNode;
				return added;
			}
		return Success();
	}

private:
	std::string m_name;
	int64_t m_priority;
	NodeId m_parent;
	std::vector<NodeId> m_members;
};

/// Suppression (et dissociation d'un groupe) : sous-arbre conservé pour l'annulation.
class DeleteNodeCommand final : public Command {
public:
	DeleteNodeCommand(NodeId id, std::string name) : m_id(id), m_name(std::move(name)) {}
	[[nodiscard]] std::string Name() const override { return m_name; }
	Status Apply(Document &d) override {
		auto removed = d.RemoveSubtree(m_id);
		if (!removed)
			return Err(removed.Error());
		m_saved = std::move(removed.Value());
		return Success();
	}
	Status Revert(Document &d) override { return m_saved ? d.InsertSubtree(*m_saved) : Failure(ErrorCode::Nothing, "rien à restaurer"); }

private:
	NodeId m_id;
	std::string m_name;
	std::optional<Subtree> m_saved;
};

class MoveNodeCommand final : public Command {
public:
	MoveNodeCommand(NodeId id, NodeId parent, size_t position) : m_id(id), m_parent(parent), m_position(position) {}
	[[nodiscard]] std::string Name() const override { return "Déplacer"; }
	Status Apply(Document &d) override {
		const Node *n = d.Find(m_id);
		if (!n)
			return Failure(ErrorCode::NotFound, std::format("nœud {} introuvable", m_id));
		m_oldParent = n->Parent();
		m_oldIndex = d.Find(m_oldParent)->IndexOfChild(m_id);
		return d.MoveNode(m_id, m_parent, m_position);
	}
	Status Revert(Document &d) override {
		const Node *n = d.Find(m_id);
		if (!n)
			return Failure(ErrorCode::NotFound, std::format("nœud {} introuvable", m_id));
		size_t position = m_oldIndex;
		// MoveNode exprime la position avant le retrait : compensation dans le même parent.
		if (n->Parent() == m_oldParent && m_oldIndex > d.Find(m_oldParent)->IndexOfChild(m_id))
			++position;
		return d.MoveNode(m_id, m_oldParent, position);
	}

private:
	NodeId m_id, m_parent;
	size_t m_position;
	NodeId m_oldParent = kNoNode;
	size_t m_oldIndex = 0;
};

class SetPropertyCommand final : public Command {
public:
	SetPropertyCommand(NodeId id, PropertyId property, std::optional<PropertyValue> value)
		: m_id(id), m_property(property), m_value(std::move(value)) {}
	[[nodiscard]] std::string Name() const override {
		return std::format("{} {}", m_value ? "Modifier" : "Rétablir l'héritage de", Info(m_property).name);
	}
	Status Apply(Document &d) override {
		if (!d.Contains(m_id))
			return Failure(ErrorCode::NotFound, std::format("nœud {} introuvable", m_id));
		const PropertyValue *current = d.Explicit(m_id, m_property);
		m_old = current ? std::optional<PropertyValue>(*current) : std::nullopt;
		return m_value ? d.SetProperty(m_id, m_property, *m_value) : d.ClearProperty(m_id, m_property);
	}
	Status Revert(Document &d) override { return m_old ? d.SetProperty(m_id, m_property, *m_old) : d.ClearProperty(m_id, m_property); }
	bool Merge(const Command &next) override {
		const auto *other = dynamic_cast<const SetPropertyCommand *>(&next);
		if (!other || other->m_id != m_id || other->m_property != m_property || m_property != PropertyId::Text || !other->m_value)
			return false;
		m_value = other->m_value; // l'ancienne valeur reste celle d'avant la première frappe
		return true;
	}

private:
	NodeId m_id;
	PropertyId m_property;
	std::optional<PropertyValue> m_value, m_old;
};

class MembershipCommand final : public Command {
public:
	MembershipCommand(NodeId node, NodeId group, bool add) : m_node(node), m_group(group), m_add(add) {}
	[[nodiscard]] std::string Name() const override { return m_add ? "Ajouter au groupe" : "Retirer du groupe"; }
	Status Apply(Document &d) override {
		if (m_add)
			return d.AddToGroup(m_node, m_group);
		const Node *n = d.Find(m_node);
		const Node *g = d.Find(m_group);
		if (!n || !g || !n->InGroup(m_group))
			return Failure(ErrorCode::NotFound, std::format("{} n'appartient pas au groupe {}", m_node, m_group));
		const auto groups = n->Groups();
		const auto it = std::ranges::find(groups, m_group, &Membership::group);
		m_groupIndex = size_t(it - groups.begin());
		m_automatic = it->automatic;
		m_memberIndex = size_t(std::ranges::find(g->Members(), m_node) - g->Members().begin());
		return d.RemoveFromGroup(m_node, m_group);
	}
	Status Revert(Document &d) override {
		if (m_add)
			return d.RemoveFromGroup(m_node, m_group);
		return d.AddToGroup(m_node, m_group, m_automatic, m_groupIndex, m_memberIndex);
	}

private:
	NodeId m_node, m_group;
	bool m_add;
	size_t m_groupIndex = 0, m_memberIndex = 0;
	bool m_automatic = false;
};

class ResolutionCommand final : public Command {
public:
	explicit ResolutionCommand(GroupResolution r) : m_value(r) {}
	[[nodiscard]] std::string Name() const override { return "Politique des groupes"; }
	Status Apply(Document &d) override {
		m_old = d.Resolution();
		d.SetResolution(m_value);
		return Success();
	}
	Status Revert(Document &d) override {
		d.SetResolution(m_old);
		return Success();
	}

private:
	GroupResolution m_value, m_old = GroupResolution::PriorityFirst;
};

class CompositeCommand final : public Command {
public:
	CompositeCommand(std::string name, std::vector<CommandPtr> commands, bool applied)
		: m_name(std::move(name)), m_commands(std::move(commands)), m_applied(applied) {}
	[[nodiscard]] std::string Name() const override { return m_name; }
	Status Apply(Document &d) override {
		if (m_applied) { // transaction déjà exécutée au fil de l'eau
			m_applied = false;
			return Success();
		}
		for (size_t i = 0; i < m_commands.size(); ++i)
			if (auto s = m_commands[i]->Apply(d); !s) {
				for (size_t k = i; k-- > 0;)
					(void)m_commands[k]->Revert(d);
				return s;
			}
		return Success();
	}
	Status Revert(Document &d) override {
		for (size_t k = m_commands.size(); k-- > 0;)
			if (auto s = m_commands[k]->Revert(d); !s) {
				for (size_t j = k + 1; j < m_commands.size(); ++j) // retour à l'état appliqué
					(void)m_commands[j]->Apply(d);
				return s;
			}
		return Success();
	}
	[[nodiscard]] NodeId Created() const override {
		for (const CommandPtr &c : m_commands)
			if (const NodeId id = c->Created(); id != kNoNode)
				return id;
		return kNoNode;
	}

private:
	std::string m_name;
	std::vector<CommandPtr> m_commands;
	bool m_applied;
};

} // namespace

CommandPtr CreateNode(NodeType type, NodeId parent, size_t position) { return std::make_unique<CreateNodeCommand>(type, parent, position); }
CommandPtr DeleteNode(NodeId id) { return std::make_unique<DeleteNodeCommand>(id, "Supprimer"); }
CommandPtr MoveNode(NodeId id, NodeId parent, size_t position) { return std::make_unique<MoveNodeCommand>(id, parent, position); }
CommandPtr Duplicate(NodeId id, NodeId parent, size_t position) { return std::make_unique<DuplicateCommand>(id, parent, position); }
CommandPtr SetProperty(NodeId id, PropertyId property, PropertyValue value) {
	return std::make_unique<SetPropertyCommand>(id, property, std::move(value));
}
CommandPtr ClearProperty(NodeId id, PropertyId property) { return std::make_unique<SetPropertyCommand>(id, property, std::nullopt); }
CommandPtr CreateGroup(std::string name, int64_t priority, NodeId parentGroup) {
	return std::make_unique<CreateGroupCommand>(std::move(name), priority, parentGroup);
}
CommandPtr AddToGroup(NodeId node, NodeId group) { return std::make_unique<MembershipCommand>(node, group, true); }
CommandPtr RemoveFromGroup(NodeId node, NodeId group) { return std::make_unique<MembershipCommand>(node, group, false); }
CommandPtr SetResolution(GroupResolution resolution) { return std::make_unique<ResolutionCommand>(resolution); }
CommandPtr Composite(std::string name, std::vector<CommandPtr> commands) {
	return std::make_unique<CompositeCommand>(std::move(name), std::move(commands), false);
}
CommandPtr GroupNodes(std::string name, std::vector<NodeId> nodes, int64_t priority) {
	return std::make_unique<CreateGroupCommand>(std::move(name), priority, kNoNode, std::move(nodes));
}
CommandPtr Ungroup(NodeId group) { return std::make_unique<DeleteNodeCommand>(group, "Dissocier"); }

// ── Historique ──

Status History::Execute(CommandPtr command) {
	if (!command)
		return Failure(ErrorCode::InvalidValue, "commande vide");
	if (auto s = command->Apply(m_document); !s)
		return s;
	if (m_transaction) {
		m_transaction->push_back(std::move(command));
		return Success();
	}
	if (m_merge && m_mergeable && m_position > 0 && m_position == m_entries.size() && m_entries.back().command->Merge(*command)) {
		m_entries.back().id = m_nextId++; // l'état « enregistré » ne correspond plus
		m_changed.Emit();
		return Success();
	}
	// Les entrées rétablissables sont abandonnées.
	for (size_t i = m_position; i < m_entries.size(); ++i)
		if (m_entries[i].id == m_cleanId)
			m_cleanId = UINT64_MAX; // état enregistré devenu inatteignable
	m_entries.resize(m_position);
	m_entries.push_back({std::move(command), m_nextId++});
	if (m_entries.size() > m_limit)
		m_entries.erase(m_entries.begin(), m_entries.begin() + std::ptrdiff_t(m_entries.size() - m_limit));
	m_position = m_entries.size();
	m_mergeable = true;
	m_changed.Emit();
	return Success();
}

Status History::Undo() {
	if (!CanUndo())
		return Failure(ErrorCode::Nothing, "rien à annuler");
	if (auto s = m_entries[m_position - 1].command->Revert(m_document); !s)
		return s;
	--m_position;
	m_mergeable = false;
	m_changed.Emit();
	return Success();
}

Status History::Redo() {
	if (!CanRedo())
		return Failure(ErrorCode::Nothing, "rien à rétablir");
	if (auto s = m_entries[m_position].command->Apply(m_document); !s)
		return s;
	++m_position;
	m_mergeable = false;
	m_changed.Emit();
	return Success();
}

Status History::JumpTo(size_t position) {
	if (position > m_entries.size())
		return Failure(ErrorCode::InvalidValue, "position hors de l'historique");
	while (m_position > position)
		if (auto s = Undo(); !s)
			return s;
	while (m_position < position)
		if (auto s = Redo(); !s)
			return s;
	return Success();
}

std::vector<History::Entry> History::Entries() const {
	std::vector<Entry> out;
	out.reserve(m_entries.size());
	for (const Stored &s : m_entries)
		out.push_back({s.command->Name(), s.id});
	return out;
}

std::string History::UndoName() const { return CanUndo() ? m_entries[m_position - 1].command->Name() : std::string(); }
std::string History::RedoName() const { return CanRedo() ? m_entries[m_position].command->Name() : std::string(); }

void History::BeginTransaction(std::string name) {
	if (m_transaction)
		return; // transactions non imbriquées : la transaction en cours absorbe la nouvelle
	m_transaction = std::make_unique<std::vector<CommandPtr>>();
	m_transactionName = std::move(name);
}

Status History::Commit() {
	if (!m_transaction)
		return Failure(ErrorCode::Nothing, "aucune transaction en cours");
	auto commands = std::move(*m_transaction);
	m_transaction.reset();
	if (commands.empty())
		return Success();
	m_mergeable = false;
	auto composite = std::make_unique<CompositeCommand>(std::move(m_transactionName), std::move(commands), true);
	return Execute(std::move(composite)); // Apply « déjà exécutée » : simple inscription
}

Status History::Rollback() {
	if (!m_transaction)
		return Failure(ErrorCode::Nothing, "aucune transaction en cours");
	auto commands = std::move(*m_transaction);
	m_transaction.reset();
	for (size_t k = commands.size(); k-- > 0;)
		if (auto s = commands[k]->Revert(m_document); !s)
			return s;
	m_changed.Emit();
	return Success();
}

void History::SetLimit(size_t limit) {
	m_limit = std::max<size_t>(1, limit);
	if (m_entries.size() > m_limit) {
		const size_t drop = std::min(m_entries.size() - m_limit, m_position);
		m_entries.erase(m_entries.begin(), m_entries.begin() + std::ptrdiff_t(drop));
		m_position -= drop;
		m_entries.resize(std::min(m_entries.size(), m_limit));
	}
}

void History::Clear() {
	m_entries.clear();
	m_position = 0;
	m_cleanId = 0;
	m_mergeable = false;
	m_changed.Emit();
}

} // namespace doc
