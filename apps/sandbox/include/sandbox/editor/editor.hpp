#pragma once
/**
 * @file editor.hpp
 * @brief Logique d'édition du sandbox, sans fenêtre ni GPU : contexte d'édition (géométrie
 *        libre ou groupe ouvert), outil courant, sélections, boîte de mesures, commandes et
 *        annulation. L'interface lui transmet des rayons du monde et un projecteur ; elle lit
 *        en retour l'inférence, l'aperçu et les messages à afficher.
 *
 * Les outils du noyau (modeling::Tool) travaillent dans le repère LOCAL du modèle actif : le
 * rayon est ramené dans ce repère et le projecteur composé avec la transformation du groupe.
 * Au niveau racine, l'éditeur traite lui-même les groupes : sélection d'un bloc, déplacement,
 * peinture et gomme du groupe entier.
 */
#include <functional>
#include <memory>
#include <vector>

#include <sdl3/render/scene3d.hpp>

#include "sandbox/document/document.hpp"
#include "sandbox/modeling/inference.hpp"
#include "sandbox/modeling/tools.hpp"

namespace sandbox {

/// Position de la souris vue par l'éditeur (repère du monde).
struct Pointer {
	math::FRay ray;			   ///< rayon du monde (direction unitaire)
	math::FVector2 cursor;	   ///< pixels
	modeling::Projector project; ///< point du monde → pixels (NONE derrière la caméra)
	bool shift = false, ctrl = false;
};

/// Vues normalisées (menu Caméra).
enum class StandardView : uint8_t { Iso, Top, Bottom, Front, Back, Left, Right };

/// Alignement des groupes sélectionnés sur leur bord / centre commun.
/// `int(a) / 3` = axe (0 X, 1 Y, 2 Z) ; `int(a) % 3` = bord bas / centre / bord haut.
enum class Align3D : uint8_t { Left, CenterX, Right, Bottom, CenterY, Top, Back, CenterZ, Front };

/// Caméra de navigation : orbite autour d'un pivot, déplacement, zoom vers le curseur,
/// projection perspective ou parallèle.
class NavCamera {
public:
	NavCamera();
	[[nodiscard]] const sdl3::Camera3D &Camera() const noexcept { return m_camera; }
	void Orbit(float dxPixels, float dyPixels);
	void Pan(float dxPixels, float dyPixels, float viewportHeight);
	/// Molette : rapproche (`steps` > 0) vers le point du monde visé par le curseur.
	void ZoomAt(float steps, const math::FVector3 &focus);
	/// Cadre une boîte (vue courante conservée).
	void ZoomExtents(const math::FAABB &box, float aspect);
	void SetView(StandardView view, const math::FAABB &box, float aspect);
	void SetOrthographic(bool on);
	[[nodiscard]] bool Orthographic() const noexcept { return m_camera.orthographic; }
	void SetFov(float degrees);
	[[nodiscard]] SceneView Save(String name) const;
	void Restore(const SceneView &view);
	[[nodiscard]] math::FVector3 Target() const noexcept { return m_orbit.target; }

private:
	void Apply();
	sdl3::OrbitController m_orbit;
	sdl3::Camera3D m_camera;
};

class Editor {
public:
	Editor();

	// ── Document ─────────────────────────────────────────────────────────
	[[nodiscard]] Document &Doc() noexcept { return m_doc; }
	[[nodiscard]] const Document &Doc() const noexcept { return m_doc; }
	[[nodiscard]] const History &Undos() const noexcept { return m_history; }
	/// Remplace le document (nouveau, ouvert) : historique, sélections et outil réinitialisés.
	void Reset(Document doc);
	[[nodiscard]] bool Modified() const noexcept { return m_modified; }
	void MarkSaved() noexcept { m_modified = false; }

	// ── Contexte d'édition ───────────────────────────────────────────────
	[[nodiscard]] EntityId Context() const noexcept { return m_context; }
	[[nodiscard]] modeling::Model &ActiveModel();
	[[nodiscard]] math::FMatrix4 ActiveTransform() const { return m_doc.TransformOf(m_context); }
	bool OpenGroup(EntityId id);
	/// Sort du groupe ouvert : vers son parent (groupe imbriqué) ou la géométrie libre.
	void CloseGroup();
	/// Revient à la géométrie libre, quel que soit le niveau ouvert.
	void CloseAllGroups();
	/// Ouvre les ancêtres d'un groupe (du plus haut au parent) puis le sélectionne : un nœud
	/// choisi dans l'arborescence devient éditable à son niveau.
	void Reveal(EntityId id);
	/// Change le parent d'un groupe (0 : racine), un pas d'annulation ; faux si impossible.
	bool Reparent(EntityId id, EntityId parent);
	/// Groupe sous le rayon (le plus proche, visible, non verrouillé) ; 0 sinon.
	[[nodiscard]] EntityId PickEntity(const math::FRay &worldRay, float *distance = nullptr) const;
	/// Rien sous le rayon à la racine (ni groupe, ni face libre) — pour la sélection par cadre.
	[[nodiscard]] bool EmptyAt(const math::FRay &worldRay) const;

	// ── Outils ───────────────────────────────────────────────────────────
	void SetTool(modeling::ToolKind kind);
	[[nodiscard]] modeling::ToolKind ToolKind() const noexcept { return m_tool->Kind(); }
	[[nodiscard]] bool ToolBusy() const;
	void Hover(const Pointer &p);
	void Click(const Pointer &p);
	/// Échap : vide la saisie, sinon abandonne l'opération, sinon revient à la sélection.
	void Escape();
	[[nodiscard]] const modeling::Inference &Hovered() const noexcept { return m_hover; }
	[[nodiscard]] modeling::ToolPreview Preview() const;
	/// Déplacement en cours d'un groupe (aperçu) : décalage du monde.
	[[nodiscard]] Option<math::FVector3> EntityMoveOffset() const noexcept;
	uint32_t material = 0; ///< matériau de la peinture et des nouvelles faces
	modeling::InferenceSettings inference;

	// ── Boîte de mesures (saisie au clavier pendant un outil) ────────────
	[[nodiscard]] const String &MeasureInput() const noexcept { return m_measure; }
	/// Texte tapé ; faux s'il n'est pas destiné à la boîte (raccourci d'outil).
	bool TypeText(std::string_view text);
	void Backspace();
	/// Entrée : applique la saisie à l'outil ; outil au repos juste après une opération,
	/// la refait avec cette cote (correction, un seul pas d'annulation).
	void SubmitMeasure();
	/// La dernière opération peut être corrigée en tapant une cote.
	[[nodiscard]] bool CanAmend() const;

	// ── Dimensions exactes ───────────────────────────────────────────────
	/// Position, lacet (degrés, autour de la verticale) et dimensions du monde d'un groupe.
	struct Placement {
		math::FVector3 position;
		float yawDegrees = 0.f;
		math::FVector3 size; ///< largeur (X), hauteur (Y), profondeur (Z)
	};
	[[nodiscard]] Option<Placement> EntityPlacement(EntityId id) const;
	/// Replace un groupe ; les dimensions sont obtenues par mise à l'échelle depuis son origine.
	/// `merge` : suite du même geste (glissé d'un champ), un seul pas d'annulation.
	bool SetEntityPlacement(EntityId id, const Placement &placement, String label, bool merge = false);
	/// Boîte de la sélection de faces et d'arêtes (repère du modèle actif) ; NONE si vide.
	[[nodiscard]] Option<math::FAABB> SelectionBounds();
	/// Met la sélection aux dimensions `size` (axes nuls ignorés), depuis le coin bas de sa boîte.
	bool ResizeSelection(const math::FVector3 &size, String label, bool merge = false);

	// ── Sélection et commandes ───────────────────────────────────────────
	[[nodiscard]] modeling::Selection &Selection() noexcept { return m_selection; }
	[[nodiscard]] const std::vector<EntityId> &SelectedEntities() const noexcept { return m_entities; }
	void SelectEntity(EntityId id, bool add = false);
	void ClearSelection();
	void SelectAll();
	/// Aligne les groupes sélectionnés (bords / centres) ; un pas d'annulation.
	void AlignSelection(Align3D align);
	/// Répartit uniformément les groupes sélectionnés le long d'un axe (0 X, 1 Y, 2 Z).
	void DistributeSelection(int axis);
	/// Déplace les groupes sélectionnés d'un `delta` du monde (pour le gizmo ; faire un
	/// `Checkpoint` au début du glissé).
	void MoveSelected(const math::FVector3 &delta);
	/// Fait pivoter les groupes sélectionnés de `angle` (radians) autour de `axis` passant
	/// par `center` (pour le gizmo de rotation ; faire un `Checkpoint` d'abord).
	void RotateSelected(const math::FVector3 &center, const math::FVector3 &axis, float angle);
	/// Redimensionne uniformément les groupes sélectionnés du facteur `factor` autour de
	/// `center` (pour le gizmo d'échelle ; faire un `Checkpoint` d'abord).
	void ScaleSelected(const math::FVector3 &center, float factor);
	bool DeleteSelection();
	bool GroupSelection();
	bool ExplodeSelection();
	bool DuplicateSelection();
	/// Groupe sélectionné → composant (ses copies en deviennent des instances). Faux sans groupe.
	bool MakeComponent();
	/// Groupe sélectionné → hors de son composant.
	bool MakeUnique();
	/// Groupe ouvert d'un composant modifié : recopie sa géométrie dans les autres instances.
	/// Appelée par les opérations de l'éditeur ; vrai si des instances ont changé.
	bool SyncInstances();
	/// Importe un modèle glTF ou `.model` (format « sandbox.model ») en nouveau groupe posé au sol en `at` (matériaux ajoutés à la
	/// palette), mis à l'échelle `scale` (unités du fichier → mètres), sélectionné ; un pas
	/// d'annulation. Erreur de lecture rendue.
	Result<EntityId, String> ImportModel(const String &path, const math::FVector3 &at, float scale = 1.f);
	void PaintSelection(uint32_t materialIndex);
	bool Undo();
	bool Redo();
	/// Efface l'historique annuler/rétablir (le document reste inchangé).
	void ClearHistory();
	/// Modification de l'interface (panneaux) : mémorise l'état avant, pour l'annuler.
	void Checkpoint(String label);
	/// Idem, fusionnée avec le pas précédent de même libellé si `merge` (frappe, glissé).
	void Change(String label, bool merge) { Edit(std::move(label), merge); }

	[[nodiscard]] const String &Status() const noexcept { return m_status; }
	[[nodiscard]] bool StatusIsError() const noexcept { return m_statusError; }
	NavCamera camera;

private:
	modeling::ToolContext Ctx();
	modeling::ToolEvent LocalEvent(const Pointer &p);
	void Report(const modeling::ToolResult &r, const Document &before);
	void Commit(const Document &before, String label);
	bool HandleRootClick(const Pointer &p);
	void SetStatus(String text, bool error = false);

	Document m_doc;
	History m_history;
	EntityId m_context = kRootId;
	uint64_t m_syncedRevision = 0; ///< révision du groupe ouvert déjà recopiée dans ses instances
	std::unique_ptr<modeling::Tool> m_tool;
	modeling::Selection m_selection;
	std::vector<EntityId> m_entities;
	modeling::Inference m_hover;
	String m_measure, m_status;
	bool m_statusError = false, m_modified = false;
	// Déplacement d'un groupe (outil Déplacer au niveau racine).
	Option<math::FVector3> m_moveAnchor, m_moveOffset;
	// Correction de la dernière opération par une cote tapée.
	bool m_amendable = false;
	String m_amendLabel;							///< message de l'opération (« Pousser/Tirer de 1,5 m »)
	Option<math::FVector3> m_lastGroupMove;			///< direction du dernier déplacement de groupe
	/// Pas d'annulation : fusionné avec le précédent s'il porte le même libellé et que
	/// `merge` est vrai (glissé d'un champ numérique).
	void Edit(String label, bool merge);
	void MoveGroups(const math::FVector3 &delta);
};

} // namespace sandbox
