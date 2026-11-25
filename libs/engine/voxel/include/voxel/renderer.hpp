#pragma once
/**
 * @file renderer.hpp
 * @brief Rendu des chunks voxel sur SDL_GPU (wrapper `sdl3::Gpu*`).
 *
 * - Une paire de tampons de sommets par chunk (opaque/découpé, translucide) ;
 *   tampon d'index de quads partagé (quad i → sommets 4i..4i+3).
 * - Téléversements groupés : un seul tampon de transfert et une seule passe de
 *   copie par image, plafonnés en octets pour éviter les à-coups.
 * - Élimination par frustum (AABB de chunk), opaques triés de l'avant vers
 *   l'arrière (early-Z), translucides de l'arrière vers l'avant.
 * - Texture-tableau des blocs (`kTextureSize`², mipmaps), échantillonnage au plus
 *   proche avec niveau de détail fonction de la distance (un mip de plus tous les
 *   `lodInterval` blocs).
 * - Boîtes instanciées (entités) et contour filaire (bloc visé) en un appel chacun.
 */
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <core/robin_map.hpp>
#include <math/math.hpp>
#include <sdl3/render/gpu.hpp>

#include "mesher.hpp" // ChunkVertex, DetailVertex
#include "model.hpp"  // ModelVertex

namespace voxel {

/// Caméra à la première personne (lacet / tangage), pour le monde voxel.
struct Camera {
	math::FVector3 position{0, 80, 0};
	float yaw = 0.f;   // radians, 0 = -Z
	float pitch = 0.f; // radians
	float fovY = 70.f * math::DEG2RAD;
	float zNear = 0.1f;
	float zFar = 1000.f;

	[[nodiscard]] math::FVector3 Forward() const noexcept { return math::DirectionFromYawPitch(yaw, pitch); }
	[[nodiscard]] math::FMatrix4 View() const noexcept {
		return math::FMatrix4::LookAt(position, position + Forward(), {0, 1, 0});
	}
	[[nodiscard]] math::FMatrix4 ViewProjection(float aspect) const noexcept {
		return math::FMatrix4::Perspective(fovY, aspect, zNear, zFar) * View();
	}
};

/// Lumière ponctuelle dynamique (entité porteuse d'un `LightEmitter`).
struct PointLight {
	enum class Type : uint32_t { Point = 0, Spot = 1, Directional = 2 };
	math::FVector3 position{};
	float range = 8.f;
	math::FVector3 color{1.f, 1.f, 1.f};
	float intensity = 1.f;
	Type type = Type::Point;
	math::FVector3 direction{0.f, -1.f, 0.f}; // spot / directionnelle
	float innerCos = 0.75f;					   // spot : cos du demi-angle intérieur
	float outerCos = 0.5f;					   // spot : cos du demi-angle extérieur
	float radius = 0.5f;					   // directionnelle : rayon du faisceau
};
inline constexpr size_t kMaxPointLights = 16;

struct BoxInstance {
	math::FVector3 center;
	math::FVector3 halfExtents;
	uint8_t r, g, b, a;
};

/// Sommet des modèles texturés (skin du joueur) : `voxel::ModelVertex` (model.hpp).

/// Un modèle texturé à dessiner (poignée issue de `UploadModel` + matrice monde).
struct ModelDraw {
	uint64_t model = 0; // poignée de `UploadModel` ; 0 : ignoré
	math::FMatrix4 transform; // matrice monde
};

/// Résolution (carrée) des textures de blocs dans la texture-tableau. Chaque face de
/// bloc 1×1 occupe toute la texture (ratio 1:1, aucune rognure) ; une texture source
/// plus grande donne plus de détail. Les mipmaps fournissent le niveau de détail.
inline constexpr int kTextureSize = 128;
/// Nombre de niveaux de mipmaps : 128 → 64 → … → 1.
inline constexpr int kTextureMipLevels = 8;

struct SceneParams {
	math::FVector4 skyColor{0.55f, 0.74f, 0.95f, 1.f};
	float fogStart = 120.f;
	float fogEnd = 240.f;
	float daylight = 1.f;
	bool wireframeOutline = true;
	/// Distance (en blocs) entre deux niveaux de détail : au-delà de `lodInterval` on
	/// saute un pixel de plus, puis encore un à 2×`lodInterval`, etc.
	float lodInterval = 16.f;
	/// Tête de la caméra dans un bloc opaque : n'affiche que du noir.
	bool blackout = false;
	/// Intensité des réflexions spéculaires (nombre de rebonds d'un rayon lumineux).
	float reflectionBounces = 2.f;
	std::vector<PointLight> lights; // au plus kMaxPointLights (les plus proches de la caméra)
};

/// Paramètres du post-traitement plein écran (effets de statut / atmosphère).
struct PostParams {
	math::FVector4 effects{0.f, 0.f, 0.f, 0.f}; // x = sous-marin, y = chaleur, z = éblouissement, w = étourdissement
	math::FVector4 effects2{0.f, 0.f, 0.f, 0.f}; // x = asphyxie, y = temps, z = largeur, w = hauteur
};

struct RenderStats {
	size_t chunksResident = 0;
	size_t chunksDrawn = 0;
	size_t drawCalls = 0;
	size_t verticesDrawn = 0;
	size_t gpuBytes = 0;
	size_t uploadedBytes = 0;
};

class Renderer {
public:
	[[nodiscard]] static std::unique_ptr<Renderer> Create(sdl3::GpuDevice &device);
	~Renderer();

	/// Téléverse la texture-tableau des blocs : `layers` images RGBA8 `kTextureSize`²
	/// consécutives (mipmaps générées automatiquement).
	bool SetBlockTextures(std::span<const uint8_t> rgba, uint32_t layers);

	/// Programme le (re)téléversement d'un maillage de chunk (pris en charge au prochain Render).
	void QueueChunkMesh(uint64_t key, const math::IVec3 &origin, std::vector<ChunkVertex> opaque, std::vector<ChunkVertex> translucent,
						std::vector<DetailVertex> detail = {});
	void RemoveChunk(uint64_t key);
	void Clear();

	/// Enregistre les passes de copie puis de rendu dans le tampon de commandes de l'image,
	/// vers les cibles `color`/`depth` (`aspect` = largeur / hauteur).
	void Render(sdl3::GpuCommandBuffer &cmd, sdl3::GpuTexture &color, sdl3::GpuTexture &depth, float aspect, const Camera &camera,
				const SceneParams &params, std::span<const BoxInstance> boxes, const BoxInstance *outline);

	/// Rendu de la scène (mêmes maillages de chunks déjà téléversés) vers des
	/// cibles couleur/profondeur arbitraires — viewport embarqué, éditeur….
	/// `cmd` doit être un tampon de commandes ouvert (non soumis) ; le tampon
	/// d'instances de boîtes est (re)créé à la demande.
	void RenderPass(sdl3::GpuCommandBuffer &cmd, SDL_GPUTexture *color, SDL_GPUTexture *depth, float aspect, const Camera &camera,
					const SceneParams &params, std::span<const BoxInstance> boxes, const BoxInstance *outline);

	[[nodiscard]] const RenderStats &Stats() const noexcept { return m_stats; }
	void SetUploadBudget(size_t bytesPerFrame) noexcept { m_uploadBudget = bytesPerFrame; }
	/// Format de profondeur des pipelines (les cibles des viewports doivent l'utiliser).
	[[nodiscard]] SDL_GPUTextureFormat DepthFormat() const noexcept { return m_depthFormat; }

	/// Téléverse la texture de skin (RGBA8, dimensions arbitraires) du modèle du joueur.
	bool SetSkinTexture(const uint8_t *rgba, int w, int h);
	/// Téléverse la grille d'opacité (taille³ octets, 0 vide → 255 opaque) pour les
	/// ombres portées : les parties transparentes laissent passer une partie de la lumière.
	bool SetShadowVolume(std::span<const uint8_t> opacity, uint32_t size, const math::FVector3 &center);
	/// Téléverse le maillage du modèle (liste de triangles) — par ex. le bipède du joueur.
	bool SetModelMesh(std::span<const ModelVertex> vertices);
	/// Rend le modèle texturé dans `color`/`depth` (charge la profondeur existante),
	/// transformé par `model` (matrice monde). `cmd` doit être un tampon ouvert.
	void RenderModel(sdl3::GpuCommandBuffer &cmd, SDL_GPUTexture *color, SDL_GPUTexture *depth, float aspect, const Camera &camera,
					 const SceneParams &params, const math::FMatrix4 &model);
	/// Crée (ou retrouve) un modèle texturé nommé et téléverse sa texture ; retourne sa
	/// poignée (0 si vide). La texture n'est téléversée qu'une fois par clé (le jeu fait
	/// varier la clé quand l'image change).
	uint64_t UploadModel(std::string_view key, std::span<const uint8_t> rgba, int w, int h);
	/// Re-téléverse le maillage d'un modèle (appelé à chaque image si la pose change).
	bool UpdateModelMesh(uint64_t handle, std::span<const ModelVertex> vertices);
	/// Libère un modèle (poignée) : maillage et texture.
	void ReleaseModel(uint64_t handle);
	/// Rend plusieurs modèles texturés (matrice monde chacun) dans `color`/`depth`.
	void RenderModels(sdl3::GpuCommandBuffer &cmd, SDL_GPUTexture *color, SDL_GPUTexture *depth, float aspect, const Camera &camera,
					  const SceneParams &params, std::span<const ModelDraw> draws);
	/// Applique le post-traitement : lit `scene`, écrit `out` (plein écran).
	void PostProcess(sdl3::GpuCommandBuffer &cmd, SDL_GPUTexture *scene, SDL_GPUTexture *out, const PostParams &params);

private:
	Renderer() = default;
	bool Init(sdl3::GpuDevice &device);
	void FlushUploads(sdl3::GpuCommandBuffer &cmd);

	struct ChunkGpu {
		math::IVec3 origin;
		sdl3::GpuBuffer opaque, translucent, detail;
		uint32_t opaqueQuads = 0, translucentQuads = 0, detailQuads = 0;
	};
	struct PendingUpload {
		uint64_t key;
		math::IVec3 origin;
		std::vector<ChunkVertex> opaque, translucent;
		std::vector<DetailVertex> detail;
	};

	SDL_GPUDevice *m_device = nullptr;
	sdl3::GpuDevice *m_dev = nullptr; // appartient au device
	SDL_GPUTextureFormat m_depthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
	sdl3::GpuGraphicsPipeline m_opaque, m_translucent, m_boxes, m_lines, m_detail;
	sdl3::GpuGraphicsPipeline m_model;				   // modèle texturé (skin du joueur)
	sdl3::GpuGraphicsPipeline m_post;				   // post-traitement plein écran
	sdl3::GpuBuffer m_quadIndices, m_cubeVertices, m_lineVertices, m_instances;
	uint32_t m_instanceCapacity = 0;
	sdl3::GpuBuffer m_modelVertices; // maillage du modèle
	uint32_t m_modelVertexCount = 0;
	struct ModelGpu {
		sdl3::GpuBuffer vertices;
		uint32_t vertexCount = 0;
		sdl3::GpuTexture texture;
	};
	core::RobinMap<std::string, uint64_t, core::Hasher<std::string>> m_modelKeys; // clé → poignée
	core::RobinMap<uint64_t, ModelGpu> m_models;								  // poignée → modèle texturé
	uint64_t m_nextModel = 1;
	sdl3::GpuTexture m_blockTextures;
	sdl3::GpuTexture m_skinTexture; // texture de skin 2D
	sdl3::GpuSampler m_sampler, m_skinSampler;
	sdl3::GpuTexture m_shadowVolume; // grille d'opacité 3D (ombres portées)
	sdl3::GpuSampler m_shadowSampler;
	uint32_t m_shadowSize = 0;		 // 0 : pas de volume d'ombres
	math::FVector3 m_shadowCenter{};
	core::RobinMap<uint64_t, ChunkGpu> m_chunks;
	std::vector<PendingUpload> m_pending;
	size_t m_uploadBudget = 16u << 20;
	RenderStats m_stats;
	static constexpr uint32_t kMaxQuads = 1u << 17;
};

} // namespace voxel
