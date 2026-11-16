# engine.audio

Traitement du signal audio, sans entrée-sortie (les flux passent par `sdl3::AudioStream`) :
oscillateurs à bande limitée (PolyBLEP), fenêtres, FFT radix-2, spectre en dB, niveaux
RMS / crête, filtres biquad RBJ, écrêtage doux, ligne de retard, tampon circulaire
d'analyse.
