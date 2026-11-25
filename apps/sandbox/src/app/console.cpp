// Console du sandbox (comme la Sortie de Godot ou l'Output Log d'Unreal) : messages des scripts
// pendant la visite, résultats de « Vérifier » ; une erreur de script cliquée ouvre son script
// à la ligne fautive.
#include <format>

#include "sandbox/app/sandbox_app.hpp"

namespace sandbox {

namespace {
constexpr size_t kMaxLog = 1000;
}

void SandboxApp::Log(game::GameMessage message) {
	if (message.error && m_game)
		++m_gameErrors;
	m_log.push_back(std::move(message));
	while (m_log.size() > kMaxLog)
		m_log.pop_front();
}

void SandboxApp::OpenScriptAt(EntityId source, int line, const std::string &message) {
	Editor &ed = m_editor;
	ed.CloseAllGroups();
	ed.ClearSelection();
	if (source != 0) {
		if (!ed.Doc().Find(source)) {
			Ui().Notify("Ce groupe n'existe plus", Theme().warning, 3.f);
			return;
		}
		ed.SelectEntity(source);
	}
	std::string label;
	(void)EditedScript(&label);
	m_scriptCheckFor = label;
	m_scriptCheck = message;
	m_scriptErrorLine = line;
	m_gotoLine = std::max(line, 1);
	ShowPanel("script");
}

void SandboxApp::DrawConsole() {
	ui::Context &u = Ui();
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = u.Style();
	size_t errors = 0;
	for (const game::GameMessage &m : m_log)
		errors += m.error ? 1u : 0u;
	u.Row({1.f, 1.f, 1.f}, 28.f);
	u.Label(std::format("{} message{} · {} erreur{}", m_log.size(), m_log.size() > 1 ? "s" : "", errors, errors > 1 ? "s" : ""), th.textDim, 12.f);
	u.Checkbox("Erreurs seules", m_logErrorsOnly);
	if (u.Button("Effacer", !m_log.empty()))
		m_log.clear();
	if (m_log.empty()) {
		u.Wrapped("Les messages des scripts (print, game.say), leurs erreurs et les résultats de « Vérifier » s'affichent ici.", th.textDim, 12.f);
		return;
	}
	for (size_t i = 0; i < m_log.size(); ++i) {
		const game::GameMessage &m = m_log[i];
		if (m_logErrorsOnly && !m.error)
			continue;
		const bool link = m.script && m.line > 0; // erreur positionnée : cliquable
		const sdl3::FRect r = u.Next(22.f);
		u.PushId(uint64_t(i) + 5000);
		const auto it = u.Interact(u.MakeId("log"), r);
		u.PopId();
		if (link && it.hovered)
			c.FillRect(r, th.widgetHover);
		const sdl3::Color ink = m.error ? th.danger : m.script ? th.text : th.textDim;
		if (m.time > 0.0)
			c.Text(r.x + 4.f, r.y + 4.f, std::format("{:6.2f} s", m.time), th.textDim, 11.f);
		c.Text(r.x + 64.f, r.y + 3.f, c.Ellipsize(m.text, r.w - 160.f, 12.5f), ink, 12.5f);
		if (link) {
			const std::string where = std::format("ligne {} ›", m.line);
			c.Text(r.Right() - c.MeasureText(where, 11.f) - 6.f, r.y + 4.f, where, th.accent, 11.f);
			if (it.leftClicked)
				OpenScriptAt(m.source, m.line, m.text);
		}
	}
}

} // namespace sandbox
