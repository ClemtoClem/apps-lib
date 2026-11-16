#include <cstring>
#include "consoles/arm/gpu/gpu_2d.hpp"
#include "consoles/arm/core.hpp"
#include "consoles/arm/settings.hpp"

namespace consoles::arm {


Gpu2D::Gpu2D(Core *core_, bool engine_) : m_core(core_), m_engine(engine_) {
    if (m_engine == 0) {
        m_bgVramAddr  = 0x6000000;
        m_objVramAddr = 0x6400000;
        m_palette     = m_core->memory.GetPalette();
        m_oam         = m_core->memory.GetOam();
        m_extPalettes = m_core->memory.GetEngAExtPal();
    } else {
        m_bgVramAddr  = 0x6200000;
        m_objVramAddr = 0x6600000;
        m_palette     = m_core->memory.GetPalette() + 0x400;
        m_oam         = m_core->memory.GetOam() + 0x400;
        m_extPalettes = m_core->memory.GetEngBExtPal();
    }
}

uint32_t Gpu2D::Rgb5ToRgb6(uint32_t color) {
    uint8_t r = ((color >> 0) & 0x1F) * 2;
    uint8_t g = ((color >> 5) & 0x1F) * 2;
    uint8_t b = ((color >> 10) & 0x1F) * 2;
    return (color & 0xFFFC0000) | (b << 12) | (g << 6) | r;
}

void Gpu2D::DrawGbaScanline(int line) {
    if (line == 0) {
        m_internalX[0] = m_bgX[0];
        m_internalX[1] = m_bgX[1];
        m_internalY[0] = m_bgY[0];
        m_internalY[1] = m_bgY[1];
    }
    uint16_t backdrop = static_cast<uint16_t>(U8TO16(m_palette, 0) & ~BIT(15));
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 240; j++)
            m_layers[i][j] = backdrop;
        memset(m_priorities[i], 4, 240 * sizeof(uint8_t));
        memset(m_blendBits[i], 5, 240 * sizeof(uint8_t));
    }
    if (m_dispCnt & BIT(12)) {
        if (m_dispCnt & BIT(15))
            DrawObjects<1>(line, true);
        DrawObjects<1>(line, false);
    }
    switch (m_dispCnt & 0x7) {
        case 0:
            if (m_dispCnt & BIT(11))
                DrawText<1>(3, line);
            if (m_dispCnt & BIT(10))
                DrawText<1>(2, line);
            if (m_dispCnt & BIT(9))
                DrawText<1>(1, line);
            if (m_dispCnt & BIT(8))
                DrawText<1>(0, line);
            break;
        case 1:
            if (m_dispCnt & BIT(10))
                DrawAffine<1>(2, line);
            if (m_dispCnt & BIT(9))
                DrawText<1>(1, line);
            if (m_dispCnt & BIT(8))
                DrawText<1>(0, line);
            break;
        case 2:
            if (m_dispCnt & BIT(11))
                DrawAffine<1>(3, line);
            if (m_dispCnt & BIT(10))
                DrawAffine<1>(2, line);
            break;
        case 3:
        case 4:
        case 5:
            if (m_dispCnt & BIT(10))
                DrawExtendedGba(2, line);
            break;
        default:
            LOG_VERBOSE("Unknown GBA BG mode: %d\n", dispCnt & 0x0007);
            break;
    }
    uint8_t mode = (m_bldCnt >> 6) & 0x3;
    for (int i = 0; i < 240; i++) {
        if (m_layers[0][i] & BIT(25)) {
            if (m_bldCnt & BIT(8 + m_blendBits[1][i]))
                goto alpha;
            else if (mode < 2 || !(m_bldCnt & BIT(m_blendBits[0][i])))
                continue;
        } else if (mode == 0 || !(m_bldCnt & BIT(m_blendBits[0][i])) ||
                   (mode == 1 && !(m_bldCnt & BIT(8 + m_blendBits[1][i])))) {
            continue;
        }
        if (m_dispCnt & 0x0000E000) {
            uint8_t enabled;
            if ((m_dispCnt & BIT(13)) && (i >= m_winX1[0] && i < m_winX2[0]) != m_winHFlip[0] &&
                (line >= m_winY1[0] && line < m_winY2[0]) != m_winVFlip[0])
                enabled = static_cast<uint8_t>(m_winIn >> 0);
            else if ((m_dispCnt & BIT(14)) && (i >= m_winX1[1] && i < m_winX2[1]) != m_winHFlip[1] &&
                     (line >= m_winY1[1] && line < m_winY2[1]) != m_winVFlip[1])
                enabled = static_cast<uint8_t>(m_winIn >> 8);
            else if ((m_dispCnt & BIT(15)) && (m_framebuffer[line * 256 + i] & BIT(24)))
                enabled = static_cast<uint8_t>(m_winOut >> 8);
            else
                enabled = static_cast<uint8_t>(m_winOut >> 0);
            if (!(enabled & BIT(5)))
                continue;
        }
        switch (mode) {
            case 1:
            alpha : {
                uint8_t eva = static_cast<uint8_t>(std::min((m_bldAlpha >> 0) & 0x1F, 16));
                uint8_t evb = static_cast<uint8_t>(std::min((m_bldAlpha >> 8) & 0x1F, 16));
                uint8_t r =
                    static_cast<uint8_t>(std::min((((m_layers[0][i] >> 0) & 0x1F) * eva + ((m_layers[1][i] >> 0) & 0x1F) * evb) / 16, 31U));
                uint8_t g =
                    static_cast<uint8_t>(std::min((((m_layers[0][i] >> 5) & 0x1F) * eva + ((m_layers[1][i] >> 5) & 0x1F) * evb) / 16, 31U));
                uint8_t b =
                    static_cast<uint8_t>(std::min((((m_layers[0][i] >> 10) & 0x1F) * eva + ((m_layers[1][i] >> 10) & 0x1F) * evb) / 16, 31U));
                m_layers[0][i] = (b << 10) | (g << 5) | r;
                continue;
            }
            case 2:
                if (m_bldY) {
                    uint8_t r = (m_layers[0][i] >> 0) & 0x1F;
                    r = static_cast<uint8_t>(r + ((31 - r) * m_bldY / 16));
                    uint8_t g = (m_layers[0][i] >> 5) & 0x1F;
                    g = static_cast<uint8_t>(g + ((31 - g) * m_bldY / 16));
                    uint8_t b = (m_layers[0][i] >> 10) & 0x1F;
                    b = static_cast<uint8_t>(b + ((31 - b) * m_bldY / 16));
                    m_layers[0][i] = (b << 10) | (g << 5) | r;
                }
                continue;
            case 3:
                if (m_bldY) {
                    uint8_t r = (m_layers[0][i] >> 0) & 0x1F;
                    r = static_cast<uint8_t>(r - (r * m_bldY / 16));
                    uint8_t g = (m_layers[0][i] >> 5) & 0x1F;
                    g = static_cast<uint8_t>(g - (g * m_bldY / 16));
                    uint8_t b = (m_layers[0][i] >> 10) & 0x1F;
                    b = static_cast<uint8_t>(b - (b * m_bldY / 16));
                    m_layers[0][i] = (b << 10) | (g << 5) | r;
                }
                continue;
        }
    }
    memcpy(&m_framebuffer[line * 256], m_layers[0], 240 * sizeof(uint32_t));
}

void Gpu2D::DrawScanline(int line) {
    if (line == 0) {
        m_internalX[0] = m_bgX[0];
        m_internalX[1] = m_bgX[1];
        m_internalY[0] = m_bgY[0];
        m_internalY[1] = m_bgY[1];
    }
    uint16_t backdrop = static_cast<uint16_t>(U8TO16(m_palette, 0) & ~BIT(15));
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 256; j++)
            m_layers[i][j] = backdrop;
        memset(m_priorities[i], 4, 256 * sizeof(uint8_t));
        memset(m_blendBits[i], 5, 256 * sizeof(uint8_t));
    }
    if (m_dispCnt & BIT(12)) {
        if (m_dispCnt & BIT(15))
            DrawObjects<0>(line, true);
        DrawObjects<0>(line, false);
    }
    switch (m_dispCnt & 0x7) {
        case 0:
            if (m_dispCnt & BIT(11))
                DrawText<0>(3, line);
            if (m_dispCnt & BIT(10))
                DrawText<0>(2, line);
            if (m_dispCnt & BIT(9))
                DrawText<0>(1, line);
            if (m_dispCnt & BIT(8))
                DrawText<0>(0, line);
            break;
        case 1:
            if (m_dispCnt & BIT(11))
                DrawAffine<0>(3, line);
            if (m_dispCnt & BIT(10))
                DrawText<0>(2, line);
            if (m_dispCnt & BIT(9))
                DrawText<0>(1, line);
            if (m_dispCnt & BIT(8))
                DrawText<0>(0, line);
            break;
        case 2:
            if (m_dispCnt & BIT(11))
                DrawAffine<0>(3, line);
            if (m_dispCnt & BIT(10))
                DrawAffine<0>(2, line);
            if (m_dispCnt & BIT(9))
                DrawText<0>(1, line);
            if (m_dispCnt & BIT(8))
                DrawText<0>(0, line);
            break;
        case 3:
            if (m_dispCnt & BIT(11))
                DrawExtended(3, line);
            if (m_dispCnt & BIT(10))
                DrawText<0>(2, line);
            if (m_dispCnt & BIT(9))
                DrawText<0>(1, line);
            if (m_dispCnt & BIT(8))
                DrawText<0>(0, line);
            break;
        case 4:
            if (m_dispCnt & BIT(11))
                DrawExtended(3, line);
            if (m_dispCnt & BIT(10))
                DrawAffine<0>(2, line);
            if (m_dispCnt & BIT(9))
                DrawText<0>(1, line);
            if (m_dispCnt & BIT(8))
                DrawText<0>(0, line);
            break;
        case 5:
            if (m_dispCnt & BIT(11))
                DrawExtended(3, line);
            if (m_dispCnt & BIT(10))
                DrawExtended(2, line);
            if (m_dispCnt & BIT(9))
                DrawText<0>(1, line);
            if (m_dispCnt & BIT(8))
                DrawText<0>(0, line);
            break;
        case 6:
            if (m_dispCnt & BIT(10))
                DrawLarge(2, line);
            break;
        default:
            LOG_VERBOSE("Unknown engine %c BG mode: %d\n", ((engine == 0) ? 'A' : 'B'), dispCnt & 0x7);
            break;
    }
    uint8_t mode = (m_bldCnt >> 6) & 0x3;
    for (int i = 0; i < 256; i++) {
        if (m_layers[0][i] & BIT(26)) {
            if (m_bldCnt & BIT(8 + m_blendBits[1][i])) {
                uint8_t eva = ((m_layers[0][i] >> 18) & 0x3F) + 1;
                if (eva == 64)
                    continue;
                uint32_t blend = Rgb5ToRgb6(m_layers[1][i]);
                uint8_t  evb   = 64 - eva;
                uint8_t  r     = static_cast<uint8_t>(std::min((((m_layers[0][i] >> 0) & 0x3F) * eva + ((blend >> 0) & 0x3F) * evb) / 64, 63U));
                uint8_t  g     = static_cast<uint8_t>(std::min((((m_layers[0][i] >> 6) & 0x3F) * eva + ((blend >> 6) & 0x3F) * evb) / 64, 63U));
                uint8_t  b   = static_cast<uint8_t>(std::min((((m_layers[0][i] >> 12) & 0x3F) * eva + ((blend >> 12) & 0x3F) * evb) / 64, 63U));
                m_layers[0][i] = (b << 12) | (g << 6) | r;
                continue;
            } else if (mode < 2 || !(m_bldCnt & BIT(m_blendBits[0][i]))) {
                continue;
            }
        } else {
            m_layers[0][i] = Rgb5ToRgb6(m_layers[0][i]);
            if (m_layers[0][i] & BIT(25)) {
                if (m_bldCnt & BIT(8 + m_blendBits[1][i]))
                    goto alpha;
                else if (mode < 2 || !(m_bldCnt & BIT(m_blendBits[0][i])))
                    continue;
            } else if (mode == 0 || !(m_bldCnt & BIT(m_blendBits[0][i])) ||
                       (mode == 1 && !(m_bldCnt & BIT(8 + m_blendBits[1][i])))) {
                continue;
            }
        }
        if (m_dispCnt & 0x0000E000) {
            uint8_t enabled;
            if ((m_dispCnt & BIT(13)) && (i >= m_winX1[0] && i < m_winX2[0]) != m_winHFlip[0] &&
                (line >= m_winY1[0] && line < m_winY2[0]) != m_winVFlip[0])
                enabled = static_cast<uint8_t>(m_winIn >> 0);
            else if ((m_dispCnt & BIT(14)) && (i >= m_winX1[1] && i < m_winX2[1]) != m_winHFlip[1] &&
                     (line >= m_winY1[1] && line < m_winY2[1]) != m_winVFlip[1])
                enabled = static_cast<uint8_t>(m_winIn >> 8);
            else if ((m_dispCnt & BIT(15)) && (m_framebuffer[line * 256 + i] & BIT(24)))
                enabled = static_cast<uint8_t>(m_winOut >> 8);
            else
                enabled = static_cast<uint8_t>(m_winOut >> 0);
            if (!(enabled & BIT(5)))
                continue;
        }
        switch (mode) {
            case 1:
            alpha : {
                uint32_t blend = (m_layers[1][i] & BIT(26)) ? m_layers[1][i] : Rgb5ToRgb6(m_layers[1][i]);
                uint8_t  eva   = static_cast<uint8_t>(std::min((m_bldAlpha >> 0) & 0x1F, 16));
                uint8_t  evb   = static_cast<uint8_t>(std::min((m_bldAlpha >> 8) & 0x1F, 16));
                uint8_t  r     = static_cast<uint8_t>(std::min((((m_layers[0][i] >> 0) & 0x3F) * eva + ((blend >> 0) & 0x3F) * evb) / 16, 63U));
                uint8_t  g     = static_cast<uint8_t>(std::min((((m_layers[0][i] >> 6) & 0x3F) * eva + ((blend >> 6) & 0x3F) * evb) / 16, 63U));
                uint8_t  b   = static_cast<uint8_t>(std::min((((m_layers[0][i] >> 12) & 0x3F) * eva + ((blend >> 12) & 0x3F) * evb) / 16, 63U));
                m_layers[0][i] = (b << 12) | (g << 6) | r;
                continue;
            }
            case 2:
                if (m_bldY) {
                    uint8_t r = (m_layers[0][i] >> 0) & 0x3F;
                    r = static_cast<uint8_t>(r + ((63 - r) * m_bldY / 16));
                    uint8_t g = (m_layers[0][i] >> 6) & 0x3F;
                    g = static_cast<uint8_t>(g + ((63 - g) * m_bldY / 16));
                    uint8_t b = (m_layers[0][i] >> 12) & 0x3F;
                    b = static_cast<uint8_t>(b + ((63 - b) * m_bldY / 16));
                    m_layers[0][i] = (b << 12) | (g << 6) | r;
                }
                continue;
            case 3:
                if (m_bldY) {
                    uint8_t r = (m_layers[0][i] >> 0) & 0x3F;
                    r = static_cast<uint8_t>(r - (r * m_bldY / 16));
                    uint8_t g = (m_layers[0][i] >> 6) & 0x3F;
                    g = static_cast<uint8_t>(g - (g * m_bldY / 16));
                    uint8_t b = (m_layers[0][i] >> 12) & 0x3F;
                    b = static_cast<uint8_t>(b - (b * m_bldY / 16));
                    m_layers[0][i] = (b << 12) | (g << 6) | r;
                }
                continue;
        }
    }
    switch ((m_dispCnt >> 16) & 0x3) {
        case 0: {
            memset(&m_framebuffer[line * 256], 0xFF, 256 * sizeof(uint32_t));
            break;
        }
        case 1: {
            memcpy(&m_framebuffer[line * 256], m_layers[0], 256 * sizeof(uint32_t));
            break;
        }
        case 2: {
            uint32_t address = 0x6800000 + ((m_dispCnt & 0x000C0000) >> 18) * 0x20000 + static_cast<uint32_t>(line * 256 * 2);
            for (int i = 0; i < 256; i++)
                m_framebuffer[line * 256 + i] = Rgb5ToRgb6(m_core->memory.Read<uint16_t>(0, address + static_cast<uint32_t>(i * 2)));
            break;
        }
        case 3: {
            LOG("Unimplemented engine %c display mode: display FIFO\n", ((m_engine == 0) ? 'A' : 'B'));
            break;
        }
    }
    switch ((m_masterBright >> 14) & 0x3) {
        case 1:
            if (uint8_t factor = static_cast<uint8_t>(std::min(m_masterBright & 0x1F, 16))) {
                for (int i = 0; i < 256; i++) {
                    uint32_t *pixel = &m_framebuffer[line * 256 + i];
                    uint8_t   r     = (*pixel >> 0) & 0x3F;
                    r = static_cast<uint8_t>(r + ((63 - r) * factor / 16));
                    uint8_t g = (*pixel >> 6) & 0x3F;
                    g = static_cast<uint8_t>(g + ((63 - g) * factor / 16));
                    uint8_t b = (*pixel >> 12) & 0x3F;
                    b = static_cast<uint8_t>(b + ((63 - b) * factor / 16));
                    *pixel = (b << 12) | (g << 6) | r;
                }
            }
            break;
        case 2:
            if (uint8_t factor = static_cast<uint8_t>(std::min(m_masterBright & 0x1F, 16))) {
                for (int i = 0; i < 256; i++) {
                    uint32_t *pixel = &m_framebuffer[line * 256 + i];
                    uint8_t   r     = (*pixel >> 0) & 0x3F;
                    r = static_cast<uint8_t>(r - (r * factor / 16));
                    uint8_t g = (*pixel >> 6) & 0x3F;
                    g = static_cast<uint8_t>(g - (g * factor / 16));
                    uint8_t b = (*pixel >> 12) & 0x3F;
                    b = static_cast<uint8_t>(b - (b * factor / 16));
                    *pixel = (b << 12) | (g << 6) | r;
                }
            }
            break;
    }
}

void Gpu2D::DrawBgPixel(int bg, int line, int x, uint32_t pixel) {
    if (m_dispCnt & 0x0000E000) {
        uint8_t enabled;
        if ((m_dispCnt & BIT(13)) && (x >= m_winX1[0] && x < m_winX2[0]) != m_winHFlip[0] &&
            (line >= m_winY1[0] && line < m_winY2[0]) != m_winVFlip[0])
            enabled = static_cast<uint8_t>(m_winIn >> 0);
        else if ((m_dispCnt & BIT(14)) && (x >= m_winX1[1] && x < m_winX2[1]) != m_winHFlip[1] &&
                 (line >= m_winY1[1] && line < m_winY2[1]) != m_winVFlip[1])
            enabled = static_cast<uint8_t>(m_winIn >> 8);
        else if ((m_dispCnt & BIT(15)) && (m_framebuffer[line * 256 + x] & BIT(24)))
            enabled = static_cast<uint8_t>(m_winOut >> 8);
        else
            enabled = static_cast<uint8_t>(m_winOut >> 0);
        if (!(enabled & BIT(bg)))
            return;
    }
    if ((m_bgCnt[bg] & 0x3) <= m_priorities[0][x]) {
        m_layers[1][x]     = m_layers[0][x];
        m_priorities[1][x] = m_priorities[0][x];
        m_blendBits[1][x]  = m_blendBits[0][x];
        m_layers[0][x]     = pixel;
        m_priorities[0][x] = m_bgCnt[bg] & 0x3;
        m_blendBits[0][x]  = static_cast<int8_t>(bg);
    } else if ((m_bgCnt[bg] & 0x3) <= m_priorities[1][x]) {
        m_layers[1][x]     = pixel;
        m_priorities[1][x] = m_bgCnt[bg] & 0x3;
        m_blendBits[1][x]  = static_cast<int8_t>(bg);
    }
}

void Gpu2D::DrawObjPixel(int line, int x, uint32_t pixel, int8_t priority) {
    if (m_dispCnt & 0x0000E000) {
        uint8_t enabled;
        if ((m_dispCnt & BIT(13)) && (x >= m_winX1[0] && x < m_winX2[0]) != m_winHFlip[0] &&
            (line >= m_winY1[0] && line < m_winY2[0]) != m_winVFlip[0])
            enabled = static_cast<uint8_t>(m_winIn >> 0);
        else if ((m_dispCnt & BIT(14)) && (x >= m_winX1[1] && x < m_winX2[1]) != m_winHFlip[1] &&
                 (line >= m_winY1[1] && line < m_winY2[1]) != m_winVFlip[1])
            enabled = static_cast<uint8_t>(m_winIn >> 8);
        else if ((m_dispCnt & BIT(15)) && (m_framebuffer[line * 256 + x] & BIT(24)))
            enabled = static_cast<uint8_t>(m_winOut >> 8);
        else
            enabled = static_cast<uint8_t>(m_winOut >> 0);
        if (!(enabled & BIT(4)))
            return;
    }
    if (!(m_layers[0][x] & BIT(15)) || priority < m_priorities[0][x]) {
        m_layers[0][x]     = pixel;
        m_priorities[0][x] = priority;
        m_blendBits[0][x]  = 4;
    }
}

template <bool gbaMode> void Gpu2D::DrawText(int bg, int line) {
    if (!gbaMode && bg == 0 && (m_dispCnt & BIT(3))) {
        uint32_t *data     = m_core->gpu3DRenderer.GetLine(line);
        bool      resShift = m_core->settings.highRes3D;
        for (int i = 0; i < 256; i++) {
            if (data[i << resShift] & 0xFC0000)
                DrawBgPixel(bg, line, i, data[i << resShift]);
        }
        return;
    }
    uint32_t tileBase  = m_bgVramAddr + ((m_dispCnt >> 11) & 0x70000) + ((m_bgCnt[bg] << 3) & 0x0F800);
    uint32_t indexBase = m_bgVramAddr + ((m_dispCnt >> 8) & 0x70000) + ((m_bgCnt[bg] << 12) & 0x3C000);
    int yOffset = (((m_bgCnt[bg] & BIT(6)) ? (line - (line % (((m_mosaic >> 4) & 0xF) + 1))) : line) + m_bgVOfs[bg]) & 0x1FF;
    tileBase += (yOffset & 0xF8) << 3;
    if (yOffset >= 256 && (m_bgCnt[bg] & BIT(15)))
        tileBase += (m_bgCnt[bg] & BIT(14)) ? 0x1000 : 0x800;
    if (m_bgCnt[bg] & BIT(7)) {
        for (int i = 0; i <= (gbaMode ? 240 : 256); i += 8) {
            int      xOffset  = (i + m_bgHOfs[bg]) & 0x1FF;
            uint32_t tileAddr = tileBase + ((xOffset & 0xF8) >> 2);
            if (xOffset >= 256 && (m_bgCnt[bg] & BIT(14)))
                tileAddr += 0x800;
            uint16_t tile = m_core->memory.Read<uint16_t>(gbaMode, tileAddr);
            uint8_t *pal;
            if (m_dispCnt & BIT(30)) {
                int slot = (bg < 2 && (m_bgCnt[bg] & BIT(13))) ? (bg + 2) : bg;
                if (!m_extPalettes[slot])
                    return;
                pal = &m_extPalettes[slot][(tile & 0xF000) >> 3];
            } else {
                pal = m_palette;
            }
            uint32_t indexAddr =
                indexBase + (tile & 0x3FF) * 64 + ((tile & BIT(11)) ? (7 - (yOffset & 7)) : (yOffset & 7)) * 8;
            uint64_t indices = m_core->memory.Read<uint32_t>(gbaMode, indexAddr) |
                               ((uint64_t)m_core->memory.Read<uint32_t>(gbaMode, indexAddr + 4) << 32);
            for (int x = i - (xOffset & 7) + ((tile & BIT(10)) ? 7 : 0); indices != 0;
                 ((tile & BIT(10)) ? x-- : x++), indices >>= 8) {
                if (x >= 0 && x < (gbaMode ? 240 : 256) && (indices & 0xFF))
                    DrawBgPixel(bg, line, x, U8TO16(pal, (indices & 0xFF) * 2) | BIT(15));
            }
        }
    } else {
        for (int i = 0; i <= (gbaMode ? 240 : 256); i += 8) {
            int      xOffset  = (i + m_bgHOfs[bg]) & 0x1FF;
            uint32_t tileAddr = tileBase + ((xOffset & 0xF8) >> 2);
            if (xOffset >= 256 && (m_bgCnt[bg] & BIT(14)))
                tileAddr += 0x800;
            uint16_t tile = m_core->memory.Read<uint16_t>(gbaMode, tileAddr);
            uint8_t *pal  = &m_palette[(tile & 0xF000) >> 7];
            uint32_t indexAddr =
                indexBase + (tile & 0x3FF) * 32 + ((tile & BIT(11)) ? (7 - (yOffset & 7)) : (yOffset & 7)) * 4;
            uint32_t indices = m_core->memory.Read<uint32_t>(gbaMode, indexAddr);
            for (int x = i - (xOffset & 7) + ((tile & BIT(10)) ? 7 : 0); indices != 0;
                 ((tile & BIT(10)) ? x-- : x++), indices >>= 4) {
                if (x >= 0 && x < (gbaMode ? 240 : 256) && (indices & 0xF))
                    DrawBgPixel(bg, line, x, U8TO16(pal, (indices & 0xF) * 2) | BIT(15));
            }
        }
    }
}

template <bool gbaMode> void Gpu2D::DrawAffine(int bg, int line) {
    uint32_t tileBase  = m_bgVramAddr + ((m_bgCnt[bg] << 3) & 0x0F800) + ((m_dispCnt >> 11) & 0x70000);
    uint32_t indexBase = m_bgVramAddr + ((m_bgCnt[bg] << 12) & 0x3C000) + ((m_dispCnt >> 8) & 0x70000);
    int      rotscaleX = m_internalX[bg - 2] - m_bgPA[bg - 2];
    int      rotscaleY = m_internalY[bg - 2] - m_bgPC[bg - 2];
    int      size      = 128 << ((m_bgCnt[bg] & 0xC000) >> 14);
    for (int i = 0; i < (gbaMode ? 240 : 256); i++) {
        int x = (rotscaleX += m_bgPA[bg - 2]) >> 8;
        int y = (rotscaleY += m_bgPC[bg - 2]) >> 8;
        if (bg < 2 || (m_bgCnt[bg] & BIT(13))) {
            x &= size - 1;
            y &= size - 1;
        } else if (x < 0 || x >= size || y < 0 || y >= size) {
            continue;
        }
        uint32_t tileAddr  = tileBase + static_cast<uint32_t>((y / 8) * (size / 8)) + static_cast<uint32_t>((x / 8));
        uint8_t  tile      = m_core->memory.Read<uint8_t>(gbaMode, tileAddr);
        uint32_t indexAddr = indexBase + tile * 64 + (y & 7) * 8 + (x & 7);
        uint8_t  index     = m_core->memory.Read<uint8_t>(gbaMode, indexAddr);
        if (index)
            DrawBgPixel(bg, line, i, U8TO16(m_palette, index * 2) | BIT(15));
    }
    m_internalX[bg - 2] += m_bgPB[bg - 2];
    m_internalY[bg - 2] += m_bgPD[bg - 2];
}

void Gpu2D::DrawExtended(int bg, int line) {
    int rotscaleX = m_internalX[bg - 2] - m_bgPA[bg - 2];
    int rotscaleY = m_internalY[bg - 2] - m_bgPC[bg - 2];
    if (m_bgCnt[bg] & BIT(7)) {
        uint32_t dataBase = m_bgVramAddr + ((m_bgCnt[bg] << 6) & 0x7C000);
        int      sizeX, sizeY;
        switch ((m_bgCnt[bg] >> 14) & 0x3) {
            case 0:
                sizeX = 128;
                sizeY = 128;
                break;
            case 1:
                sizeX = 256;
                sizeY = 256;
                break;
            case 2:
                sizeX = 512;
                sizeY = 256;
                break;
            case 3:
                sizeX = 512;
                sizeY = 512;
                break;
        }
        if (m_bgCnt[bg] & BIT(2)) {
            for (int i = 0; i < 256; i++) {
                int x = (rotscaleX += m_bgPA[bg - 2]) >> 8;
                int y = (rotscaleY += m_bgPC[bg - 2]) >> 8;
                if (m_bgCnt[bg] & BIT(13)) {
                    x &= sizeX - 1;
                    y &= sizeY - 1;
                } else if (x < 0 || x >= sizeX || y < 0 || y >= sizeY) {
                    continue;
                }
                uint16_t pixel = m_core->memory.Read<uint16_t>(0, dataBase + static_cast<uint32_t>((y * sizeX + x) * 2));
                if (pixel & BIT(15))
                    DrawBgPixel(bg, line, i, pixel);
            }
        } else {
            for (int i = 0; i < 256; i++) {
                int x = (rotscaleX += m_bgPA[bg - 2]) >> 8;
                int y = (rotscaleY += m_bgPC[bg - 2]) >> 8;
                if (m_bgCnt[bg] & BIT(13)) {
                    x &= sizeX - 1;
                    y &= sizeY - 1;
                } else if (x < 0 || x >= sizeX || y < 0 || y >= sizeY) {
                    continue;
                }
                uint8_t index = m_core->memory.Read<uint8_t>(0, dataBase + static_cast<uint32_t>(y * sizeX) + static_cast<uint32_t>(x));
                if (index)
                    DrawBgPixel(bg, line, i, U8TO16(m_palette, index * 2) | BIT(15));
            }
        }
    } else {
        uint32_t tileBase  = m_bgVramAddr + ((m_bgCnt[bg] << 3) & 0x0F800) + ((m_dispCnt >> 11) & 0x70000);
        uint32_t indexBase = m_bgVramAddr + ((m_bgCnt[bg] << 12) & 0x3C000) + ((m_dispCnt >> 8) & 0x70000);
        size_t   size      = 128 << ((m_bgCnt[bg] >> 14) & 0x3);
        uint8_t *pal       = m_palette;
        for (int i = 0; i < 256; i++) {
            int x = (rotscaleX += m_bgPA[bg - 2]) >> 8;
            int y = (rotscaleY += m_bgPC[bg - 2]) >> 8;
            if (m_bgCnt[bg] & BIT(13)) {
                x = static_cast<int>(static_cast<size_t>(x) & (size - 1));
                y = static_cast<int>(static_cast<size_t>(y) & (size - 1));
            } else if (x < 0 || size_t(x) >= size || y < 0 || size_t(y) >= size) {
                continue;
            }
            uint32_t tileAddr = static_cast<uint32_t>(tileBase + (static_cast<size_t>((y / 8)) * (size / 8) + static_cast<size_t>((x / 8))) * 2);
            uint16_t tile     = m_core->memory.Read<uint16_t>(0, tileAddr);
            if (m_dispCnt & BIT(30)) {
                if (!m_extPalettes[bg])
                    continue;
                pal = &m_extPalettes[bg][(tile >> 3) & 0x1E00];
            }
            uint32_t indexAddr = indexBase + (tile & 0x3FF) * 64 + (((tile & BIT(11)) ? (7 - y) : y) & 7) * 8 +
                                 (((tile & BIT(10)) ? (7 - x) : x) & 7);
            uint8_t index = m_core->memory.Read<uint8_t>(0, indexAddr);
            if (index)
                DrawBgPixel(bg, line, i, U8TO16(pal, index * 2) | BIT(15));
        }
    }
    m_internalX[bg - 2] += m_bgPB[bg - 2];
    m_internalY[bg - 2] += m_bgPD[bg - 2];
}

void Gpu2D::DrawExtendedGba(int bg, int line) {
    uint8_t  mode      = m_dispCnt & 0x7;
    uint32_t dataBase  = m_bgVramAddr + ((m_bgCnt[bg] << 3) & 0xF800) + ((mode > 3 && (m_dispCnt & BIT(4))) ? 0xA000 : 0);
    int      rotscaleX = m_internalX[bg - 2] - m_bgPA[bg - 2];
    int      rotscaleY = m_internalY[bg - 2] - m_bgPC[bg - 2];
    int      sizeX     = (mode == 5) ? 160 : 240;
    int      sizeY     = (mode == 5) ? 128 : 160;
    if (mode == 4) {
        for (int i = 0; i < 240; i++) {
            int x = (rotscaleX += m_bgPA[bg - 2]) >> 8;
            int y = (rotscaleY += m_bgPC[bg - 2]) >> 8;
            if (x < 0 || x >= sizeX || y < 0 || y >= sizeY)
                continue;
            uint8_t index = m_core->memory.Read<uint8_t>(1, dataBase + static_cast<uint32_t>(y * sizeX) + static_cast<uint32_t>(x));
            if (index)
                DrawBgPixel(bg, line, i, U8TO16(m_palette, index * 2) | BIT(15));
        }
    } else {
        for (int i = 0; i < 240; i++) {
            int x = (rotscaleX += m_bgPA[bg - 2]) >> 8;
            int y = (rotscaleY += m_bgPC[bg - 2]) >> 8;
            if (x < 0 || x >= sizeX || y < 0 || y >= sizeY)
                continue;
            uint16_t pixel = m_core->memory.Read<uint16_t>(1, dataBase + static_cast<uint32_t>((y * sizeX + x) * 2));
            DrawBgPixel(bg, line, i, pixel | BIT(15));
        }
    }
    m_internalX[bg - 2] += m_bgPB[bg - 2];
    m_internalY[bg - 2] += m_bgPD[bg - 2];
}

void Gpu2D::DrawLarge(int bg, int line) {
    int rotscaleX = m_internalX[bg - 2] - m_bgPA[bg - 2];
    int rotscaleY = m_internalY[bg - 2] - m_bgPC[bg - 2];
    int sizeX     = ((m_bgCnt[bg] >> 14) & 0x3) ? 1024 : 512;
    int sizeY     = ((m_bgCnt[bg] >> 14) & 0x3) ? 512 : 1024;
    for (int i = 0; i < 256; i++) {
        int x = (rotscaleX += m_bgPA[bg - 2]) >> 8;
        int y = (rotscaleY += m_bgPC[bg - 2]) >> 8;
        if (m_bgCnt[bg] & BIT(13)) {
            x &= sizeX - 1;
            y &= sizeY - 1;
        } else if (x < 0 || x >= sizeX || y < 0 || y >= sizeY) {
            continue;
        }
        if (m_engine == 1)
            y &= (sizeY / 4) - 1;
        uint8_t index = m_core->memory.Read<uint8_t>(0, m_bgVramAddr + static_cast<uint32_t>(y * sizeX) + static_cast<uint32_t>(x));
        if (index)
            DrawBgPixel(bg, line, i, U8TO16(m_palette, index * 2) | BIT(15));
    }
    m_internalX[bg - 2] += m_bgPB[bg - 2];
    m_internalY[bg - 2] += m_bgPD[bg - 2];
}

template <bool gbaMode> void Gpu2D::DrawObjects(int line, bool window) {
    for (int i = 0; i < 128; i++) {
        uint8_t byte = m_oam[i * 8 + 1];
        uint8_t type = (byte >> 2) & 0x3;
        if ((byte & 0x3) == 2 || (type == 2) != window)
            continue;
        uint16_t object[3] = {(uint16_t)U8TO16(m_oam, i * 8 + 0), (uint16_t)U8TO16(m_oam, i * 8 + 2),
                              (uint16_t)U8TO16(m_oam, i * 8 + 4)};
        int      width, height;
        switch (((object[0] >> 12) & 0xC) | ((object[1] >> 14) & 0x3)) {
            case 0x0:
                width  = 8;
                height = 8;
                break;
            case 0x1:
                width  = 16;
                height = 16;
                break;
            case 0x2:
                width  = 32;
                height = 32;
                break;
            case 0x3:
                width  = 64;
                height = 64;
                break;
            case 0x4:
                width  = 16;
                height = 8;
                break;
            case 0x5:
                width  = 32;
                height = 8;
                break;
            case 0x6:
                width  = 32;
                height = 16;
                break;
            case 0x7:
                width  = 64;
                height = 32;
                break;
            case 0x8:
                width  = 8;
                height = 16;
                break;
            case 0x9:
                width  = 8;
                height = 32;
                break;
            case 0xA:
                width  = 16;
                height = 32;
                break;
            case 0xB:
                width  = 32;
                height = 64;
                break;
            default:
                LOG_VERBOSE("Unknown object dimensions: shape=%d, size=%d\n", (object[0] >> 14) & 0x3, (object[1] >> 14) & 0x3);
                continue;
        }
        int width2 = width, height2 = height;
        if ((object[0] & 0x0300) == 0x0300) {
            width2 *= 2;
            height2 *= 2;
        }
        int y = object[0] & 0xFF;
        if (y >= 192)
            y -= 256;
        int spriteY = ((object[0] & BIT(12)) ? (line - (line % (((m_mosaic >> 12) & 0xF) + 1))) : line) - y;
        if (spriteY < 0 || spriteY >= height2)
            continue;
        int x = object[1] & 0x1FF;
        if (x >= 256)
            x -= 512;
        int8_t priority = ((object[2] >> 10) & 0x3) - 1;
        if (!gbaMode && type == 3) {
            uint32_t dataBase;
            int      bitmapWidth;
            if (m_dispCnt & BIT(6)) {
                dataBase    = m_objVramAddr + static_cast<uint32_t>((object[2] & 0x3FF) * ((m_dispCnt & BIT(22)) ? 256 : 128));
                bitmapWidth = width;
            } else {
                uint8_t xMask = (m_dispCnt & BIT(5)) ? 0x1F : 0x0F;
                dataBase      = m_objVramAddr + static_cast<uint32_t>((object[2] & xMask) * 0x10) + static_cast<uint32_t>((object[2] & 0x3FF & ~xMask) * 0x80);
                bitmapWidth   = (m_dispCnt & BIT(5)) ? 256 : 128;
            }
            if (object[0] & BIT(8)) {
                int16_t params[4] = {(int16_t)U8TO16(m_oam, ((object[1] >> 9) & 0x1F) * 0x20 + 0x06),
                                     (int16_t)U8TO16(m_oam, ((object[1] >> 9) & 0x1F) * 0x20 + 0x0E),
                                     (int16_t)U8TO16(m_oam, ((object[1] >> 9) & 0x1F) * 0x20 + 0x16),
                                     (int16_t)U8TO16(m_oam, ((object[1] >> 9) & 0x1F) * 0x20 + 0x1E)};
                for (int j = 0; j < width2; j++) {
                    int offset = x + j;
                    if (offset < 0 || offset >= (gbaMode ? 240 : 256))
                        continue;
                    int rotscaleX =
                        ((params[0] * (j - width2 / 2) + params[1] * (spriteY - height2 / 2)) >> 8) + width / 2;
                    if (rotscaleX < 0 || rotscaleX >= width)
                        continue;
                    int rotscaleY =
                        ((params[2] * (j - width2 / 2) + params[3] * (spriteY - height2 / 2)) >> 8) + height / 2;
                    if (rotscaleY < 0 || rotscaleY >= height)
                        continue;
                    uint16_t pixel =
                        m_core->memory.Read<uint16_t>(0, dataBase + static_cast<uint32_t>((rotscaleY * bitmapWidth + rotscaleX) * 2));
                    if (pixel & BIT(15))
                        DrawObjPixel(line, offset, pixel, priority);
                }
            } else {
                dataBase += static_cast<uint32_t>(((object[1] & BIT(13)) ? (height - spriteY - 1) : spriteY) * bitmapWidth * 2);
                for (int j = 0; j < width; j++) {
                    int offset = (object[1] & BIT(12)) ? (x + width - j - 1) : (x + j);
                    if (offset < 0 || offset >= (gbaMode ? 240 : 256))
                        continue;
                    uint16_t pixel = m_core->memory.Read<uint16_t>(0, dataBase + static_cast<uint32_t>(j * 2));
                    if (pixel & BIT(15))
                        DrawObjPixel(line, offset, pixel, priority);
                }
            }
            continue;
        }
        uint32_t tileBase;
        if (gbaMode) {
            tileBase = static_cast<uint32_t>(0x6010000 + (object[2] & 0x3FF) * 32);
        } else {
            uint16_t bound = (m_dispCnt & BIT(4)) ? (32 << ((m_dispCnt >> 20) & 0x3)) : 32;
            tileBase       = m_objVramAddr + (object[2] & 0x3FF) * bound;
        }
        if (object[0] & BIT(8)) {
            int16_t params[4] = {(int16_t)U8TO16(m_oam, ((object[1] >> 9) & 0x1F) * 0x20 + 0x06),
                                 (int16_t)U8TO16(m_oam, ((object[1] >> 9) & 0x1F) * 0x20 + 0x0E),
                                 (int16_t)U8TO16(m_oam, ((object[1] >> 9) & 0x1F) * 0x20 + 0x16),
                                 (int16_t)U8TO16(m_oam, ((object[1] >> 9) & 0x1F) * 0x20 + 0x1E)};
            if (object[0] & BIT(13)) {
                int      mapWidth = (m_dispCnt & BIT(gbaMode ? 6 : 4)) ? width : 128;
                uint8_t *pal;
                if (m_dispCnt & BIT(31)) {
                    if (!m_extPalettes[4])
                        continue;
                    pal = &m_extPalettes[4][(object[2] & 0xF000) >> 3];
                } else {
                    pal = &m_palette[0x200];
                }
                for (int j = 0; j < width2; j++) {
                    int offset = x + j;
                    if (offset < 0 || offset >= (gbaMode ? 240 : 256))
                        continue;
                    int rotscaleX =
                        ((params[0] * (j - width2 / 2) + params[1] * (spriteY - height2 / 2)) >> 8) + width / 2;
                    if (rotscaleX < 0 || rotscaleX >= width)
                        continue;
                    int rotscaleY =
                        ((params[2] * (j - width2 / 2) + params[3] * (spriteY - height2 / 2)) >> 8) + height / 2;
                    if (rotscaleY < 0 || rotscaleY >= height)
                        continue;
                    uint8_t index = m_core->memory.Read<uint8_t>(
                        gbaMode, tileBase + static_cast<uint32_t>(((rotscaleY / 8) * mapWidth + rotscaleY % 8) * 8) + static_cast<uint32_t>((rotscaleX / 8) * 64) +
                                     static_cast<uint32_t>(rotscaleX % 8));
                    if (index && type == 2) {
                        m_framebuffer[line * 256 + offset] |= BIT(24);
                    } else {
                        if (index)
                            DrawObjPixel(line, offset, ((type == 1) << 25) | BIT(15) | U8TO16(pal, index * 2),
                                         priority);
                        if (gbaMode && priority < m_priorities[0][offset] && m_blendBits[0][offset] == 4)
                            m_priorities[0][offset] = priority;
                    }
                }
            } else {
                int      mapWidth = (m_dispCnt & BIT(gbaMode ? 6 : 4)) ? width : 256;
                uint8_t *pal      = &m_palette[0x200 + ((object[2] & 0xF000) >> 12) * 32];
                for (int j = 0; j < width2; j++) {
                    int offset = x + j;
                    if (offset < 0 || offset >= (gbaMode ? 240 : 256))
                        continue;
                    int rotscaleX =
                        ((params[0] * (j - width2 / 2) + params[1] * (spriteY - height2 / 2)) >> 8) + width / 2;
                    if (rotscaleX < 0 || rotscaleX >= width)
                        continue;
                    int rotscaleY =
                        ((params[2] * (j - width2 / 2) + params[3] * (spriteY - height2 / 2)) >> 8) + height / 2;
                    if (rotscaleY < 0 || rotscaleY >= height)
                        continue;
                    uint8_t index = m_core->memory.Read<uint8_t>(
                        gbaMode, tileBase + static_cast<uint32_t>(((rotscaleY / 8) * mapWidth + rotscaleY % 8) * 4) + static_cast<uint32_t>((rotscaleX / 8) * 32) +
                                     static_cast<uint32_t>((rotscaleX % 8) / 2));
                    index = (rotscaleX & 1) ? ((index & 0xF0) >> 4) : (index & 0x0F);
                    if (index && type == 2) {
                        m_framebuffer[line * 256 + offset] |= BIT(24);
                    } else {
                        if (index)
                            DrawObjPixel(line, offset, ((type == 1) << 25) | BIT(15) | U8TO16(pal, index * 2),
                                         priority);
                        if (gbaMode && priority < m_priorities[0][offset] && m_blendBits[0][offset] == 4)
                            m_priorities[0][offset] = priority;
                    }
                }
            }
        } else if (object[0] & BIT(13)) {
            int mapWidth = (m_dispCnt & BIT(gbaMode ? 6 : 4)) ? width : 128;
            if (object[1] & BIT(13))
                tileBase += static_cast<uint32_t>((7 - (spriteY % 8) + ((height - 1 - spriteY) / 8) * mapWidth) * 8);
            else
                tileBase += static_cast<uint32_t>(((spriteY % 8) + (spriteY / 8) * mapWidth) * 8);
            uint8_t *pal;
            if (m_dispCnt & BIT(31)) {
                if (!m_extPalettes[4])
                    continue;
                pal = &m_extPalettes[4][(object[2] & 0xF000) >> 3];
            } else {
                pal = &m_palette[0x200];
            }
            for (int j = 0; j < width; j++) {
                int offset = (object[1] & BIT(12)) ? (x + width - j - 1) : (x + j);
                if (offset < 0 || offset >= (gbaMode ? 240 : 256))
                    continue;
                uint8_t index = m_core->memory.Read<uint8_t>(gbaMode, tileBase + static_cast<uint32_t>((j / 8) * 64) + static_cast<uint32_t>(j % 8));
                if (index && type == 2) {
                    m_framebuffer[line * 256 + offset] |= BIT(24);
                } else {
                    if (index)
                        DrawObjPixel(line, offset, ((type == 1) << 25) | BIT(15) | U8TO16(pal, index * 2), priority);
                    if (gbaMode && priority < m_priorities[0][offset] && m_blendBits[0][offset] == 4)
                        m_priorities[0][offset] = priority;
                }
            }
        } else {
            int mapWidth = (m_dispCnt & BIT(gbaMode ? 6 : 4)) ? width : 256;
            if (object[1] & BIT(13))
                tileBase += static_cast<uint32_t>((7 - (spriteY % 8) + ((height - 1 - spriteY) / 8) * mapWidth) * 4);
            else
                tileBase += static_cast<uint32_t>(((spriteY % 8) + (spriteY / 8) * mapWidth) * 4);
            uint8_t *pal = &m_palette[0x200 + ((object[2] & 0xF000) >> 12) * 32];
            for (int j = 0; j < width; j++) {
                int offset = (object[1] & BIT(12)) ? (x + width - j - 1) : (x + j);
                if (offset < 0 || offset >= (gbaMode ? 240 : 256))
                    continue;
                uint8_t index = m_core->memory.Read<uint8_t>(gbaMode, tileBase + static_cast<uint32_t>((j / 8) * 32) + static_cast<uint32_t>((j % 8) / 2));
                index         = (j & 1) ? ((index & 0xF0) >> 4) : (index & 0x0F);
                if (index && type == 2) {
                    m_framebuffer[line * 256 + offset] |= BIT(24);
                } else {
                    if (index)
                        DrawObjPixel(line, offset, ((type == 1) << 25) | BIT(15) | U8TO16(pal, index * 2), priority);
                    if (gbaMode && priority < m_priorities[0][offset] && m_blendBits[0][offset] == 4)
                        m_priorities[0][offset] = priority;
                }
            }
        }
    }
}

void Gpu2D::WriteDispCnt(uint32_t mask, uint32_t value) {
    mask &= ((m_engine == 0) ? 0xFFFFFFFF : 0xC0B1FFF7);
    m_dispCnt = (m_dispCnt & ~mask) | (value & mask);
    if (m_core->IsGbaMode())
        m_dispCnt &= 0xFFFF;
}

void Gpu2D::WriteBgCnt(int bg, uint16_t mask, uint16_t value) {
    if (m_core->IsGbaMode() && bg < 2)
        mask &= 0xDFFF;
    m_bgCnt[bg] = (m_bgCnt[bg] & ~mask) | (value & mask);
}

void Gpu2D::WriteBgHOfs(int bg, uint16_t mask, uint16_t value) {
    mask &= 0x01FF;
    m_bgHOfs[bg] = (m_bgHOfs[bg] & ~mask) | (value & mask);
}

void Gpu2D::WriteBgVOfs(int bg, uint16_t mask, uint16_t value) {
    mask &= 0x01FF;
    m_bgVOfs[bg] = (m_bgVOfs[bg] & ~mask) | (value & mask);
}

void Gpu2D::WriteBgPA(int bg, uint16_t mask, uint16_t value) {
    m_bgPA[bg - 2] = static_cast<int16_t>((m_bgPA[bg - 2] & ~mask) | (value & mask));
}

void Gpu2D::WriteBgPB(int bg, uint16_t mask, uint16_t value) {
    m_bgPB[bg - 2] = static_cast<int16_t>((m_bgPB[bg - 2] & ~mask) | (value & mask));
}

void Gpu2D::WriteBgPC(int bg, uint16_t mask, uint16_t value) {
    m_bgPC[bg - 2] = static_cast<int16_t>((m_bgPC[bg - 2] & ~mask) | (value & mask));
}

void Gpu2D::WriteBgPD(int bg, uint16_t mask, uint16_t value) {
    m_bgPD[bg - 2] = static_cast<int16_t>((m_bgPD[bg - 2] & ~mask) | (value & mask));
}

void Gpu2D::WriteBgX(int bg, uint32_t mask, uint32_t value) {
    mask &= 0x0FFFFFFF;
    const uint32_t bits = (static_cast<uint32_t>(m_bgX[bg - 2]) & ~mask) | (value & mask);
    // Extension du signe sur 28 bits.
    m_bgX[bg - 2] = static_cast<int32_t>((bits & BIT(27)) ? (bits | 0xF0000000u) : (bits & ~0xF0000000u));
    m_internalX[bg - 2] = m_bgX[bg - 2];
}

void Gpu2D::WriteBgY(int bg, uint32_t mask, uint32_t value) {
    mask &= 0x0FFFFFFF;
    const uint32_t bits = (static_cast<uint32_t>(m_bgY[bg - 2]) & ~mask) | (value & mask);
    // Extension du signe sur 28 bits.
    m_bgY[bg - 2] = static_cast<int32_t>((bits & BIT(27)) ? (bits | 0xF0000000u) : (bits & ~0xF0000000u));
    m_internalY[bg - 2] = m_bgY[bg - 2];
}

void Gpu2D::WriteWinH(int win, uint16_t mask, uint16_t value) {
    if (mask & 0x00FF)
        m_winX2[win] = (value & 0x00FF) >> 0;
    if (mask & 0xFF00)
        m_winX1[win] = (value & 0xFF00) >> 8;
    if ((m_winHFlip[win] = (m_winX1[win] > m_winX2[win])))
        SWAP(m_winX1[win], m_winX2[win]);
}

void Gpu2D::WriteWinV(int win, uint16_t mask, uint16_t value) {
    if (mask & 0x00FF)
        m_winY2[win] = (value & 0x00FF) >> 0;
    if (mask & 0xFF00)
        m_winY1[win] = (value & 0xFF00) >> 8;
    if ((m_winVFlip[win] = (m_winY1[win] > m_winY2[win])))
        SWAP(m_winY1[win], m_winY2[win]);
}

void Gpu2D::WriteWinIn(uint16_t mask, uint16_t value) {
    mask &= 0x3F3F;
    m_winIn = (m_winIn & ~mask) | (value & mask);
}

void Gpu2D::WriteWinOut(uint16_t mask, uint16_t value) {
    mask &= 0x3F3F;
    m_winOut = (m_winOut & ~mask) | (value & mask);
}

void Gpu2D::WriteMosaic(uint16_t mask, uint16_t value) {
    m_mosaic = (m_mosaic & ~mask) | (value & mask);
}

void Gpu2D::WriteBldCnt(uint16_t mask, uint16_t value) {
    mask &= 0x3FFF;
    m_bldCnt = (m_bldCnt & ~mask) | (value & mask);
}

void Gpu2D::WriteBldAlpha(uint16_t mask, uint16_t value) {
    mask &= 0x1F1F;
    m_bldAlpha = (m_bldAlpha & ~mask) | (value & mask);
}

void Gpu2D::WriteBldY(uint8_t value) {
    m_bldY = value & 0x1F;
    if (m_bldY > 16)
        m_bldY = 16;
}

void Gpu2D::WriteMasterBright(uint16_t mask, uint16_t value) {
    mask &= 0xC01F;
    m_masterBright = (m_masterBright & ~mask) | (value & mask);
}

} // namespace consoles::arm

