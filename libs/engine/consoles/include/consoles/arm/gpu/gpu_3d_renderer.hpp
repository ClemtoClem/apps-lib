#pragma once
#include <atomic>
#include <cstdint>
#include <thread>
#include <memory>
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {

class Core;
struct Vertex;
struct Polygon;
class Gpu3DRenderer {
  private:
    Core *m_core;

    bool             m_resShift                       = false;
    uint32_t         m_framebuffer[2][256 * 192 * 4]  = {};
    int32_t          m_depthBuffer[2][256 * 192 * 4]  = {};
    uint32_t         m_attribBuffer[2][256 * 192 * 4] = {};
    uint8_t          m_stencilBuffer[256 * 192 * 4]   = {};
    bool             m_stencilClear[256 * 2]          = {};
    int              m_polygonTop[2048]               = {};
    int              m_polygonBot[2048]               = {};
    int              m_activeThreads                  = 0;
    std::unique_ptr<std::thread> m_threads[3];
    std::atomic<int> m_ready[192 * 2];
    uint16_t         m_disp3DCnt     = 0;
    uint16_t         m_edgeColor[8]  = {};
    uint32_t         m_clearColor    = 0;
    uint16_t         m_clearDepth    = 0;
    uint32_t         m_fogColor      = 0;
    uint16_t         m_fogOffset     = 0;
    uint8_t          m_fogTable[32]  = {};
    uint16_t         m_toonTable[32] = {};

  public:
    Gpu3DRenderer(Core *core);
    ~Gpu3DRenderer();

    void      DrawScanline(int line);
    uint32_t *GetLine(int line);
    uint16_t  ReadDisp3DCnt() {
        return m_disp3DCnt;
    }

    void WriteDisp3DCnt(uint16_t mask, uint16_t value);
    void WriteEdgeColor(int index, uint16_t mask, uint16_t value);
    void WriteClearColor(uint32_t mask, uint32_t value);
    void WriteClearDepth(uint16_t mask, uint16_t value);
    void WriteFogColor(uint32_t mask, uint32_t value);
    void WriteFogOffset(uint16_t mask, uint16_t value);
    void WriteFogTable(int index, uint8_t value);
    void WriteToonTable(int index, uint16_t mask, uint16_t value);

    /**
     * @brief Arrête proprement le pool de threads de rendu (identique au destructeur).
     *
     * Appelé avant toute sauvegarde/chargement d'état : les threads de rendu
     * lisent les polygones/sommets de Gpu3D et la VRAM de Memory, qui sont
     * sur le point d'être lus ou écrasés par IoState(). Il n'y a rien à
     * relancer explicitement après un chargement : le pool est recréé de
     * façon paresseuse à la prochaine frame (DrawScanline(0)).
     */
    void Quiesce();

    /**
     * @brief Sérialise/désérialise les registres du moteur de rendu 3D pour les save states.
     *
     * Les buffers de rendu (framebuffer/depthBuffer/attribBuffer/stencilBuffer/
     * polygonTop/polygonBot) sont du scratch recalculé à chaque frame à partir
     * des polygones de Gpu3D : ils ne sont pas sérialisés.
     * @param archive Archive de sauvegarde/chargement.
     */
    void IoState(StateArchive &archive) {
        Quiesce();
        archive.Io(m_disp3DCnt);
        archive.Io(m_edgeColor);
        archive.Io(m_clearColor);
        archive.Io(m_clearDepth);
        archive.Io(m_fogColor);
        archive.Io(m_fogOffset);
        archive.Io(m_fogTable);
        archive.Io(m_toonTable);
    }

  private:
    static uint32_t Rgba5ToRgba6(uint32_t color);
    uint32_t       *GetLine1(int line);
    void            DrawThreaded(int thread);
    void            DrawScanline1(int line);
    void            FinishScanline(int line);
    uint8_t        *GetTexture(uint32_t address);
    uint8_t        *GetPalette(uint32_t address);
    static uint32_t InterpolateLinear(uint32_t v1, uint32_t v2, uint32_t x1, uint32_t x, uint32_t x2);
    static uint32_t InterpolateLinRev(uint32_t v1, uint32_t v2, uint32_t x1, uint32_t x, uint32_t x2);
    static uint32_t InterpolateFactor(uint32_t factor, uint32_t shift, uint32_t v1, uint32_t v2);
    static uint32_t InterpolateColor(uint32_t c1, uint32_t c2, uint32_t x1, uint32_t x, uint32_t x2);
    uint32_t        ReadTexture(Polygon *polygon, int s, int t);
    void            DrawPolygon(int line, int polygonIndex);
};

} // namespace consoles::arm

