#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <core/core.hpp>
#include <math/math.hpp>
#include "sdl3/core/stdinc.hpp" // couche de base : math (min/max/clamp/lerp...), memory, ctype, env

namespace sdl3 {

// ============================================================================
// Color
// ============================================================================

/**
 * @brief X-macro listant la palette de couleurs nommées de la bibliothèque.
 *
 * `LIST_COLORS` n'est pas destiné à être utilisé directement : il énumère
 * chaque couleur comme `_c(NOM, RR, GG, BB, AA)` (composantes en hexadécimal,
 * sans le `0x` — ex. `_c(RED, FF, 00, 00, FF)` pour `#FF0000FF`). Pour
 * générer du code à partir de cette liste, définissez une macro `_c` avant
 * d'inclure `LIST_COLORS`, puis annulez-la avec `#undef _c` — c'est exactement
 * ce que font `Color` et `FColor` ci-dessous pour générer leurs méthodes
 * fabriques statiques (`Color::RED()`, `FColor::RED()`, etc.), une par
 * couleur de la liste, sans dupliquer les valeurs.
 */
#define LIST_COLORS \
	_c(TRANSPARENT, 00, 00, 00, 00);    /* #00000000 */ \
	_c(ALICE_BLUE, F0, F8, FF, FF);     /* #F0F8FFFF */ \
	_c(AMBER_GREEN, 9A, 80, 3A, FF);    /* #9A803AFF */ \
	_c(AMBER_SUN, FF, 99, 88, FF);      /* #FF9988FF */ \
	_c(ANTIQUE_WHITE, FA, EB, D7, FF);  /* #FAEBD7FF */ \
	_c(AQUA_MARINE, F7, FF, D4, FF);    /* #F7FFD4FF */ \
	_c(AZURE, F0, FF, FF, FF);          /* #F0FFFFFF */ \
	_c(BEIGE, F5, F5, DC, FF);          /* #F5F5DCFF */ \
	_c(BISQUE, FF, E4, C4, FF);         /* #FFE4C4FF */ \
	_c(BLACK, 00, 00, 00, FF);          /* #000000FF */ \
	_c(BLUE, 00, 00, FF, FF);           /* #0000FFFF */ \
	_c(BLUE_VIOLET, 50, 2A, E2, FF);    /* #502AE2FF */ \
	_c(BROWN, A5, 2A, 2A, FF); \
	_c(BURLY_WOOD, DE, B8, 87, FF); \
	_c(CADET_BLUE, 5F, 9E, A0, FF); \
	_c(CHARTREUSE, 7F, FF, 00, FF); \
	_c(CHOCOLATE, D2, 69, 1E, FF); \
	_c(CORAL, FF, 7F, 50, FF); \
	_c(CORN_FLOWER_BLUE, 64, 95, ED, FF); \
	_c(CORN_SILK, FF, F8, DC, FF); \
	_c(CRIMSON, DC, 14, 50, FF); \
	_c(CYAN, 00, FF, FF, FF); \
	_c(DARK_BLUE, 00, 00, 8B, FF); \
	_c(DARK_CYAN, 00, 8B, 8B, FF); \
	_c(DARK_GOLDEN_ROD, B8, 86, 0B, FF);        /* #B8860BFF */ \
	_c(DARK_GREY1, 10, 10, 10, FF);             /* #101010FF */ \
	_c(DARK_GREY2, 20, 20, 20, FF);             /* #202020FF */ \
	_c(DARK_GREY3, 30, 30, 30, FF);             /* #303030FF */ \
	_c(DARK_GREY4, 40, 40, 40, FF);             /* #404040FF */ \
	_c(DARK_GREY5, 50, 50, 50, FF);             /* #505050FF */ \
	_c(DARK_GREEN, 00, 64, 00, FF);             /* #006400FF */ \
	_c(DARK_KAKI, AD, A7, 5B, FF);              /* #AdA75BFF */ \
	_c(DARK_MAGENTA, 8B, 00, 8B, FF);           /* #8B008BFF */ \
	_c(DARK_OLIVER_GREEN, 45, 5B, 1F, FF);      /* #455B1FFF */ \
	_c(DARK_ORANGE, E7, 74, 00, FF);            /* #E77400FF */ \
	_c(DARK_ORCHILD, 99, 32, CC, FF);           /* #9932CCFF */ \
	_c(DARK_PINK, C7, 8D, 9B, FF); \
	_c(DARK_PURPLE, 61, 00, 94, FF); \
	_c(DARK_RED, 8B, 00, 00, FF); \
	_c(DARK_SALMON, E9, 96, 7A, FF); \
	_c(DARK_SEA_GREEN, 8F, BC, 8F, FF); \
	_c(DARK_SLATE_BLUE, 48, 3D, 8B, FF); \
	_c(DARK_STEEL_BLUE, 15, 27, 36, FF); \
	_c(DODGER_BLUE1, 00, 60, CF, FF); \
	_c(DODGER_BLUE2, 1E, 90, FF, FF); \
	_c(DODGER_BLUE3, 46, B8, FF, FF); \
	_c(FIRE_BRICK, B2, 22, 22, FF); \
	_c(FLORAL_WHITE, FF, FA, F0, FF); \
	_c(FOREST_GREEN, 22, 8B, 22, FF); \
	_c(GOLD, FF, D7, 00, FF); \
	_c(GOLD_ROD, DA, A5, 20, FF); \
	_c(GREY1, 70, 70, 70, FF); \
	_c(GREY2, 80, 80, 80, FF); \
	_c(GREY3, 90, 90, 90, FF); \
	_c(GREEN, 00, FF, 00, FF); \
	_c(GREEN_YELLOW, AD, FF, 2F, FF); \
	_c(HOT_PINK, FF, 69, B4, FF); \
	_c(INDIGO, 4B, 00, B2, FF); \
	_c(IVORY, FF, FF, F0, FF); \
	_c(KHAKI, F0, E6, 8C, FF); \
	_c(LARCHMERE, 70, BA, A7, FF); \
	_c(LAVENDER, E6, E6, FA, FF); \
	_c(LAWN_GREEN, 7C, FC, 00, FF); \
	_c(LIGHT_BLUE, AD, D8, E6, FF); \
	_c(LIGHT_CORAL, F0, 80, 80, FF); \
	_c(LIGHT_CYAN, E0, FF, FF, FF); \
	_c(LIGHT_GREEN, 90, EE, 90, FF); \
	_c(LIGHT_GREY1, C6, C6, C6, FF); \
	_c(LIGHT_GREY2, D3, D3, D3, FF); \
	_c(LIGHT_GREY3, DC, DC, DC, FF); \
	_c(LIGHT_ORANGE, FF, B4, 10, FF); \
	_c(LIGHT_PINK1, FF, B6, C1, FF); \
	_c(LIGHT_PINK2, FF, A9, DC, FF); \
	_c(LIGHT_PINK3, FF, BF, FF, FF); \
	_c(LIGHT_SALMON, FF, A0, 7A, FF); \
	_c(LIGHT_STEEL_BLUE, 16, 82, B4, FF); \
	_c(LIGHT_YELLOW, FF, D9, A6, FF); \
	_c(LIME_GREEN, 32, CD, 32, FF); \
	_c(MAGENTA, FF, 00, FF, FF); \
	_c(MAROON, 80, 00, 00, FF); \
	_c(MEDIUM_AQUA_MARINE, 66, CD, AA, FF); \
	_c(MEDIUM_GREY1, 1C, 1C, 1C, FF); \
	_c(MEDIUM_SLATE_BLUE, 7B, 68, EE, FF); \
	_c(MEDIUM_STEEL_BLUE, 38, 68, 90, FF); \
	_c(ORANGE, FF, 64, 00, FF); \
	_c(ORANGE_JUICE, FF, A5, 00, FF); \
	_c(ORANGE_RED, FF, 45, 00, FF);         /* #FF4500FF */ \
	_c(PERU, CD, 85, 3F, FF);               /* #CD853FFF */ \
	_c(PINK, FF, 81, CA, FF);               /* #FF81CAFF */ \
	_c(PURPLE, 99, 00, CC, FF);             /* #9900CCFF */ \
	_c(PURPLE2, 66, 30, 6C, FF);            /* #66306CFF */ \
	_c(RED, FF, 00, 00, FF);                /* #FF0000FF */ \
	_c(SIENNA, A0, 52, 2D, FF);             /* #87CEEBFF */ \
	_c(SKY_BLUE, 87, CE, EB, FF); \
	_c(STEEL_BLUE, 31, 5B, 7E, FF); \
	_c(SLATE_BLUE, 46, 82, B4, FF); \
	_c(SLATE_GREY, 7A, 8A, 7C, FF); \
	_c(TURQUOISE, 40, E0, D0, FF); \
	_c(VIOLET, EE, 82, EE, FF); \
	_c(WEAT, F5, DE, B3, FF); \
	_c(WHITE, FF, FF, FF, FF); \
	_c(WHITE_SMOKE, F5, F5, F5, FF); \
	_c(YELLOW, FF, FF, 00, FF); \
	_c(YELLOW_GREEN, 9A, CD, 32, FF); \
	\
	/* ---- Palette étendue : noms X11/CSS + noms usuels ---- */ \
	_c(AQUA, 00, FF, FF, FF);                       /* #00FFFFFF */ \
	_c(BLANCHED_ALMOND, FF, EB, CD, FF);            /* #FFEBCDFF */ \
	_c(BURNT_ORANGE, CC, 55, 00, FF);               /* #CC5500FF */ \
	_c(CERULEAN, 00, 7B, A7, FF);                   /* #007BA7FF */ \
	_c(CHAMPAGNE, F7, E7, CE, FF);                  /* #F7E7CEFF */ \
	_c(COBALT_BLUE, 00, 47, AB, FF);                /* #0047ABFF */ \
	_c(COPPER, B8, 73, 33, FF);                     /* #B87333FF */ \
	_c(CREAM, FF, FD, D0, FF);                      /* #FFFDD0FF */ \
	_c(DARK_BROWN, 65, 43, 21, FF);                 /* #654321FF */ \
	_c(DARK_VIOLET, 94, 00, D3, FF);                /* #9400D3FF */ \
	_c(DEEP_PINK, FF, 14, 93, FF);                  /* #FF1493FF */ \
	_c(DEEP_SKY_BLUE, 00, BF, FF, FF);              /* #00BFFFFF */ \
	_c(DIM_GREY, 69, 69, 69, FF);                   /* #696969FF */ \
	_c(DUSTY_ROSE, DC, AE, 96, FF);                 /* #DCAE96FF */ \
	_c(EMERALD, 50, C8, 78, FF);                    /* #50C878FF */ \
	_c(FERN_GREEN, 4F, 79, 42, FF);                 /* #4F7942FF */ \
	_c(FUCHSIA, FF, 00, FF, FF);                    /* #FF00FFFF */ \
	_c(GAINSBORO, DC, DC, DC, FF);                  /* #DCDCDCFF */ \
	_c(GHOST_WHITE, F8, F8, FF, FF);                /* #F8F8FFFF */ \
	_c(GOLDEN_YELLOW, FF, DF, 00, FF);              /* #FFDF00FF */ \
	_c(SEA_GREEN, 2E, 8B, 57, FF);                  /* #2E8B57FF */ \
	_c(HONEYDEW, F0, FF, F0, FF);                   /* #F0FFF0FF */ \
	_c(INDIAN_RED, CD, 5C, 5C, FF);                 /* #CD5C5CFF */ \
	_c(JADE, 00, A8, 6B, FF);                       /* #00A86BFF */ \
	_c(KHAKI_DARK, BD, B7, 6B, FF);                 /* #BDB76BFF */ \
	_c(LAVENDER_BLUSH, FF, F0, F5, FF);             /* #FFF0F5FF */ \
	_c(LEMON, FF, F4, 4F, FF);                      /* #FFF44FFF */ \
	_c(LEMON_CHIFFON, FF, FA, CD, FF);              /* #FFFACDFF */ \
	_c(LILAC, C8, A2, C8, FF);                      /* #C8A2C8FF */ \
	_c(LIME, 00, FF, 00, FF);                       /* #00FF00FF */ \
	_c(LINEN, FA, F0, E6, FF);                      /* #FAF0E6FF */ \
	_c(MIDNIGHT_BLUE, 19, 19, 70, FF);              /* #191970FF */ \
	_c(MINT, 3E, B4, 89, FF);                       /* #3EB489FF */ \
	_c(MINT_CREAM, F5, FF, FA, FF);                 /* #F5FFFAFF */ \
	_c(MISTY_ROSE, FF, E4, E1, FF);                 /* #FFE4E1FF */ \
	_c(MOSS_GREEN, 8A, 9A, 5B, FF);                 /* #8A9A5BFF */ \
	_c(NAVAJO_WHITE, FF, DE, AD, FF);               /* #FFDEADFF */ \
	_c(NAVY, 00, 00, 80, FF);                       /* #000080FF */ \
	_c(OLIVE, 80, 80, 00, FF);                      /* #808000FF */ \
	_c(OLIVE_DRAB, 6B, 8E, 23, FF);                 /* #6B8E23FF */ \
	_c(ORCHID, DA, 70, D6, FF);                     /* #DA70D6FF */ \
	_c(PALE_GOLDENROD, EE, E8, AA, FF);             /* #EEE8AAFF */ \
	_c(PALE_GREEN, 98, FB, 98, FF);                 /* #98FB98FF */ \
	_c(PALE_TURQUOISE, AF, EE, EE, FF);             /* #AFEEEEFF */ \
	_c(PALE_VIOLET_RED, DB, 70, 93, FF);            /* #DB7093FF */ \
	_c(PAPAYA_WHIP, FF, EF, D5, FF);                /* #FFEFD5FF */ \
	_c(PEACH_PUFF, FF, DA, B9, FF);                 /* #FFDAB9FF */ \
	_c(PERIWINKLE, CC, CC, FF, FF);                 /* #CCCCFFFF */ \
	_c(PLUM, DD, A0, DD, FF);                       /* #DDA0DDFF */ \
	_c(POWDER_BLUE, B0, E0, E6, FF);                /* #B0E0E6FF */ \
	_c(REBECCA_PURPLE, 66, 33, 99, FF);             /* #663399FF */ \
	_c(ROSY_BROWN, BC, 8F, 8F, FF);                 /* #BC8F8FFF */ \
	_c(ROYAL_BLUE, 41, 69, E1, FF);                 /* #4169E1FF */ \
	_c(SADDLE_BROWN, 8B, 45, 13, FF);               /* #8B4513FF */ \
	_c(SALMON, FA, 80, 72, FF);                     /* #FA8072FF */ \
	_c(SANDY_BROWN, F4, A4, 60, FF);                /* #F4A460FF */ \
	_c(SEASHELL, FF, F5, EE, FF);                   /* #FFF5EEFF */ \
	_c(SILVER, C0, C0, C0, FF);                     /* #C0C0C0FF */ \
	_c(SKY_BLUE_LIGHT, 87, CE, FA, FF);             /* #87CEFAFF */ \
	_c(SLATE_GREY_DARK, 2F, 4F, 4F, FF);            /* #2F4F4FFF */ \
	_c(SNOW, FF, FA, FA, FF);                       /* #FFFAFAFF */ \
	_c(SPRING_GREEN, 00, FF, 7F, FF);               /* #00FF7FFF */ \
	_c(STEEL_BLUE_LIGHT, B0, C4, DE, FF);           /* #B0C4DEFF */ \
	_c(TAN, D2, B4, 8C, FF);                        /* #D2B48CFF */ \
	_c(TEAL, 00, 80, 80, FF);                       /* #008080FF */ \
	_c(THISTLE, D8, BF, D8, FF);                    /* #D8BFD8FF */ \
	_c(TOMATO, FF, 63, 47, FF);                     /* #FF6347FF */ \
	_c(TURQUOISE_DARK, 00, CE, D1, FF);             /* #00CED1FF */ \
	_c(TURQUOISE_MEDIUM, 48, D1, CC, FF);           /* #48D1CCFF */ \
	_c(ULTRAMARINE, 3F, 00, FF, FF);                /* #3F00FFFF */ \
	_c(VANILLA, F3, E5, AB, FF);                    /* #F3E5ABFF */ \
	_c(VIOLET_RED, C7, 15, 85, FF);                 /* #C71585FF */ \
	_c(WHEAT, F5, DE, B3, FF);                      /* #F5DEB3FF */ \
	_c(WINE, 72, 2F, 37, FF);                       /* #722F37FF */ \
	_c(WISTERIA, C9, A0, DC, FF);                   /* #C9A0DCFF */ \
	_c(ZAFFRE, 00, 14, A8, FF);                     /* #0014A8FF */ \
	_c(AMARANTH, E5, 2B, 50, FF);                   /* #E52B50FF */ \
	_c(APRICOT, FB, CE, B1, FF);                    /* #FBCEB1FF */ \
	_c(BABY_BLUE, 89, CF, F0, FF);                  /* #89CFF0FF */ \
	_c(BABY_PINK, F4, C2, C2, FF);                  /* #F4C2C2FF */ \
	_c(BANANA_YELLOW, FF, E1, 35, FF);              /* #FFE135FF */ \
	_c(BLUE_CERULEAN, 2A, 52, BE, FF);              /* #2A52BEFF */ \
	_c(BLUE_ELECTRIC, 7D, F9, FF, FF);              /* #7DF9FFFF */ \
	_c(BLUE_MAYA, 73, C2, FB, FF);                  /* #73C2FBFF */ \
	_c(BRONZE, CD, 7F, 32, FF);                     /* #CD7F32FF */ \
	_c(BURGUNDY, 80, 00, 20, FF);                   /* #800020FF */ \
	_c(CAMBRIDGE_BLUE, A3, C1, AD, FF);             /* #A3C1ADFF */ \
	_c(CAMEL, C1, 9A, 6B, FF);                      /* #C19A6BFF */ \
	_c(CARAMEL, FF, D5, 9A, FF);                    /* #FFD59AFF */ \
	_c(CARMINE, 96, 00, 18, FF);                    /* #960018FF */ \
	_c(CARROT, ED, 91, 21, FF);                     /* #ED9121FF */ \
	_c(CELADON, AC, E1, AF, FF);                    /* #ACE1AFFF */ \
	_c(CHARCOAL, 36, 45, 4F, FF);                   /* #36454FFF */ \
	_c(CHESTNUT, 95, 45, 35, FF);                   /* #954535FF */ \
	_c(COFFEE, 6F, 4E, 37, FF);                     /* #6F4E37FF */ \
	_c(COOL_GREY, 8C, 92, AC, FF);                  /* #8C92ACFF */ \
	_c(DAFFODIL, FF, FF, 31, FF);                   /* #FFFF31FF */ \
	_c(DANDELION, F0, E1, 30, FF);                  /* #F0E130FF */ \
	_c(DENIM, 15, 60, BD, FF);                      /* #1560BDFF */ \
	_c(DESERT_SAND, ED, C9, AF, FF);                /* #EDC9AFFF */ \
	_c(EGGPLANT, 61, 40, 51, FF);                   /* #614051FF */ \
	_c(EGGSHELL, F0, EA, D6, FF);                   /* #F0EAD6FF */ \
	_c(FAWN, E5, AA, 70, FF);                       /* #E5AA70FF */ \
	_c(FELDGRAU, 4D, 5D, 53, FF);                   /* #4D5D53FF */ \
	_c(FIRE_ENGINE_RED, CE, 20, 29, FF);            /* #CE2029FF */ \
	_c(FLAX, EE, DC, 82, FF);                       /* #EEDC82FF */ \
	_c(FRENCH_BLUE, 00, 72, BB, FF);                /* #0072BBFF */ \
	_c(FRENCH_ROSE, F6, 4A, 8A, FF);                /* #F64A8AFF */ \
	_c(GAMBOGE, E4, 9B, 0F, FF);                    /* #E49B0FFF */ \
	_c(GINGER, B0, 65, 00, FF);                     /* #B06500FF */ \
	_c(GOLD_METALLIC, D4, AF, 37, FF);              /* #D4AF37FF */ \
	_c(GOLDEN_BROWN, 99, 65, 15, FF);               /* #996515FF */ \
	_c(GRAPE, 6F, 2D, A8, FF);                      /* #6F2DA8FF */ \
	_c(GREY_ASH, B2, BE, B5, FF);                   /* #B2BEB5FF */ \
	_c(GUNMETAL, 2C, 35, 39, FF);                   /* #2C3539FF */ \
	_c(HARLEQUIN, 3F, FF, 00, FF);                  /* #3FFF00FF */ \
	_c(HELIOTROPE, DF, 73, FF, FF);                 /* #DF73FFFF */ \
	_c(HONEY, F0, B0, 00, FF);                      /* #F0B000FF */ \
	_c(ICEBERG, 71, A6, D2, FF);                    /* #71A6D2FF */ \
	_c(INDEPENDENCE, 4C, 51, 6D, FF);               /* #4C516DFF */ \
	_c(IVY_GREEN, 1D, 5C, 3D, FF);                  /* #1D5C3DFF */ \
	_c(JASMINE, F8, DE, 7E, FF);                    /* #F8DE7EFF */ \
	_c(JASPER, D7, 3B, 3E, FF);                     /* #D73B3EFF */ \
	_c(JET, 34, 34, 34, FF);                        /* #343434FF */ \
	_c(JUNGLE_GREEN, 29, AB, 87, FF);               /* #29AB87FF */ \
	_c(KELLY_GREEN, 4C, BB, 17, FF);                /* #4CBB17FF */ \
	_c(LAPIS_LAZULI, 26, 61, 9C, FF);               /* #26619CFF */ \
	_c(LAVENDER_PINK, FB, AE, D2, FF);              /* #FBAED2FF */ \
	_c(LAVENDER_ROSE, FB, A0, E3, FF);              /* #FBA0E3FF */ \
	_c(LIGHT_PLUM, DD, B6, D6, FF);                 /* #DDB6D6FF */ \
	_c(MACARONI_CHEESE, FF, BD, 88, FF);            /* #FFBD88FF */ \
	_c(MAGENTA_METALLIC, C2, 00, 78, FF);           /* #C20078FF */ \
	_c(MAGNOLIA, F8, F4, FF, FF);                   /* #F8F4FFFF */ \
	_c(MAHOGANY, C0, 40, 00, FF);                   /* #C04000FF */ \
	_c(MAIZE, FB, EC, 5D, FF);                      /* #FBEC5DFF */ \
	_c(MALACHITE, 0B, DA, 51, FF);                  /* #0BDA51FF */ \
	_c(MANATEE, 97, 9A, AA, FF);                    /* #979AAAFF */ \
	_c(MANDARIN, F3, 7A, 48, FF);                   /* #F37A48FF */ \
	_c(MANGO, FD, BE, 02, FF);                      /* #FDBE02FF */ \
	_c(MELON, FD, BC, B4, FF);                      /* #FDBCB4FF */ \
	_c(MINT_GREEN, 98, FF, 98, FF);                 /* #98FF98FF */ \
	_c(MOONSTONE, 3A, A8, C1, FF);                  /* #3AA8C1FF */ \
	_c(MULBERRY, C5, 4B, 8C, FF);                   /* #C54B8CFF */ \
	_c(MUSTARD, FF, DB, 58, FF);                    /* #FFDB58FF */ \
	_c(MYRTLE, 21, 42, 1E, FF);                     /* #21421EFF */ \
	_c(NAPLES_YELLOW, FA, DA, 5E, FF);              /* #FADA5EFF */ \
	_c(NEON_GREEN, 39, FF, 14, FF);                 /* #39FF14FF */ \
	_c(NEON_PINK, FF, 6E, C7, FF);                  /* #FF6EC7FF */ \
	_c(OCHRE, CC, 77, 22, FF);                      /* #CC7722FF */ \
	_c(OPAL, A8, C3, BC, FF);                       /* #A8C3BCFF */ \
	_c(ORANGE_PEEL, FF, 9F, 00, FF);                /* #FF9F00FF */ \
	_c(OXBLOOD, 4A, 00, 00, FF);                    /* #4A0000FF */ \
	_c(PALE_BROWN, 98, 76, 54, FF);                 /* #987654FF */ \
	_c(PALE_CERULEAN, 9B, C4, E2, FF);              /* #9BC4E2FF */ \
	_c(PALE_CORNFLOWER_BLUE, AB, CD, EF, FF);       /* #ABCDEFFF */ \
	_c(PALE_MAGENTA, F9, 84, E5, FF);               /* #F984E5FF */ \
	_c(PALE_ORANGE, FF, CC, 99, FF);                /* #FFCC99FF */ \
	_c(PALE_ROSE, FA, DA, DD, FF);                  /* #FADADDFF */ \
	_c(PALE_SILVER, C9, C0, BB, FF);                /* #C9C0BBFF */ \
	_c(PANSY_PURPLE, 78, 18, 4A, FF);               /* #78184AFF */ \
	_c(PARCHMENT, F1, E9, D2, FF);                  /* #F1E9D2FF */ \
	_c(PASTEL_BLUE, AE, C6, CF, FF);                /* #AEC6CFFF */ \
	_c(PASTEL_GREEN, 77, DD, 77, FF);               /* #77DD77FF */ \
	_c(PASTEL_MAGENTA, F4, 9A, C2, FF);             /* #F49AC2FF */ \
	_c(PASTEL_ORANGE, FF, B3, 47, FF);              /* #FFB347FF */ \
	_c(PASTEL_PINK, DE, A5, A4, FF);                /* #DEA5A4FF */ \
	_c(PASTEL_PURPLE, B3, 9E, B5, FF);              /* #B39EB5FF */ \
	_c(PASTEL_RED, FF, 69, 61, FF);                 /* #FF6961FF */ \
	_c(PASTEL_YELLOW, FD, FD, 96, FF);              /* #FDFD96FF */ \
	_c(PAYNE_GREY, 53, 68, 78, FF);                 /* #536878FF */ \
	_c(PEACH_YELLOW, FA, DF, AD, FF);               /* #FADFADFF */ \
	_c(PEARL, EA, E0, C8, FF);                      /* #EAE0C8FF */ \
	_c(PERIDOT, E6, E2, 00, FF);                    /* #E6E200FF */ \
	_c(PERSIAN_BLUE, 1C, 39, BB, FF);               /* #1C39BBFF */ \
	_c(PERSIAN_GREEN, 00, A6, 93, FF);              /* #00A693FF */ \
	_c(PERSIAN_PINK, F7, 7F, BE, FF);               /* #F77FBEFF */ \
	_c(PERSIAN_RED, CC, 33, 33, FF);                /* #CC3333FF */ \
	_c(PERSIAN_ROSE, FE, 28, A2, FF);               /* #FE28A2FF */ \
	_c(PHLOX, DF, 00, FF, FF);                      /* #DF00FFFF */ \
	_c(PICTON_BLUE, 45, B1, E8, FF);                /* #45B1E8FF */ \
	_c(PINK_PEARL, E7, AC, CF, FF);                 /* #E7ACCFFF */ \
	_c(PINK_SALMON, FF, 91, A4, FF);                /* #FF91A4FF */ \
	_c(PISTACHIO, 93, C5, 72, FF);                  /* #93C572FF */ \
	_c(PLATINUM, E5, E4, E2, FF);                   /* #E5E4E2FF */ \
	_c(POMPADOUR, 66, 00, 45, FF);                  /* #660045FF */ \
	_c(POPPY, E3, 53, 35, FF);                      /* #E35335FF */ \
	_c(PRIMROSE, ED, EA, 9A, FF);                   /* #EDEA9AFF */ \
	_c(PUMPKIN, FF, 75, 18, FF);                    /* #FF7518FF */ \
	_c(PURPLE_HEART, 74, 42, C8, FF);               /* #7442C8FF */ \
	_c(PURPLE_MOUNTAIN, 96, 78, B6, FF);            /* #9678B6FF */ \
	_c(QUEEN_BLUE, 43, 6B, 95, FF);                 /* #436B95FF */ \
	_c(QUEEN_PINK, E8, CC, D7, FF);                 /* #E8CCD7FF */ \
	_c(RAZZLE_DAZZLE_ROSE, FF, 33, CC, FF);         /* #FF33CCFF */ \
	_c(RED_BRICK, CB, 41, 54, FF);                  /* #CB4154FF */ \
	_c(RED_IMPERIAL, ED, 29, 39, FF);               /* #ED2939FF */ \
	_c(RED_JAPANESE, 9E, 1A, 1A, FF);               /* #9E1A1AFF */ \
	_c(RED_RUBY, 9B, 11, 1E, FF);                   /* #9B111EFF */ \
	_c(RICH_BLACK, 0A, 0A, 0A, FF);                 /* #0A0A0AFF */ \
	_c(ROBIN_EGG_BLUE, 00, CC, CC, FF);             /* #00CCCCFF */ \
	_c(ROSE_QUARTZ, AA, 98, A9, FF);                /* #AA98A9FF */ \
	_c(ROSE_VALE, AB, 4E, 52, FF);                  /* #AB4E52FF */ \
	_c(ROSEWOOD, 65, 00, 0B, FF);                   /* #65000BFF */ \
	_c(ROSSO_CORSA, D4, 00, 00, FF);                /* #D40000FF */ \
	_c(ROYAL_AZURE, 00, 38, A8, FF);                /* #0038A8FF */ \
	_c(ROYAL_FUCHSIA, CA, 2C, 92, FF);              /* #CA2C92FF */ \
	_c(RUDDY, FF, 00, 28, FF);                      /* #FF0028FF */ \
	_c(RUSSET, 80, 46, 1B, FF);                     /* #80461BFF */ \
	_c(RUSSIAN_GREEN, 67, 92, 67, FF);              /* #679267FF */ \
	_c(SAFFRON, F4, C4, 30, FF);                    /* #F4C430FF */ \
	_c(SAGE, BC, B8, 8A, FF);                       /* #BCB88AFF */ \
	_c(SAND, C2, B2, 80, FF);                       /* #C2B280FF */ \
	_c(SAPPHIRE, 0F, 52, BA, FF);                   /* #0F52BAFF */ \
	_c(SCARLET, FF, 24, 00, FF);                    /* #FF2400FF */ \
	_c(SCHOOL_BUS_YELLOW, FF, D8, 00, FF);          /* #FFD800FF */ \
	_c(SEA_BLUE, 00, 69, 94, FF);                   /* #006994FF */ \
	_c(SEPIA, 70, 42, 14, FF);                      /* #704214FF */ \
	_c(SHADOW, 8A, 79, 5D, FF);                     /* #8A795DFF */ \
	_c(SHOCKING_PINK, FC, 0F, C0, FF);              /* #FC0FC0FF */ \
	_c(SILVER_METALLIC, AA, A9, AD, FF);            /* #AAA9ADFF */ \
	_c(SLATE, 70, 80, 90, FF);                      /* #708090FF */ \
	_c(SPACE_CADET, 1D, 29, 51, FF);                /* #1D2951FF */ \
	_c(SPANISH_BLUE, 00, 70, B8, FF);               /* #0070B8FF */ \
	_c(SPANISH_RED, E6, 00, 26, FF);                /* #E60026FF */ \
	_c(SPRING_BUD, A7, FC, 00, FF);                 /* #A7FC00FF */ \
	_c(STEEL, 71, 79, 7E, FF);                      /* #71797EFF */ \
	_c(STORM_GREY, 71, 74, 86, FF);                 /* #717486FF */ \
	_c(STRAWBERRY, FC, 5A, 8D, FF);                 /* #FC5A8DFF */ \
	_c(SUNGLOW, FF, CC, 33, FF);                    /* #FFCC33FF */ \
	_c(SUNRAY, E3, AB, 57, FF);                     /* #E3AB57FF */ \
	_c(SUNSET, FA, D6, A5, FF);                     /* #FAD6A5FF */ \
	_c(TANGELO, F9, 4D, 00, FF);                    /* #F94D00FF */ \
	_c(TEA_GREEN, D0, F0, C0, FF);                  /* #D0F0C0FF */ \
	_c(TEA_ROSE, F8, 83, 79, FF);                   /* #F88379FF */ \
	_c(TERRA_COTTA, E2, 72, 5B, FF);                /* #E2725BFF */ \
	_c(THULIAN_PINK, DE, 6F, A1, FF);               /* #DE6FA1FF */ \
	_c(TIFFANY_BLUE, 0A, BA, B5, FF);               /* #0ABAB5FF */ \
	_c(TIMBERWOLF, DB, D7, D2, FF);                 /* #DBD7D2FF */ \
	_c(TOPAZ, FF, C8, 7C, FF);                      /* #FFC87CFF */ \
	_c(TUSCAN_RED, 7C, 30, 30, FF);                 /* #7C3030FF */ \
	_c(TYRIAN_PURPLE, 66, 02, 3C, FF);              /* #66023CFF */ \
	_c(ULTRAMARINE_DARK, 12, 0A, 8F, FF);           /* #120A8FFF */ \
	_c(UMBER, 63, 51, 47, FF);                      /* #635147FF */ \
	_c(UMBER_BURNT, 8A, 33, 24, FF);                /* #8A3324FF */ \
	_c(UNMELLOW_YELLOW, FF, FF, 66, FF);            /* #FFFF66FF */ \
	_c(VEGAS_GOLD, C5, B3, 58, FF);                 /* #C5B358FF */ \
	_c(VERDIGRIS, 43, B3, AE, FF);                  /* #43B3AEFF */ \
	_c(VERMILION, E3, 42, 34, FF);                  /* #E34234FF */ \
	_c(VICTORIA, 53, 44, 91, FF);                   /* #534491FF */ \
	_c(VIOLET_ELECTRIC, 7F, 00, FF, FF);            /* #7F00FFFF */ \
	_c(VIRIDIAN, 40, 82, 6D, FF);                   /* #40826DFF */ \
	_c(VIVID_ORANGE, FF, A0, 00, FF);               /* #FFA000FF */ \
	_c(WATERMELON, FC, 6C, 85, FF);                 /* #FC6C85FF */ \
	_c(WILLOW, 9C, 9A, 40, FF);                     /* #9C9A40FF */ \
	_c(WINTER_SKY, FF, 00, 7C, FF);                 /* #FF007CFF */ \
	_c(WOOD, B5, 9A, 6A, FF);                       /* #B59A6AFF */ \
	_c(XANADU, 73, 86, 78, FF);                     /* #738678FF */ \
	_c(YALE_BLUE, 0F, 4D, 92, FF);                  /* #0F4D92FF */ \
	_c(YELLOW_CANARY, FF, EF, 00, FF);              /* #FFEF00FF */ \
	_c(YELLOW_LEMON, FF, F7, 00, FF);               /* #FFF700FF */ \
	_c(YELLOW_OLIVE, 9B, 96, 04, FF);               /* #9B9604FF */ \
	_c(YELLOW_PALE, FF, FF, ED, FF);                /* #FFFFEDFF */ \
	_c(YELLOW_SAFETY, EE, D2, 02, FF);              /* #EED202FF */ \
	_c(YELLOW_TRANSPORTATION, FE, CE, 00, FF);      /* #FECE00FF */ \
	_c(YELLOW_WARM, FF, E8, 7C, FF);                /* #FFE87CFF */ \
	_c(ZUCCHINI, 7A, 94, 2E, FF);                   /* #7A942EFF */ \
	\
	/* ---- Palette ui:: (ajoutée lors de la migration FColor, cf. */ \
	/* memory/project_ui_ecs_module.md) — couleurs récurrentes du thème */ \
	/* Aero/dark de ui::, dédupliquées par valeur RGBA exacte. ---- */ \
	_c(UI_TEXT_PRIMARY, DC, DE, E8, FF);        /* #DCDEE8FF */ \
	_c(UI_TEXT_BRIGHT, EB, EB, EB, FF);         /* #EBEBEBFF */ \
	_c(UI_TEXT_MUTED, 78, 7C, 96, FF);          /* #787C96FF */ \
	_c(UI_BORDER_SLATE, 37, 3C, 54, FF);        /* #373C54FF */ \
	_c(UI_BORDER_MUTED, 46, 4C, 6E, FF);        /* #464C6EFF */ \
	_c(UI_BORDER_MUTED2, 46, 4E, 6E, FF);       /* #464E6EFF */ \
	_c(UI_PANEL_DARK, 2A, 2C, 3A, FF);          /* #2A2C3AFF */ \
	_c(UI_PANEL_DARK2, 2D, 30, 42, FF);         /* #2D3042FF */ \
	_c(UI_PANEL_DARKER, 1C, 1E, 2C, FF);        /* #1C1E2CFF */ \
	_c(UI_BG_DEEP, 18, 1A, 26, FF);             /* #181A26FF */ \
	_c(UI_WINDOW_BG, 0A, 0C, 14, FF);           /* #0A0C14FF */ \
	_c(UI_APP_BG, 10, 11, 19, FF);              /* #101119FF */ \
	_c(UI_ACCENT_BLUE, 46, 82, D2, FF);         /* #4682D2FF */ \
	_c(UI_ACCENT_BLUE_PRIMARY, 3C, 8C, DC, FF); /* #3C8CDCFF */ \
	_c(UI_ACCENT_BLUE_LIGHT, 64, A0, E6, FF);   /* #64A0E6FF */ \
	_c(UI_ACCENT_BLUE_BRIGHT, 56, 9E, E8, FF);  /* #569EE8FF */ \
	_c(UI_ACCENT_BLUE_HOVER, 78, AA, FA, FF);   /* #78AAFAFF */ \
	_c(UI_ACCENT_BLUE_DEEP, 3C, 64, AA, FF);    /* #3C64AAFF */ \
	_c(UI_ACCENT_BLUE_GLOW, 3C, 64, AA, A0);    /* #3C64AAA0 */ \
	_c(UI_ACCENT_BLUE_SKY, 46, 96, E6, FF);     /* #4696E6FF */ \
	_c(UI_ACCENT_SKY_BRIGHT, 5A, AA, EB, FF);   /* #5AAAEBFF */ \
	_c(UI_ACCENT_GREEN, 46, D2, 8C, FF);        /* #46D28CFF */ \
	_c(UI_NODE_HEADER_BLUE, 34, 56, 96, FF);    /* #345696FF */ \
	_c(UI_NODE_HEADER_NEUTRAL, 3C, 41, 4E, FF); /* #3C414EFF */ \
	_c(UI_AERO_TITLE_DARK, 12, 28, 46, FF);     /* #122846FF */ \
	_c(UI_AERO_TITLE_LIGHT, 3C, 6E, AA, FF);    /* #3C6EAAFF */ \
	_c(UI_AERO_GLASS_BLUE, 28, 5A, A0, C8);     /* #285AA0C8 */ \
	_c(UI_OVERLAY_DARK, 14, 15, 1C, BE);        /* #14151CBE */ \
	_c(UI_HIGHLIGHT_FAINT, FF, FF, FF, 18);     /* #FFFFFF18 */ \
	_c(UI_WHITE_SOFT, FF, FF, FF, 5A);          /* #FFFFFF5A */ \
	_c(UI_WHITE_STRONG, FF, FF, FF, C8);        /* #FFFFFFC8 */


/**
 * @brief Couleur RGBA 8 bits par composante (0-255), disposition SDL_Color.
 *
 * Représentation "octet" adaptée au stockage et à l'interop directe avec
 * l'API C de SDL (`operator SDL_Color()`). Pour les calculs de mélange,
 * d'assombrissement/éclaircissement, etc., préférer @ref FColor (0.0-1.0),
 * plus précis et sans arrondi intermédiaire — convertir via `ToFloat()` /
 * `FColor(Color)`.
 *
 * Une méthode fabrique statique par couleur de @ref LIST_COLORS est générée
 * ci-dessous (`Color::RED()`, `Color::WHITE()`, `Color::DARK_GREY1()`, ...).
 */
struct Color {
	uint8_t r = 0, g = 0, b = 0, a = 255;

	#define _c(name, r, g, b, a) [[nodiscard]] static constexpr Color name(){ return {(0x##r), (0x##g), (0x##b), (0x##a)}; }
	LIST_COLORS
	#undef _c

	constexpr Color() = default;
	constexpr Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255) : r(r_), g(g_), b(b_), a(a_) {}
	constexpr explicit Color(const SDL_Color& c) : r(c.r), g(c.g), b(c.b), a(c.a) {}

	[[nodiscard]] constexpr operator SDL_Color() const { return {r, g, b, a}; }

	/// Conversion vers `SDL_FColor` (composantes normalisées 0.0-1.0).
	[[nodiscard]] constexpr SDL_FColor ToFloat() const { return {r / 255.f, g / 255.f, b / 255.f, a / 255.f}; }

	constexpr bool operator==(const Color &other) const {
		return r == other.r && g == other.g && b == other.b && a == other.a;
	}

	/// Couleur nommée de @ref LIST_COLORS. La recherche ignore la casse, les espaces, les tirets
	/// et les soulignés : « SEA_GREEN », « sea green », « SeaGreen » et « sea-green » sont
	/// équivalents. std::nullopt si le nom est inconnu.
	[[nodiscard]] static std::optional<Color> FromName(std::string_view name);
	/// Nom de @ref LIST_COLORS de cette valeur RGBA exacte (le premier de la liste si plusieurs
	/// noms partagent la valeur, ex. CYAN et AQUA) ; std::nullopt si aucune couleur nommée.
	[[nodiscard]] std::optional<std::string_view> Name() const;
	/// Tous les noms de @ref LIST_COLORS, dans l'ordre de la liste (choix d'une couleur par son
	/// nom : la valeur s'obtient avec `FromName`).
	[[nodiscard]] static std::span<const std::string_view> Names();
};

/**
 * @brief Couleur RGBA en virgule flottante, composantes normalisées et
 * bornées à [0, 1] (toute valeur hors bornes est silencieusement clampée par
 * le constructeur et par chaque opération).
 *
 * Format naturel pour les calculs colorimétriques (mélange, luminance,
 * assombrissement...) et pour l'upload GPU (`SDL_FColor`, uniforms de
 * shader) — voir @ref Color pour l'équivalent 8 bits/composante utilisé pour
 * le stockage et l'interop SDL directe.
 *
 * Une méthode fabrique statique par couleur de @ref LIST_COLORS est générée
 * ci-dessous (`FColor::RED()`, `FColor::WHITE()`, ...), avec conversion
 * automatique des octets hexadécimaux (0-255) vers [0, 1].
 */
struct FColor {
	float r, g, b, a;

	#define _c(name, r, g, b, a)                                                                                     \
		[[nodiscard]] static constexpr FColor name() {                                                               \
			return {(0x##r) / 255.f, (0x##g) / 255.f, (0x##b) / 255.f, (0x##a) / 255.f};                             \
		}
	LIST_COLORS
	#undef _c

	// =====================================================================
	// Construction
	// =====================================================================

	/// Construit depuis des composantes déjà normalisées ; toute valeur hors
	/// [0, 1] est clampée (voir la garantie de classe ci-dessus).
	constexpr FColor(
		float r_ = 0.f,
		float g_ = 0.f,
		float b_ = 0.f,
		float a_ = 0.f)
		: r(Clamp(r_, 0.f, 1.f))
		, g(Clamp(g_, 0.f, 1.f))
		, b(Clamp(b_, 0.f, 1.f))
		, a(Clamp(a_, 0.f, 1.f)) {
	}

	/// `SDL_FColor` est déjà normalisé (0.0-1.0) : copie directe, sans conversion.
	constexpr FColor(const SDL_FColor& color)
		: r(color.r),
		  g(color.g),
		  b(color.b),
		  a(color.a) {
	}

	/// Convertit depuis @ref Color : les octets (0-255) sont normalisés vers [0, 1].
	constexpr FColor(const Color& color)
		: r(color.r / 255.f),
		  g(color.g / 255.f),
		  b(color.b / 255.f),
		  a(color.a / 255.f) {
	}

	// =====================================================================
	// Conversion SDL
	// =====================================================================

	/// Conversion vers `SDL_Color` (octets 0-255) — troncature, pas d'arrondi.
	[[nodiscard]]
	constexpr operator SDL_Color() const {
		return {
			static_cast<Uint8>(r * 255.f),
			static_cast<Uint8>(g * 255.f),
			static_cast<Uint8>(b * 255.f),
			static_cast<Uint8>(a * 255.f)
		};
	}

	/// Conversion vers `SDL_FColor` : déjà le même format, copie directe.
	[[nodiscard]]
	constexpr operator SDL_FColor() const {
		return { r, g, b, a };
	}

	/// Équivalent explicite de `operator SDL_Color()`, mais vers @ref Color
	/// (le type de la bibliothèque plutôt que le type C de SDL).
	[[nodiscard]] constexpr Color ToByte() const {
		return {uint8_t(r * 255), uint8_t(g * 255), uint8_t(b * 255), uint8_t(a * 255)};
	}


	// =====================================================================
	// Comparaison
	// =====================================================================

	/// Égalité exacte des 4 composantes (pas de tolérance — deux couleurs
	/// visuellement identiques mais issues de calculs flottants différents
	/// peuvent comparer inégales).
	[[nodiscard]]
	constexpr bool operator==(const FColor& c) const {
		return r == c.r &&
				g == c.g &&
				b == c.b &&
				a == c.a;
	}

	[[nodiscard]]
	constexpr bool operator!=(const FColor& c) const {
		return !(*this == c);
	}


	// =====================================================================
	// Opérations entre couleurs
	// =====================================================================
	// Arithmétique composante par composante (RGBA). Le résultat repasse par
	// le constructeur clampant : chaque composante reste dans [0, 1] même si
	// l'opération dépasserait ces bornes (ex. addition de deux couleurs claires).

	[[nodiscard]]
	constexpr FColor operator+(const FColor& c) const {
		return {
			r + c.r,
			g + c.g,
			b + c.b,
			a + c.a
		};
	}

	[[nodiscard]]
	constexpr FColor operator-(const FColor& c) const {
		return {
			r - c.r,
			g - c.g,
			b - c.b,
			a - c.a
		};
	}

	[[nodiscard]]
	constexpr FColor operator*(const FColor& c) const {
		return {
			r * c.r,
			g * c.g,
			b * c.b,
			a * c.a
		};
	}

	[[nodiscard]]
	constexpr FColor operator/(const FColor& c) const {
		return {
			c.r != 0.f ? r / c.r : 0.f,
			c.g != 0.f ? g / c.g : 0.f,
			c.b != 0.f ? b / c.b : 0.f,
			c.a != 0.f ? a / c.a : 0.f
		};
	}


	// =====================================================================
	// Opérations avec un scalaire
	// =====================================================================
	// Comme ci-dessus (résultat clampé), mais avec un facteur unique appliqué
	// aux 4 composantes — pratique pour un fondu global (`color * alpha`).

	[[nodiscard]]
	constexpr FColor operator*(float value) const {
		return {
			r * value,
			g * value,
			b * value,
			a * value
		};
	}

	/// Division par zéro : retourne la couleur par défaut (noir transparent)
	/// plutôt que de propager `inf`/`nan`.
	[[nodiscard]]
	constexpr FColor operator/(float value) const {
		if (value == 0.f)
			return {};

		return {
			r / value,
			g / value,
			b / value,
			a / value
		};
	}


	// =====================================================================
	// Assignations
	// =====================================================================
	// Formes composées (`+=`, `*=`, ...) des opérateurs binaires ci-dessus —
	// mêmes garanties de clamping, rien de spécifique à documenter par méthode.

	constexpr FColor& operator+=(const FColor& c) {
		return *this = *this + c;
	}

	constexpr FColor& operator-=(const FColor& c) {
		return *this = *this - c;
	}

	constexpr FColor& operator*=(const FColor& c) {
		return *this = *this * c;
	}

	constexpr FColor& operator/=(const FColor& c) {
		return *this = *this / c;
	}

	constexpr FColor& operator*=(float value) {
		return *this = *this * value;
	}

	constexpr FColor& operator/=(float value) {
		return *this = *this / value;
	}


	// =====================================================================
	// Darken
	// =====================================================================
	// Convention utilisée pour Darken/Lighten/Invert/Grayscale/Saturate/
	// Desaturate ci-après : la forme au présent (`Darken`) modifie *this en
	// place et le retourne (chaînable) ; la forme au participe passé
	// (`Darkened`) opère sur une copie et laisse l'original intact — même
	// paire que `Reset`/`Reseted` n'existe pas dans ce fichier, mais le
	// principe est identique à `Applied`/`AppliedRGB` plus bas.

	/**
	 * @brief Assombrit vers le noir (alpha inchangé).
	 *
	 * `factor` est clampé à [0, 1] : 0 donne noir (RGB à 0), 1 laisse la
	 * couleur inchangée.
	 *
	 * @code
	 * FColor c{1.f, .5f, .2f};
	 * c.Darken(0.5f); // -> {0.5f, 0.25f, 0.1f, ...}
	 * @endcode
	 */
	constexpr FColor& Darken(float factor) {
		factor = Clamp(factor, 0.f, 1.f);

		r *= factor;
		g *= factor;
		b *= factor;

		return *this;
	}

	[[nodiscard]]
	constexpr FColor Darkened(float factor) const {
		FColor result = *this;
		return result.Darken(factor);
	}


	// =====================================================================
	// Lighten
	// =====================================================================

	/**
	 * @brief Éclaircit vers le blanc (alpha inchangé).
	 *
	 * `factor` clampé à [0, 1] : 0 laisse la couleur inchangée, 1 donne
	 * blanc. Interpolation composante par composante vers 1.0 (pas une
	 * simple addition — évite tout dépassement).
	 */
	constexpr FColor& Lighten(float factor) {
		factor = Clamp(factor, 0.f, 1.f);

		r += (1.f - r) * factor;
		g += (1.f - g) * factor;
		b += (1.f - b) * factor;

		return *this;
	}

	[[nodiscard]]
	constexpr FColor Lightened(float factor) const {
		FColor result = *this;
		return result.Lighten(factor);
	}


	// =====================================================================
	// Inversion
	// =====================================================================

	/// Inverse RGB (`1 - composante`) ; l'alpha n'est volontairement pas
	/// touché (inverser la transparence n'a généralement pas de sens visuel).
	constexpr FColor& Invert() {
		r = 1.f - r;
		g = 1.f - g;
		b = 1.f - b;

		return *this;
	}

	[[nodiscard]]
	constexpr FColor Inverted() const {
		FColor result = *this;
		return result.Invert();
	}


	// =====================================================================
	// Grayscale
	// =====================================================================

	constexpr FColor& Grayscale() {
		// Luminance perceptuelle.
		const float luminance =
			r * 0.2126f +
			g * 0.7152f +
			b * 0.0722f;

		r = luminance;
		g = luminance;
		b = luminance;

		return *this;
	}

	[[nodiscard]]
	constexpr FColor Grayscaled() const {
		FColor result = *this;
		return result.Grayscale();
	}


	// =====================================================================
	// Saturation
	// =====================================================================

	constexpr FColor& Saturate(float factor) {
		const float luminance =
			r * 0.2126f +
			g * 0.7152f +
			b * 0.0722f;

		factor = std::max(factor, 0.f);

		r = luminance + (r - luminance) * factor;
		g = luminance + (g - luminance) * factor;
		b = luminance + (b - luminance) * factor;

		r = Clamp(r, 0.f, 1.f);
		g = Clamp(g, 0.f, 1.f);
		b = Clamp(b, 0.f, 1.f);

		return *this;
	}

	[[nodiscard]]
	constexpr FColor Saturated(float factor) const {
		FColor result = *this;
		return result.Saturate(factor);
	}


	// =====================================================================
	// Désaturation
	// =====================================================================

	constexpr FColor& Desaturate(float factor) {
		factor = Clamp(factor, 0.f, 1.f);

		const float luminance =
			r * 0.2126f +
			g * 0.7152f +
			b * 0.0722f;

		r += (luminance - r) * factor;
		g += (luminance - g) * factor;
		b += (luminance - b) * factor;

		return *this;
	}

	[[nodiscard]]
	constexpr FColor Desaturated(float factor) const {
		FColor result = *this;
		return result.Desaturate(factor);
	}


	// =====================================================================
	// Alpha
	// =====================================================================

	constexpr FColor& SetAlpha(float alpha) {
		a = Clamp(alpha, 0.f, 1.f);
		return *this;
	}

	constexpr FColor& MultiplyAlpha(float factor) {
		a = Clamp(a * factor, 0.f, 1.f);
		return *this;
	}

	[[nodiscard]]
	constexpr FColor WithAlpha(float alpha) const {
		FColor result = *this;
		return result.SetAlpha(alpha);
	}


	// =====================================================================
	// Lerp
	// =====================================================================

	[[nodiscard]]
	static constexpr FColor Lerp(
		const FColor& a,
		const FColor& b,
		float factor, float alpha = 0.f) {
		factor = Clamp(factor, 0.f, 1.f);

		return {
			a.r + (b.r - a.r) * factor,
			a.g + (b.g - a.g) * factor,
			a.b + (b.b - a.b) * factor,
			(alpha == 0.f) ? (a.a + (b.a - a.a) * factor) : alpha
		};
	}


	constexpr FColor& LerpTo(
		const FColor& other,
		float factor) {
		*this = Lerp(*this, other, factor);
		return *this;
	}


	// =====================================================================
	// Blend
	// =====================================================================

	[[nodiscard]]
	static constexpr FColor Blend(
		const FColor& source,
		const FColor& destination) {
		const float outAlpha =
			source.a + destination.a * (1.f - source.a);

		if (outAlpha <= 0.f)
			return {};

		return {
			(
				source.r * source.a +
				destination.r * destination.a * (1.f - source.a)
			) / outAlpha,

			(
				source.g * source.a +
				destination.g * destination.a * (1.f - source.a)
			) / outAlpha,

			(
				source.b * source.a +
				destination.b * destination.a * (1.f - source.a)
			) / outAlpha,

			outAlpha
		};
	}


	// =====================================================================
	// Multiply blending
	// =====================================================================

	[[nodiscard]]
	constexpr FColor Multiply(const FColor& c) const {
		return {
			r * c.r,
			g * c.g,
			b * c.b,
			a
		};
	}


	// =====================================================================
	// Screen blending
	// =====================================================================

	[[nodiscard]]
	constexpr FColor Screen(const FColor& c) const {
		return {
			1.f - (1.f - r) * (1.f - c.r),
			1.f - (1.f - g) * (1.f - c.g),
			1.f - (1.f - b) * (1.f - c.b),
			a
		};
	}


	// =====================================================================
	// Overlay blending
	// =====================================================================

	[[nodiscard]]
	constexpr FColor Overlay(const FColor& c) const {
		const auto overlay = [](float base, float blend)
		{
			return base < 0.5f
				? 2.f * base * blend
				: 1.f - 2.f * (1.f - base) * (1.f - blend);
		};

		return {
			overlay(r, c.r),
			overlay(g, c.g),
			overlay(b, c.b),
			a
		};
	}


	// =====================================================================
	// Apply
	// =====================================================================

	/*
		* Applique une fonction à chaque composante RGBA.
		*
		* Exemple :
		*
		* color.Apply([](float x) {
		*     return x * x;
		* });
		*/

	template<std::invocable<float> F>
	constexpr FColor& Apply(F&& function) {
		r = Clamp(
			static_cast<float>(std::invoke(function, r)),
			0.f,
			1.f
		);

		g = Clamp(
			static_cast<float>(std::invoke(function, g)),
			0.f,
			1.f
		);

		b = Clamp(
			static_cast<float>(std::invoke(function, b)),
			0.f,
			1.f
		);

		a = Clamp(
			static_cast<float>(std::invoke(function, a)),
			0.f,
			1.f
		);

		return *this;
	}


	template<std::invocable<float> F>
	[[nodiscard]]
	FColor Applied(F&& function) const {
		FColor result = *this;
		result.Apply(std::forward<F>(function));
		return result;
	}


	// =====================================================================
	// ApplyRGB
	// =====================================================================

	template<std::invocable<float> F>
	constexpr FColor& ApplyRGB(F&& function) {
		r = Clamp(
			static_cast<float>(std::invoke(function, r)),
			0.f,
			1.f
		);

		g = Clamp(
			static_cast<float>(std::invoke(function, g)),
			0.f,
			1.f
		);

		b = Clamp(
			static_cast<float>(std::invoke(function, b)),
			0.f,
			1.f
		);

		return *this;
	}


	template<std::invocable<float> F>
	[[nodiscard]]
	FColor AppliedRGB(F&& function) const {
		FColor result = *this;
		result.ApplyRGB(std::forward<F>(function));
		return result;
	}


	// =====================================================================
	// ApplyAlpha
	// =====================================================================

	/// Comme @ref Apply, mais uniquement sur l'alpha — RGB inchangé.
	template<std::invocable<float> F>
	constexpr FColor& ApplyAlpha(F&& function) {
		a = Clamp(
			static_cast<float>(std::invoke(function, a)),
			0.f,
			1.f
		);

		return *this;
	}


	template<std::invocable<float> F>
	[[nodiscard]]
	FColor AppliedAlpha(F&& function) const {
		FColor result = *this;
		result.ApplyAlpha(std::forward<F>(function));
		return result;
	}
	/// Couleur nommée de @ref LIST_COLORS (même recherche que `Color::FromName`).
	[[nodiscard]] static std::optional<FColor> FromName(std::string_view name);
	/// Nom de la couleur nommée de même valeur, composantes arrondies à l'octet le plus proche.
	[[nodiscard]] std::optional<std::string_view> Name() const;
};



// ============================================================================
// Couleurs nommées : recherche par nom et nom d'une couleur
// ============================================================================

namespace detail {

struct NamedColor {
	std::string_view name;
	Color color;
};

/// Nombre d'entrées de @ref LIST_COLORS (chaque `_c(...)` y est suivi d'un `;`).
inline constexpr size_t kNamedColorCount = [] {
	size_t n = 0;
#define _c(name, r, g, b, a) ++n
	LIST_COLORS
#undef _c
	return n;
}();

/// Table (nom, couleur) dans l'ordre de @ref LIST_COLORS, calculée à la compilation.
inline constexpr std::array<NamedColor, kNamedColorCount> kNamedColors = [] {
	std::array<NamedColor, kNamedColorCount> out{};
	size_t i = 0;
#define _c(name, r, g, b, a) out[i++] = NamedColor{#name, Color(0x##r, 0x##g, 0x##b, 0x##a)}
	LIST_COLORS
#undef _c
	return out;
}();

/// Clé de recherche : minuscules, sans espaces, tirets ni soulignés.
inline std::string ColorKey(std::string_view name) {
	std::string key;
	key.reserve(name.size());
	for (const char c : name)
		if (c != '_' && c != '-' && c != ' ')
			key.push_back(char(std::tolower(static_cast<unsigned char>(c))));
	return key;
}

inline uint32_t PackColor(const Color &c) noexcept {
	return uint32_t(c.r) << 24 | uint32_t(c.g) << 16 | uint32_t(c.b) << 8 | uint32_t(c.a);
}

} // namespace detail

inline std::optional<Color> Color::FromName(std::string_view name) {
	static const std::unordered_map<std::string, Color> byKey = [] {
		std::unordered_map<std::string, Color> m;
		m.reserve(detail::kNamedColorCount);
		for (const detail::NamedColor &n : detail::kNamedColors)
			m.emplace(detail::ColorKey(n.name), n.color);
		return m;
	}();
	const auto it = byKey.find(detail::ColorKey(name));
	if (it == byKey.end())
		return std::nullopt;
	return it->second;
}

inline std::span<const std::string_view> Color::Names() {
	static constexpr std::array<std::string_view, detail::kNamedColorCount> names = [] {
		std::array<std::string_view, detail::kNamedColorCount> out{};
		for (size_t i = 0; i < out.size(); ++i)
			out[i] = detail::kNamedColors[i].name;
		return out;
	}();
	return names;
}

inline std::optional<std::string_view> Color::Name() const {
	static const std::unordered_map<uint32_t, std::string_view> byValue = [] {
		std::unordered_map<uint32_t, std::string_view> m;
		m.reserve(detail::kNamedColorCount);
		for (const detail::NamedColor &n : detail::kNamedColors)
			m.emplace(detail::PackColor(n.color), n.name); // emplace garde le premier nom
		return m;
	}();
	const auto it = byValue.find(detail::PackColor(*this));
	if (it == byValue.end())
		return std::nullopt;
	return it->second;
}

inline std::optional<FColor> FColor::FromName(std::string_view name) {
	const std::optional<Color> c = Color::FromName(name);
	if (!c)
		return std::nullopt;
	return FColor(*c);
}

inline std::optional<std::string_view> FColor::Name() const {
	const auto byte = [](float v) { return uint8_t(std::lround(Clamp(v, 0.f, 1.f) * 255.f)); };
	return Color(byte(r), byte(g), byte(b), byte(a)).Name();
}


// ============================================================================
// Point / FPoint
// ============================================================================

struct FPoint;

struct Point {
	int x = 0, y = 0;

	constexpr Point() = default;
	constexpr Point(int x_, int y_) : x(x_), y(y_) {}
	constexpr explicit Point(SDL_Point p) : x(p.x), y(p.y) {}
	constexpr explicit Point(FPoint p);

	[[nodiscard]] constexpr operator SDL_Point() const { return {x, y}; }

	[[nodiscard]] constexpr Point operator+(Point o) const { return {x + o.x, y + o.y}; }
	[[nodiscard]] constexpr Point operator-(Point o) const { return {x - o.x, y - o.y}; }
	[[nodiscard]] constexpr Point operator*(int s) const { return {x * s, y * s}; }
	[[nodiscard]] constexpr Point operator/(int s) const { return {x / s, y / s}; }

	constexpr bool operator==(const Point &other) const { return (x == other.x) && (y == other.y); }
};

struct FPoint {
	float x = 0.f, y = 0.f;

	constexpr FPoint() = default;
	constexpr FPoint(float x_, float y_) : x(x_), y(y_) {}
	constexpr explicit FPoint(SDL_FPoint p) : x(p.x), y(p.y) {}
	constexpr explicit FPoint(Point p) : x(float(p.x)), y(float(p.y)) {}
	/// Interop avec le module math (Foundation) : math::FVector2 <-> sdl3::FPoint.
	constexpr FPoint(const math::FVector2 &v) : x(v.x), y(v.y) {}
	[[nodiscard]] constexpr operator math::FVector2() const { return {x, y}; }

	[[nodiscard]] constexpr operator SDL_FPoint() const { return {x, y}; }
	[[nodiscard]] constexpr operator SDL_Point() const {
		return {static_cast<int>(Round(x)), static_cast<int>(Round(y))};
	}

	[[nodiscard]] constexpr FPoint operator+(FPoint o) const { return {x + o.x, y + o.y}; }
	[[nodiscard]] constexpr FPoint operator-(FPoint o) const { return {x - o.x, y - o.y}; }
	[[nodiscard]] constexpr FPoint operator*(float s) const { return {x * s, y * s}; }
	[[nodiscard]] constexpr FPoint operator/(float s) const { return {x / s, y / s}; }

	constexpr bool operator==(const FPoint &other) const { return (x == other.x) && (y == other.y); }

	/// Produit scalaire.
	[[nodiscard]] constexpr float Dot(const FPoint &o) const { return x * o.x + y * o.y; }
	/// Norme euclidienne.
	[[nodiscard]] float Length() const { return std::sqrt(x * x + y * y); }
	/// Distance euclidienne à `o`.
	[[nodiscard]] float Distance(const FPoint &o) const { return (*this - o).Length(); }
	/// Interpolation linéaire composante par composante.
	[[nodiscard]] constexpr FPoint Lerp(const FPoint &o, float t) const { return {x + (o.x - x) * t, y + (o.y - y) * t}; }
	/// Vecteur de même direction, norme 1 (vecteur nul si longueur nulle).
	[[nodiscard]] FPoint Normalized() const { const float l = Length(); return l > 0.f ? FPoint{x / l, y / l} : FPoint{}; }
};

constexpr Point::Point(FPoint r) :
	x(static_cast<int>(Round(r.x))), y(static_cast<int>(Round(r.y))) {}

// ============================================================================
// Rect / FRect
// ============================================================================

struct FRect;

struct Rect {
	int x = 0, y = 0, w = 0, h = 0;

	constexpr Rect() = default;
	constexpr Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
	constexpr Rect(Point pos, Point size) : x(pos.x), y(pos.y), w(size.x), h(size.y) {}
	constexpr explicit Rect(SDL_Rect r) : x(r.x), y(r.y), w(r.w), h(r.h) {}
	constexpr explicit Rect(FRect r);

	[[nodiscard]] constexpr operator SDL_Rect() const { return {x, y, w, h}; }

	[[nodiscard]] constexpr Point Pos() const { return {x, y}; }
	[[nodiscard]] constexpr Point GetSize() const { return {w, h}; }
	[[nodiscard]] constexpr Point Center() const { return {x + w / 2, y + h / 2}; }
	[[nodiscard]] constexpr bool Contains(Point p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
	[[nodiscard]] constexpr bool Intersects(Rect o) const {
		return x < o.x + o.w && x + w > o.x && y < o.y + o.h && y + h > o.y;
	}
	constexpr bool operator==(const Rect &other) const {
		return (x == other.x) && (y == other.y) && (w == other.w) && (h == other.h);
	}
};

struct FRect {
	float x = 0.f, y = 0.f, w = 0.f, h = 0.f;

	constexpr FRect() = default;
	constexpr FRect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
	constexpr FRect(FPoint pos, FPoint size) : x(pos.x), y(pos.y), w(size.x), h(size.y) {}
	constexpr explicit FRect(SDL_FRect r) : x(r.x), y(r.y), w(r.w), h(r.h) {}
	constexpr explicit FRect(Rect r) : x(float(r.x)), y(float(r.y)), w(float(r.w)), h(float(r.h)) {}

	[[nodiscard]] constexpr operator SDL_FRect() const { return {x, y, w, h}; }
	[[nodiscard]] constexpr operator SDL_Rect() const {
		return {static_cast<int>(Round(x)), static_cast<int>(Round(y)),
				static_cast<int>(Round(w)), static_cast<int>(Round(h))};
	}

	[[nodiscard]] constexpr FPoint Pos() const { return {x, y}; }
	[[nodiscard]] constexpr FPoint GetSize() const { return {w, h}; }
	[[nodiscard]] constexpr FPoint Center() const { return {x + w * .5f, y + h * .5f}; }
	[[nodiscard]] constexpr float Right() const { return x + w; }
	[[nodiscard]] constexpr float Bottom() const { return y + h; }
	[[nodiscard]] constexpr bool Contains(FPoint p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
	[[nodiscard]] constexpr bool Contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
	[[nodiscard]] constexpr bool Intersects(const FRect &o) const {
		return x < o.x + o.w && x + w > o.x && y < o.y + o.h && y + h > o.y;
	}
	/// Rectangle décalé vers l'intérieur de `d` sur chaque bord.
	[[nodiscard]] constexpr FRect Inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
	/// Rectangle décalé vers l'intérieur de `dx` (horizontal) et `dy` (vertical).
	[[nodiscard]] constexpr FRect Inset(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
	/// Rectangle d'intersection (taille nulle si disjoint).
	[[nodiscard]] constexpr FRect Intersection(const FRect &o) const {
		float x1 = x > o.x ? x : o.x;
		float y1 = y > o.y ? y : o.y;
		float x2 = (x + w < o.x + o.w) ? x + w : o.x + o.w;
		float y2 = (y + h < o.y + o.h) ? y + h : o.y + o.h;
		return {x1, y1, x2 > x1 ? x2 - x1 : 0.f, y2 > y1 ? y2 - y1 : 0.f};
	}
	/// Alias de `Intersection` (rectangle d'intersection).
	[[nodiscard]] constexpr FRect Intersect(const FRect &o) const { return Intersection(o); }
	constexpr bool operator==(const FRect &other) const {
		return (x == other.x) && (y == other.y) && (w == other.w) && (h == other.h);
	}
};

constexpr Rect::Rect(FRect r) :
	x(static_cast<int>(Round(r.x))), y(static_cast<int>(Round(r.y))),
	w(static_cast<int>(Round(r.w))), h(static_cast<int>(Round(r.h))) {}

// ============================================================================
// Fonctions libres sur rectangles (SDL_rect.h)
// ============================================================================

namespace rect {

[[nodiscard]] inline bool Empty(const Rect &r) { const SDL_Rect x = r; return SDL_RectEmpty(&x); }
[[nodiscard]] inline bool Empty(const FRect &r) { const SDL_FRect x = r; return SDL_RectEmptyFloat(&x); }

[[nodiscard]] inline bool Intersects(const Rect &a, const Rect &b) {
	const SDL_Rect A = a, B = b; return SDL_HasRectIntersection(&A, &B);
}
[[nodiscard]] inline bool Intersects(const FRect &a, const FRect &b) {
	const SDL_FRect A = a, B = b; return SDL_HasRectIntersectionFloat(&A, &B);
}

[[nodiscard]] inline Option<Rect> Intersection(const Rect &a, const Rect &b) {
	const SDL_Rect A = a, B = b; SDL_Rect out{}; if (!SDL_GetRectIntersection(&A, &B, &out)) return NONE; return Some(Rect(out));
}
[[nodiscard]] inline Option<FRect> Intersection(const FRect &a, const FRect &b) {
	const SDL_FRect A = a, B = b; SDL_FRect out{}; if (!SDL_GetRectIntersectionFloat(&A, &B, &out)) return NONE; return Some(FRect(out));
}

[[nodiscard]] inline Option<Rect> Union(const Rect &a, const Rect &b) {
	const SDL_Rect A = a, B = b; SDL_Rect out{}; if (!SDL_GetRectUnion(&A, &B, &out)) return NONE; return Some(Rect(out));
}
[[nodiscard]] inline Option<FRect> Union(const FRect &a, const FRect &b) {
	const SDL_FRect A = a, B = b; SDL_FRect out{}; if (!SDL_GetRectUnionFloat(&A, &B, &out)) return NONE; return Some(FRect(out));
}

[[nodiscard]] inline bool Equal(const Rect &a, const Rect &b) {
	const SDL_Rect A = a, B = b; return SDL_RectsEqual(&A, &B);
}
[[nodiscard]] inline bool Equal(const FRect &a, const FRect &b) {
	const SDL_FRect A = a, B = b; return SDL_RectsEqualFloat(&A, &B);
}

/// Plus petit rectangle englobant les points donnés, éventuellement borné par `clip`.
[[nodiscard]] inline Option<Rect> EnclosingPoints(std::span<const SDL_Point> points, Option<Rect> clip = NONE) {
	SDL_Rect out{};
	SDL_Rect clipR{}; const SDL_Rect *clipP = nullptr;
	if (clip) { clipR = *clip; clipP = &clipR; }
	if (!SDL_GetRectEnclosingPoints(points.data(), int(points.size()), clipP, &out)) return NONE;
	return Some(Rect(out));
}

} // namespace rect

// ============================================================================
// Corners / Sides — définis dans le module Foundation math:: (types
// géométriques purs, sans SDL) ; exposés ici pour Renderer::DrawRoundedRect...
// ============================================================================

using Corners = math::Corners;
using Sides = math::Sides;

// ============================================================================
// Vertex
// ============================================================================

struct Vertex {
	FPoint position;  /**< Vertex position, in SDL_Renderer coordinates  */
	FColor color;     /**< Vertex color */
	FPoint texCoord; /**< Normalized texture coordinates, if needed */

	constexpr Vertex() = default;
	constexpr Vertex(const FPoint& position_, const FColor& color_, const FPoint& texCoord_)
		: position(position_), color(color_), texCoord(texCoord_) {}
	constexpr explicit Vertex(const SDL_Vertex& v) : position(v.position), color(v.color), texCoord(v.tex_coord) {}

	[[nodiscard]] constexpr operator SDL_Vertex() const { return {position, color, texCoord}; }

	[[nodiscard]] constexpr bool operator==(const Vertex &other) const {
		return position == other.position && color == other.color && texCoord == other.texCoord;
	}
};

// ============================================================================
// Enums
// ============================================================================

enum class BlendMode : int {
	NONE	= SDL_BLENDMODE_NONE,
	BLEND	= SDL_BLENDMODE_BLEND,
	ADD		= SDL_BLENDMODE_ADD,
	MOD		= SDL_BLENDMODE_MOD,
	MUL		= SDL_BLENDMODE_MUL,
	BLEND_PREMULTIPLIED = SDL_BLENDMODE_BLEND_PREMULTIPLIED, ///< couleurs déjà multipliées par l'alpha
	ADD_PREMULTIPLIED	= SDL_BLENDMODE_ADD_PREMULTIPLIED,
};

enum class FlipMode {
	NONE		= SDL_FLIP_NONE,
	Horizontal	= SDL_FLIP_HORIZONTAL,
	Vertical	= SDL_FLIP_VERTICAL,
};

} // namespace sdl3
