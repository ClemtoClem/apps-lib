#pragma once
/**
 * @file dsp.hpp
 * @brief Traitement du signal audio, sans entrée-sortie : oscillateurs, analyse
 *        spectrale, niveaux, filtres, effets. Les flux (lecture, capture) passent par
 *        `sdl3::AudioStream` ; ce module ne connaît que des échantillons `float`.
 */
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace audio {

// ── Oscillateur ─────────────────────────────────────────────────────────────

enum class Waveform : uint8_t { Sine, Square, Triangle, Sawtooth, Noise };

/// Oscillateur à accumulateur de phase. Carré et dent de scie à bande limitée (PolyBLEP :
/// les discontinuités sont lissées sur un échantillon, d'où très peu de repliement pour un
/// coût de quelques opérations), triangle par intégration du carré, bruit blanc xorshift.
class Oscillator {
public:
	Waveform shape = Waveform::Sine;
	float frequency = 440.f; ///< Hz
	float amplitude = 0.5f;	 ///< 0 … 1
	bool enabled = true;

	/// Échantillon suivant à la fréquence d'échantillonnage `sampleRate`.
	[[nodiscard]] float Next(float sampleRate) noexcept;
	/// Remplit `out` (additionne si `add`).
	void Render(std::span<float> out, float sampleRate, bool add = false) noexcept;
	void Reset() noexcept;
	[[nodiscard]] float Phase() const noexcept { return m_phase; }

private:
	float m_phase = 0.f;	///< [0, 1)
	float m_triangle = 0.f; ///< état de l'intégrateur du triangle
	uint32_t m_noise = 0x9E3779B9u;
};

// ── Analyse spectrale ───────────────────────────────────────────────────────

enum class WindowFunction : uint8_t { Rectangular, Hann, Hamming, Blackman };

/// Applique la fenêtre en place (avant une FFT).
void ApplyWindow(std::span<float> data, WindowFunction fn) noexcept;

using Complex = std::complex<float>;

[[nodiscard]] constexpr bool IsPowerOfTwo(size_t n) noexcept { return n != 0 && (n & (n - 1)) == 0; }

/// FFT d'un signal réel (Cooley-Tukey radix-2, itérative) ; taille : puissance de 2,
/// sinon résultat vide.
[[nodiscard]] std::vector<Complex> Fft(std::span<const float> samples);

/// Spectre d'amplitude en dB (0 dB : sinus d'amplitude 1), une valeur par fréquence de
/// [0, Nyquist) : la moitié utile d'une FFT de signal réel. `floorDb` borne le bas.
[[nodiscard]] std::vector<float> MagnitudeDb(std::span<const Complex> spectrum, float floorDb = -120.f);

/// Fréquence centrale (Hz) de chaque valeur rendue par MagnitudeDb.
[[nodiscard]] std::vector<float> BinFrequencies(size_t fftSize, float sampleRate);

// ── Niveaux ─────────────────────────────────────────────────────────────────

[[nodiscard]] float Rms(std::span<const float> samples) noexcept;
[[nodiscard]] float Peak(std::span<const float> samples) noexcept;
/// Amplitude linéaire → dB (−∞ remplacé par `floorDb`).
[[nodiscard]] float ToDb(float amplitude, float floorDb = -120.f) noexcept;

// ── Filtres et effets ───────────────────────────────────────────────────────

enum class BiQuadKind : uint8_t { LowPass, HighPass, BandPass };

/// Filtre biquad RBJ (« Audio EQ Cookbook », forme directe I).
class BiQuad {
public:
	/// Recalcule les coefficients sans toucher à la ligne de retard (balayage sans clic).
	void Design(BiQuadKind kind, float sampleRate, float cutoffHz, float q = 0.7071f) noexcept;
	[[nodiscard]] float Step(float x) noexcept;
	void Process(std::span<float> samples) noexcept;
	void Reset() noexcept { m_x1 = m_x2 = m_y1 = m_y2 = 0.f; }

private:
	float m_b0 = 1.f, m_b1 = 0.f, m_b2 = 0.f, m_a1 = 0.f, m_a2 = 0.f;
	float m_x1 = 0.f, m_x2 = 0.f, m_y1 = 0.f, m_y2 = 0.f;
};

/// Écrêtage doux : transparent sous `threshold`, coude en tanh au-dessus.
[[nodiscard]] float SoftClip(float x, float threshold = 0.8f) noexcept;
void SoftClip(std::span<float> samples, float threshold = 0.8f) noexcept;

/// Ligne de retard (écho) : retard en échantillons, réinjection, mélange sec / mouillé.
class DelayLine {
public:
	explicit DelayLine(size_t maxSamples = 1);
	void SetDelay(size_t samples) noexcept;
	[[nodiscard]] size_t Delay() const noexcept { return m_delay; }
	float feedback = 0.f; ///< 0 … < 1
	float mix = 1.f;	  ///< 0 : sec, 1 : uniquement le signal retardé
	[[nodiscard]] float Step(float x) noexcept;
	void Process(std::span<float> samples) noexcept;
	void Clear() noexcept;

private:
	std::vector<float> m_buffer;
	size_t m_write = 0, m_delay = 0;
};

/// Tampon circulaire d'analyse : garde les `capacity` derniers échantillons.
class RingBuffer {
public:
	explicit RingBuffer(size_t capacity);
	void Push(std::span<const float> samples) noexcept;
	/// Copie les `out.size()` derniers échantillons, du plus ancien au plus récent
	/// (zéros devant si le tampon n'en contient pas encore assez).
	void Latest(std::span<float> out) const noexcept;
	[[nodiscard]] size_t Capacity() const noexcept { return m_data.size(); }
	[[nodiscard]] size_t Size() const noexcept { return m_size; }
	void Clear() noexcept;

private:
	std::vector<float> m_data;
	size_t m_head = 0, m_size = 0;
};

} // namespace audio
