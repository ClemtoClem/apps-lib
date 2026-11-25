# graphics.app

Cycle de vie des applications graphiques : contexte SDL, fenêtre (barre de titre
personnalisée en option), rendu 2D, polices du canvas, interface immédiate, boucle à pas
fixe (`FixedStepClock`) et rendu interpolé, options `--frames=N` / `--screenshot=F.png`
pour les essais automatiques.

```cpp
class Demo final : public app::Application {
	void OnFixedUpdate(double step) override { /* simulation */ }
	void OnRender(const sdl3::FRect &area, double alpha) override { /* dessin */ }
};

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Démo";
	config.customFrame = true;
	config.ParseArgs(argc, argv);
	return Demo().Run(config);
}
```

## Thèmes

`app::MidnightTheme()` (défaut), `app::LightTheme()` et `app::ContrastTheme()` rendent un
`ui::Theme` commun aux applications (example-ui, scribe). Les widgets immédiats de `graphics.ui`
dérivent leurs fonds des couleurs du thème (boutons, champs, barres de défilement, onglets,
séparateurs de la disposition ancrable) : un thème clair n'affiche plus de pavés sombres.
