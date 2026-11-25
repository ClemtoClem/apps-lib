#pragma once
/**
 * @file chunk.hpp
 * @brief Chunk : tableau plat 1D de 32³ `BlockId`, liste des entités qu'il contient
 *        (grille spatiale) et état de cycle de vie.
 */
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <ecs/ecs.hpp>

#include "types.hpp"

namespace voxel {

enum class ChunkState : uint8_t {
	Loading,   // requête DB / génération en cours (thread du pool)
	Ready,	   // voxels disponibles
	Unloading, // sauvegarde asynchrone en cours
};

class Chunk {
public:
	explicit Chunk(const IVec3 &coord) : m_coord(coord), m_key(MakeChunkKey(coord)) {}

	[[nodiscard]] const IVec3 &Coord() const noexcept { return m_coord; }
	[[nodiscard]] ChunkKey Key() const noexcept { return m_key; }
	[[nodiscard]] IVec3 Origin() const noexcept { return m_coord * kChunkSize; }

	// ── Voxels : tableau plat 1D, ou valeur uniforme sans allocation ──────
	[[nodiscard]] BlockId Get(int x, int y, int z) const noexcept {
		return m_voxels ? m_voxels[static_cast<size_t>(VoxelIndex(x, y, z))] : m_fill;
	}
	void Set(int x, int y, int z, BlockId b) noexcept { Set(x, y, z, b, 0); }
	void Set(int x, int y, int z, BlockId b, uint8_t orient) noexcept {
		if (!m_voxels) {
			if (b == m_fill && orient == 0)
				return;
			Materialize();
		}
		const size_t i = static_cast<size_t>(VoxelIndex(x, y, z));
		BlockId &slot = m_voxels[i];
		m_nonAir += static_cast<uint32_t>(b != kAir) - static_cast<uint32_t>(slot != kAir);
		slot = b;
		if (orient != 0) {
			if (!m_orient) // initialisé à 0 : les autres voxels gardent l'orientation par défaut
				m_orient = std::make_unique<uint8_t[]>(kChunkVolume);
			m_orient[i] = orient;
		} else if (m_orient) {
			m_orient[i] = 0;
		}
		++m_version;
		m_modified = true;
	}
	[[nodiscard]] uint8_t GetOrient(int x, int y, int z) const noexcept {
		return m_orient ? m_orient[static_cast<size_t>(VoxelIndex(x, y, z))] : uint8_t{0};
	}
	/// Alloue le tableau plat (rempli avec la valeur uniforme) et le retourne.
	std::span<BlockId> Materialize() {
		if (!m_voxels) {
			m_voxels = std::make_unique_for_overwrite<BlockId[]>(kChunkVolume);
			std::fill_n(m_voxels.get(), kChunkVolume, m_fill);
		}
		return {m_voxels.get(), kChunkVolume};
	}
	/// Libère le tableau si tous les voxels sont identiques (ciel, roche pleine).
	void Compact() noexcept {
		if (!m_voxels || m_orient)
			return; // orientations par voxel : le chunk n'est pas uniforme
		const BlockId first = m_voxels[0];
		if (std::all_of(m_voxels.get(), m_voxels.get() + kChunkVolume, [first](BlockId b) { return b == first; })) {
			m_voxels.reset();
			m_fill = first;
		}
		RecountNonAir();
	}
	void Fill(BlockId b) noexcept {
		m_voxels.reset();
		m_orient.reset();
		m_fill = b;
		RecountNonAir();
	}
	/// Adopte un tableau plat déjà rempli (désérialisation, génération).
	void Adopt(std::unique_ptr<BlockId[]> voxels) noexcept {
		m_voxels = std::move(voxels);
		m_orient.reset();
		Compact();
	}
	[[nodiscard]] bool IsUniform() const noexcept { return !m_voxels; }
	[[nodiscard]] BlockId UniformValue() const noexcept { return m_fill; }
	/// Copie les voxels dans `out` (kChunkVolume éléments).
	void CopyTo(std::span<BlockId> out) const noexcept {
		if (m_voxels)
			std::copy_n(m_voxels.get(), kChunkVolume, out.begin());
		else
			std::fill_n(out.begin(), kChunkVolume, m_fill);
	}
	[[nodiscard]] const BlockId *RawVoxels() const noexcept { return m_voxels.get(); }
	[[nodiscard]] const uint8_t *RawOrients() const noexcept { return m_orient.get(); }
	[[nodiscard]] bool HasOrients() const noexcept { return m_orient != nullptr; }
	/// Copie les orientations dans `out` (kChunkVolume éléments, 0 par défaut).
	void CopyOrientsTo(std::span<uint8_t> out) const noexcept {
		if (m_orient)
			std::copy_n(m_orient.get(), kChunkVolume, out.begin());
		else
			std::fill_n(out.begin(), kChunkVolume, uint8_t{0});
	}
	/// Adopte un tableau d'orientations (0 = identité) ; libère si tout est nul.
	void AdoptOrients(std::unique_ptr<uint8_t[]> orients) noexcept {
		m_orient = std::move(orients);
		if (m_orient && std::all_of(m_orient.get(), m_orient.get() + kChunkVolume, [](uint8_t v) { return v == 0; }))
			m_orient.reset();
	}

	[[nodiscard]] bool IsEmpty() const noexcept { return m_nonAir == 0; }
	[[nodiscard]] uint32_t NonAirCount() const noexcept { return m_nonAir; }
	void RecountNonAir() noexcept {
		if (m_voxels)
			m_nonAir = static_cast<uint32_t>(std::count_if(m_voxels.get(), m_voxels.get() + kChunkVolume, [](BlockId b) { return b != kAir; }));
		else
			m_nonAir = m_fill == kAir ? 0 : kChunkVolume;
		++m_version;
	}
	[[nodiscard]] size_t MemoryBytes() const noexcept {
		return sizeof(*this) + (m_voxels ? kChunkVolume * sizeof(BlockId) : 0) + (m_orient ? kChunkVolume : 0);
	}

	[[nodiscard]] uint32_t Version() const noexcept { return m_version; }
	[[nodiscard]] bool Modified() const noexcept { return m_modified; }
	void SetModified(bool m) noexcept { m_modified = m; }

	ChunkState state = ChunkState::Loading;
	bool meshDirty = true;		  // à remailler
	bool meshPending = false;	  // maillage en cours sur le pool
	bool fromDisk = false;		  // chargé depuis la base (sinon généré)

	// ── Grille spatiale d'entités ────────────────────────────────────────
	/// Entités actuellement situées dans ce chunk (ordre quelconque, retrait O(1)).
	std::vector<ecs::Entity> entities;

private:
	IVec3 m_coord;
	ChunkKey m_key;
	std::unique_ptr<BlockId[]> m_voxels;
	std::unique_ptr<uint8_t[]> m_orient; // orientation par voxel (null = tout 0)
	BlockId m_fill = kAir;
	uint32_t m_nonAir = 0;
	uint32_t m_version = 1;
	bool m_modified = false;
};

} // namespace voxel
