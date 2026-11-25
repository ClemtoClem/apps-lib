#pragma once
/**
 * @file state_machine.hpp
 * @brief Machine à états d'animation réduite (une couche) : états nommés à pose statique
 *        (valeurs de canaux), transitions conditionnées par des paramètres booléens, fondu
 *        linéaire entre les poses pendant la transition. Version épurée de la machine de
 *        Fyrox (`State` + `Transition` + `Rule`, sans `BlendSpace` ni masques de couche).
 *
 *   game::StateMachine m;
 *   m.AddState("repos", {0.f, 0.f});
 *   m.AddState("marche", {1.f, 1.f});
 *   m.AddTransition("repos", "marche", "avance", 0.4f);
 *   m.SetParameter("avance", true);
 *   m.Tick(dt);                    // fondu vers « marche »
 *   const std::vector<float> &pose = m.Pose();
 */
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sandbox::game {

/// Machine à états d'animation réduite : états nommés à pose (valeurs de canaux),
/// transitions déclenchées par des paramètres booléens, fondu linéaire entre poses.
class StateMachine {
public:
	struct State {
		std::string name;
		std::vector<float> pose; ///< une valeur par canal (position, rotation…)
	};
	struct Transition {
		std::string from, to;
		float duration = 0.25f;	 ///< durée du fondu (secondes)
		std::string condition;	 ///< paramètre booléen qui déclenche la transition
	};

	/// Ajoute un état ; le premier ajouté devient l'état courant.
	void AddState(std::string name, std::vector<float> pose) {
		m_states.push_back({std::move(name), std::move(pose)});
		if (m_current == kNone)
			SetState(m_states.back().name);
	}
	void AddTransition(std::string from, std::string to, std::string condition, float duration = 0.25f) {
		m_transitions.push_back({std::move(from), std::move(to), duration, std::move(condition)});
	}
	void SetParameter(std::string name, bool value) {
		for (auto &[n, v] : m_params)
			if (n == name) {
				v = value;
				return;
			}
		m_params.emplace_back(std::move(name), value);
	}
	[[nodiscard]] bool GetParameter(std::string_view name) const noexcept {
		for (const auto &[n, v] : m_params)
			if (n == name)
				return v;
		return false;
	}
	/// Force un état immédiatement (sans fondu).
	void SetState(std::string_view name) {
		if (const size_t idx = IndexOf(name); idx != kNone) {
			m_current = idx;
			m_blending = false;
			m_pose = m_states[idx].pose;
		}
	}
	/// Avance la machine : transitions, fondu, pose courante.
	void Tick(float dt) {
		if (m_current == kNone)
			return;
		if (!m_blending) // cherche une transition déclenchée
			for (const Transition &tr : m_transitions)
				if (tr.from == m_states[m_current].name && GetParameter(tr.condition))
					if (const size_t to = IndexOf(tr.to); to != kNone) {
						m_from = m_current;
						m_to = to;
						m_blendTime = 0.f;
						m_blendDuration = tr.duration;
						m_blending = true;
						break;
					}
		if (m_blending) {
			m_blendTime += dt;
			const float t = m_blendDuration > 0.f ? std::clamp(m_blendTime / m_blendDuration, 0.f, 1.f) : 1.f;
			BlendPose(m_states[m_from], m_states[m_to], t);
			if (t >= 1.f) {
				m_current = m_to;
				m_blending = false;
			}
		} else {
			m_pose = m_states[m_current].pose;
		}
	}
	[[nodiscard]] const std::vector<float> &Pose() const noexcept { return m_pose; }
	[[nodiscard]] std::string_view Current() const noexcept { return m_current != kNone ? std::string_view(m_states[m_current].name) : std::string_view{}; }
	[[nodiscard]] bool Blending() const noexcept { return m_blending; }

private:
	static constexpr size_t kNone = SIZE_MAX;

	[[nodiscard]] size_t IndexOf(std::string_view name) const noexcept {
		for (size_t i = 0; i < m_states.size(); ++i)
			if (m_states[i].name == name)
				return i;
		return kNone;
	}
	void BlendPose(const State &a, const State &b, float t) {
		const size_t n = std::min(a.pose.size(), b.pose.size());
		m_pose.resize(n);
		for (size_t i = 0; i < n; ++i)
			m_pose[i] = a.pose[i] + (b.pose[i] - a.pose[i]) * t;
	}

	std::vector<State> m_states;
	std::vector<Transition> m_transitions;
	std::vector<std::pair<std::string, bool>> m_params;
	size_t m_current = kNone;
	bool m_blending = false;
	size_t m_from = kNone, m_to = kNone;
	float m_blendTime = 0.f, m_blendDuration = 0.25f;
	std::vector<float> m_pose;
};

} // namespace sandbox::game
