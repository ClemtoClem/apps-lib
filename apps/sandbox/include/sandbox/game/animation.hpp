#pragma once
/**
 * @file animation.hpp
 * @brief Animation : lecteur temporel de courbes (`math::Curve`, Phase 0) avec signaux de
 *        timeline, et animation par trames d'une feuille de sprites. Pures (aucun rendu) —
 *        l'appelant applique les valeurs aux nœuds 3D ou aux éléments du HUD 2D.
 *
 *   game::Animation anim;
 *   anim.AddTrack(courbeDePosition);
 *   anim.AddSignal({"pas", 0.5f});
 *   anim.Tick(dt);
 *   const float x = anim.Value(0);          // position à l'instant courant
 *   for (const std::string &s : anim.TakeSignals()) …; // « pas » quand franchi
 */
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <math/curve.hpp>
#include <math/rect.hpp>

namespace sandbox::game {

/// Signal de timeline : marqueur nommé émis quand la lecture le franchit.
struct AnimSignal {
	std::string name;
	float time = 0.f; ///< instant (ou numéro de trame pour une feuille de sprites)
};

/// Lecteur d'animation : avance le temps, échantillonne les courbes, émet les signaux.
class Animation {
public:
	std::vector<math::Curve> tracks; ///< une courbe par propriété animée
	std::vector<AnimSignal> signals;
	float speed = 1.f;	///< multiplicateur de vitesse (négatif : lecture à l'envers)
	bool looped = false; ///< boucle sur la durée totale
	bool playing = true;

	void AddTrack(math::Curve curve) {
		if (!curve.keys.empty())
			m_duration = std::max(m_duration, curve.keys.back().location);
		tracks.push_back(std::move(curve));
	}
	void AddSignal(AnimSignal signal) {
		m_duration = std::max(m_duration, signal.time);
		signals.push_back(std::move(signal));
	}
	/// Avance de `dt` secondes ; émet les signaux franchis (lus via `TakeSignals`).
	void Tick(float dt) {
		m_prevTime = m_time;
		if (!playing || m_duration <= 0.f)
			return;
		const float next = m_time + dt * speed;
		if (looped) {
			m_time = std::fmod(next, m_duration);
			if (m_time < 0.f)
				m_time += m_duration;
		} else if (next >= m_duration) {
			m_time = m_duration;
			playing = false;
		} else if (next <= 0.f) {
			m_time = 0.f;
			playing = false;
		} else {
			m_time = next;
		}
		EmitSignals(m_prevTime, m_time);
	}
	/// Valeur de la piste `track` au temps courant.
	[[nodiscard]] float Value(size_t track) const { return track < tracks.size() ? tracks[track].ValueAt(m_time) : 0.f; }
	[[nodiscard]] float Time() const noexcept { return m_time; }
	[[nodiscard]] float Duration() const noexcept { return m_duration; }
	/// Signaux émis depuis le dernier `TakeSignals` (ou `Tick`).
	[[nodiscard]] std::vector<std::string> TakeSignals() {
		std::vector<std::string> out;
		out.swap(m_fired);
		return out;
	}

private:
	void EmitSignals(float from, float to) {
		const bool forward = to > from;
		for (const AnimSignal &s : signals) {
			const bool crossed = forward ? (s.time > from && s.time <= to)
										 : (to < from ? (s.time > from || s.time <= to) : false);
			if (crossed)
				m_fired.push_back(s.name);
		}
	}
	float m_time = 0.f, m_prevTime = 0.f, m_duration = 0.f;
	std::vector<std::string> m_fired;
};

/// Animation par trames d'une feuille de sprites (grille `columns`×`rows`).
class SpriteSheetAnimation {
public:
	float speed = 12.f; ///< trames par seconde (négatif : à l'envers)
	bool looping = true;
	bool playing = true;
	int columns = 1, rows = 1;
	int frameCount = 1;
	std::vector<AnimSignal> signals; ///< signaux par numéro de trame

	/// Avance de `dt` secondes ; émet les signaux franchis.
	void Tick(float dt) {
		if (!playing)
			return;
		const float prev = m_frame;
		const float total = float(std::max(1, frameCount));
		const float next = m_frame + dt * speed;
		if (looping) {
			m_frame = std::fmod(next, total);
			if (m_frame < 0.f)
				m_frame += total;
		} else if (next >= total) {
			m_frame = total - 1.f;
			playing = false;
		} else if (next <= 0.f) {
			m_frame = 0.f;
			playing = false;
		} else {
			m_frame = next;
		}
		const bool forward = m_frame > prev;
		for (const AnimSignal &s : signals) {
			const bool crossed = forward ? (s.time > prev && s.time <= m_frame)
										 : (m_frame < prev ? (s.time > prev || s.time <= m_frame) : false);
			if (crossed)
				m_fired.push_back(s.name);
		}
	}
	/// Rectangle UV normalisé (0..1) de la trame `frame` (coin haut-gauche, y vers le bas).
	[[nodiscard]] math::FRect FrameUv(int frame) const {
		const int cols = std::max(1, columns);
		const int nrows = std::max(1, rows); // « nrows » : ne pas masquer le membre `rows`
		const int f = frame % (cols * nrows);
		const float w = 1.f / float(cols), h = 1.f / float(nrows);
		return {float(f % cols) * w, float(f / cols) * h, w, h};
	}
	[[nodiscard]] float Frame() const noexcept { return m_frame; }
	[[nodiscard]] std::vector<std::string> TakeSignals() {
		std::vector<std::string> out;
		out.swap(m_fired);
		return out;
	}

private:
	float m_frame = 0.f;
	std::vector<std::string> m_fired;
};

} // namespace sandbox::game
