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
