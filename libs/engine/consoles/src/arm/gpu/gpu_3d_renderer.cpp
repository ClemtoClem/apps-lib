#include <cstring>
#include <vector>
#include "consoles/arm/gpu/gpu_3d_renderer.hpp"
#include "consoles/arm/core.hpp"
#include "consoles/arm/settings.hpp"

namespace consoles::arm {


Gpu3DRenderer::Gpu3DRenderer(Core *core_) : m_core(core_) {
    for (int i = 0; i < 192 * 2; i++)
        m_ready[i].store(3);
}

Gpu3DRenderer::~Gpu3DRenderer() {
    Quiesce();
}

void Gpu3DRenderer::Quiesce() {
    for (int i = 0; i < m_activeThreads; i++) {
        if (m_threads[i]) {
            m_threads[i]->join();
            m_threads[i].reset();
        }
    }
    m_activeThreads = 0;
}

uint32_t Gpu3DRenderer::Rgba5ToRgba6(uint32_t color) {
    uint8_t r = ((color >> 0) & 0x1F) * 2;
    if (r > 0)
        r++;
    uint8_t g = ((color >> 5) & 0x1F) * 2;
    if (g > 0)
        g++;
    uint8_t b = ((color >> 10) & 0x1F) * 2;
    if (b > 0)
        b++;
    uint8_t a = ((color >> 15) & 0x1F) * 2;
    if (a > 0)
        a++;
    return (a << 18) | (b << 12) | (g << 6) | r;
}

uint32_t *Gpu3DRenderer::GetLine(int line) {
    if (m_resShift) {
        uint32_t *data = GetLine1(line * 2);
        GetLine1(line * 2 + 1);
        return data;
    }
    return GetLine1(line);
}

uint32_t *Gpu3DRenderer::GetLine1(int line) {
    if (m_ready[line].load() < 3 && line + m_activeThreads * 2 < (192 << m_resShift)) {
        int next = line + m_activeThreads * 2;
        switch (m_ready[next].exchange(1)) {
            case 0:
                DrawScanline1(next);
                m_ready[next].store(2);
                break;
            case 2:
                if (m_ready[next].exchange(2) == 3)
                    m_ready[next].store(3);
                break;
            case 3:
                m_ready[next].store(3);
                break;
        }
    }
    while (m_ready[line].load() < 3)
        std::this_thread::yield();
    return &m_framebuffer[0][line * 256 * 2];
}

void Gpu3DRenderer::DrawScanline(int line) {
    if (line == 0) {
        for (int i = 0; i < m_core->gpu3D.GetPolygonCount(); i++) {
            m_polygonTop[i]     = 192 * 2;
            m_polygonBot[i]     = 0 * 2;
            Polygon *polygon = &m_core->gpu3D.GetPolygons()[i];
            for (int j = 0; j < polygon->size; j++) {
                Vertex *vertex = &polygon->vertices[j];
                if (vertex->y < m_polygonTop[i])
                    m_polygonTop[i] = vertex->y;
                if (vertex->y > m_polygonBot[i])
                    m_polygonBot[i] = vertex->y;
            }
            if (m_polygonTop[i] == m_polygonBot[i])
                m_polygonBot[i]++;
        }
        m_resShift = m_core->settings.highRes3D;
        for (int i = 0; i < m_activeThreads; i++) {
            if (m_threads[i]) {
                m_threads[i]->join();
                m_threads[i].reset();
            }
        }
        m_activeThreads = m_core->settings.threaded3D;
        if (m_activeThreads > 3)
            m_activeThreads = 3;
        if (m_activeThreads > 0) {
            int end = 192 << m_resShift;
            for (int i = 0; i < end; i++)
                m_ready[i].store(0);
            for (int i = 0; i < m_activeThreads; i++) {
                m_threads[i] = std::make_unique<std::thread>(&Gpu3DRenderer::DrawThreaded, this, i);
            }
        }
    }
    if (m_activeThreads == 0) {
        if (m_resShift) {
            for (int i = line * 2; i < line * 2 + 2; i++) {
                DrawScanline1(i);
                if (i > 0)
                    FinishScanline(i - 1);
                if (i == 383)
                    FinishScanline(383);
            }
        } else {
            DrawScanline1(line);
            if (line > 0)
                FinishScanline(line - 1);
            if (line == 191)
                FinishScanline(191);
        }
    }
}

void Gpu3DRenderer::DrawThreaded(int thread) {
    int i, end = 192 << m_resShift;
    for (i = thread; i < end; i += m_activeThreads) {
        switch (m_ready[i].exchange(1)) {
            case 0:
                DrawScanline1(i);
                m_ready[i].store(2);
                break;
            case 2:
                m_ready[i].store(2);
                break;
        }
        if (i < m_activeThreads)
            continue;
        int prev = i - m_activeThreads;
        while ((prev > 0 && m_ready[prev - 1].load() < 2) || m_ready[prev].load() < 2 || m_ready[prev + 1].load() < 2)
            std::this_thread::yield();
        FinishScanline(prev);
        m_ready[prev].store(3);
    }
    int prev = i - m_activeThreads;
    while (m_ready[prev - 1].load() < 2 || m_ready[prev].load() < 2 || (prev < 191 && m_ready[prev + 1].load() < 2))
        std::this_thread::yield();
    FinishScanline(prev);
    m_ready[prev].store(3);
}

void Gpu3DRenderer::DrawScanline1(int line) {
    uint32_t color  = BIT(26) | Rgba5ToRgba6(((m_clearColor & 0x001F0000) >> 1) | (m_clearColor & 0x00007FFF));
    int32_t  depth  = (m_clearDepth == 0x7FFF) ? 0xFFFFFF : (m_clearDepth << 9);
    uint32_t attrib = ((m_clearColor & BIT(15)) >> 2) | ((m_clearColor & 0x3F000000) >> 18) |
                      ((m_clearColor & 0x3F000000) >> 24) | (0x3F << 15) |
                      (((m_clearColor & 0x001F0000) && ((m_clearColor & 0x001F0000) >> 16) < 31) << 12);
    int start = line * 256 * 2, end = start + (256 << m_resShift);
    for (int i = start; i < end; i++) {
        m_framebuffer[0][i]  = color;
        m_depthBuffer[0][i]  = depth;
        m_attribBuffer[0][i] = attrib;
    }
    m_stencilClear[line] = false;
    std::vector<int> translucent;
    for (int i = 0; i < m_core->gpu3D.GetPolygonCount(); i++) {
        if (line < m_polygonTop[i] || line >= m_polygonBot[i])
            continue;
        Polygon *polygon = &m_core->gpu3D.GetPolygons()[i];
        if (polygon->alpha < 0x3F || polygon->textureFmt == 1 || polygon->textureFmt == 6)
            translucent.push_back(i);
        else
            DrawPolygon(line, i);
    }
    for (unsigned int i = 0; i < translucent.size(); i++)
        DrawPolygon(line, translucent[i]);
}

void Gpu3DRenderer::FinishScanline(int line) {
    if (m_disp3DCnt & BIT(5)) {
        int offset = line * 256 * 2;
        int w      = (256 << m_resShift) - 1;
        int h      = (192 << m_resShift) - 1;
        for (int i = offset; i <= offset + w; i++) {
            if (m_attribBuffer[0][i] & BIT(14)) {
                uint32_t id[4]    = {(((i & w) > 0) ? m_attribBuffer[0][i - 1] : (m_clearColor >> 24)) & 0x3F,
                                     (((i & w) < w) ? m_attribBuffer[0][i + 1] : (m_clearColor >> 24)) & 0x3F,
                                     ((line > 0) ? m_attribBuffer[0][i - 512] : (m_clearColor >> 24)) & 0x3F,
                                     ((line < h) ? m_attribBuffer[0][i + 512] : (m_clearColor >> 24)) & 0x3F};
                int32_t  depth[4] = {
                     (((i & w) > 0) ? m_depthBuffer[0][i - 1] : ((m_clearDepth == 0x7FFF) ? 0xFFFFFF : (m_clearDepth << 9))),
                     (((i & w) < w) ? m_depthBuffer[0][i + 1] : ((m_clearDepth == 0x7FFF) ? 0xFFFFFF : (m_clearDepth << 9))),
                     ((line > 0) ? m_depthBuffer[0][i - 512] : ((m_clearDepth == 0x7FFF) ? 0xFFFFFF : (m_clearDepth << 9))),
                     ((line < h) ? m_depthBuffer[0][i + 512] : ((m_clearDepth == 0x7FFF) ? 0xFFFFFF : (m_clearDepth << 9)))};
                for (int j = 0; j < 4; j++) {
                    if ((m_attribBuffer[0][i] & 0x3F) != id[j] && m_depthBuffer[0][i] < depth[j]) {
                        m_framebuffer[0][i] =
                            BIT(26) | Rgba5ToRgba6((0x1F << 15) | m_edgeColor[(m_attribBuffer[0][i] & 0x3F) >> 3]);
                        m_attribBuffer[0][i] = (m_attribBuffer[0][i] & ~(0x3Fu << 15)) | (0x20 << 15);
                        break;
                    }
                }
            }
        }
    }
    if (m_disp3DCnt & BIT(7)) {
        uint32_t fog     = Rgba5ToRgba6(((m_fogColor & 0x001F0000) >> 1) | (m_fogColor & 0x00007FFF));
        int      fogStep = 0x400 >> ((m_disp3DCnt & 0x0F00) >> 8);
        for (int layer = 0; layer < ((m_disp3DCnt & BIT(4)) ? 2 : 1); layer++) {
            int start = line * 256 * 2, end = start + (256 << m_resShift);
            for (int i = start; i < end; i++) {
                if (m_attribBuffer[layer][i] & BIT(13)) {
                    int32_t offset = ((m_depthBuffer[layer][i] / 0x200) - m_fogOffset);
                    int     n      = (fogStep > 0) ? (offset / fogStep - 1) : ((offset > 0) ? 31 : 0);
                    uint8_t density;
                    if (n >= 31) {
                        density = m_fogTable[31];
                    } else if (n < 0 || fogStep == 0) {
                        density = m_fogTable[0];
                    } else {
                        int m = offset % fogStep;
                        density =
                            static_cast<uint8_t>(((m >= 0) ? ((m_fogTable[n + 1] * m + m_fogTable[n] * (fogStep - m)) / fogStep) : m_fogTable[0]));
                    }
                    if (density == 127)
                        density++;
                    uint8_t a =
                        static_cast<uint8_t>((((fog >> 18) & 0x3F) * density + ((m_framebuffer[layer][i] >> 18) & 0x3F) * (128 - density)) /
                        128);
                    if (m_disp3DCnt & BIT(6)) {
                        m_framebuffer[layer][i] = (m_framebuffer[layer][i] & ~(0x3Fu << 18)) | (a << 18);
                    } else {
                        uint8_t r =
                            static_cast<uint8_t>((((fog >> 0) & 0x3F) * density + ((m_framebuffer[layer][i] >> 0) & 0x3F) * (128 - density)) /
                            128);
                        uint8_t g =
                            static_cast<uint8_t>((((fog >> 6) & 0x3F) * density + ((m_framebuffer[layer][i] >> 6) & 0x3F) * (128 - density)) /
                            128);
                        uint8_t b = static_cast<uint8_t>((((fog >> 12) & 0x3F) * density +
                                     ((m_framebuffer[layer][i] >> 12) & 0x3F) * (128 - density)) /
                                    128);
                        m_framebuffer[layer][i] = BIT(26) | (a << 18) | (b << 12) | (g << 6) | r;
                    }
                }
            }
        }
    }
    if (m_disp3DCnt & BIT(4)) {
        int start = line * 256 * 2, end = start + (256 << m_resShift);
        for (int i = start; i < end; i++) {
            if (((m_attribBuffer[0][i] >> 15) & 0x3F) < 0x3F) {
                if ((m_framebuffer[1][i] >> 18) & 0x3F)
                    m_framebuffer[0][i] = BIT(26) | InterpolateColor(m_framebuffer[1][i], m_framebuffer[0][i], 0,
                                                                   ((m_attribBuffer[0][i] >> 15) & 0x3F), 0x3F);
                else
                    m_framebuffer[0][i] = (m_framebuffer[0][i] & ~0xFC0000u) | ((m_attribBuffer[0][i] & 0x1F8000) << 3);
            }
        }
    }
}

uint8_t *Gpu3DRenderer::GetTexture(uint32_t address) {
    uint8_t *slot = m_core->memory.GetTex3D()[address >> 17];
    return slot ? &slot[address & 0x1FFFF] : nullptr;
}

uint8_t *Gpu3DRenderer::GetPalette(uint32_t address) {
    uint8_t *slot = m_core->memory.GetPal3D()[address >> 14];
    return slot ? &slot[address & 0x3FFF] : nullptr;
}

uint32_t Gpu3DRenderer::InterpolateLinear(uint32_t v1, uint32_t v2, uint32_t x1, uint32_t x, uint32_t x2) {
    if (x <= x1)
        return v1;
    if (x >= x2)
        return v2;
    if (v1 <= v2)
        return v1 + (v2 - v1) * (x - x1) / (x2 - x1);
    else
        return v2 + (v1 - v2) * (x2 - x) / (x2 - x1);
}

uint32_t Gpu3DRenderer::InterpolateLinRev(uint32_t v1, uint32_t v2, uint32_t x1, uint32_t x, uint32_t x2) {
    if (x <= x1)
        return v1;
    if (x >= x2)
        return v2;
    if (v1 <= v2)
        return v2 - (v2 - v1) * (x2 - x) / (x2 - x1);
    else
        return v1 - (v1 - v2) * (x - x1) / (x2 - x1);
}

uint32_t Gpu3DRenderer::InterpolateFactor(uint32_t factor, uint32_t shift, uint32_t v1, uint32_t v2) {
    if (v1 <= v2)
        return v1 + (((v2 - v1) * factor) >> shift);
    else
        return v2 + (((v1 - v2) * ((1 << shift) - factor)) >> shift);
}

uint32_t Gpu3DRenderer::InterpolateColor(uint32_t c1, uint32_t c2, uint32_t x1, uint32_t x, uint32_t x2) {
    uint32_t r = InterpolateLinear((c1 >> 0) & 0x3F, (c2 >> 0) & 0x3F, x1, x, x2);
    uint32_t g = InterpolateLinear((c1 >> 6) & 0x3F, (c2 >> 6) & 0x3F, x1, x, x2);
    uint32_t b = InterpolateLinear((c1 >> 12) & 0x3F, (c2 >> 12) & 0x3F, x1, x, x2);
    uint32_t a = (((c1 >> 18) & 0x3F) > ((c2 >> 18) & 0x3F)) ? ((c1 >> 18) & 0x3F) : ((c2 >> 18) & 0x3F);
    return (a << 18) | (b << 12) | (g << 6) | r;
}

uint32_t Gpu3DRenderer::ReadTexture(Polygon *polygon, int s, int t) {
    if (polygon->repeatS) {
        if (polygon->flipS && (s & polygon->sizeS))
            s = -1 - s;
        s &= polygon->sizeS - 1;
    } else if (s < 0) {
        s = 0;
    } else if (s >= polygon->sizeS) {
        s = polygon->sizeS - 1;
    }
    if (polygon->repeatT) {
        if (polygon->flipT && (t & polygon->sizeT))
            t = -1 - t;
        t &= polygon->sizeT - 1;
    } else if (t < 0) {
        t = 0;
    } else if (t >= polygon->sizeT) {
        t = polygon->sizeT - 1;
    }
    switch (polygon->textureFmt) {
        case 1: {
            uint32_t address = polygon->textureAddr + static_cast<uint32_t>((t * polygon->sizeS + s));
            uint8_t *data    = GetTexture(address);
            if (!data)
                return 0;
            uint8_t  index   = *data;
            uint8_t *palette = GetPalette(polygon->paletteAddr);
            if (!palette)
                return 0;
            uint16_t color = static_cast<uint16_t>(U8TO16(palette, (index & 0x1F) * 2) & ~BIT(15));
            uint8_t  alpha = static_cast<uint8_t>((index >> 5) * 4 + (index >> 5) / 2);
            return Rgba5ToRgba6((alpha << 15) | color);
        }
        case 2: {
            uint32_t address = polygon->textureAddr + static_cast<uint32_t>((t * polygon->sizeS + s) / 4);
            uint8_t *data    = GetTexture(address);
            if (!data)
                return 0;
            uint8_t index = (*data >> ((s % 4) * 2)) & 0x03;
            if (polygon->transparent0 && index == 0)
                return 0;
            uint8_t *palette = GetPalette(polygon->paletteAddr);
            if (!palette)
                return 0;
            return Rgba5ToRgba6((0x1F << 15) | U8TO16(palette, index * 2));
        }
        case 3: {
            uint32_t address = polygon->textureAddr + static_cast<uint32_t>((t * polygon->sizeS + s) / 2);
            uint8_t *data    = GetTexture(address);
            if (!data)
                return 0;
            uint8_t index = (*data >> ((s % 2) * 4)) & 0x0F;
            if (polygon->transparent0 && index == 0)
                return 0;
            uint8_t *palette = GetPalette(polygon->paletteAddr);
            if (!palette)
                return 0;
            return Rgba5ToRgba6((0x1F << 15) | U8TO16(palette, index * 2));
        }
        case 4: {
            uint32_t address = polygon->textureAddr + static_cast<uint32_t>((t * polygon->sizeS + s));
            uint8_t *data    = GetTexture(address);
            if (!data)
                return 0;
            uint8_t index = *data;
            if (polygon->transparent0 && index == 0)
                return 0;
            uint8_t *palette = GetPalette(polygon->paletteAddr);
            if (!palette)
                return 0;
            return Rgba5ToRgba6((0x1F << 15) | U8TO16(palette, index * 2));
        }
        case 5: {
            int      tile    = (t / 4) * (polygon->sizeS / 4) + (s / 4);
            uint32_t address = polygon->textureAddr + static_cast<uint32_t>((tile * 4 + t % 4));
            uint8_t *data    = GetTexture(address);
            if (!data)
                return 0;
            uint8_t index = (*data >> ((s % 4) * 2)) & 0x03;
            address =
                0x20000 + (polygon->textureAddr % 0x20000) / 2 + ((polygon->textureAddr / 0x20000 == 2) ? 0x10000 : 0);
            if (!(data = GetTexture(address)))
                return 0;
            uint16_t palBase = U8TO16(data, tile * 2);
            uint8_t *palette = GetPalette(polygon->paletteAddr + (palBase & 0x3FFF) * 4);
            if (!palette)
                return 0;
            switch ((palBase & 0xC000) >> 14) {
                case 0: {
                    if (index == 3)
                        return 0;
                    return Rgba5ToRgba6((0x1F << 15) | U8TO16(palette, index * 2));
                }
                case 1: {
                    switch (index) {
                        case 2: {
                            uint32_t c1 = Rgba5ToRgba6((0x1F << 15) | U8TO16(palette, 0));
                            uint32_t c2 = Rgba5ToRgba6((0x1F << 15) | U8TO16(palette, 2));
                            return InterpolateColor(c1, c2, 0, 1, 2);
                        }
                        case 3: {
                            return 0;
                        }
                        default: {
                            return Rgba5ToRgba6((0x1F << 15) | U8TO16(palette, index * 2));
                        }
                    }
                }
                case 2: {
                    return Rgba5ToRgba6((0x1F << 15) | U8TO16(palette, index * 2));
                }
                case 3: {
                    switch (index) {
                        case 2: {
                            uint32_t c1 = Rgba5ToRgba6((0x1F << 15) | U8TO16(palette, 0));
                            uint32_t c2 = Rgba5ToRgba6((0x1F << 15) | U8TO16(palette, 2));
                            return InterpolateColor(c1, c2, 0, 3, 8);
                        }
                        case 3: {
                            uint32_t c1 = Rgba5ToRgba6((0x1F << 15) | U8TO16(palette, 0));
                            uint32_t c2 = Rgba5ToRgba6((0x1F << 15) | U8TO16(palette, 2));
                            return InterpolateColor(c1, c2, 0, 5, 8);
                        }
                        default: {
                            return Rgba5ToRgba6((0x1F << 15) | U8TO16(palette, index * 2));
                        }
                    }
                }
            }
            return 0; // inaccessible : (palBase & 0xC000) >> 14 ne vaut que 0..3
        }
        case 6: {
            uint32_t address = polygon->textureAddr + static_cast<uint32_t>((t * polygon->sizeS + s));
            uint8_t *data    = GetTexture(address);
            if (!data)
                return 0;
            uint8_t  index   = *data;
            uint8_t *palette = GetPalette(polygon->paletteAddr);
            if (!palette)
                return 0;
            uint16_t color = static_cast<uint16_t>(U8TO16(palette, (index & 0x07) * 2) & ~BIT(15));
            uint8_t  alpha = index >> 3;
            return Rgba5ToRgba6((alpha << 15) | color);
        }
        default: {
            uint8_t *data = GetTexture(polygon->textureAddr);
            if (!data)
                return 0;
            uint16_t color = U8TO16(data, (t * polygon->sizeS + s) * 2);
            uint8_t  alpha = (color & BIT(15)) ? 0x1F : 0;
            return Rgba5ToRgba6((alpha << 15) | color);
        }
    }
}

void Gpu3DRenderer::DrawPolygon(int line, int polygonIndex) {
    Polygon *polygon = &m_core->gpu3D.GetPolygons()[polygonIndex];
    Vertex   *vertices[10] = {};
    for (int i = 0; i < polygon->size; i++)
        vertices[i] = &polygon->vertices[i];
    if (polygon->crossed)
        SWAP(vertices[2], vertices[3]);
    int start = 0;
    for (int i = 0; i < polygon->size; i++) {
        if (vertices[start]->y > vertices[i]->y)
            start = i;
    }
    int v[4] = {start, (start + 1) % polygon->size, start, (start - 1 + polygon->size) % polygon->size};
    while (vertices[v[1]]->y <= line) {
        v[0] = v[1];
        v[1] = (v[1] + 1) % polygon->size;
        if (v[0] == start)
            break;
    }
    while (vertices[v[3]]->y <= line) {
        v[2] = v[3];
        v[3] = (v[3] - 1 + polygon->size) % polygon->size;
        if (v[2] == start)
            break;
    }
    if (polygon->clockwise) {
        SWAP(v[0], v[2]);
        SWAP(v[1], v[3]);
    }
    if (vertices[v[0]]->y > vertices[v[1]]->y)
        SWAP(v[0], v[1]);
    if (vertices[v[2]]->y > vertices[v[3]]->y)
        SWAP(v[2], v[3]);
    uint32_t x1, x2, x3, x4;
    uint32_t x1a = 0x3F, x2a = 0x3F, x3a = 0x3F, x4a = 0x3F;
    if (vertices[v[0]]->y == vertices[v[1]]->y) {
        x1 = static_cast<uint32_t>(vertices[v[0]]->x);
        x2 = static_cast<uint32_t>(vertices[v[1]]->x);
        if (vertices[v[0]]->x > vertices[v[1]]->x) {
            SWAP(v[0], v[1]);
            SWAP(x1, x2);
        }
    } else if (abs(vertices[v[1]]->x - vertices[v[0]]->x) > vertices[v[1]]->y - vertices[v[0]]->y) {
        x1 = InterpolateLinear(static_cast<uint32_t>(vertices[v[0]]->x << 1), static_cast<uint32_t>(vertices[v[1]]->x << 1), static_cast<uint32_t>(vertices[v[0]]->y), static_cast<uint32_t>(line),
                               static_cast<uint32_t>(vertices[v[1]]->y)) +
             1;
        x2 = InterpolateLinear(static_cast<uint32_t>(vertices[v[0]]->x << 1), static_cast<uint32_t>(vertices[v[1]]->x << 1), static_cast<uint32_t>(vertices[v[0]]->y), static_cast<uint32_t>(line + 1),
                               static_cast<uint32_t>(vertices[v[1]]->y)) +
             1;
        bool negative = (vertices[v[0]]->x > vertices[v[1]]->x);
        if (negative) {
            SWAP(v[0], v[1]);
            SWAP(x1, x2);
        }
        if (m_disp3DCnt & BIT(4)) {
            x1a = InterpolateLinear(static_cast<uint32_t>(vertices[v[0]]->y << 6), static_cast<uint32_t>(vertices[v[1]]->y << 6), static_cast<uint32_t>(vertices[v[0]]->x << 1), x1,
                                    static_cast<uint32_t>(vertices[v[1]]->x << 1)) &
                  0x3F;
            x2a = InterpolateLinear(static_cast<uint32_t>(vertices[v[0]]->y << 6), static_cast<uint32_t>(vertices[v[1]]->y << 6), static_cast<uint32_t>(vertices[v[0]]->x << 1), x2 - 2,
                                    static_cast<uint32_t>(vertices[v[1]]->x << 1)) &
                  0x3F;
            if (negative) {
                x1a = 0x3F - x1a;
                x2a = 0x3F - x2a;
            }
        }
        x1 >>= 1;
        x2 >>= 1;
    } else {
        if (vertices[v[0]]->x > vertices[v[1]]->x)
            x1 = InterpolateLinRev(static_cast<uint32_t>(vertices[v[0]]->x << 6), static_cast<uint32_t>(vertices[v[1]]->x << 6), static_cast<uint32_t>(vertices[v[0]]->y), static_cast<uint32_t>(line),
                                   static_cast<uint32_t>(vertices[v[1]]->y)) -
                 1;
        else
            x1 = InterpolateLinear(static_cast<uint32_t>(vertices[v[0]]->x << 6), static_cast<uint32_t>(vertices[v[1]]->x << 6), static_cast<uint32_t>(vertices[v[0]]->y), static_cast<uint32_t>(line),
                                   static_cast<uint32_t>(vertices[v[1]]->y));
        if (m_disp3DCnt & BIT(4)) {
            if (abs(vertices[v[1]]->x - vertices[v[0]]->x) == vertices[v[1]]->y - vertices[v[0]]->y)
                x2a = x1a = 0x20;
            else
                x2a = x1a = 0x3F - (x1 & 0x3F);
        }
        x2 = x1 >>= 6;
    }
    if (vertices[v[2]]->y == vertices[v[3]]->y) {
        x3 = static_cast<uint32_t>(vertices[v[2]]->x);
        x4 = static_cast<uint32_t>(vertices[v[3]]->x);
        if (vertices[v[2]]->x > vertices[v[3]]->x) {
            SWAP(v[2], v[3]);
            SWAP(x3, x4);
        }
    } else if (vertices[v[2]]->x == vertices[v[3]]->x) {
        x3 = static_cast<uint32_t>(vertices[v[2]]->x);
        if ((vertices[v[0]]->x != vertices[v[1]]->x || vertices[v[0]]->x != vertices[v[2]]->x) && x3 > 0)
            x3--;
        x4 = x3;
    } else if (abs(vertices[v[3]]->x - vertices[v[2]]->x) > vertices[v[3]]->y - vertices[v[2]]->y) {
        x3 = InterpolateLinear(static_cast<uint32_t>(vertices[v[2]]->x << 1), static_cast<uint32_t>(vertices[v[3]]->x << 1), static_cast<uint32_t>(vertices[v[2]]->y), static_cast<uint32_t>(line),
                               static_cast<uint32_t>(vertices[v[3]]->y)) +
             1;
        x4 = InterpolateLinear(static_cast<uint32_t>(vertices[v[2]]->x << 1), static_cast<uint32_t>(vertices[v[3]]->x << 1), static_cast<uint32_t>(vertices[v[2]]->y), static_cast<uint32_t>(line + 1),
                               static_cast<uint32_t>(vertices[v[3]]->y)) +
             1;
        bool negative = (vertices[v[2]]->x > vertices[v[3]]->x);
        if (negative) {
            SWAP(v[2], v[3]);
            SWAP(x3, x4);
        }
        if (m_disp3DCnt & BIT(4)) {
            x3a = InterpolateLinear(static_cast<uint32_t>(vertices[v[2]]->y << 6), static_cast<uint32_t>(vertices[v[3]]->y << 6), static_cast<uint32_t>(vertices[v[2]]->x << 1), x3,
                                    static_cast<uint32_t>(vertices[v[3]]->x << 1)) &
                  0x3F;
            x4a = InterpolateLinear(static_cast<uint32_t>(vertices[v[2]]->y << 6), static_cast<uint32_t>(vertices[v[3]]->y << 6), static_cast<uint32_t>(vertices[v[2]]->x << 1), x4 - 2,
                                    static_cast<uint32_t>(vertices[v[3]]->x << 1)) &
                  0x3F;
            if (!negative) {
                x3a = 0x3F - x3a;
                x4a = 0x3F - x4a;
            }
        }
        x3 >>= 1;
        x4 >>= 1;
    } else {
        if (vertices[v[2]]->x > vertices[v[3]]->x)
            x3 = InterpolateLinRev(static_cast<uint32_t>(vertices[v[2]]->x << 6), static_cast<uint32_t>(vertices[v[3]]->x << 6), static_cast<uint32_t>(vertices[v[2]]->y), static_cast<uint32_t>(line),
                                   static_cast<uint32_t>(vertices[v[3]]->y)) -
                 1;
        else
            x3 = InterpolateLinear(static_cast<uint32_t>(vertices[v[2]]->x << 6), static_cast<uint32_t>(vertices[v[3]]->x << 6), static_cast<uint32_t>(vertices[v[2]]->y), static_cast<uint32_t>(line),
                                   static_cast<uint32_t>(vertices[v[3]]->y));
        if (m_disp3DCnt & BIT(4)) {
            if (abs(vertices[v[3]]->x - vertices[v[2]]->x) == vertices[v[3]]->y - vertices[v[2]]->y)
                x4a = x3a = 0x20;
            else
                x4a = x3a = x3 & 0x3F;
        }
        x4 = x3 >>= 6;
    }
    if (x2 > x1)
        x2--;
    if (x4 > x3)
        x4--;
    if (x2 > x3) {
        x2 = x3;
    }
    if (x1 > x2) {
        x2  = x1;
        x1a = 0x3F - x1a;
        x2a = 0x3F - x2a;
    }
    if (x2 > x3) {
        x3 = x2;
    }
    if (x3 > x4) {
        x3  = x4;
        x3a = 0x3F - x3a;
        x4a = 0x3F - x4a;
    }
    if (x1 > x4) {
        SWAP(v[0], v[2]);
        SWAP(v[1], v[3]);
        SWAP(x1, x3);
        SWAP(x2, x4);
        SWAP(x1a, x3a);
        SWAP(x2a, x4a);
    }
    uint32_t xe1[2], xe[2], xe2[2];
    bool     hideLeft, hideRight;
    if (abs(vertices[v[1]]->x - vertices[v[0]]->x) > abs(vertices[v[1]]->y - vertices[v[0]]->y)) {
        xe1[0]   = static_cast<uint32_t>(vertices[v[0]]->x);
        xe[0]    = x1;
        xe2[0]   = static_cast<uint32_t>(vertices[v[1]]->x);
        hideLeft = (vertices[v[0]]->y < vertices[v[1]]->y);
    } else {
        xe1[0]   = static_cast<uint32_t>(vertices[v[0]]->y);
        xe[0]    = static_cast<uint32_t>(line);
        xe2[0]   = static_cast<uint32_t>(vertices[v[1]]->y);
        hideLeft = false;
    }
    if (abs(vertices[v[3]]->x - vertices[v[2]]->x) > abs(vertices[v[3]]->y - vertices[v[2]]->y)) {
        xe1[1]    = static_cast<uint32_t>(vertices[v[2]]->x);
        xe[1]     = x4;
        xe2[1]    = static_cast<uint32_t>(vertices[v[3]]->x);
        hideRight = (vertices[v[2]]->y > vertices[v[3]]->y);
    } else {
        xe1[1]    = static_cast<uint32_t>(vertices[v[2]]->y);
        xe[1]     = static_cast<uint32_t>(line);
        xe2[1]    = static_cast<uint32_t>(vertices[v[3]]->y);
        hideRight = (vertices[v[2]]->x != vertices[v[3]]->x);
    }
    uint32_t ws[4];
    if (polygon->wShift >= 0) {
        for (int i = 0; i < 4; i++)
            ws[i] = static_cast<uint32_t>(vertices[v[i]]->w >> polygon->wShift);
    } else {
        for (int i = 0; i < 4; i++)
            ws[i] = static_cast<uint32_t>(vertices[v[i]]->w << -polygon->wShift);
    }
    uint32_t ze[2], we[2];
    uint32_t re[2], ge[2], be[2];
    uint32_t se[2], te[2];
    for (int i = 0; i < 2; i++) {
        int i2 = i * 2;
        ze[i]  = InterpolateLinear(static_cast<uint32_t>(vertices[v[i2]]->z), static_cast<uint32_t>(vertices[v[i2 + 1]]->z), xe1[i], xe[i], xe2[i]);
        if (ws[i2] == ws[i2 + 1] && !(ws[i2] & 0x00FE)) {
            we[i] = InterpolateLinear(ws[i2], ws[i2 + 1], xe1[i], xe[i], xe2[i]);
            re[i] = InterpolateLinear(((vertices[v[i2]]->color >> 0) & 0x3F) << 3,
                                      ((vertices[v[i2 + 1]]->color >> 0) & 0x3F) << 3, xe1[i], xe[i], xe2[i]);
            ge[i] = InterpolateLinear(((vertices[v[i2]]->color >> 6) & 0x3F) << 3,
                                      ((vertices[v[i2 + 1]]->color >> 6) & 0x3F) << 3, xe1[i], xe[i], xe2[i]);
            be[i] = InterpolateLinear(((vertices[v[i2]]->color >> 12) & 0x3F) << 3,
                                      ((vertices[v[i2 + 1]]->color >> 12) & 0x3F) << 3, xe1[i], xe[i], xe2[i]);
            se[i] = InterpolateLinear(static_cast<uint32_t>((int32_t)vertices[v[i2]]->s + 0xFFFF), static_cast<uint32_t>((int32_t)vertices[v[i2 + 1]]->s + 0xFFFF),
                                      xe1[i], xe[i], xe2[i]) -
                    0xFFFF;
            te[i] = InterpolateLinear(static_cast<uint32_t>((int32_t)vertices[v[i2]]->t + 0xFFFF), static_cast<uint32_t>((int32_t)vertices[v[i2 + 1]]->t + 0xFFFF),
                                      xe1[i], xe[i], xe2[i]) -
                    0xFFFF;
        } else {
            uint32_t factor;
            if (xe[i] <= xe1[i]) {
                factor = 0;
            } else if (xe[i] >= xe2[i]) {
                factor = (1 << 9);
            } else {
                uint32_t wa = (ws[i2] >> 1) + ((ws[i2] & 1) && !(ws[i2 + 1] & 1));
                uint32_t s  = (m_resShift & ((xe[i] - xe1[i]) >> 8));
                factor      = ((((ws[i2] >> 1) * (xe[i] - xe1[i])) << (9 - s)) /
                          ((ws[i2 + 1] >> 1) * (xe2[i] - xe[i]) + wa * (xe[i] - xe1[i])))
                         << s;
            }
            we[i] = InterpolateFactor(factor, 9, ws[i2], ws[i2 + 1]);
            re[i] = InterpolateFactor(factor, 9, ((vertices[v[i2]]->color >> 0) & 0x3F) << 3,
                                      ((vertices[v[i2 + 1]]->color >> 0) & 0x3F) << 3);
            ge[i] = InterpolateFactor(factor, 9, ((vertices[v[i2]]->color >> 6) & 0x3F) << 3,
                                      ((vertices[v[i2 + 1]]->color >> 6) & 0x3F) << 3);
            be[i] = InterpolateFactor(factor, 9, ((vertices[v[i2]]->color >> 12) & 0x3F) << 3,
                                      ((vertices[v[i2 + 1]]->color >> 12) & 0x3F) << 3);
            se[i] = InterpolateFactor(factor, 9, static_cast<uint32_t>((int32_t)vertices[v[i2]]->s + 0xFFFF),
                                      static_cast<uint32_t>((int32_t)vertices[v[i2 + 1]]->s + 0xFFFF)) -
                    0xFFFF;
            te[i] = InterpolateFactor(factor, 9, static_cast<uint32_t>((int32_t)vertices[v[i2]]->t + 0xFFFF),
                                      static_cast<uint32_t>((int32_t)vertices[v[i2 + 1]]->t + 0xFFFF)) -
                    0xFFFF;
        }
    }
    if (polygon->mode == 3 && polygon->id == 0) {
        if (!m_stencilClear[line]) {
            memset(&m_stencilBuffer[line * 256 * 2], 0, 256 << m_resShift);
            m_stencilClear[line] = true;
        }
    } else {
        m_stencilClear[line] = false;
    }
    uint32_t x1e = x1, x4e = ++x4;
    if (polygon->alpha != 0 && !(m_disp3DCnt & (BIT(4) | BIT(5)))) {
        if (hideLeft)
            x1e = x2 + 1;
        if (hideRight)
            x4e = x3;
        if (!(hideLeft && hideRight) && x4e <= x1e)
            x4e = x1e + 1;
    }
    bool     horizontal = (line == m_polygonTop[polygonIndex] || line == m_polygonBot[polygonIndex] - 1);
    int      lastS = 0xFFFF, lastT = 0xFFFF;
    uint32_t texel = 0;
    for (uint32_t x = x1; x < x4; x++) {
        if (!horizontal && polygon->alpha == 0 && x == x2 + 1 && x3 > x2)
            x = x3;
        if (x >= uint32_t(256 << m_resShift))
            break;
        bool     layer = 0;
        int      i     = static_cast<int>(static_cast<uint32_t>(line * 256 * 2) + x);
        uint32_t factor;
        if (we[0] == we[1] && !(we[0] & 0x007F)) {
            factor = UINT32_MAX;
        } else if (x <= x1) {
            factor = 0;
        } else if (x >= x4) {
            factor = (1 << 8);
        } else {
            uint32_t s = (m_resShift & ((x - x1) >> 8));
            factor     = (((we[0] * (x - x1)) << (8 - s)) / (we[1] * (x4 - x) + we[0] * (x - x1))) << s;
        }
        int32_t depth;
        if (polygon->wBuffer) {
            depth = static_cast<int32_t>((factor == uint32_t(-1)) ? InterpolateLinear(we[0], we[1], x1, x, x4)
                                   : InterpolateFactor(factor, 8, we[0], we[1]));
            if (polygon->wShift > 0)
                depth <<= polygon->wShift;
            else if (polygon->wShift < 0)
                depth >>= -polygon->wShift;
        } else {
            depth = static_cast<int32_t>(InterpolateLinear(ze[0], ze[1], x1, x, x4));
        }
        bool depthPass[2];
        if (polygon->depthTestEqual) {
            uint32_t margin = (polygon->wBuffer ? 0xFF : 0x200);
            depthPass[0]    = (uint32_t(m_depthBuffer[0][i]) >= static_cast<uint32_t>(depth) - margin && uint32_t(m_depthBuffer[0][i]) <= static_cast<uint32_t>(depth) + margin);
            depthPass[1]    = (m_disp3DCnt & BIT(4)) && (m_attribBuffer[0][i] & BIT(14)) &&
                           (uint32_t(m_depthBuffer[1][i]) >= static_cast<uint32_t>(depth) - margin && uint32_t(m_depthBuffer[1][i]) <= static_cast<uint32_t>(depth) + margin);
        } else {
            depthPass[0] = (m_depthBuffer[0][i] > depth);
            depthPass[1] = (m_disp3DCnt & BIT(4)) && (m_attribBuffer[0][i] & BIT(14)) && (m_depthBuffer[1][i] > depth);
        }
        if (polygon->mode == 3) {
            if (polygon->id == 0) {
                if (!depthPass[0])
                    m_stencilBuffer[i] |= BIT(0);
                if (!depthPass[1])
                    m_stencilBuffer[i] |= BIT(1);
                continue;
            } else if (!depthPass[0] || !(m_stencilBuffer[i] & BIT(0)) || (m_attribBuffer[0][i] & 0x3F) == uint32_t(polygon->id)) {
                if (!depthPass[1] || !(m_stencilBuffer[i] & BIT(1)) || (m_attribBuffer[1][i] & 0x3F) == uint32_t(polygon->id))
                    continue;
                layer = 1;
            }
        } else if (!depthPass[0]) {
            if (!depthPass[1])
                continue;
            layer = 1;
        }
        uint32_t rv, gv, bv;
        if (factor == uint32_t(-1)) {
            rv = InterpolateLinear(re[0], re[1], x1, x, x4) >> 3;
            gv = InterpolateLinear(ge[0], ge[1], x1, x, x4) >> 3;
            bv = InterpolateLinear(be[0], be[1], x1, x, x4) >> 3;
        } else {
            rv = InterpolateFactor(factor, 8, re[0], re[1]) >> 3;
            gv = InterpolateFactor(factor, 8, ge[0], ge[1]) >> 3;
            bv = InterpolateFactor(factor, 8, be[0], be[1]) >> 3;
        }
        uint32_t color = ((polygon->alpha ? polygon->alpha : 0x3F) << 18) | (bv << 12) | (gv << 6) | rv;
        if (polygon->textureFmt != 0) {
            int s, t;
            if (factor == uint32_t(-1)) {
                s = (int)(InterpolateLinear(se[0] + 0xFFFF, se[1] + 0xFFFF, x1, x, x4) - 0xFFFF) >> 4;
                t = (int)(InterpolateLinear(te[0] + 0xFFFF, te[1] + 0xFFFF, x1, x, x4) - 0xFFFF) >> 4;
            } else {
                s = (int)(InterpolateFactor(factor, 8, se[0] + 0xFFFF, se[1] + 0xFFFF) - 0xFFFF) >> 4;
                t = (int)(InterpolateFactor(factor, 8, te[0] + 0xFFFF, te[1] + 0xFFFF) - 0xFFFF) >> 4;
            }
            if (s != lastS || t != lastT) {
                lastS = s;
                lastT = t;
                texel = ReadTexture(polygon, s, t);
            }
            switch (polygon->mode) {
                case 0: {
                    uint8_t r = static_cast<uint8_t>(((((texel >> 0) & 0x3F) + 1) * (((color >> 0) & 0x3F) + 1) - 1) / 64);
                    uint8_t g = static_cast<uint8_t>(((((texel >> 6) & 0x3F) + 1) * (((color >> 6) & 0x3F) + 1) - 1) / 64);
                    uint8_t b = static_cast<uint8_t>(((((texel >> 12) & 0x3F) + 1) * (((color >> 12) & 0x3F) + 1) - 1) / 64);
                    uint8_t a = static_cast<uint8_t>(((((texel >> 18) & 0x3F) + 1) * (((color >> 18) & 0x3F) + 1) - 1) / 64);
                    color     = (a << 18) | (b << 12) | (g << 6) | r;
                    break;
                }
                case 1:
                case 3: {
                    uint8_t at = ((texel >> 18) & 0x3F);
                    uint8_t r  = static_cast<uint8_t>((((texel >> 0) & 0x3F) * at + ((color >> 0) & 0x3F) * (63 - at)) / 64);
                    uint8_t g  = static_cast<uint8_t>((((texel >> 6) & 0x3F) * at + ((color >> 6) & 0x3F) * (63 - at)) / 64);
                    uint8_t b  = static_cast<uint8_t>((((texel >> 12) & 0x3F) * at + ((color >> 12) & 0x3F) * (63 - at)) / 64);
                    uint8_t a  = ((color >> 18) & 0x3F);
                    color      = (a << 18) | (b << 12) | (g << 6) | r;
                    break;
                }
                case 2: {
                    uint32_t toon = Rgba5ToRgba6(m_toonTable[(color & 0x3F) / 2]);
                    uint8_t  r, g, b;
                    if (m_disp3DCnt & BIT(1)) {
                        r = static_cast<uint8_t>(((((texel >> 0) & 0x3F) + 1) * (((color >> 0) & 0x3F) + 1) - 1) / 64);
                        g = static_cast<uint8_t>(((((texel >> 6) & 0x3F) + 1) * (((color >> 6) & 0x3F) + 1) - 1) / 64);
                        b = static_cast<uint8_t>(((((texel >> 12) & 0x3F) + 1) * (((color >> 12) & 0x3F) + 1) - 1) / 64);
                        r += ((toon >> 0) & 0x3F);
                        if (r > 63)
                            r = 63;
                        g += ((toon >> 6) & 0x3F);
                        if (g > 63)
                            g = 63;
                        b += ((toon >> 12) & 0x3F);
                        if (b > 63)
                            b = 63;
                    } else {
                        r = static_cast<uint8_t>(((((texel >> 0) & 0x3F) + 1) * (((toon >> 0) & 0x3F) + 1) - 1) / 64);
                        g = static_cast<uint8_t>(((((texel >> 6) & 0x3F) + 1) * (((toon >> 6) & 0x3F) + 1) - 1) / 64);
                        b = static_cast<uint8_t>(((((texel >> 12) & 0x3F) + 1) * (((toon >> 12) & 0x3F) + 1) - 1) / 64);
                    }
                    uint8_t a = static_cast<uint8_t>(((((texel >> 18) & 0x3F) + 1) * (((color >> 18) & 0x3F) + 1) - 1) / 64);
                    color     = (a << 18) | (b << 12) | (g << 6) | r;
                    break;
                }
            }
        } else if (polygon->mode == 2) {
            uint32_t toon = Rgba5ToRgba6(m_toonTable[(color & 0x3F) / 2]);
            uint8_t  r, g, b;
            if (m_disp3DCnt & BIT(1)) {
                r = ((color >> 0) & 0x3F) + ((toon >> 0) & 0x3F);
                if (r > 63)
                    r = 63;
                g = ((color >> 6) & 0x3F) + ((toon >> 6) & 0x3F);
                if (g > 63)
                    g = 63;
                b = ((color >> 12) & 0x3F) + ((toon >> 12) & 0x3F);
                if (b > 63)
                    b = 63;
            } else {
                r = ((toon >> 0) & 0x3F);
                g = ((toon >> 6) & 0x3F);
                b = ((toon >> 12) & 0x3F);
            }
            color = (color & 0xFC0000) | (b << 12) | (g << 6) | r;
        }
        if (!(color & 0xFC0000) || ((x < x1e || x >= x4e) && ((color >> 18) == 0x3F || !(m_disp3DCnt & BIT(3)))))
            continue;
        if ((color >> 18) == 0x3F) {
            uint8_t edgeAlpha = 0x3F;
            bool    edge      = (x <= x2 || x >= x3 || horizontal);
            if ((m_disp3DCnt & BIT(4)) && layer == 0 && edge) {
                m_framebuffer[1][i]  = m_framebuffer[0][i];
                m_depthBuffer[1][i]  = m_depthBuffer[0][i];
                m_attribBuffer[1][i] = m_attribBuffer[0][i];
                if (x <= x2)
                    edgeAlpha = static_cast<uint8_t>(InterpolateLinear(x1a, x2a, x1, x, x2));
                else if (x >= x3)
                    edgeAlpha = static_cast<uint8_t>(InterpolateLinear(x3a, x4a, x3, x, x4));
            }
            m_framebuffer[layer][i]  = BIT(26) | color;
            m_depthBuffer[layer][i]  = depth;
            m_attribBuffer[layer][i] = (m_attribBuffer[layer][i] & 0x0FC0) | (edgeAlpha << 15) | (edge << 14) |
                                     (polygon->fog << 13) | static_cast<uint32_t>(polygon->id);
        } else if (!(m_attribBuffer[layer][i] & BIT(12)) || ((m_attribBuffer[layer][i] >> 6) & 0x3F) != uint32_t(polygon->id)) {
            m_framebuffer[layer][i] = BIT(26) | (((m_disp3DCnt & BIT(3)) && (m_framebuffer[layer][i] & 0xFC0000))
                                                   ? InterpolateColor(m_framebuffer[layer][i], color, 0, color >> 18, 63)
                                                   : color);
            if (polygon->transNewDepth)
                m_depthBuffer[layer][i] = depth;
            m_attribBuffer[layer][i] =
                (m_attribBuffer[layer][i] & (0x1FC03F | (polygon->fog << 13))) | BIT(12) | static_cast<uint32_t>((polygon->id << 6));
            if ((m_disp3DCnt & BIT(4)) && layer == 0 && (m_attribBuffer[0][i] & BIT(14))) {
                m_framebuffer[1][i] = BIT(26) | (((m_disp3DCnt & BIT(3)) && (m_framebuffer[1][i] & 0xFC0000))
                                                   ? InterpolateColor(m_framebuffer[1][i], color, 0, color >> 18, 63)
                                                   : color);
                if (polygon->transNewDepth)
                    m_depthBuffer[1][i] = depth;
                m_attribBuffer[1][i] =
                    (m_attribBuffer[1][i] & (0x1FC03F | (polygon->fog << 13))) | BIT(12) | static_cast<uint32_t>((polygon->id << 6));
            }
        }
    }
}

void Gpu3DRenderer::WriteDisp3DCnt(uint16_t mask, uint16_t value) {
    if (value & BIT(12))
        m_disp3DCnt = static_cast<uint16_t>(m_disp3DCnt & ~BIT(12));
    if (value & BIT(13))
        m_disp3DCnt = static_cast<uint16_t>(m_disp3DCnt & ~BIT(13));
    mask &= 0x4FFF;
    if ((value & mask) == (m_disp3DCnt & mask))
        return;
    m_disp3DCnt = (m_disp3DCnt & ~mask) | (value & mask);
    m_core->gpu.Invalidate3D();
}

void Gpu3DRenderer::WriteEdgeColor(int index, uint16_t mask, uint16_t value) {
    mask &= 0x7FFF;
    if ((value & mask) == (m_edgeColor[index] & mask))
        return;
    m_edgeColor[index] = (m_edgeColor[index] & ~mask) | (value & mask);
    m_core->gpu.Invalidate3D();
}

void Gpu3DRenderer::WriteClearColor(uint32_t mask, uint32_t value) {
    mask &= 0x3F1FFFFF;
    if ((value & mask) == (m_clearColor & mask))
        return;
    m_clearColor = (m_clearColor & ~mask) | (value & mask);
    m_core->gpu.Invalidate3D();
}

void Gpu3DRenderer::WriteClearDepth(uint16_t mask, uint16_t value) {
    mask &= 0x7FFF;
    if ((value & mask) == (m_clearDepth & mask))
        return;
    m_clearDepth = (m_clearDepth & ~mask) | (value & mask);
    m_core->gpu.Invalidate3D();
}

void Gpu3DRenderer::WriteToonTable(int index, uint16_t mask, uint16_t value) {
    mask &= 0x7FFF;
    if ((value & mask) == (m_toonTable[index] & mask))
        return;
    m_toonTable[index] = (m_toonTable[index] & ~mask) | (value & mask);
    m_core->gpu.Invalidate3D();
}

void Gpu3DRenderer::WriteFogColor(uint32_t mask, uint32_t value) {
    mask &= 0x001F7FFF;
    if ((value & mask) == (m_fogColor & mask))
        return;
    m_fogColor = (m_fogColor & ~mask) | (value & mask);
    m_core->gpu.Invalidate3D();
}

void Gpu3DRenderer::WriteFogOffset(uint16_t mask, uint16_t value) {
    mask &= 0x7FFF;
    if ((value & mask) == (m_fogOffset & mask))
        return;
    m_fogOffset = (m_fogOffset & ~mask) | (value & mask);
    m_core->gpu.Invalidate3D();
}

void Gpu3DRenderer::WriteFogTable(int index, uint8_t value) {
    if ((value & 0x7F) == (m_fogTable[index] & 0x7F))
        return;
    m_fogTable[index] = value & 0x7F;
    m_core->gpu.Invalidate3D();
}

} // namespace consoles::arm

