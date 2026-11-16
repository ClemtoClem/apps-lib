#pragma once
/**
 * @file renderer3d.hpp
 * @brief Rendu 3D sur SDL_GPU (SPIR-V embarqué), intégré au wrapper `sdl3`.
 *
 * Chaque image, l'application empile ses dessins puis appelle `Render`, qui :
 *  1. téléverse toutes les instances et tous les segments de l'image en une passe de copie ;
 *  2. rend la carte d'ombre du soleil (projection orthographique, PCF 3×3) ;
 *  3. dessine le ciel, les maillages opaques, les segments puis les transparents (triés
 *     de l'arrière vers l'avant) dans la `RenderTarget3D`.
 * Tous les maillages sont instanciés : un `Draw` est une instance, `DrawInstances` en
 * dessine des milliers en un appel. La cible s'affiche sans copie dans le renderer 2D
 * créé sur le même device (`app::AppConfig::gpu`, `RenderTarget3D::View`).
 *
 * ```
 * auto r3d = sdl3::Renderer3D::Create(*Gpu());
 * const sdl3::MeshId cube = r3d->Upload(sdl3::MeshData::Cube());
 * // OnOffscreen :
 * target.Resize(*r3d, Renderer(), w, h);
 * r3d->Draw(cube, transform.Matrix(), sdl3::Material::Plastic(sdl3::Srgb(0x4a90ff)));
 * auto cmd = Gpu()->AcquireCommandBuffer();
 * r3d->Render(cmd, target, camera, lighting);
 * (void)cmd.Submit();
 * // OnRender : Canvas().Image(*target.View(), area);
 * ```
 */
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <math/math.hpp>
#include <sdl3/render/gpu.hpp>
#include <sdl3/render/render.hpp>

#include "sdl3/render/mesh3d.hpp"
#include "sdl3/render/scene3d.hpp"

namespace sdl3 {

class Renderer3D;

using MeshId = uint32_t; // 0 : invalide

/// Une instance de maillage : matrice monde et teinte (multipliée par la couleur du matériau).
struct Instance {
	math::FMatrix4 model = math::FMatrix4::Identity();
	math::FVector4 tint{1.f, 1.f, 1.f, 1.f};
};
static_assert(sizeof(Instance) == 80);

/// Extrémité de segment : position du monde, couleur sRGB 8 bits.
struct LineVertex {
	math::FVector3 position;
	uint8_t r, g, b, a;
};
static_assert(sizeof(LineVertex) == 16);

/// Bloc d'éclairage tel que lu par les shaders (std140, uniquement des vec4).
struct LightBlock {
	math::FVector4 cameraPos, sunDir, sunColor, skyZenith, skyHorizon, groundColor, fogColor, counts;
	math::FVector4 pointPos[kMaxPointLights], pointColor[kMaxPointLights];
	math::FVector4 spotPos[kMaxSpotLights], spotDir[kMaxSpotLights], spotColor[kMaxSpotLights];
};

/// Remplit le bloc d'éclairage (lumières en surnombre ignorées, directions normalisées).
[[nodiscard]] LightBlock PackLights(const Lighting &lighting, const math::FVector3 &cameraPos, bool shadow, float shadowTexel) noexcept;

struct RenderStats {
	uint32_t drawCalls = 0; // toutes passes (ombre comprise)
	uint32_t instances = 0; // image principale
	uint64_t triangles = 0;
	uint32_t lines = 0;
};

/// Cible couleur + profondeur, et sa vue dans le renderer 2D.
class RenderTarget3D {
public:
	/// (Re)crée les textures si la taille change ; `view2d` : renderer 2D (créé sur le même
	/// device) qui affichera la cible, nullptr pour un rendu hors écran seulement.
	/// Faux si la taille est nulle ou en cas d'échec.
	bool Resize(Renderer3D &renderer, sdl3::Renderer *view2d, int width, int height);
	[[nodiscard]] bool Ready() const noexcept { return m_color && m_depth; }
	[[nodiscard]] int Width() const noexcept { return m_width; }
	[[nodiscard]] int Height() const noexcept { return m_height; }
	[[nodiscard]] float Aspect() const noexcept { return m_height > 0 ? float(m_width) / float(m_height) : 1.f; }
	[[nodiscard]] sdl3::GpuTexture &Color() noexcept { return m_color; }
	[[nodiscard]] sdl3::GpuTexture &Depth() noexcept { return m_depth; }
	/// Texture du renderer 2D affichant la cible (nullptr tant que la cible n'existe pas).
	[[nodiscard]] sdl3::Texture *View() noexcept { return m_view ? &*m_view : nullptr; }
	/// Relit la couleur (RGBA, ligne par ligne depuis le haut) ; attend la fin du GPU. Vide en cas d'échec.
	[[nodiscard]] std::vector<uint8_t> ReadPixels(sdl3::GpuDevice &device);

private:
	sdl3::GpuTexture m_color, m_depth;
	std::optional<sdl3::Texture> m_view;
	int m_width = 0, m_height = 0;
};

class Renderer3D {
public:
	static constexpr SDL_GPUTextureFormat kColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

	/// Crée les pipelines, la carte d'ombre (`shadowMapSize`²) et la texture blanche ;
	/// nullptr (erreur journalisée) si le device ne les accepte pas.
	[[nodiscard]] static std::unique_ptr<Renderer3D> Create(sdl3::GpuDevice &device, uint32_t shadowMapSize = 2048);
	~Renderer3D();
	Renderer3D(const Renderer3D &) = delete;
	Renderer3D &operator=(const Renderer3D &) = delete;

	/// Envoie un maillage au GPU ; 0 en cas d'échec.
	[[nodiscard]] MeshId Upload(const MeshData &mesh);
	/// Remplace la géométrie d'un maillage existant (édition en direct) ; faux en cas d'échec.
	bool Replace(MeshId id, const MeshData &mesh);
	/// Libère un maillage ; son identifiant sera réutilisé par un prochain Upload.
	void Release(MeshId id);
	/// Boîte englobante du maillage (repère de l'objet).
	[[nodiscard]] math::FAABB Bounds(MeshId mesh) const noexcept;
	/// Texture RGBA 8 bits (`width`×`height`×4 octets), mipmaps, répétée ; 0 en cas d'échec.
	[[nodiscard]] TextureId CreateTexture(std::span<const uint8_t> rgba, uint32_t width, uint32_t height);

	// ── Dessins de l'image (consommés par Render) ────────────────────────
	void Draw(MeshId mesh, const math::FMatrix4 &model, const Material &material, const math::FVector4 &tint = {1.f, 1.f, 1.f, 1.f});
	void DrawInstances(MeshId mesh, const Material &material, std::span<const Instance> instances);
	void Line(const math::FVector3 &a, const math::FVector3 &b, uint8_t r, uint8_t g, uint8_t bl, uint8_t alpha = 255);
	/// Segment pointillé : traits de `dash`, espaces de `gap` (unités du monde).
	void DashedLine(const math::FVector3 &a, const math::FVector3 &b, float dash, float gap, uint8_t r, uint8_t g, uint8_t bl, uint8_t alpha = 255);
	/// Grille au sol (plan Y = `y`) de `halfSize` de demi-côté, une ligne tous les `step`.
	void Grid(float halfSize, float step, float y = 0.f, uint8_t gray = 110, uint8_t alpha = 90);
	/// Repère X (rouge), Y (vert), Z (bleu) de longueur `length` en `origin`.
	void Axes(const math::FVector3 &origin, float length);
	/// Arêtes d'une boîte transformée.
	void WireBox(const math::FAABB &box, const math::FMatrix4 &model, uint8_t r, uint8_t g, uint8_t b);

	/// Rend les dessins empilés dans `target` (passes enregistrées dans `cmd`), puis vide la pile.
	void Render(sdl3::GpuCommandBuffer &cmd, RenderTarget3D &target, const Camera3D &camera, const Lighting &lighting);
	/// Abandonne les dessins empilés (image sautée).
	void Clear() noexcept;

	[[nodiscard]] const RenderStats &Stats() const noexcept { return m_stats; }
	[[nodiscard]] sdl3::GpuDevice &Device() noexcept { return *m_device; }
	[[nodiscard]] SDL_GPUTextureFormat DepthFormat() const noexcept { return m_depthFormat; }

private:
	struct GpuMesh {
		sdl3::GpuBuffer vertices, indices;
		uint32_t indexCount = 0;
		math::FAABB bounds{};
	};
	struct DrawItem {
		MeshId mesh = 0;
		Material material;
		uint32_t firstInstance = 0, instanceCount = 0;
		float depth = 0.f; // distance à la caméra (tri des transparents)
	};

	Renderer3D() = default;
	bool Init(sdl3::GpuDevice &device, uint32_t shadowMapSize);
	bool UploadFrameData(sdl3::GpuCommandBuffer &cmd);
	void DrawItems(sdl3::GpuRenderPass &pass, sdl3::GpuCommandBuffer &cmd, std::span<const DrawItem *const> items, bool shadowPass);

	sdl3::GpuDevice *m_device = nullptr;
	SDL_GPUTextureFormat m_depthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
	sdl3::GpuGraphicsPipeline m_opaque, m_opaqueTwoSided, m_transparent, m_shadow, m_sky, m_lines;
	sdl3::GpuSampler m_linear, m_shadowSampler;
	sdl3::GpuTexture m_shadowMap;
	uint32_t m_shadowSize = 0;
	std::vector<GpuMesh> m_meshes;		   // MeshId - 1
	std::vector<sdl3::GpuTexture> m_textures; // TextureId (0 : blanche)

	std::vector<DrawItem> m_items;
	std::vector<Instance> m_instances;
	std::vector<LineVertex> m_lineVertices;
	sdl3::GpuBuffer m_instanceBuffer, m_lineBuffer;
	sdl3::GpuTransferBuffer m_staging;
	uint32_t m_instanceCapacity = 0, m_lineCapacity = 0, m_stagingCapacity = 0;
	RenderStats m_stats;
};

} // namespace sdl3
