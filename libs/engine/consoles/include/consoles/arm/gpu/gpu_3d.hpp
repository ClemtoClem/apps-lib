
#pragma once
#include <cstdint>
#include <functional>
#include <queue>
#include <vector>
#include <memory>
#include "consoles/arm/defines.hpp"
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {

class Core;
enum GXState {
    GX_IDLE = 0,
    GX_RUNNING,
    GX_HALTED
};

struct Entry {
    Entry() : command(0), param(0) {
    }
    Entry(uint8_t command_, uint32_t param_) : command(command_), param(param_) {
    }
    uint8_t  command;
    uint32_t param;
};

struct Matrix {
    int32_t data[4 * 4] = {1 << 12, 0 << 12, 0 << 12, 0 << 12, 0 << 12, 1 << 12, 0 << 12, 0 << 12,
                           0 << 12, 0 << 12, 1 << 12, 0 << 12, 0 << 12, 0 << 12, 0 << 12, 1 << 12};
    Matrix  operator*(Matrix &mtx);
};

struct Vector {
    int32_t x = 0, y = 0, z = 0;
    int32_t operator*(Vector &vtr);
    Vector  operator*(Matrix &mtx);
};

struct Vertex : Vector {
    int32_t  w = 0;
    int16_t  s = 0, t = 0;
    uint32_t color = 0;
    Vertex   operator*(Matrix &mtx);
};

struct Polygon {
    int      size           = 0;
    Vertex  *vertices       = nullptr;
    bool     crossed        = false;
    bool     clockwise      = false;
    int      mode           = 0;
    bool     transNewDepth  = false;
    bool     depthTestEqual = false;
    bool     fog            = false;
    uint8_t  alpha          = 0;
    int      id             = 0;
    uint32_t textureAddr = 0, paletteAddr = 0;
    int      sizeS = 0, sizeT = 0;
    bool     repeatS = false, repeatT = false;
    bool     flipS = false, flipT = false;
    int      textureFmt   = 0;
    bool     transparent0 = false;
    bool     wBuffer      = false;
    int      wShift       = 0;
};

class Gpu3D {
  public:
    Gpu3D(Core *core);
    void SwapBuffers();
    bool ShouldSwap() {
        return m_state == GX_HALTED;
    }
    Polygon *GetPolygons() {
        return m_polygonsOut;
    }
    int GetPolygonCount() {
        return m_polygonCountOut;
    }
    uint32_t ReadGxStat() {
        return m_gxStat;
    }
    uint32_t ReadPosResult(int index) {
        return static_cast<uint32_t>(m_posResult[index]);
    }
    uint32_t ReadVecResult(int index) {
        return static_cast<uint32_t>(m_vecResult[index]);
    }
    uint32_t ReadRamCount();
    uint32_t ReadClipMtxResult(int index);
    uint32_t ReadVecMtxResult(int index);

    void WriteGxFifo(uint32_t mask, uint32_t value);
    void WriteMtxMode(uint32_t mask, uint32_t value);
    void WriteMtxPush(uint32_t mask, uint32_t value);
    void WriteMtxPop(uint32_t mask, uint32_t value);
    void WriteMtxStore(uint32_t mask, uint32_t value);
    void WriteMtxRestore(uint32_t mask, uint32_t value);
    void WriteMtxIdentity(uint32_t mask, uint32_t value);
    void WriteMtxLoad44(uint32_t mask, uint32_t value);
    void WriteMtxLoad43(uint32_t mask, uint32_t value);
    void WriteMtxMult44(uint32_t mask, uint32_t value);
    void WriteMtxMult43(uint32_t mask, uint32_t value);
    void WriteMtxMult33(uint32_t mask, uint32_t value);
    void WriteMtxScale(uint32_t mask, uint32_t value);
    void WriteMtxTrans(uint32_t mask, uint32_t value);
    void WriteColor(uint32_t mask, uint32_t value);
    void WriteNormal(uint32_t mask, uint32_t value);
    void WriteTexCoord(uint32_t mask, uint32_t value);
    void WriteVtx16(uint32_t mask, uint32_t value);
    void WriteVtx10(uint32_t mask, uint32_t value);
    void WriteVtxXY(uint32_t mask, uint32_t value);
    void WriteVtxXZ(uint32_t mask, uint32_t value);
    void WriteVtxYZ(uint32_t mask, uint32_t value);
    void WriteVtxDiff(uint32_t mask, uint32_t value);
    void WritePolygonAttr(uint32_t mask, uint32_t value);
    void WriteTexImageParam(uint32_t mask, uint32_t value);
    void WritePlttBase(uint32_t mask, uint32_t value);
    void WriteDifAmb(uint32_t mask, uint32_t value);
    void WriteSpeEmi(uint32_t mask, uint32_t value);
    void WriteLightVector(uint32_t mask, uint32_t value);
    void WriteLightColor(uint32_t mask, uint32_t value);
    void WriteShininess(uint32_t mask, uint32_t value);
    void WriteBeginVtxs(uint32_t mask, uint32_t value);
    void WriteEndVtxs(uint32_t mask, uint32_t value);
    void WriteSwapBuffers(uint32_t mask, uint32_t value);
    void WriteViewport(uint32_t mask, uint32_t value);
    void WriteBoxTest(uint32_t mask, uint32_t value);
    void WritePosTest(uint32_t mask, uint32_t value);
    void WriteVecTest(uint32_t mask, uint32_t value);
    void WriteGxStat(uint32_t mask, uint32_t value);

    /**
     * @brief Sérialise/désérialise l'état du moteur de géométrie 3D pour les save states.
     * @param archive Archive de sauvegarde/chargement.
     */
    void IoState(StateArchive &archive);

  private:
    Core *m_core;
    GXState           m_state = GX_IDLE;
    std::queue<Entry> m_fifo;

    size_t m_pipeSize    = 0;
    size_t m_testQueue   = 0;
    size_t m_matrixQueue = 0;

    static const uint8_t paramCounts[0x100];

    uint8_t m_matrixMode = 0;
    bool    m_clipDirty  = false;

    Matrix    m_projection, m_projectionStack;
    Matrix    m_coordinate, m_coordinateStack[32];
    Matrix    m_direction, m_directionStack[32];
    Matrix    m_texture, m_textureStack;
    Matrix    m_clip;
    Vertex    m_vertices1[6144], m_vertices2[6144];
    Vertex   *m_verticesIn = m_vertices1, *m_verticesOut = m_vertices2;
    int       m_vertexCountIn = 0, m_vertexCountOut = 0;
    Polygon  m_polygons1[2048], m_polygons2[2048];
    Polygon *m_polygonsIn = m_polygons1, *m_polygonsOut = m_polygons2;
    int       m_polygonCountIn = 0, m_polygonCountOut = 0;
    Vertex    m_savedVertex;
    Polygon  m_savedPolygon;

    int16_t               m_s, m_t;
    int                   m_vertexCount      = 0;
    bool                  m_clockwise        = false;
    int                   m_polygonType      = 0;
    int                   m_textureCoordMode = 0;
    uint32_t              m_polygonAttr      = 0;
    uint8_t               m_enabledLights    = 0;
    bool                  m_renderBack = false, m_renderFront = false;
    uint32_t              m_diffuseColor = 0, m_ambientColor = 0;
    uint32_t              m_specularColor = 0, m_emissionColor = 0;
    bool                  m_shininessEnabled = false;
    Vector                m_lightVector[4], m_halfVector[4];
    uint32_t              m_lightColor[4]  = {};
    uint8_t               m_shininess[128] = {};
    uint16_t              m_viewportX = 0, m_viewportY = 0;
    uint16_t              m_viewportWidth = 256, m_viewportHeight = 192;
    uint32_t              m_gxFifo       = 0x00000000;
    uint32_t              m_gxStat       = 0x04000000;
    int32_t               m_posResult[4] = {};
    int16_t               m_vecResult[3] = {};
    int                   m_gxFifoCount  = 0;
    std::function<void()> m_runCommandTask;

  private:
    static uint32_t Rgb5ToRgb6(uint16_t color);
    static Vertex   Intersection(Vertex *vtx1, Vertex *vtx2, int32_t val1, int32_t val2);
    static bool     ClipPolygon(Vertex *unclipped, Vertex *clipped, int *size);

    void RunCommand();
    void AddVertex();
    void AddPolygon();
    void MtxModeCmd(uint32_t param);
    void MtxPushCmd();
    void MtxPopCmd(uint32_t param);
    void MtxStoreCmd(uint32_t param);
    void MtxRestoreCmd(uint32_t param);
    void MtxIdentityCmd();
    void MtxLoad44Cmd(std::vector<uint32_t> &params);
    void MtxLoad43Cmd(std::vector<uint32_t> &params);
    void MtxMult44Cmd(std::vector<uint32_t> &params);
    void MtxMult43Cmd(std::vector<uint32_t> &params);
    void MtxMult33Cmd(std::vector<uint32_t> &params);
    void MtxScaleCmd(std::vector<uint32_t> &params);
    void MtxTransCmd(std::vector<uint32_t> &params);
    void ColorCmd(uint32_t param);
    void NormalCmd(uint32_t param);
    void TexCoordCmd(uint32_t param);
    void Vtx16Cmd(std::vector<uint32_t> &params);
    void Vtx10Cmd(uint32_t param);
    void VtxXYCmd(uint32_t param);
    void VtxXZCmd(uint32_t param);
    void VtxYZCmd(uint32_t param);
    void VtxDiffCmd(uint32_t param);
    void PolygonAttrCmd(uint32_t param);
    void TexImageParamCmd(uint32_t param);
    void PlttBaseCmd(uint32_t param);
    void DifAmbCmd(uint32_t param);
    void SpeEmiCmd(uint32_t param);
    void LightVectorCmd(uint32_t param);
    void LightColorCmd(uint32_t param);
    void ShininessCmd(std::vector<uint32_t> &params);
    void BeginVtxsCmd(uint32_t param);
    void SwapBuffersCmd(uint32_t param);
    void ViewportCmd(uint32_t param);
    void BoxTestCmd(std::vector<uint32_t> &params);
    void PosTestCmd(std::vector<uint32_t> &params);
    void VecTestCmd(uint32_t param);
    void AddEntry(Entry entry);
};

} // namespace consoles::arm

