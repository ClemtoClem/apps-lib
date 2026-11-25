// catalog.cpp — Catalogue des outils et fonctionnalités du sandbox : une description
// structurée (outils, navigation, panneaux, notions, API de script) consultable depuis
// « Aide → Catalogue », avec recherche plein-texte et raccourcis affichés depuis le KeyMap.
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "sandbox/app/sandbox_app.hpp"

namespace sandbox {

namespace {

/// Découpe `text` en lignes tenant dans `width` pixels (à la taille `size`).
std::vector<std::string> WrapText(sdl3::Canvas &c, std::string_view text, float width, float size) {
	std::vector<std::string> lines;
	std::string current;
	size_t start = 0;
	while (start <= text.size()) {
		const size_t sp = text.find_first_of(" \t\n", start);
		const std::string_view word = text.substr(start, sp == std::string_view::npos ? std::string_view::npos : sp - start);
		const std::string candidate = current.empty() ? std::string(word) : current + " " + std::string(word);
		if (!word.empty() && !current.empty() && c.MeasureText(candidate, size) > width) {
			lines.push_back(current);
			current = std::string(word);
		} else if (!word.empty()) {
			current = candidate;
		}
		if (sp == std::string_view::npos)
			break;
		if (text[sp] == '\n') { // saut de ligne explicite
			if (!current.empty())
				lines.push_back(current);
			current.clear();
		}
		start = sp + 1;
	}
	if (!current.empty())
		lines.push_back(current);
	return lines;
}

} // namespace

void SandboxApp::PopulateCatalog() {
	const auto add = [this](const char *id, const char *category, const char *name, const char *summary, const char *description) {
		m_catalog.Add({id, category, name, 0, summary, description});
	};
	const auto tool = [&](modeling::ToolKind k, const char *summary, const char *description) {
		add(ToolActionId(k), "Outils", modeling::ToolName(k), summary, description);
	};

	// ── Outils ──────────────────────────────────────────────────────────────
	tool(modeling::ToolKind::SELECT, "Sélectionne des faces, des arêtes ou un groupe.",
		 "Cliquez une face, une arête ou un groupe pour le sélectionner. Glissez pour une sélection par cadre (Ctrl : étendre). Double-clic : ouvrir un groupe.");
	tool(modeling::ToolKind::LINE, "Dessine une ligne ou une arête.",
		 "Cliquez le point de départ puis le point d'arrivée. Tapez une longueur puis Entrée pour la fixer. Une ligne fermée forme une face.");
	tool(modeling::ToolKind::RECTANGLE, "Dessine un rectangle sur le plan de travail.",
		 "Cliquez un coin puis le coin opposé. Tapez « largeur;hauteur » puis Entrée (une cote vide suit la souris).");
	tool(modeling::ToolKind::CIRCLE, "Dessine un cercle (ou un arc).",
		 "Cliquez le centre puis un point du rayon. Tapez le rayon (ou « rayon;segments ») puis Entrée.");
	tool(modeling::ToolKind::POLYGON, "Dessine un polygone régulier.",
		 "Cliquez le centre puis un sommet. Le nombre de côtés se règle dans les préférences de l'outil.");
	tool(modeling::ToolKind::PUSH_PULL, "Extrude une face en volume (ou perce un mur).",
		 "Cliquez une face puis tirez la souris. Tapez une hauteur puis Entrée. Pousser jusqu'à la face opposée d'un solide perce une ouverture.");
	tool(modeling::ToolKind::OFFSET, "Copie une arête ou une face en parallèle.",
		 "Cliquez une arête (ou une face) puis tirez vers l'intérieur ou l'extérieur. Tapez la distance puis Entrée.");
	tool(modeling::ToolKind::FOLLOW_ME, "Extrude une face le long d'un chemin.",
		 "Cliquez la face à extruder puis le chemin à suivre ; la face est balayée le long du chemin.");
	tool(modeling::ToolKind::MOVE, "Déplace la sélection.",
		 "Cliquez un point de départ puis la destination. Ctrl pendant le déplacement : copie. Tapez une distance puis Entrée.");
	tool(modeling::ToolKind::ROTATE, "Fait pivoter la sélection autour d'un axe.",
		 "Cliquez le centre puis définissez l'angle. Tapez un angle (degrés) puis Entrée.");
	tool(modeling::ToolKind::SCALE, "Redimensionne la sélection.",
		 "Cliquez un point d'ancrage puis tirez. Tapez un facteur (ou « lx;ly;lz ») puis Entrée.");
	tool(modeling::ToolKind::ERASER, "Efface des arêtes ou des faces.",
		 "Cliquez une arête ou une face pour l'effacer. Effacer une arête interne d'une face la fusionne.");
	tool(modeling::ToolKind::PAINT, "Applique un matériau à une face ou un groupe.",
		 "Choisissez un matériau dans la palette puis cliquez la face (ou le groupe) à peindre.");
	tool(modeling::ToolKind::TAPE, "Mesure une distance et pose des guides.",
		 "Cliquez deux points pour mesurer. Le résultat s'affiche dans la boîte de mesures ; un guide peut être posé.");

	// ── Navigation et vues ─────────────────────────────────────────────────
	add("nav.orbite", "Navigation", "Orbite", "Fait pivoter la caméra autour du modèle.", "Maintenez le bouton du milieu (ou l'outil Orbite) et glissez. Molette : zoom vers le curseur.");
	add("nav.panoramique", "Navigation", "Panoramique", "Décale la vue dans le plan de l'écran.", "Glissez avec le bouton droit (ou Maj + molette glissée).");
	add("camera.zoom_etendu", "Navigation", "Zoom étendu", "Cadre tout le modèle.", "La caméra recule pour montrer l'ensemble de la scène.");
	add("vue.iso", "Navigation", "Vue isométrique", "Projection isométrique de la scène.", "Vue par défaut pour la modélisation en volume.");
	add("vue.dessus", "Navigation", "Vue de dessus", "Regarde la scène depuis le haut.", "Utile pour les plans au sol.");
	add("vue.face", "Navigation", "Vue de face", "Regarde la scène de face.", "Élévation frontale.");
	add("vue.droite", "Navigation", "Vue de droite", "Regarde la scène depuis la droite.", "Élévation latérale.");

	// ── Panneaux ───────────────────────────────────────────────────────────
	add("panneau.inspecteur", "Panneaux", "Inspecteur", "Règle le nœud sélectionné (géométrie, comportement, script).",
		 "Affiche les propriétés de la sélection ; sans sélection, celles de la scène. Les paramètres « export let » d'un script y sont réglables.");
	add("panneau.structure", "Panneaux", "Structure", "Arborescence des groupes et des nœuds.",
		 "Repliez/dépliez avec les flèches, cliquez pour révéler un nœud, glissez-déposez pour changer de parent.");
	add("panneau.materiaux", "Panneaux", "Matériaux", "Palette des matériaux du document.",
		 "Créez, renommez et colorez les matériaux ; appliquez-les avec l'outil Peinture.");
	add("panneau.balises", "Panneaux", "Balises", "Étiquettes pour retrouver et filtrer les groupes.",
		 "Attribuez des balises aux groupes puis filtrez la scène.");
	add("panneau.scenes", "Panneaux", "Scènes", "Vues de caméra enregistrées du document.",
		 "Ajoutez une scène (Caméra → Ajouter une scène) puis basculez entre elles.");
	add("panneau.fichiers", "Panneaux", "Fichiers", "Explorateur des fichiers du projet.",
		 "Double-clic : ouvrir une scène, éditer un script ou un JSON ; clic droit : insérer, importer, attribuer.");
	add("panneau.ressources", "Panneaux", "Ressources", "Modèles glTF, sons et scripts à importer.",
		 "Importez un modèle glTF, attribuez un son ou un script au groupe sélectionné.");
	add("panneau.console", "Panneaux", "Console", "Messages des scripts et erreurs.",
		 "Une erreur cliquée ouvre son script à la ligne fautive.");
	add("panneau.vue3d", "Panneaux", "Vue 3D", "Prévisualisation et édition de la scène 3D.",
		 "Clic : outil courant ; double-clic : ouvrir/fermer un groupe ; molette : zoom ; glissé : orbite/panoramique.");
	add("panneau.vue2d", "Panneaux", "Vue 2D", "Éditeur du calque 2D (HUD du mode Jeu).",
		 "Rectangles, cercles, lignes, textes et images nommés, placés par-dessus la scène 3D.");
	add("panneau.script", "Panneaux", "Script", "Éditeur de code des scripts du projet.",
		 "Coloration, numéros de ligne, indentation automatique ; « Vérifier » surligne les erreurs.");

	// ── Notions ─────────────────────────────────────────────────────────────
	add("notion.groupes", "Notions", "Groupes", "Un groupe regroupe une géométrie avec son propre repère.",
		 "Déplacer un parent emporte ses enfants ; Ctrl+G groupe la sélection ; « Éclater » verse la géométrie dans le parent.");
	add("notion.composants", "Notions", "Composants", "Copies partageant la même géométrie (instances).",
		 "Modifier un composant (groupe ouvert) modifie ses autres instances ; « Rendre unique » le détache.");
	add("notion.calque2d", "Notions", "Calque 2D", "Le calque HUD affiché par-dessus la scène en jeu.",
		 "Éléments nommés modifiables par les scripts (hud.text, hud.move, hud.size…).");
	add("notion.projets", "Notions", "Projets", "Un projet regroupe scènes, objets, modèles et scripts.",
		 "Le fichier « .project » recense le projet ; la scène active s'ouvre au lancement.");
	add("notion.annulation", "Notions", "Annulation", "Annule et rétablit les opérations.",
		 "Ctrl+Z annule, Ctrl+Y rétablit. Après une opération, taper une nouvelle cote la refait (un seul pas d'annulation).");

	// ── Scripts ─────────────────────────────────────────────────────────────
	add("script.behaviour", "Scripts", "Behaviour", "Classe de base d'un groupe scripté.",
		 "Dérivez `class X extends Behaviour` ; méthodes : name, position, move, set_position, rotate, turn, show, hide, distance.");
	add("script.scene", "Scripts", "Scene", "Classe du script de scène.",
		 "`class X extends Scene` : say, time, player, position, move, turn, visible, light, set_player_speed…");
	add("script.physicsbody", "Scripts", "PhysicsBody", "Corps physique d'un groupe.",
		 "Méthodes : push, velocity. Le comportement (solide/dynamique/fantôme) se règle dans l'onglet Jeu.");
	add("script.light3d", "Scripts", "Light3D", "Lampe ponctuelle ou spot du groupe.",
		 "Méthode : light(bool). Couleur, intensité et portée se règlent dans l'inspecteur.");
	add("script.audiosource", "Scripts", "AudioSource", "Source sonore du groupe.",
		 "Méthodes : play, stop. Fichier, volume et boucle se règlent dans l'onglet Jeu.");
	add("script.rappels", "Scripts", "Rappels", "Fonctions appelées par le moteur.",
		 "OnStart, OnUpdate(dt), OnTouch(qui), OnUse, OnTrigger. Un script en erreur est arrêté, la partie continue.");
	add("script.export", "Scripts", "export let", "Paramètre de script réglable dans l'inspecteur.",
		 "`export let vitesse = 4.0` : la valeur par défaut s'affiche dans l'inspecteur, chaque nœud garde la sienne.");
	add("script.import", "Scripts", "import", "Importe un module de script du projet.",
		 "`const i = import \"interactions\"` charge le module ; `class X extends i.Interrupteur` hérite de sa classe.");
}

void SandboxApp::DrawCatalogModal(const sdl3::FRect &area) {
	ui::Context &u = Ui();
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = Theme();
	c.FillRect(area, sdl3::Rgba(0, 0, 0, 150));

	const float w = std::min(area.w * 0.9f, 900.f);
	const float h = std::min(area.h * 0.9f, 640.f);
	const sdl3::FRect p{area.x + (area.w - w) * 0.5f, area.y + (area.h - h) * 0.5f, w, h};
	c.FillRoundRect(p, 12.f, th.panel);
	c.StrokeRoundRect(p, 12.f, th.panelBorder);

	// Titre + fermeture.
	c.Text(p.x + 16.f, p.y + 10.f, "Catalogue", th.text, 18.f, sdl3::CanvasFont::Bold);
	c.Text(p.x + 16.f, p.y + 32.f, "Tous les outils, panneaux, notions et API de script, avec leur raccourci.", th.textDim, 12.f);
	if (u.IconButton({p.Right() - 34.f, p.y + 8.f, 24.f, 24.f}, ui::icons::Codepoint(ui::icons::MaterialIcons::CLOSE), "Fermer"))
		m_showCatalog = false;

	// Recherche.
	u.TextInputAt({p.x + 16.f, p.y + 52.f, p.w - 32.f, 28.f}, "##catalog-search", m_catalogFilter, nullptr, "Rechercher un outil ou une fonctionnalité…");

	// Liste (gauche) + description (droite).
	const float lw = p.w * 0.40f;
	const sdl3::FRect list{p.x + 12.f, p.y + 88.f, lw, p.h - 88.f - 46.f};
	u.BeginScroll("##catalog-list", list, {.axes = ui::ScrollAxes::Vertical});
	const std::vector<size_t> matches = m_catalog.Search(m_catalogFilter);
	for (size_t idx : matches) {
		const ui::CatalogEntry &e = m_catalog.entries[idx];
		const sdl3::FRect r = u.Next(26);
		const bool sel = int(idx) == m_catalogSelected;
		if (u.ButtonAt({r.x, r.y, r.w, 24.f}, e.name, true, sel))
			m_catalogSelected = int(idx);
	}
	if (matches.empty())
		c.Text(list.x + 8.f, list.y + 4.f, "Aucun résultat.", th.textDim, 13.f);
	u.EndScroll();

	// Description (droite).
	const sdl3::FRect desc{p.x + lw + 20.f, p.y + 88.f, p.w - lw - 32.f, p.h - 88.f - 46.f};
	if (m_catalogSelected >= 0 && size_t(m_catalogSelected) < m_catalog.entries.size()) {
		const ui::CatalogEntry &e = m_catalog.entries[size_t(m_catalogSelected)];
		c.Text(desc.x, desc.y, e.name, th.text, 20.f, sdl3::CanvasFont::Bold);
		float y = desc.y + 30.f;
		// Catégorie + raccourci.
		c.Text(desc.x, y, e.category, th.accent, 13.f, sdl3::CanvasFont::Bold);
		if (const ui::KeyMap::Binding *b = m_keys.Find(e.id); b && b->chord.Valid()) {
			const std::string chord = b->chord.ToString();
			const float cw = c.MeasureText(chord, 13.f) + 14.f;
			const sdl3::FRect pill{desc.Right() - cw, y - 2.f, cw, 20.f};
			c.FillRoundRect(pill, 10.f, th.widget);
			c.Text(pill.x + 7.f, pill.y + 2.f, chord, th.text, 12.f);
		}
		y += 26.f;
		c.Text(desc.x, y, e.summary, th.textDim, 14.f);
		y += 26.f;
		// Description détaillée, repliée.
		for (const std::string &line : WrapText(c, e.description, desc.w, 14.f)) {
			if (y > desc.Bottom() - 18.f)
				break;
			c.Text(desc.x, y, line, th.text, 14.f);
			y += 20.f;
		}
	} else {
		c.Text(desc.x, desc.y, "Sélectionnez une entrée à gauche.", th.textDim, 14.f);
	}

	// Pied.
	if (u.ButtonAt({p.Right() - 96.f, p.Bottom() - 42.f, 80.f, 30.f}, "Fermer"))
		m_showCatalog = false;
}

} // namespace sandbox
