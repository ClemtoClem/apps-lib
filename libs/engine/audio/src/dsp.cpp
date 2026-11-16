// Définitions de audio/dsp.hpp
#include "audio/dsp.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace audio {

namespace {

constexpr float kTwoPi = 2.f * std::numbers::pi_v<float>;

/// Correction PolyBLEP d'une discontinuité unitaire en phase 0 (`t` : phase [0, 1),
/// `dt` : incrément de phase par échantillon).
float PolyBlep(float t, float dt) noexcept {
	if (t < dt) {
		t /= dt;
		return t + t - t * t - 1.f;
	}
	if (t > 1.f - dt) {
		t = (t - 1.f) / dt;
		return t * t + t + t + 1.f;
	}
	return 0.f;
}

} // namespace

// ── Oscillateur ─────────────────────────────────────────────────────────────

float Oscillator::Next(float sampleRate) noexcept {
	if (!enabled || sampleRate <= 0.f)
		return 0.f;
	const float dt = std::clamp(frequency / sampleRate, 0.f, 0.5f);
	const float t = m_phase;
	float value = 0.f;
	switch (shape) {
	case Waveform::Sine: value = std::sin(kTwoPi * t); break;
	case Waveform::Sawtooth: value = 2.f * t - 1.f - PolyBlep(t, dt); break;
	case Waveform::Square:
	case Waveform::Triangle: {
		float square = t < 0.5f ? 1.f : -1.f;
		square += PolyBlep(t, dt);
		square -= PolyBlep(std::fmod(t + 0.5f, 1.f), dt);
		if (shape == Waveform::Square) {
			value = square;
			break;
		}
		// Triangle : intégrale du carré, avec une légère fuite qui évite toute dérive.
		m_triangle = dt * 4.f * square + (1.f - dt * 0.01f) * m_triangle;
		value = std::clamp(m_triangle, -1.f, 1.f);
		break;
	}
	case Waveform::Noise:
		m_noise ^= m_noise << 13;
		m_noise ^= m_noise >> 17;
		m_noise ^= m_noise << 5;
		value = float(m_noise) * (2.f / 4294967295.f) - 1.f;
		break;
	}
	m_phase += dt;
	if (m_phase >= 1.f)
		m_phase -= 1.f;
	return value * amplitude;
}

void Oscillator::Render(std::span<float> out, float sampleRate, bool add) noexcept {
	for (float &s : out)
		s = add ? s + Next(sampleRate) : Next(sampleRate);
}

void Oscillator::Reset() noexcept {
	m_phase = 0.f;
	m_triangle = 0.f;
}

// ── Analyse spectrale ───────────────────────────────────────────────────────

void ApplyWindow(std::span<float> data, WindowFunction fn) noexcept {
	const size_t n = data.size();
	if (n < 2 || fn == WindowFunction::Rectangular)
		return;
	for (size_t i = 0; i < n; ++i) {
		const float t = kTwoPi * float(i) / float(n - 1);
		float w = 1.f;
		switch (fn) {
		case WindowFunction::Hann: w = 0.5f - 0.5f * std::cos(t); break;
		case WindowFunction::Hamming: w = 0.54f - 0.46f * std::cos(t); break;
		case WindowFunction::Blackman: w = 0.42f - 0.5f * std::cos(t) + 0.08f * std::cos(2.f * t); break;
		case WindowFunction::Rectangular: break;
		}
		data[i] *= w;
	}
}

std::vector<Complex> Fft(std::span<const float> samples) {
	const size_t n = samples.size();
	if (!IsPowerOfTwo(n))
		return {};
	std::vector<Complex> data(samples.begin(), samples.end());
	// Permutation par renversement des bits.
	for (size_t i = 1, j = 0; i < n; ++i) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
			std::swap(data[i], data[j]);
	}
	// Papillons de Cooley-Tukey.
	for (size_t len = 2; len <= n; len <<= 1) {
		const float angle = -kTwoPi / float(len);
		const Complex step(std::cos(angle), std::sin(angle));
		for (size_t i = 0; i < n; i += len) {
			Complex w(1.f, 0.f);
			for (size_t k = 0; k < len / 2; ++k) {
				const Complex u = data[i + k], v = data[i + k + len / 2] * w;
				data[i + k] = u + v;
				data[i + k + len / 2] = u - v;
				w *= step;
			}
		}
	}
	return data;
}

std::vector<float> MagnitudeDb(std::span<const Complex> spectrum, float floorDb) {
	const size_t half = spectrum.size() / 2;
	std::vector<float> out(half);
	// Normalisation : un sinus d'amplitude 1 tombant sur une case vaut n/2 → 0 dB.
	const float scale = spectrum.empty() ? 1.f : 2.f / float(spectrum.size());
	for (size_t i = 0; i < half; ++i)
		out[i] = ToDb(std::abs(spectrum[i]) * scale, floorDb);
	return out;
}

std::vector<float> BinFrequencies(size_t fftSize, float sampleRate) {
	std::vector<float> out(fftSize / 2);
	for (size_t i = 0; i < out.size(); ++i)
		out[i] = float(i) * sampleRate / float(fftSize);
	return out;
}

// ── Niveaux ─────────────────────────────────────────────────────────────────

float Rms(std::span<const float> samples) noexcept {
	if (samples.empty())
		return 0.f;
	double sum = 0.0;
	for (const float s : samples)
		sum += double(s) * double(s);
	return float(std::sqrt(sum / double(samples.size())));
}

float Peak(std::span<const float> samples) noexcept {
	float peak = 0.f;
	for (const float s : samples)
		peak = std::max(peak, std::abs(s));
	return peak;
}

float ToDb(float amplitude, float floorDb) noexcept {
	return amplitude > 0.f ? std::max(floorDb, 20.f * std::log10(amplitude)) : floorDb;
}

// ── Filtres et effets ───────────────────────────────────────────────────────

void BiQuad::Design(BiQuadKind kind, float sampleRate, float cutoffHz, float q) noexcept {
	const float f = std::clamp(cutoffHz, 1.f, sampleRate * 0.49f);
	const float w0 = kTwoPi * f / sampleRate, cw = std::cos(w0), alpha = std::sin(w0) / (2.f * std::max(q, 0.01f));
	float b0 = 0.f, b1 = 0.f, b2 = 0.f;
	switch (kind) {
	case BiQuadKind::LowPass:
		b0 = b2 = (1.f - cw) * 0.5f;
		b1 = 1.f - cw;
		break;
	case BiQuadKind::HighPass:
		b0 = b2 = (1.f + cw) * 0.5f;
		b1 = -(1.f + cw);
		break;
	case BiQuadKind::BandPass:
		b0 = alpha;
		b1 = 0.f;
		b2 = -alpha;
		break;
	}
	const float a0 = 1.f + alpha;
	m_b0 = b0 / a0;
	m_b1 = b1 / a0;
	m_b2 = b2 / a0;
	m_a1 = -2.f * cw / a0;
	m_a2 = (1.f - alpha) / a0;
}

float BiQuad::Step(float x) noexcept {
	const float y = m_b0 * x + m_b1 * m_x1 + m_b2 * m_x2 - m_a1 * m_y1 - m_a2 * m_y2;
	m_x2 = m_x1;
	m_x1 = x;
	m_y2 = m_y1;
	m_y1 = y;
	return y;
}

void BiQuad::Process(std::span<float> samples) noexcept {
	for (float &s : samples)
		s = Step(s);
}

float SoftClip(float x, float threshold) noexcept {
	const float a = std::abs(x);
	if (a <= threshold)
		return x;
	const float headroom = 1.f - threshold;
	const float shaped = threshold + headroom * std::tanh((a - threshold) / std::max(headroom, 1e-6f));
	return x < 0.f ? -shaped : shaped;
}

void SoftClip(std::span<float> samples, float threshold) noexcept {
	for (float &s : samples)
		s = SoftClip(s, threshold);
}

DelayLine::DelayLine(size_t maxSamples) : m_buffer(std::max<size_t>(1, maxSamples) + 1, 0.f) {}

void DelayLine::SetDelay(size_t samples) noexcept { m_delay = std::min(samples, m_buffer.size() - 1); }

float DelayLine::Step(float x) noexcept {
	const size_t read = (m_write + m_buffer.size() - m_delay) % m_buffer.size();
	const float delayed = m_delay == 0 ? x : m_buffer[read];
	m_buffer[m_write] = x + delayed * feedback;
	m_write = (m_write + 1) % m_buffer.size();
	return x * (1.f - mix) + delayed * mix;
}

void DelayLine::Process(std::span<float> samples) noexcept {
	for (float &s : samples)
		s = Step(s);
}

void DelayLine::Clear() noexcept { std::ranges::fill(m_buffer, 0.f); }

RingBuffer::RingBuffer(size_t capacity) : m_data(std::max<size_t>(1, capacity), 0.f) {}

void RingBuffer::Push(std::span<const float> samples) noexcept {
	for (const float s : samples) {
		m_data[m_head] = s;
		m_head = (m_head + 1) % m_data.size();
	}
	m_size = std::min(m_data.size(), m_size + samples.size());
}

void RingBuffer::Latest(std::span<float> out) const noexcept {
	const size_t n = out.size(), cap = m_data.size();
	for (size_t i = 0; i < n; ++i) {
		const size_t age = n - i; // 1 : le plus récent
		out[i] = age <= m_size && age <= cap ? m_data[(m_head + cap - age) % cap] : 0.f;
	}
}

void RingBuffer::Clear() noexcept {
	std::ranges::fill(m_data, 0.f);
	m_head = m_size = 0;
}

} // namespace audio
