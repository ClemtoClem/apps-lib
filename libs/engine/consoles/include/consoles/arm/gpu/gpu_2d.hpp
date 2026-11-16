#pragma once
#include <cstdint>
#include <memory>
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {

class Core;
class Gpu2D {
  public:
    Gpu2D(Core *core, bool engine);
    void      DrawGbaScanline(int line);
    void      DrawScanline(int line);
    uint32_t *GetFramebuffer() {
        return m_framebuffer;
    }
    uint32_t *GetRawLine() {
        return m_layers[0];
    }
    uint32_t ReadDispCnt() {
        return m_dispCnt;
    }
    uint16_t ReadBgCnt(int bg) {
        return m_bgCnt[bg];
    }
    uint16_t ReadWinIn() {
        return m_winIn;
    }
    uint16_t ReadWinOut() {
        return m_winOut;
    }
    uint16_t ReadBldCnt() {
        return m_bldCnt;
    }
    uint16_t ReadBldAlpha() {
        return m_bldAlpha;
    }
    uint16_t ReadMasterBright() {
        return m_masterBright;
    }
    void WriteDispCnt(uint32_t mask, uint32_t value);
    void WriteBgCnt(int bg, uint16_t mask, uint16_t value);
    void WriteBgHOfs(int bg, uint16_t mask, uint16_t value);
    void WriteBgVOfs(int bg, uint16_t mask, uint16_t value);
    void WriteBgPA(int bg, uint16_t mask, uint16_t value);
    void WriteBgPB(int bg, uint16_t mask, uint16_t value);
    void WriteBgPC(int bg, uint16_t mask, uint16_t value);
    void WriteBgPD(int bg, uint16_t mask, uint16_t value);
    void WriteBgX(int bg, uint32_t mask, uint32_t value);
    void WriteBgY(int bg, uint32_t mask, uint32_t value);
    void WriteWinH(int win, uint16_t mask, uint16_t value);
    void WriteWinV(int win, uint16_t mask, uint16_t value);
    void WriteWinIn(uint16_t mask, uint16_t value);
    void WriteWinOut(uint16_t mask, uint16_t value);
    void WriteMosaic(uint16_t mask, uint16_t value);
    void WriteBldCnt(uint16_t mask, uint16_t value);
    void WriteBldAlpha(uint16_t mask, uint16_t value);
    void WriteBldY(uint8_t value);
    void WriteMasterBright(uint16_t mask, uint16_t value);

    /**
     * @brief Sérialise/désérialise les registres du moteur 2D pour les save states.
     *
     * Le framebuffer/les couches/priorités/blendBits sont du scratch de rendu
     * recalculé à chaque ligne : ils ne sont pas sérialisés. Les pointeurs
     * palette/oam/extPalettes et bgVramAddr/objVramAddr sont fixés une fois
     * pour toutes à la construction (adresses dans les tableaux de Memory) et
     * n'ont donc pas besoin d'être restaurés ; extPalettes est de toute façon
     * reconstruit par Memory::RebuildVram() après le chargement.
     * @param archive Archive de sauvegarde/chargement.
     */
    void IoState(StateArchive &archive) {
        archive.Io(m_internalX);
        archive.Io(m_internalY);
        archive.Io(m_winHFlip);
        archive.Io(m_winVFlip);
        archive.Io(m_dispCnt);
        archive.Io(m_bgCnt);
        archive.Io(m_bgHOfs);
        archive.Io(m_bgVOfs);
        archive.Io(m_bgPA);
        archive.Io(m_bgPB);
        archive.Io(m_bgPC);
        archive.Io(m_bgPD);
        archive.Io(m_bgX);
        archive.Io(m_bgY);
        archive.Io(m_winX1);
        archive.Io(m_winX2);
        archive.Io(m_winY1);
        archive.Io(m_winY2);
        archive.Io(m_winIn);
        archive.Io(m_winOut);
        archive.Io(m_bldCnt);
        archive.Io(m_mosaic);
        archive.Io(m_bldAlpha);
        archive.Io(m_bldY);
        archive.Io(m_masterBright);
    }

  private:
    Core *m_core;
    bool      m_engine;
    uint32_t  m_bgVramAddr, m_objVramAddr;
    uint8_t  *m_palette, *m_oam;
    uint8_t **m_extPalettes;
    uint32_t  m_framebuffer[256 * 192] = {};
    uint32_t  m_layers[2][256]         = {};
    int8_t    m_priorities[2][256]     = {};
    int8_t    m_blendBits[2][256]      = {};
    int       m_internalX[2]           = {};
    int       m_internalY[2]           = {};
    bool      m_winHFlip[2]            = {};
    bool      m_winVFlip[2]            = {};
    uint32_t  m_dispCnt                = 0;
    uint16_t  m_bgCnt[4]               = {};
    uint16_t  m_bgHOfs[4]              = {};
    uint16_t  m_bgVOfs[4]              = {};
    int16_t   m_bgPA[2]                = {};
    int16_t   m_bgPB[2]                = {};
    int16_t   m_bgPC[2]                = {};
    int16_t   m_bgPD[2]                = {};
    int32_t   m_bgX[2]                 = {};
    int32_t   m_bgY[2]                 = {};
    uint16_t  m_winX1[2]               = {};
    uint16_t  m_winX2[2]               = {};
    uint16_t  m_winY1[2]               = {};
    uint16_t  m_winY2[2]               = {};
    uint16_t  m_winIn                  = 0;
    uint16_t  m_winOut                 = 0;
    uint16_t  m_bldCnt                 = 0;
    uint16_t  m_mosaic                 = 0;
    uint16_t  m_bldAlpha               = 0;
    uint8_t   m_bldY                   = 0;
    uint16_t  m_masterBright           = 0;

    static uint32_t Rgb5ToRgb6(uint32_t color);

    void                         DrawBgPixel(int bg, int line, int x, uint32_t pixel);
    void                         DrawObjPixel(int line, int x, uint32_t pixel, int8_t priority);
    template <bool gbaMode> void DrawText(int bg, int line);
    template <bool gbaMode> void DrawAffine(int bg, int line);
    template <bool gbaMode> void DrawObjects(int line, bool window);
    void                         DrawExtended(int bg, int line);
    void                         DrawExtendedGba(int bg, int line);
    void                         DrawLarge(int bg, int line);
};

} // namespace consoles::arm

