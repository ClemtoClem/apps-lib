#include <cstring>
#include "consoles/arm/gpu/gpu_3d.hpp"
#include "consoles/arm/core.hpp"
#include "consoles/arm/settings.hpp"

namespace consoles::arm {


Matrix Matrix::operator*(Matrix &mtx) {
    Matrix result;
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            result.data[y * 4 + x] =
                static_cast<int32_t>(((int64_t)data[y * 4 + 0] * mtx.data[0 + x] + (int64_t)data[y * 4 + 1] * mtx.data[4 + x] +
                 (int64_t)data[y * 4 + 2] * mtx.data[8 + x] + (int64_t)data[y * 4 + 3] * mtx.data[12 + x]) >>
                12);
        }
    }
    return result;
}

int32_t Vector::operator*(Vector &vtr) {
    return static_cast<int32_t>(((int64_t)x * vtr.x + (int64_t)y * vtr.y + (int64_t)z * vtr.z) >> 12);
}

Vector Vector::operator*(Matrix &mtx) {
    Vector result;
    result.x = static_cast<int32_t>(((int64_t)x * mtx.data[0] + (int64_t)y * mtx.data[4] + (int64_t)z * mtx.data[8]) >> 12);
    result.y = static_cast<int32_t>(((int64_t)x * mtx.data[1] + (int64_t)y * mtx.data[5] + (int64_t)z * mtx.data[9]) >> 12);
    result.z = static_cast<int32_t>(((int64_t)x * mtx.data[2] + (int64_t)y * mtx.data[6] + (int64_t)z * mtx.data[10]) >> 12);
    return result;
}

Vertex Vertex::operator*(Matrix &mtx) {
    Vertex result = *this;
    result.x =
        static_cast<int32_t>(((int64_t)x * mtx.data[0] + (int64_t)y * mtx.data[4] + (int64_t)z * mtx.data[8] + (int64_t)w * mtx.data[12]) >>
        12);
    result.y =
        static_cast<int32_t>(((int64_t)x * mtx.data[1] + (int64_t)y * mtx.data[5] + (int64_t)z * mtx.data[9] + (int64_t)w * mtx.data[13]) >>
        12);
    result.z =
        static_cast<int32_t>(((int64_t)x * mtx.data[2] + (int64_t)y * mtx.data[6] + (int64_t)z * mtx.data[10] + (int64_t)w * mtx.data[14]) >>
        12);
    result.w =
        static_cast<int32_t>(((int64_t)x * mtx.data[3] + (int64_t)y * mtx.data[7] + (int64_t)z * mtx.data[11] + (int64_t)w * mtx.data[15]) >>
        12);
    return result;
}

const uint8_t Gpu3D::paramCounts[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1,  0, 16, 12, 16, 12, 9, 3, 3, 0, 0, 0,
    1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 32, 0, 0,  0,  0,  0,  0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,  0, 0,  0,  0,  0,  0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 1, 0, 0,  0, 0,  0,  0,  0,  0, 0, 0, 0, 0, 0,
};

Gpu3D::Gpu3D(Core *core_) : m_core(core_) {
    m_runCommandTask = std::bind(&Gpu3D::RunCommand, this);
}

uint32_t Gpu3D::Rgb5ToRgb6(uint16_t color) {
    uint8_t r = ((color >> 0) & 0x1F) * 2;
    if (r > 0)
        r++;
    uint8_t g = ((color >> 5) & 0x1F) * 2;
    if (g > 0)
        g++;
    uint8_t b = ((color >> 10) & 0x1F) * 2;
    if (b > 0)
        b++;
    return (b << 12) | (g << 6) | r;
}

Vertex Gpu3D::Intersection(Vertex *vtx1, Vertex *vtx2, int32_t val1, int32_t val2) {
    Vertex  vertex;
    int64_t d1 = val1 + vtx1->w;
    int64_t d2 = val2 + vtx2->w;
    if (d2 == d1)
        return *vtx1;
    vertex.x     = static_cast<int32_t>(((d2 * vtx1->x) - (d1 * vtx2->x)) / (d2 - d1));
    vertex.y     = static_cast<int32_t>(((d2 * vtx1->y) - (d1 * vtx2->y)) / (d2 - d1));
    vertex.z     = static_cast<int32_t>(((d2 * vtx1->z) - (d1 * vtx2->z)) / (d2 - d1));
    vertex.w     = static_cast<int32_t>(((d2 * vtx1->w) - (d1 * vtx2->w)) / (d2 - d1));
    vertex.s     = static_cast<int16_t>(((d2 * vtx1->s) - (d1 * vtx2->s)) / (d2 - d1));
    vertex.t     = static_cast<int16_t>(((d2 * vtx1->t) - (d1 * vtx2->t)) / (d2 - d1));
    uint8_t r    = static_cast<uint8_t>(((d2 * ((vtx1->color >> 0) & 0x3F)) - (d1 * ((vtx2->color >> 0) & 0x3F))) / (d2 - d1));
    uint8_t g    = static_cast<uint8_t>(((d2 * ((vtx1->color >> 6) & 0x3F)) - (d1 * ((vtx2->color >> 6) & 0x3F))) / (d2 - d1));
    uint8_t b    = static_cast<uint8_t>(((d2 * ((vtx1->color >> 12) & 0x3F)) - (d1 * ((vtx2->color >> 12) & 0x3F))) / (d2 - d1));
    vertex.color = (vtx1->color & 0xFC0000) | (b << 12) | (g << 6) | r;
    return vertex;
}

bool Gpu3D::ClipPolygon(Vertex *unclipped, Vertex *clipped, int *size) {
    bool   clip = false;
    Vertex vertices[10];
    memcpy(vertices, unclipped, static_cast<size_t>(*size) * sizeof(Vertex));
    for (int i = 0; i < 6; i++) {
        int oldSize = *size;
        *size       = 0;
        for (int j = 0; j < oldSize; j++) {
            Vertex *current  = &vertices[j];
            Vertex *previous = &vertices[(j - 1 + oldSize) % oldSize];
            int32_t currentVal, previousVal;
            switch (i) {
                case 0:
                    currentVal  = current->x;
                    previousVal = previous->x;
                    break;
                case 1:
                    currentVal  = -current->x;
                    previousVal = -previous->x;
                    break;
                case 2:
                    currentVal  = current->y;
                    previousVal = previous->y;
                    break;
                case 3:
                    currentVal  = -current->y;
                    previousVal = -previous->y;
                    break;
                case 4:
                    currentVal  = current->z;
                    previousVal = previous->z;
                    break;
                case 5:
                    currentVal  = -current->z;
                    previousVal = -previous->z;
                    break;
            }
            if (currentVal >= -current->w) {
                if (previousVal < -previous->w) {
                    clipped[(*size)++] = Intersection(current, previous, currentVal, previousVal);
                    clip               = true;
                }
                clipped[(*size)++] = *current;
            } else if (previousVal >= -previous->w) {
                clipped[(*size)++] = Intersection(current, previous, currentVal, previousVal);
                clip               = true;
            }
        }
        memcpy(vertices, clipped, static_cast<size_t>(*size) * sizeof(Vertex));
    }
    return clip;
}

void Gpu3D::RunCommand() {
    Entry                 entry = m_fifo.front();
    int                   count = paramCounts[entry.command];
    std::vector<uint32_t> params;
    if (count > 1) {
        params.reserve(static_cast<std::vector<unsigned int>::size_type>(count));
        for (int i = 0; i < count; i++) {
            params.push_back(m_fifo.front().param);
            m_fifo.pop();
        }
    } else {
        count = 1;
        m_fifo.pop();
    }
    switch (entry.command) {
        case 0x10:
            MtxModeCmd(entry.param);
            break;
        case 0x11:
            MtxPushCmd();
            break;
        case 0x12:
            MtxPopCmd(entry.param);
            break;
        case 0x13:
            MtxStoreCmd(entry.param);
            break;
        case 0x14:
            MtxRestoreCmd(entry.param);
            break;
        case 0x15:
            MtxIdentityCmd();
            break;
        case 0x16:
            MtxLoad44Cmd(params);
            break;
        case 0x17:
            MtxLoad43Cmd(params);
            break;
        case 0x18:
            MtxMult44Cmd(params);
            break;
        case 0x19:
            MtxMult43Cmd(params);
            break;
        case 0x1A:
            MtxMult33Cmd(params);
            break;
        case 0x1B:
            MtxScaleCmd(params);
            break;
        case 0x1C:
            MtxTransCmd(params);
            break;
        case 0x20:
            ColorCmd(entry.param);
            break;
        case 0x21:
            NormalCmd(entry.param);
            break;
        case 0x22:
            TexCoordCmd(entry.param);
            break;
        case 0x23:
            Vtx16Cmd(params);
            break;
        case 0x24:
            Vtx10Cmd(entry.param);
            break;
        case 0x25:
            VtxXYCmd(entry.param);
            break;
        case 0x26:
            VtxXZCmd(entry.param);
            break;
        case 0x27:
            VtxYZCmd(entry.param);
            break;
        case 0x28:
            VtxDiffCmd(entry.param);
            break;
        case 0x29:
            PolygonAttrCmd(entry.param);
            break;
        case 0x2A:
            TexImageParamCmd(entry.param);
            break;
        case 0x2B:
            PlttBaseCmd(entry.param);
            break;
        case 0x30:
            DifAmbCmd(entry.param);
            break;
        case 0x31:
            SpeEmiCmd(entry.param);
            break;
        case 0x32:
            LightVectorCmd(entry.param);
            break;
        case 0x33:
            LightColorCmd(entry.param);
            break;
        case 0x34:
            ShininessCmd(params);
            break;
        case 0x40:
            BeginVtxsCmd(entry.param);
            break;
        case 0x41:
            break;
        case 0x50:
            SwapBuffersCmd(entry.param);
            break;
        case 0x60:
            ViewportCmd(entry.param);
            break;
        case 0x70:
            BoxTestCmd(params);
            break;
        case 0x71:
            PosTestCmd(params);
            break;
        case 0x72:
            VecTestCmd(entry.param);
            break;
        default: {
            LOG_VERBOSE("Unknown GXFIFO command: 0x%X\n", entry.command);
            break;
        }
    }
    m_pipeSize = 4 - ((m_pipeSize + static_cast<size_t>(count)) & 1);
    if (m_pipeSize > m_fifo.size())
        m_pipeSize = m_fifo.size();
    m_gxStat = static_cast<uint32_t>((m_gxStat & ~0x01FF0000u) | ((m_fifo.size() - m_pipeSize) << 16));
    if (m_fifo.size() - m_pipeSize == 0)
        m_gxStat |= BIT(26);
    if (m_fifo.size() == 0)
        m_gxStat &= ~BIT(27);
    if (m_fifo.size() - m_pipeSize < 128 && !(m_gxStat & BIT(25))) {
        m_gxStat |= BIT(25);
        m_core->dma[0].Trigger(7);
    }
    switch ((m_gxStat & 0xC0000000) >> 30) {
        case 1:
            if (m_gxStat & BIT(25))
                m_core->interpreter[0].SendInterrupt(21);
            break;
        case 2:
            if (m_gxStat & BIT(26))
                m_core->interpreter[0].SendInterrupt(21);
            break;
    }
    if (m_fifo.size() - m_pipeSize <= 256)
        m_core->interpreter[0].Unhalt(1);
    if (m_state != GX_HALTED) {
        if (!m_fifo.empty() && m_fifo.size() >= paramCounts[m_fifo.front().command])
            m_core->Schedule(Task(&m_runCommandTask, 2));
        else
            m_state = GX_IDLE;
    }
}

void Gpu3D::SwapBuffers() {
    bool     resShift = m_core->settings.highRes3D;
    uint16_t x        = m_viewportX << resShift;
    uint16_t y        = m_viewportY << resShift;
    uint16_t w        = m_viewportWidth << resShift;
    uint16_t h        = m_viewportHeight << resShift;
    uint16_t xMask    = static_cast<uint16_t>((0x200 << resShift) - 1);
    uint16_t yMask    = static_cast<uint16_t>((0x100 << resShift) - 1);
    for (int i = 0; i < m_vertexCountIn; i++) {
        if (m_verticesIn[i].w != 0) {
            m_verticesIn[i].x = static_cast<int32_t>((((int64_t)m_verticesIn[i].x + m_verticesIn[i].w) * w / (m_verticesIn[i].w * 2) + x) & xMask);
            m_verticesIn[i].y = static_cast<int32_t>(((-(int64_t)m_verticesIn[i].y + m_verticesIn[i].w) * h / (m_verticesIn[i].w * 2) + y) & yMask);
            m_verticesIn[i].z = static_cast<int32_t>((((((int64_t)m_verticesIn[i].z << 14) / m_verticesIn[i].w) + 0x3FFF) << 9));
        }
    }
    for (int i = 0; i < m_polygonCountIn; i++) {
        Polygon *p     = &m_polygonsIn[i];
        uint32_t  value = static_cast<uint32_t>(p->vertices[0].w);
        for (int j = 1; j < p->size; j++) {
            if ((uint32_t)p->vertices[j].w > value)
                value = static_cast<uint32_t>(p->vertices[j].w);
        }
        while ((value >> p->wShift) > 0xFFFF)
            p->wShift += 4;
        if (p->wShift == 0 && value != 0) {
            while ((value << -(p->wShift - 4)) <= 0xFFFF)
                p->wShift -= 4;
        }
    }
    SWAP(m_verticesOut, m_verticesIn);
    m_vertexCountOut = m_vertexCountIn;
    m_vertexCountIn  = 0;
    m_vertexCount    = 0;
    SWAP(m_polygonsOut, m_polygonsIn);
    m_polygonCountOut = m_polygonCountIn;
    m_polygonCountIn  = 0;
    m_core->gpu.Invalidate3D();
    if (!m_fifo.empty() && m_fifo.size() >= paramCounts[m_fifo.front().command]) {
        m_core->Schedule(Task(&m_runCommandTask, 2));
        m_state = GX_RUNNING;
    } else {
        m_state = GX_IDLE;
    }
}

void Gpu3D::AddVertex() {
    if (m_vertexCountIn >= 6144)
        return;
    m_verticesIn[m_vertexCountIn]   = m_savedVertex;
    m_verticesIn[m_vertexCountIn].w = 1 << 12;
    if (m_textureCoordMode == 3) {
        Matrix matrix               = m_texture;
        matrix.data[12]             = (int32_t)m_s << 12;
        matrix.data[13]             = (int32_t)m_t << 12;
        Vertex vertex               = m_verticesIn[m_vertexCountIn] * matrix;
        m_verticesIn[m_vertexCountIn].s = static_cast<int16_t>(vertex.x >> 12);
        m_verticesIn[m_vertexCountIn].t = static_cast<int16_t>(vertex.y >> 12);
    }
    if (m_clipDirty) {
        m_clip      = m_coordinate * m_projection;
        m_clipDirty = false;
    }
    m_verticesIn[m_vertexCountIn] = m_verticesIn[m_vertexCountIn] * m_clip;
    m_vertexCountIn++;
    m_vertexCount++;
    switch (m_polygonType) {
        case 0:
            if (m_vertexCount % 3 == 0)
                AddPolygon();
            break;
        case 1:
            if (m_vertexCount % 4 == 0)
                AddPolygon();
            break;
        case 2:
            if (m_vertexCount >= 3)
                AddPolygon();
            break;
        case 3:
            if (m_vertexCount >= 4 && m_vertexCount % 2 == 0)
                AddPolygon();
            break;
    }
}

void Gpu3D::AddPolygon() {
    if (m_polygonCountIn >= 2048)
        return;
    int size              = 3 + (m_polygonType & 1);
    m_savedPolygon.size     = size;
    m_savedPolygon.vertices = &m_verticesIn[m_vertexCountIn - size];
    Vertex unclipped[4];
    memcpy(unclipped, m_savedPolygon.vertices, static_cast<size_t>(size) * sizeof(Vertex));
    if (m_polygonType == 3)
        SWAP(unclipped[2], unclipped[3]);
    int64_t x1 = unclipped[1].x - unclipped[0].x;
    int64_t y1 = unclipped[1].y - unclipped[0].y;
    int64_t w1 = unclipped[1].w - unclipped[0].w;
    int64_t x2 = unclipped[2].x - unclipped[0].x;
    int64_t y2 = unclipped[2].y - unclipped[0].y;
    int64_t w2 = unclipped[2].w - unclipped[0].w;
    int64_t xc = y1 * w2 - w1 * y2;
    int64_t yc = w1 * x2 - x1 * w2;
    int64_t wc = x1 * y2 - y1 * x2;
    while (xc != (int32_t)xc || yc != (int32_t)yc || wc != (int32_t)wc) {
        xc >>= 4;
        yc >>= 4;
        wc >>= 4;
    }
    int64_t dot            = xc * unclipped[0].x + yc * unclipped[0].y + wc * unclipped[0].w;
    m_savedPolygon.clockwise = (dot < 0);
    if (m_polygonType == 2) {
        if (m_clockwise)
            dot = -dot;
        m_clockwise = !m_clockwise;
    }
    Vertex clipped[10];
    bool   cull = (!m_renderFront && dot > 0) || (!m_renderBack && dot < 0);
    bool   wasClipped = cull ? false : ClipPolygon(unclipped, clipped, &m_savedPolygon.size);
    if (cull || m_savedPolygon.size == 0) {
        switch (m_polygonType) {
            case 0:
            case 1: {
                m_vertexCountIn -= size;
                return;
            }
            case 2: {
                if (m_vertexCount == 3) {
                    m_verticesIn[m_vertexCountIn - 3] = m_verticesIn[m_vertexCountIn - 2];
                    m_verticesIn[m_vertexCountIn - 2] = m_verticesIn[m_vertexCountIn - 1];
                    m_vertexCountIn--;
                    m_vertexCount--;
                } else if (m_vertexCountIn < 6144) {
                    m_verticesIn[m_vertexCountIn - 0] = m_verticesIn[m_vertexCountIn - 1];
                    m_verticesIn[m_vertexCountIn - 1] = m_verticesIn[m_vertexCountIn - 2];
                    m_vertexCountIn++;
                    m_vertexCount = 2;
                }
                return;
            }
            case 3: {
                if (m_vertexCount == 4) {
                    m_verticesIn[m_vertexCountIn - 4] = m_verticesIn[m_vertexCountIn - 2];
                    m_verticesIn[m_vertexCountIn - 3] = m_verticesIn[m_vertexCountIn - 1];
                    m_vertexCountIn -= 2;
                    m_vertexCount -= 2;
                } else {
                    m_vertexCount = 2;
                }
                return;
            }
        }
    }
    if (wasClipped) {
        switch (m_polygonType) {
            case 0:
            case 1: {
                m_vertexCountIn -= size;
                for (int i = 0; i < m_savedPolygon.size; i++) {
                    if (m_vertexCountIn >= 6144)
                        return;
                    m_verticesIn[m_vertexCountIn] = clipped[i];
                    m_vertexCountIn++;
                }
                break;
            }
            case 2: {
                m_vertexCountIn -= (m_vertexCount == 3) ? 3 : 1;
                m_savedPolygon.vertices = &m_verticesIn[m_vertexCountIn];
                for (int i = 0; i < m_savedPolygon.size; i++) {
                    if (m_vertexCountIn >= 6144)
                        return;
                    m_verticesIn[m_vertexCountIn] = clipped[i];
                    m_vertexCountIn++;
                }
                for (int i = 0; i < 2; i++) {
                    if (m_vertexCountIn >= 6144)
                        return;
                    m_verticesIn[m_vertexCountIn] = unclipped[1 + i];
                    m_vertexCountIn++;
                }
                m_vertexCount = 2;
                break;
            }
            case 3: {
                m_vertexCountIn -= (m_vertexCount == 4) ? 4 : 2;
                m_savedPolygon.vertices = &m_verticesIn[m_vertexCountIn];
                for (int i = 0; i < m_savedPolygon.size; i++) {
                    if (m_vertexCountIn >= 6144)
                        return;
                    m_verticesIn[m_vertexCountIn] = clipped[i];
                    m_vertexCountIn++;
                }
                for (int i = 0; i < 2; i++) {
                    if (m_vertexCountIn >= 6144)
                        return;
                    m_verticesIn[m_vertexCountIn] = unclipped[3 - i];
                    m_vertexCountIn++;
                }
                m_vertexCount = 2;
                break;
            }
        }
    }
    m_polygonsIn[m_polygonCountIn]         = m_savedPolygon;
    m_polygonsIn[m_polygonCountIn].crossed = (m_polygonType == 3 && !wasClipped);
    m_polygonsIn[m_polygonCountIn].paletteAddr <<= 4 - (m_savedPolygon.textureFmt == 2);
    m_polygonCountIn++;
}

void Gpu3D::MtxModeCmd(uint32_t param) {
    m_matrixMode = param & 0x00000003;
}

void Gpu3D::MtxPushCmd() {
    switch (m_matrixMode) {
        case 0: {
            if (!(m_gxStat & BIT(13))) {
                m_projectionStack = m_projection;
                m_gxStat |= BIT(13);
            } else {
                m_gxStat |= BIT(15);
            }
            break;
        }
        case 1:
        case 2: {
            uint8_t pointer = (m_gxStat >> 8) & 0x1F;
            if (pointer >= 30)
                m_gxStat |= BIT(15);
            if (pointer < 31) {
                m_coordinateStack[pointer] = m_coordinate;
                m_directionStack[pointer]  = m_direction;
                m_gxStat += BIT(8);
            }
            break;
        }
        case 3: {
            m_textureStack = m_texture;
            break;
        }
    }
    if (--m_matrixQueue == 0)
        m_gxStat &= ~BIT(14);
}

void Gpu3D::MtxPopCmd(uint32_t param) {
    switch (m_matrixMode) {
        case 0: {
            if (m_gxStat & BIT(13)) {
                m_gxStat &= ~BIT(13);
                m_projection = m_projectionStack;
                m_clipDirty  = true;
            } else {
                m_gxStat |= BIT(15);
            }
            break;
        }
        case 1:
        case 2: {
            uint8_t pointer = static_cast<uint8_t>(((m_gxStat >> 8) & 0x1F) - static_cast<uint32_t>(((int8_t)(param << 2) >> 2)));
            if (pointer >= 30)
                m_gxStat |= BIT(15);
            if (pointer < 31) {
                m_gxStat     = (m_gxStat & ~0x1F00u) | (pointer << 8);
                m_coordinate = m_coordinateStack[pointer];
                m_direction  = m_directionStack[pointer];
                m_clipDirty  = true;
            }
            break;
        }
        case 3: {
            m_texture = m_textureStack;
            break;
        }
    }
    if (--m_matrixQueue == 0)
        m_gxStat &= ~BIT(14);
}

void Gpu3D::MtxStoreCmd(uint32_t param) {
    switch (m_matrixMode) {
        case 0: {
            m_projectionStack = m_projection;
            break;
        }
        case 1:
        case 2: {
            int address = param & 0x0000001F;
            if (address == 31)
                m_gxStat |= BIT(15);
            m_coordinateStack[address] = m_coordinate;
            m_directionStack[address]  = m_direction;
            break;
        }
        case 3: {
            m_textureStack = m_texture;
            break;
        }
    }
}

void Gpu3D::MtxRestoreCmd(uint32_t param) {
    switch (m_matrixMode) {
        case 0: {
            m_projection = m_projectionStack;
            m_clipDirty  = true;
            break;
        }
        case 1:
        case 2: {
            int address = param & 0x0000001F;
            if (address == 31)
                m_gxStat |= BIT(15);
            m_coordinate = m_coordinateStack[address];
            m_direction  = m_directionStack[address];
            m_clipDirty  = true;
            break;
        }
        case 3: {
            m_texture = m_textureStack;
            break;
        }
    }
}

void Gpu3D::MtxIdentityCmd() {
    switch (m_matrixMode) {
        case 0: {
            m_projection = Matrix();
            m_clipDirty  = true;
            break;
        }
        case 1: {
            m_coordinate = Matrix();
            m_clipDirty  = true;
            break;
        }
        case 2: {
            m_coordinate = Matrix();
            m_direction  = Matrix();
            m_clipDirty  = true;
            break;
        }
        case 3: {
            m_texture = Matrix();
            break;
        }
    }
}

void Gpu3D::MtxLoad44Cmd(std::vector<uint32_t> &params) {
    Matrix matrix = *(Matrix *)&params[0];
    switch (m_matrixMode) {
        case 0: {
            m_projection = matrix;
            m_clipDirty  = true;
            break;
        }
        case 1: {
            m_coordinate = matrix;
            m_clipDirty  = true;
            break;
        }
        case 2: {
            m_coordinate = matrix;
            m_direction  = matrix;
            m_clipDirty  = true;
            break;
        }
        case 3: {
            m_texture = matrix;
            break;
        }
    }
}

void Gpu3D::MtxLoad43Cmd(std::vector<uint32_t> &params) {
    Matrix matrix;
    for (int i = 0; i < 4; i++)
        memcpy(&matrix.data[i * 4], &params[static_cast<std::vector<unsigned int>::size_type>(i * 3)], 3 * sizeof(int32_t));
    switch (m_matrixMode) {
        case 0: {
            m_projection = matrix;
            m_clipDirty  = true;
            break;
        }
        case 1: {
            m_coordinate = matrix;
            m_clipDirty  = true;
            break;
        }
        case 2: {
            m_coordinate = matrix;
            m_direction  = matrix;
            m_clipDirty  = true;
            break;
        }
        case 3: {
            m_texture = matrix;
            break;
        }
    }
}

void Gpu3D::MtxMult44Cmd(std::vector<uint32_t> &params) {
    Matrix matrix = *(Matrix *)&params[0];
    switch (m_matrixMode) {
        case 0: {
            m_projection = matrix * m_projection;
            m_clipDirty  = true;
            break;
        }
        case 1: {
            m_coordinate = matrix * m_coordinate;
            m_clipDirty  = true;
            break;
        }
        case 2: {
            m_coordinate = matrix * m_coordinate;
            m_direction  = matrix * m_direction;
            m_clipDirty  = true;
            break;
        }
        case 3: {
            m_texture = matrix * m_texture;
            break;
        }
    }
}

void Gpu3D::MtxMult43Cmd(std::vector<uint32_t> &params) {
    Matrix matrix;
    for (int i = 0; i < 4; i++)
        memcpy(&matrix.data[i * 4], &params[static_cast<std::vector<unsigned int>::size_type>(i * 3)], 3 * sizeof(int32_t));
    switch (m_matrixMode) {
        case 0: {
            m_projection = matrix * m_projection;
            m_clipDirty  = true;
            break;
        }
        case 1: {
            m_coordinate = matrix * m_coordinate;
            m_clipDirty  = true;
            break;
        }
        case 2: {
            m_coordinate = matrix * m_coordinate;
            m_direction  = matrix * m_direction;
            m_clipDirty  = true;
            break;
        }
        case 3: {
            m_texture = matrix * m_texture;
            break;
        }
    }
}

void Gpu3D::MtxMult33Cmd(std::vector<uint32_t> &params) {
    Matrix matrix;
    for (int i = 0; i < 3; i++)
        memcpy(&matrix.data[i * 4], &params[static_cast<std::vector<unsigned int>::size_type>(i * 3)], 3 * sizeof(int32_t));
    switch (m_matrixMode) {
        case 0: {
            m_projection = matrix * m_projection;
            m_clipDirty  = true;
            break;
        }
        case 1: {
            m_coordinate = matrix * m_coordinate;
            m_clipDirty  = true;
            break;
        }
        case 2: {
            m_coordinate = matrix * m_coordinate;
            m_direction  = matrix * m_direction;
            m_clipDirty  = true;
            break;
        }
        case 3: {
            m_texture = matrix * m_texture;
            break;
        }
    }
}

void Gpu3D::MtxScaleCmd(std::vector<uint32_t> &params) {
    Matrix matrix;
    for (int i = 0; i < 3; i++)
        matrix.data[i * 5] = (int32_t)params[static_cast<std::vector<unsigned int>::size_type>(i)];
    switch (m_matrixMode) {
        case 0: {
            m_projection = matrix * m_projection;
            m_clipDirty  = true;
            break;
        }
        case 1:
        case 2: {
            m_coordinate = matrix * m_coordinate;
            m_clipDirty  = true;
            break;
        }
        case 3: {
            m_texture = matrix * m_texture;
            break;
        }
    }
}

void Gpu3D::MtxTransCmd(std::vector<uint32_t> &params) {
    Matrix matrix;
    memcpy(&matrix.data[12], &params[0], 3 * sizeof(int32_t));
    switch (m_matrixMode) {
        case 0: {
            m_projection = matrix * m_projection;
            m_clipDirty  = true;
            break;
        }
        case 1: {
            m_coordinate = matrix * m_coordinate;
            m_clipDirty  = true;
            break;
        }
        case 2: {
            m_coordinate = matrix * m_coordinate;
            m_direction  = matrix * m_direction;
            m_clipDirty  = true;
            break;
        }
        case 3: {
            m_texture = matrix * m_texture;
            break;
        }
    }
}

void Gpu3D::ColorCmd(uint32_t param) {
    m_savedVertex.color = Rgb5ToRgb6(static_cast<uint16_t>(param));
}

void Gpu3D::NormalCmd(uint32_t param) {
    Vertex normalVector;
    normalVector.x = ((int16_t)((param & 0x000003FF) << 6)) >> 3;
    normalVector.y = ((int16_t)((param & 0x000FFC00) >> 4)) >> 3;
    normalVector.z = ((int16_t)((param & 0x3FF00000) >> 14)) >> 3;
    if (m_textureCoordMode == 2) {
        Vertex vertex   = normalVector;
        vertex.w        = 1 << 12;
        Matrix matrix   = m_texture;
        matrix.data[12] = m_s << 12;
        matrix.data[13] = m_t << 12;
        vertex          = vertex * matrix;
        m_savedVertex.s   = static_cast<int16_t>(vertex.x >> 12);
        m_savedVertex.t   = static_cast<int16_t>(vertex.y >> 12);
    }
    normalVector      = normalVector * m_direction;
    m_savedVertex.color = m_emissionColor;
    for (int i = 0; i < 4; i++) {
        if (m_enabledLights & BIT(i)) {
            int diffuseLevel = -(m_lightVector[i] * normalVector);
            if (diffuseLevel < (0 << 12))
                diffuseLevel = (0 << 12);
            if (diffuseLevel > (1 << 12))
                diffuseLevel = (1 << 12);
            int shininessLevel = -(m_halfVector[i] * normalVector);
            if (shininessLevel < (0 << 12))
                shininessLevel = (0 << 12);
            if (shininessLevel > (1 << 12))
                shininessLevel = (1 << 12);
            shininessLevel = (shininessLevel * shininessLevel) >> 12;
            if (m_shininessEnabled)
                shininessLevel = m_shininess[shininessLevel >> 5] << 4;
            int r = (m_savedVertex.color >> 0) & 0x3F;
            int g = (m_savedVertex.color >> 6) & 0x3F;
            int b = (m_savedVertex.color >> 12) & 0x3F;
            r = static_cast<int>(static_cast<uint32_t>(r) + ((((m_specularColor >> 0) & 0x3F) * ((m_lightColor[i] >> 0) & 0x3F) * static_cast<uint32_t>(shininessLevel)) >> 18));
            g = static_cast<int>(static_cast<uint32_t>(g) + ((((m_specularColor >> 6) & 0x3F) * ((m_lightColor[i] >> 6) & 0x3F) * static_cast<uint32_t>(shininessLevel)) >> 18));
            b = static_cast<int>(static_cast<uint32_t>(b) + ((((m_specularColor >> 12) & 0x3F) * ((m_lightColor[i] >> 12) & 0x3F) * static_cast<uint32_t>(shininessLevel)) >> 18));
            r = static_cast<int>(static_cast<uint32_t>(r) + ((((m_diffuseColor >> 0) & 0x3F) * ((m_lightColor[i] >> 0) & 0x3F) * static_cast<uint32_t>(diffuseLevel)) >> 18));
            g = static_cast<int>(static_cast<uint32_t>(g) + ((((m_diffuseColor >> 6) & 0x3F) * ((m_lightColor[i] >> 6) & 0x3F) * static_cast<uint32_t>(diffuseLevel)) >> 18));
            b = static_cast<int>(static_cast<uint32_t>(b) + ((((m_diffuseColor >> 12) & 0x3F) * ((m_lightColor[i] >> 12) & 0x3F) * static_cast<uint32_t>(diffuseLevel)) >> 18));
            r = static_cast<int>(static_cast<uint32_t>(r) + (((m_ambientColor >> 0) & 0x3F) * ((m_lightColor[i] >> 0) & 0x3F) >> 6));
            g = static_cast<int>(static_cast<uint32_t>(g) + (((m_ambientColor >> 6) & 0x3F) * ((m_lightColor[i] >> 6) & 0x3F) >> 6));
            b = static_cast<int>(static_cast<uint32_t>(b) + (((m_ambientColor >> 12) & 0x3F) * ((m_lightColor[i] >> 12) & 0x3F) >> 6));
            if (r < 0)
                r = 0;
            if (r > 0x3F)
                r = 0x3F;
            if (g < 0)
                g = 0;
            if (g > 0x3F)
                g = 0x3F;
            if (b < 0)
                b = 0;
            if (b > 0x3F)
                b = 0x3F;
            m_savedVertex.color = static_cast<uint32_t>((b << 12) | (g << 6) | r);
        }
    }
}

void Gpu3D::TexCoordCmd(uint32_t param) {
    m_s = (int16_t)(param >> 0);
    m_t = (int16_t)(param >> 16);
    if (m_textureCoordMode == 1) {
        Vertex vertex;
        vertex.x      = m_s << 8;
        vertex.y      = m_t << 8;
        vertex.z      = 1 << 8;
        vertex.w      = 1 << 8;
        vertex        = vertex * m_texture;
        m_savedVertex.s = static_cast<int16_t>(vertex.x >> 8);
        m_savedVertex.t = static_cast<int16_t>(vertex.y >> 8);
    } else {
        m_savedVertex.s = m_s;
        m_savedVertex.t = m_t;
    }
}

void Gpu3D::Vtx16Cmd(std::vector<uint32_t> &params) {
    m_savedVertex.x = (int16_t)(params[0] >> 0);
    m_savedVertex.y = (int16_t)(params[0] >> 16);
    m_savedVertex.z = (int16_t)(params[1]);
    AddVertex();
}

void Gpu3D::Vtx10Cmd(uint32_t param) {
    m_savedVertex.x = (int16_t)((param & 0x000003FF) << 6);
    m_savedVertex.y = (int16_t)((param & 0x000FFC00) >> 4);
    m_savedVertex.z = (int16_t)((param & 0x3FF00000) >> 14);
    AddVertex();
}

void Gpu3D::VtxXYCmd(uint32_t param) {
    m_savedVertex.x = (int16_t)(param >> 0);
    m_savedVertex.y = (int16_t)(param >> 16);
    AddVertex();
}

void Gpu3D::VtxXZCmd(uint32_t param) {
    m_savedVertex.x = (int16_t)(param >> 0);
    m_savedVertex.z = (int16_t)(param >> 16);
    AddVertex();
}

void Gpu3D::VtxYZCmd(uint32_t param) {
    m_savedVertex.y = (int16_t)(param >> 0);
    m_savedVertex.z = (int16_t)(param >> 16);
    AddVertex();
}

void Gpu3D::VtxDiffCmd(uint32_t param) {
    m_savedVertex.x += ((int16_t)((param & 0x000003FF) << 6) / 8) >> 3;
    m_savedVertex.y += ((int16_t)((param & 0x000FFC00) >> 4) / 8) >> 3;
    m_savedVertex.z += ((int16_t)((param & 0x3FF00000) >> 14) / 8) >> 3;
    AddVertex();
}

void Gpu3D::PolygonAttrCmd(uint32_t param) {
    m_polygonAttr = param;
}

void Gpu3D::TexImageParamCmd(uint32_t param) {
    m_savedPolygon.textureAddr  = (param & 0x0000FFFF) << 3;
    m_savedPolygon.sizeS        = 8 << ((param & 0x00700000) >> 20);
    m_savedPolygon.sizeT        = 8 << ((param & 0x03800000) >> 23);
    m_savedPolygon.repeatS      = param & BIT(16);
    m_savedPolygon.repeatT      = param & BIT(17);
    m_savedPolygon.flipS        = param & BIT(18);
    m_savedPolygon.flipT        = param & BIT(19);
    m_savedPolygon.textureFmt   = (param & 0x1C000000) >> 26;
    m_savedPolygon.transparent0 = param & BIT(29);
    m_textureCoordMode          = static_cast<int>((param & 0xC0000000) >> 30);
}

void Gpu3D::PlttBaseCmd(uint32_t param) {
    m_savedPolygon.paletteAddr = param & 0x00001FFF;
}

void Gpu3D::DifAmbCmd(uint32_t param) {
    m_diffuseColor = Rgb5ToRgb6(static_cast<uint16_t>(param >> 0));
    m_ambientColor = Rgb5ToRgb6(static_cast<uint16_t>(param >> 16));
    if (param & BIT(15))
        m_savedVertex.color = m_diffuseColor;
}

void Gpu3D::SpeEmiCmd(uint32_t param) {
    m_specularColor    = Rgb5ToRgb6(static_cast<uint16_t>(param >> 0));
    m_emissionColor    = Rgb5ToRgb6(static_cast<uint16_t>(param >> 16));
    m_shininessEnabled = param & BIT(15);
}

void Gpu3D::LightVectorCmd(uint32_t param) {
    m_lightVector[param >> 30].x = ((int16_t)((param & 0x000003FF) << 6)) >> 3;
    m_lightVector[param >> 30].y = ((int16_t)((param & 0x000FFC00) >> 4)) >> 3;
    m_lightVector[param >> 30].z = ((int16_t)((param & 0x3FF00000) >> 14)) >> 3;
    m_lightVector[param >> 30]   = m_lightVector[param >> 30] * m_direction;
    m_halfVector[param >> 30].x  = (m_lightVector[param >> 30].x) / 2;
    m_halfVector[param >> 30].y  = (m_lightVector[param >> 30].y) / 2;
    m_halfVector[param >> 30].z  = (m_lightVector[param >> 30].z - (1 << 12)) / 2;
}

void Gpu3D::LightColorCmd(uint32_t param) {
    m_lightColor[param >> 30] = Rgb5ToRgb6(static_cast<uint16_t>(param));
}

void Gpu3D::ShininessCmd(std::vector<uint32_t> &params) {
    for (int i = 0; i < 32; i++) {
        m_shininess[i * 4 + 0] = static_cast<uint8_t>(params[size_t(i)] >> 0);
        m_shininess[i * 4 + 1] = static_cast<uint8_t>(params[size_t(i)] >> 8);
        m_shininess[i * 4 + 2] = static_cast<uint8_t>(params[size_t(i)] >> 16);
        m_shininess[i * 4 + 3] = static_cast<uint8_t>(params[size_t(i)] >> 24);
    }
}

void Gpu3D::BeginVtxsCmd(uint32_t param) {
    if (m_vertexCount < 3 + (m_polygonType & 1))
        m_vertexCountIn -= m_vertexCount;
    m_polygonType                 = param & 0x00000003;
    m_vertexCount                 = 0;
    m_clockwise                   = false;
    m_enabledLights               = m_polygonAttr & 0x0000000F;
    m_savedPolygon.mode           = (m_polygonAttr & 0x00000030) >> 4;
    m_renderBack                  = m_polygonAttr & BIT(6);
    m_renderFront                 = m_polygonAttr & BIT(7);
    m_savedPolygon.transNewDepth  = m_polygonAttr & BIT(11);
    m_savedPolygon.depthTestEqual = m_polygonAttr & BIT(14);
    m_savedPolygon.fog            = m_polygonAttr & BIT(15);
    m_savedPolygon.alpha          = ((m_polygonAttr & 0x001F0000) >> 16) * 2;
    if (m_savedPolygon.alpha > 0)
        m_savedPolygon.alpha++;
    m_savedPolygon.id = (m_polygonAttr & 0x3F000000) >> 24;
}

void Gpu3D::SwapBuffersCmd(uint32_t param) {
    m_savedPolygon.wBuffer = param & BIT(1);
    m_state                = GX_HALTED;
}

void Gpu3D::ViewportCmd(uint32_t param) {
    m_viewportX      = ((param & 0x000000FF) >> 0) & 0x1FF;
    m_viewportY      = (191 - ((param & 0xFF000000) >> 24)) & 0xFF;
    m_viewportWidth  = (((param & 0x00FF0000) >> 16) - m_viewportX + 1) & 0x1FF;
    m_viewportHeight = ((191 - ((param & 0x0000FF00) >> 8)) - m_viewportY + 1) & 0xFF;
}

void Gpu3D::BoxTestCmd(std::vector<uint32_t> &params) {
    int16_t boxTestCoords[6] = {(int16_t)params[0],         (int16_t)(params[0] >> 16), (int16_t)params[1],
                                (int16_t)(params[1] >> 16), (int16_t)params[2],         (int16_t)(params[2] >> 16)};
    boxTestCoords[3] += boxTestCoords[0];
    boxTestCoords[4] += boxTestCoords[1];
    boxTestCoords[5] += boxTestCoords[2];
    static const uint8_t indices[8 * 3] = {0, 1, 2, 3, 1, 2, 0, 4, 2, 0, 1, 5, 3, 4, 2, 3, 1, 5, 0, 4, 5, 3, 4, 5};
    if (m_clipDirty) {
        m_clip      = m_coordinate * m_projection;
        m_clipDirty = false;
    }
    Vertex vertices[8];
    for (int i = 0; i < 8; i++) {
        vertices[i].x = boxTestCoords[indices[i * 3 + 0]];
        vertices[i].y = boxTestCoords[indices[i * 3 + 1]];
        vertices[i].z = boxTestCoords[indices[i * 3 + 2]];
        vertices[i].w = 1 << 12;
        vertices[i]   = vertices[i] * m_clip;
    }
    Vertex faces[6][4] = { {vertices[0], vertices[1], vertices[4], vertices[2]}, {vertices[3], vertices[5], vertices[7], vertices[6]}, {vertices[3], vertices[5], vertices[1], vertices[0]}, {vertices[6], vertices[7], vertices[4], vertices[2]}, {vertices[0], vertices[3], vertices[6], vertices[2]}, {vertices[1], vertices[5], vertices[7], vertices[4]}};
    if ((m_testQueue -= 3) == 0)
        m_gxStat &= ~BIT(0);
    for (int i = 0; i < 6; i++) {
        int    size = 4;
        Vertex clipped[10];
        ClipPolygon(faces[i], clipped, &size);
        if (size > 0) {
            m_gxStat |= BIT(1);
            return;
        }
    }
    m_gxStat &= ~BIT(1);
}

void Gpu3D::PosTestCmd(std::vector<uint32_t> &params) {
    m_savedVertex.x = (int16_t)(params[0] >> 0);
    m_savedVertex.y = (int16_t)(params[0] >> 16);
    m_savedVertex.z = (int16_t)(params[1]);
    m_savedVertex.w = 1 << 12;
    if (m_clipDirty) {
        m_clip      = m_coordinate * m_projection;
        m_clipDirty = false;
    }
    Vertex vertex = m_savedVertex * m_clip;
    m_posResult[0]  = vertex.x;
    m_posResult[1]  = vertex.y;
    m_posResult[2]  = vertex.z;
    m_posResult[3]  = vertex.w;
    if ((m_testQueue -= 2) == 0)
        m_gxStat &= ~BIT(0);
}

void Gpu3D::VecTestCmd(uint32_t param) {
    Vertex vector;
    vector.x     = ((int16_t)((param & 0x000003FF) << 6)) >> 3;
    vector.y     = ((int16_t)((param & 0x000FFC00) >> 4)) >> 3;
    vector.z     = ((int16_t)((param & 0x3FF00000) >> 14)) >> 3;
    vector       = vector * m_direction;
    m_vecResult[0] = ((int16_t)(vector.x << 3)) >> 3;
    m_vecResult[1] = ((int16_t)(vector.y << 3)) >> 3;
    m_vecResult[2] = ((int16_t)(vector.z << 3)) >> 3;
    if (--m_testQueue == 0)
        m_gxStat &= ~BIT(0);
}

void Gpu3D::AddEntry(Entry entry) {
    if (m_fifo.size() - m_pipeSize == 0 && m_pipeSize < 4) {
        m_fifo.push(entry);
        m_pipeSize++;
        m_gxStat |= BIT(27);
    } else {
        if (m_fifo.size() - m_pipeSize >= 256)
            m_core->interpreter[0].Halt(1);
        m_fifo.push(entry);
        m_gxStat = static_cast<uint32_t>((m_gxStat & ~0x01FF0000u) | ((m_fifo.size() - m_pipeSize) << 16));
        m_gxStat &= ~BIT(26);
        if (m_fifo.size() - m_pipeSize >= 128 && (m_gxStat & BIT(25)))
            m_gxStat &= ~BIT(25);
    }
    switch (entry.command) {
        case 0x11:
        case 0x12:
            m_matrixQueue++;
            m_gxStat |= BIT(14);
            break;
        case 0x70:
        case 0x71:
        case 0x72:
            m_testQueue++;
            m_gxStat |= BIT(0);
            break;
    }
    if (m_state == GX_IDLE && m_fifo.size() >= paramCounts[m_fifo.front().command]) {
        m_core->Schedule(Task(&m_runCommandTask, 2));
        m_state = GX_RUNNING;
    }
}

void Gpu3D::WriteGxFifo(uint32_t mask, uint32_t value) {
    if (m_gxFifo == 0) {
        m_gxFifo = value & mask;
    } else {
        Entry entry(static_cast<uint8_t>(m_gxFifo), value & mask);
        AddEntry(entry);
        m_gxFifoCount++;
        if (m_gxFifoCount == paramCounts[m_gxFifo & 0xFF]) {
            m_gxFifo >>= 8;
            m_gxFifoCount = 0;
        }
    }
    while (m_gxFifo != 0 && paramCounts[m_gxFifo & 0xFF] == 0) {
        Entry entry(static_cast<uint8_t>(m_gxFifo), 0);
        AddEntry(entry);
        m_gxFifo >>= 8;
    }
}

void Gpu3D::WriteMtxMode(uint32_t mask, uint32_t value) {
    Entry entry(0x10, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteMtxPush(uint32_t mask, uint32_t value) {
    Entry entry(0x11, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteMtxPop(uint32_t mask, uint32_t value) {
    Entry entry(0x12, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteMtxStore(uint32_t mask, uint32_t value) {
    Entry entry(0x13, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteMtxRestore(uint32_t mask, uint32_t value) {
    Entry entry(0x14, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteMtxIdentity(uint32_t mask, uint32_t value) {
    Entry entry(0x15, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteMtxLoad44(uint32_t mask, uint32_t value) {
    Entry entry(0x16, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteMtxLoad43(uint32_t mask, uint32_t value) {
    Entry entry(0x17, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteMtxMult44(uint32_t mask, uint32_t value) {
    Entry entry(0x18, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteMtxMult43(uint32_t mask, uint32_t value) {
    Entry entry(0x19, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteMtxMult33(uint32_t mask, uint32_t value) {
    Entry entry(0x1A, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteMtxScale(uint32_t mask, uint32_t value) {
    Entry entry(0x1B, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteMtxTrans(uint32_t mask, uint32_t value) {
    Entry entry(0x1C, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteColor(uint32_t mask, uint32_t value) {
    Entry entry(0x20, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteNormal(uint32_t mask, uint32_t value) {
    Entry entry(0x21, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteTexCoord(uint32_t mask, uint32_t value) {
    Entry entry(0x22, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteVtx16(uint32_t mask, uint32_t value) {
    Entry entry(0x23, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteVtx10(uint32_t mask, uint32_t value) {
    Entry entry(0x24, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteVtxXY(uint32_t mask, uint32_t value) {
    Entry entry(0x25, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteVtxXZ(uint32_t mask, uint32_t value) {
    Entry entry(0x26, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteVtxYZ(uint32_t mask, uint32_t value) {
    Entry entry(0x27, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteVtxDiff(uint32_t mask, uint32_t value) {
    Entry entry(0x28, value & mask);
    AddEntry(entry);
}

void Gpu3D::WritePolygonAttr(uint32_t mask, uint32_t value) {
    Entry entry(0x29, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteTexImageParam(uint32_t mask, uint32_t value) {
    Entry entry(0x2A, value & mask);
    AddEntry(entry);
}

void Gpu3D::WritePlttBase(uint32_t mask, uint32_t value) {
    Entry entry(0x2B, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteDifAmb(uint32_t mask, uint32_t value) {
    Entry entry(0x30, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteSpeEmi(uint32_t mask, uint32_t value) {
    Entry entry(0x31, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteLightVector(uint32_t mask, uint32_t value) {
    Entry entry(0x32, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteLightColor(uint32_t mask, uint32_t value) {
    Entry entry(0x33, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteShininess(uint32_t mask, uint32_t value) {
    Entry entry(0x34, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteBeginVtxs(uint32_t mask, uint32_t value) {
    Entry entry(0x40, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteEndVtxs(uint32_t mask, uint32_t value) {
    Entry entry(0x41, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteSwapBuffers(uint32_t mask, uint32_t value) {
    Entry entry(0x50, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteViewport(uint32_t mask, uint32_t value) {
    Entry entry(0x60, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteBoxTest(uint32_t mask, uint32_t value) {
    Entry entry(0x70, value & mask);
    AddEntry(entry);
}

void Gpu3D::WritePosTest(uint32_t mask, uint32_t value) {
    Entry entry(0x71, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteVecTest(uint32_t mask, uint32_t value) {
    Entry entry(0x72, value & mask);
    AddEntry(entry);
}

void Gpu3D::WriteGxStat(uint32_t mask, uint32_t value) {
    if (value & BIT(15))
        m_gxStat &= ~0xA000u;
    mask &= 0xC0000000;
    m_gxStat = (m_gxStat & ~mask) | (value & mask);
}

uint32_t Gpu3D::ReadRamCount() {
    return static_cast<uint32_t>((m_vertexCountIn << 16) | m_polygonCountIn);
}

uint32_t Gpu3D::ReadClipMtxResult(int index) {
    if (m_clipDirty) {
        m_clip      = m_coordinate * m_projection;
        m_clipDirty = false;
    }
    return static_cast<uint32_t>(m_clip.data[index]);
}

uint32_t Gpu3D::ReadVecMtxResult(int index) {
    return static_cast<uint32_t>(m_direction.data[(index / 3) * 4 + index % 3]);
}

/**
 * @brief Sérialise/désérialise un tableau de 2048 polygones (polygons1 ou polygons2).
 *
 * `Polygon::vertices` pointe toujours à l'intérieur du tableau de sommets
 * associé (vertices1 pour polygons1, vertices2 pour polygons2 : AddPolygon()
 * assigne toujours `vertices` à l'intérieur de `verticesIn`, et verticesIn et
 * polygonsIn permutent ensemble dans SwapBuffers(), donc l'association
 * polygons1<->vertices1 / polygons2<->vertices2 ne change jamais). On ne peut
 * pas sérialiser ce pointeur brut tel quel (il ne serait plus valide après un
 * rechargement dans un autre processus) : on le remplace par un simple index
 * relatif à `base`, recalculé après lecture.
 */
static void IoPolygonArray(StateArchive &archive, Polygon (&polys)[2048], Vertex (&base)[6144]) {
    for (int i = 0; i < 2048; i++) {
        Polygon &p       = polys[i];
        int32_t   vOffset = archive.saving ? (p.vertices ? int32_t(p.vertices - base) : -1) : 0;
        archive.Io(p); // copie brute de la structure (le pointeur vertices y est inclus mais ignoré)
        archive.Io(vOffset);
        if (!archive.saving)
            p.vertices = (vOffset >= 0) ? (base + vOffset) : nullptr;
    }
}

void Gpu3D::IoState(StateArchive &archive) {
    // Le thread de rendu (Gpu3DRenderer) lit les polygones/sommets ci-dessous :
    // il doit être arrêté avant qu'on ne les modifie (chargement) ou qu'un
    // rendu concurrent ne les lise pendant qu'on les recopie (sauvegarde).
    m_core->gpu3DRenderer.Quiesce();

    archive.Io(m_state);
    archive.IoQueue(m_fifo);
    archive.Io(m_pipeSize);
    archive.Io(m_testQueue);
    archive.Io(m_matrixQueue);
    archive.Io(m_matrixMode);
    archive.Io(m_clipDirty);
    archive.Io(m_projection);
    archive.Io(m_projectionStack);
    archive.Io(m_coordinate);
    archive.Io(m_coordinateStack);
    archive.Io(m_direction);
    archive.Io(m_directionStack);
    archive.Io(m_texture);
    archive.Io(m_textureStack);
    archive.Io(m_clip);
    archive.Io(m_vertices1);
    archive.Io(m_vertices2);

    bool inIsSet1 = (m_verticesIn == m_vertices1);
    archive.Io(inIsSet1);
    if (!archive.saving) {
        m_verticesIn  = inIsSet1 ? m_vertices1 : m_vertices2;
        m_verticesOut = inIsSet1 ? m_vertices2 : m_vertices1;
        m_polygonsIn  = inIsSet1 ? m_polygons1 : m_polygons2;
        m_polygonsOut = inIsSet1 ? m_polygons2 : m_polygons1;
    }

    archive.Io(m_vertexCountIn);
    archive.Io(m_vertexCountOut);

    IoPolygonArray(archive, m_polygons1, m_vertices1);
    IoPolygonArray(archive, m_polygons2, m_vertices2);

    archive.Io(m_polygonCountIn);
    archive.Io(m_polygonCountOut);
    archive.Io(m_savedVertex);

    // savedPolygon.vertices pointe toujours dans verticesIn (voir AddPolygon()),
    // qui vient d'être restauré ci-dessus.
    {
        int32_t vOffset =
            archive.saving ? (m_savedPolygon.vertices ? int32_t(m_savedPolygon.vertices - m_verticesIn) : -1) : 0;
        archive.Io(m_savedPolygon);
        archive.Io(vOffset);
        if (!archive.saving)
            m_savedPolygon.vertices = (vOffset >= 0) ? (m_verticesIn + vOffset) : nullptr;
    }

    archive.Io(m_s);
    archive.Io(m_t);
    archive.Io(m_vertexCount);
    archive.Io(m_clockwise);
    archive.Io(m_polygonType);
    archive.Io(m_textureCoordMode);
    archive.Io(m_polygonAttr);
    archive.Io(m_enabledLights);
    archive.Io(m_renderBack);
    archive.Io(m_renderFront);
    archive.Io(m_diffuseColor);
    archive.Io(m_ambientColor);
    archive.Io(m_specularColor);
    archive.Io(m_emissionColor);
    archive.Io(m_shininessEnabled);
    archive.Io(m_lightVector);
    archive.Io(m_halfVector);
    archive.Io(m_lightColor);
    archive.Io(m_shininess);
    archive.Io(m_viewportX);
    archive.Io(m_viewportY);
    archive.Io(m_viewportWidth);
    archive.Io(m_viewportHeight);
    archive.Io(m_gxFifo);
    archive.Io(m_gxStat);
    archive.Io(m_posResult);
    archive.Io(m_vecResult);
    archive.Io(m_gxFifoCount);

    // Le scheduler (Core::tasks) n'est pas sérialisé : si une commande GXFIFO
    // était prête à s'exécuter au moment de la sauvegarde, il faut la
    // reprogrammer, à l'identique de la condition utilisée ailleurs dans ce
    // fichier (cf. RunCommand()/SwapBuffers()).
    if (!archive.saving && m_state != GX_HALTED && !m_fifo.empty() && m_fifo.size() >= paramCounts[m_fifo.front().command])
        m_core->Schedule(Task(&m_runCommandTask, 2));
}

} // namespace consoles::arm

