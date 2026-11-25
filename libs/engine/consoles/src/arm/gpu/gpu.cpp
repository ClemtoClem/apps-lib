#include <cstring>
#include "consoles/arm/gpu/gpu.hpp"
#include "consoles/arm/core.hpp"
#include "consoles/arm/settings.hpp"

namespace consoles::arm {


Gpu::Gpu(Core *core_) : m_core(core_) {
    m_ready.store(false);
    m_drawing.store(0);
    m_gbaScanline240Task = std::bind(&Gpu::GbaScanline240, this);
    m_gbaScanline308Task = std::bind(&Gpu::GbaScanline308, this);
    m_scanline256Task    = std::bind(&Gpu::Scanline256, this);
    m_scanline355Task    = std::bind(&Gpu::Scanline355, this);
}

Gpu::~Gpu() {
    if (m_thread) {
        m_running = false;
        m_thread->join();
    }
}

void Gpu::ScheduleInit() {
    m_core->Schedule(Task(&m_scanline256Task, 256 * 6));
    m_core->Schedule(Task(&m_scanline355Task, 355 * 6));
}

void Gpu::GbaScheduleInit() {
    m_core->Schedule(Task(&m_gbaScanline240Task, 240 * 4));
    m_core->Schedule(Task(&m_gbaScanline308Task, 308 * 4));
}

uint32_t Gpu::Rgb5ToRgb8(uint32_t color) {
    uint8_t r = static_cast<uint8_t>((((color >> 0) & 0x1F) << 1) * 255 / 63);
    uint8_t g = static_cast<uint8_t>((((color >> 5) & 0x1F) << 1) * 255 / 63);
    uint8_t b = static_cast<uint8_t>((((color >> 10) & 0x1F) << 1) * 255 / 63);
    return (0xFF << 24) | (b << 16) | (g << 8) | r;
}

uint32_t Gpu::Rgb6ToRgb8(uint32_t color) {
    uint8_t r = static_cast<uint8_t>(((color >> 0) & 0x3F) * 255 / 63);
    uint8_t g = static_cast<uint8_t>(((color >> 6) & 0x3F) * 255 / 63);
    uint8_t b = static_cast<uint8_t>(((color >> 12) & 0x3F) * 255 / 63);
    return (0xFF << 24) | (b << 16) | (g << 8) | r;
}

uint16_t Gpu::Rgb6ToRgb5(uint32_t color) {
    uint8_t r = ((color >> 0) & 0x3F) / 2;
    uint8_t g = ((color >> 6) & 0x3F) / 2;
    uint8_t b = ((color >> 12) & 0x3F) / 2;
    return BIT(15) | (b << 10) | (g << 5) | r;
}

bool Gpu::GetFrame(uint32_t *out, bool gbaCrop) {
    if (!m_ready.load())
        return false;
    Buffers &buffers = m_framebuffers.front();
    if (gbaCrop) {
        if (m_core->settings.highRes3D) {
            for (size_t y = 0; y < 160; y++) {
                for (size_t x = 0; x < 240; x++) {
                    uint32_t color = Rgb5ToRgb8(buffers.framebuffer[y * 256 + x]);
                    size_t      i     = (y * 2) * (240 * 2) + (x * 2);
                    out[i + 0]     = color;
                    out[i + 1]     = color;
                    out[i + 480]   = color;
                    out[i + 481]   = color;
                }
            }
        } else {
            for (size_t y = 0; y < 160; y++)
                for (size_t x = 0; x < 240; x++)
                    out[y * 240 + x] = Rgb5ToRgb8(buffers.framebuffer[y * 256 + x]);
        }
    } else if (m_core->IsGbaMode()) {
        size_t      offset = (m_powCnt1 & BIT(15)) ? 0 : (256 * 192);
        uint32_t base   = static_cast<uint32_t>(0x6800000 + m_gbaBlock * 0x20000);
        m_gbaBlock        = !m_gbaBlock;
        if (m_core->settings.highRes3D) {
            for (size_t y = 0; y < 192; y++) {
                for (size_t x = 0; x < 256; x++) {
                    uint32_t color = Rgb5ToRgb8((x >= 8 && x < 256 - 8 && y >= 16 && y < 192 - 16)
                                                    ? buffers.framebuffer[(y - 16) * 256 + (x - 8)]
                                                    : m_core->memory.Read<uint16_t>(0, base + static_cast<uint32_t>((y * 256 + x) * 2)));
                    size_t      i     = (offset * 4) + (y * 2) * (256 * 2) + (x * 2);
                    out[i + 0]     = color;
                    out[i + 1]     = color;
                    out[i + 512]   = color;
                    out[i + 513]   = color;
                }
            }
            memset(&out[(256 * 192 - offset) * 4], 0, 256 * 192 * 4 * sizeof(uint32_t));
        } else {
            for (size_t y = 0; y < 192; y++) {
                for (size_t x = 0; x < 256; x++) {
                    out[offset + y * 256 + x] =
                        Rgb5ToRgb8((x >= 8 && x < 256 - 8 && y >= 16 && y < 192 - 16)
                                       ? buffers.framebuffer[(y - 16) * 256 + (x - 8)]
                                       : m_core->memory.Read<uint16_t>(0, base + static_cast<uint32_t>((y * 256 + x) * 2)));
                }
            }
            memset(&out[256 * 192 - offset], 0, 256 * 192 * sizeof(uint32_t));
        }
    } else {
        if (m_core->settings.highRes3D) {
            if (!buffers.hiRes3D.empty()) {
                for (size_t y = 0; y < 192 * 2; y++) {
                    for (size_t x = 0; x < 256; x++) {
                        uint32_t value = buffers.framebuffer[y * 256 + x];
                        size_t      i     = (y * 2) * (256 * 2) + (x * 2);
                        if (value & BIT(26)) {
                            uint32_t value2 = buffers.hiRes3D[(i + 0) % (256 * 192 * 4)];
                            out[i + 0]      = Rgb6ToRgb8((value2 & 0xFC0000) ? value2 : value);
                            value2          = buffers.hiRes3D[(i + 1) % (256 * 192 * 4)];
                            out[i + 1]      = Rgb6ToRgb8((value2 & 0xFC0000) ? value2 : value);
                            value2          = buffers.hiRes3D[(i + 512) % (256 * 192 * 4)];
                            out[i + 512]    = Rgb6ToRgb8((value2 & 0xFC0000) ? value2 : value);
                            value2          = buffers.hiRes3D[(i + 513) % (256 * 192 * 4)];
                            out[i + 513]    = Rgb6ToRgb8((value2 & 0xFC0000) ? value2 : value);
                        } else {
                            uint32_t color = Rgb6ToRgb8(value);
                            out[i + 0]     = color;
                            out[i + 1]     = color;
                            out[i + 512]   = color;
                            out[i + 513]   = color;
                        }
                    }
                }
            } else {
                for (size_t y = 0; y < 192 * 2; y++) {
                    for (size_t x = 0; x < 256; x++) {
                        uint32_t color = Rgb6ToRgb8(buffers.framebuffer[y * 256 + x]);
                        size_t      i     = (y * 2) * (256 * 2) + (x * 2);
                        out[i + 0]     = color;
                        out[i + 1]     = color;
                        out[i + 512]   = color;
                        out[i + 513]   = color;
                    }
                }
            }
        } else {
            for (size_t i = 0; i < 256 * 192 * 2; i++)
                out[i] = Rgb6ToRgb8(buffers.framebuffer[i]);
        }
    }
    m_mutex.lock();
    m_framebuffers.pop();
    m_ready.store(!m_framebuffers.empty());
    m_mutex.unlock();
    return true;
}

void Gpu::GbaScanline240() {
    if (m_vCount < 160) {
        if (m_thread) {
            while (m_drawing.load() != 0)
                std::this_thread::yield();
        } else {
            m_core->gpu2D[0].DrawGbaScanline(m_vCount);
        }
        m_core->dma[1].Trigger(2);
    }
    m_dispStat[1] |= BIT(1);
    if (m_dispStat[1] & BIT(4))
        m_core->interpreter[1].SendInterrupt(1);
    m_core->Schedule(Task(&m_gbaScanline240Task, 308 * 4));
}

void Gpu::GbaScanline308() {
    m_dispStat[1] = static_cast<uint16_t>(m_dispStat[1] & ~BIT(1));
    switch (++m_vCount) {
        case 160: {
            if (m_thread) {
                m_running = false;
                m_thread->join();
                m_thread.reset();
            }
            m_dispStat[1] |= BIT(0);
            if (m_dispStat[1] & BIT(3))
                m_core->interpreter[1].SendInterrupt(0);
            m_core->dma[1].Trigger(1);
            if (m_framebuffers.size() < 2) {
                Buffers buffers;
                buffers.framebuffer.assign(m_core->gpu2D[0].GetFramebuffer(), m_core->gpu2D[0].GetFramebuffer() + 256 * 160);
                m_mutex.lock();
                m_framebuffers.push(std::move(buffers));
                m_ready.store(true);
                m_mutex.unlock();
            }
            break;
        }
        case 227: {
            m_dispStat[1] = static_cast<uint16_t>(m_dispStat[1] & ~BIT(0));
            m_core->EndFrame();
            break;
        }
        case 228: {
            m_vCount = 0;
            if (m_core->settings.threaded2D && !m_thread) {
                m_running = true;
                m_thread  = std::make_unique<std::thread>(&Gpu::DrawGbaThreaded, this);
                uint64_t id = std::hash<std::thread::id>{}(m_thread->get_id());
                LOG("Create thread to run virtual GPU (id:%lu)", id);
            }
            break;
        }
    }
    if (m_vCount < 160 && m_thread)
        m_drawing.store(1);
    if (m_vCount == (m_dispStat[1] >> 8)) {
        m_dispStat[1] |= BIT(2);
        if (m_dispStat[1] & BIT(5))
            m_core->interpreter[1].SendInterrupt(2);
    } else if (m_dispStat[1] & BIT(2)) {
        m_dispStat[1] = static_cast<uint16_t>(m_dispStat[1] & ~BIT(2));
    }
    m_core->Schedule(Task(&m_gbaScanline308Task, 308 * 4));
}

void Gpu::Scanline256() {
    if (m_vCount < 192) {
        if (m_thread) {
            while (m_drawing.load() == 1)
                std::this_thread::yield();
            switch (m_drawing.exchange(3)) {
                case 2:
                    m_core->gpu2D[1].DrawScanline(m_vCount);
                case 3:
                    while (m_drawing.load() != 0)
                        std::this_thread::yield();
                    break;
            }
        } else {
            m_core->gpu2D[0].DrawScanline(m_vCount);
            m_core->gpu2D[1].DrawScanline(m_vCount);
        }
        m_core->dma[0].Trigger(2);
        if (m_vCount == 0 && (m_dispCapCnt & BIT(31)))
            m_displayCapture = true;
        if (m_displayCapture) {
            int width, height;
            switch ((m_dispCapCnt & 0x00300000) >> 20) {
                case 0:
                    width  = 128;
                    height = 128;
                    break;
                case 1:
                    width  = 256;
                    height = 64;
                    break;
                case 2:
                    width  = 256;
                    height = 128;
                    break;
                case 3:
                    width  = 256;
                    height = 192;
                    break;
            }
            uint32_t base        = 0x6800000 + ((m_dispCapCnt & 0x00030000) >> 16) * 0x20000;
            uint32_t writeOffset = ((m_dispCapCnt & 0x000C0000) >> 3) + static_cast<uint32_t>(m_vCount * width * 2);
            switch ((m_dispCapCnt & 0x60000000) >> 29) {
                case 0: {
                    uint32_t *source =
                        (m_dispCapCnt & BIT(24)) ? m_core->gpu3DRenderer.GetLine(m_vCount) : m_core->gpu2D[0].GetRawLine();
                    bool resShift = (m_core->settings.highRes3D && (m_dispCapCnt & BIT(24)));
                    for (int i = 0; i < width; i++)
                        m_core->memory.Write<uint16_t>(0, base + ((writeOffset + static_cast<uint32_t>(i * 2)) & 0x1FFFF),
                                                     Rgb6ToRgb5(source[i << resShift]));
                    break;
                }
                case 1: {
                    if (m_dispCapCnt & BIT(25)) {
                        LOG("Unimplemented display capture source: display FIFO\n");
                        break;
                    }
                    uint32_t readOffset = ((m_dispCapCnt & 0x0C000000) >> 11) + static_cast<uint32_t>(m_vCount * width * 2);
                    for (int i = 0; i < width; i++) {
                        uint16_t color = m_core->memory.Read<uint16_t>(0, base + ((readOffset + static_cast<uint32_t>(i * 2)) & 0x1FFFF));
                        m_core->memory.Write<uint16_t>(0, base + ((writeOffset + static_cast<uint32_t>(i * 2)) & 0x1FFFF), color);
                    }
                    break;
                }
                default: {
                    if (m_dispCapCnt & BIT(25)) {
                        LOG("Unimplemented display capture source: display FIFO\n");
                        break;
                    }
                    uint32_t *source =
                        (m_dispCapCnt & BIT(24)) ? m_core->gpu3DRenderer.GetLine(m_vCount) : m_core->gpu2D[0].GetRawLine();
                    bool     resShift   = (m_core->settings.highRes3D && (m_dispCapCnt & BIT(24)));
                    uint32_t readOffset = ((m_dispCapCnt & 0x0C000000) >> 11) + static_cast<uint32_t>(m_vCount * width * 2);
                    uint8_t  eva        = static_cast<uint8_t>(std::min((m_dispCapCnt >> 0) & 0x1F, 16U));
                    uint8_t  evb        = static_cast<uint8_t>(std::min((m_dispCapCnt >> 8) & 0x1F, 16U));
                    for (int i = 0; i < width; i++) {
                        uint16_t c1    = Rgb6ToRgb5(source[i << resShift]);
                        uint16_t c2    = m_core->memory.Read<uint16_t>(0, base + ((readOffset + static_cast<uint32_t>(i * 2)) & 0x1FFFF));
                        uint8_t  r     = static_cast<uint8_t>(std::min((((c1 >> 0) & 0x1F) * eva + ((c2 >> 0) & 0x1F) * evb) / 16, 31));
                        uint8_t  g     = static_cast<uint8_t>(std::min((((c1 >> 5) & 0x1F) * eva + ((c2 >> 5) & 0x1F) * evb) / 16, 31));
                        uint8_t  b     = static_cast<uint8_t>(std::min((((c1 >> 10) & 0x1F) * eva + ((c2 >> 10) & 0x1F) * evb) / 16, 31));
                        uint16_t color = BIT(15) | (b << 10) | (g << 5) | r;
                        m_core->memory.Write<uint16_t>(0, base + ((writeOffset + static_cast<uint32_t>(i * 2)) & 0x1FFFF), color);
                    }
                    break;
                }
            }
            if (m_vCount + 1 == height) {
                m_displayCapture = false;
                m_dispCapCnt &= ~BIT(31);
            }
        }
    }
    if (m_dirty3D && (m_core->gpu2D[0].ReadDispCnt() & BIT(3)) && ((m_vCount + 48) % 263) < 192) {
        if (m_vCount == 215)
            m_dirty3D = BIT(1);
        m_core->gpu3DRenderer.DrawScanline((m_vCount + 48) % 263);
        if (m_vCount == 143)
            m_dirty3D = static_cast<uint8_t>(m_dirty3D & ~BIT(1));
    }
    for (int i = 0; i < 2; i++) {
        m_dispStat[i] |= BIT(1);
        if (m_dispStat[i] & BIT(4))
            m_core->interpreter[i].SendInterrupt(1);
    }
    m_core->Schedule(Task(&m_scanline256Task, 355 * 6));
}

void Gpu::Scanline355() {
    switch (++m_vCount) {
        case 192: {
            if (m_thread) {
                m_running = false;
                m_thread->join();
                m_thread.reset();
            }
            for (int i = 0; i < 2; i++) {
                m_dispStat[i] |= BIT(0);
                if (m_dispStat[i] & BIT(3))
                    m_core->interpreter[i].SendInterrupt(0);
                m_core->dma[i].Trigger(1);
            }
            if (m_core->gpu3D.ShouldSwap())
                m_core->gpu3D.SwapBuffers();
            if (m_framebuffers.size() < 2) {
                Buffers buffers;
                buffers.framebuffer.assign(256 * 192 * 2, 0);
                if (m_powCnt1 & BIT(0)) {
                    if (m_powCnt1 & BIT(15)) {
                        memcpy(&buffers.framebuffer[0], m_core->gpu2D[0].GetFramebuffer(), 256 * 192 * sizeof(uint32_t));
                        memcpy(&buffers.framebuffer[256 * 192], m_core->gpu2D[1].GetFramebuffer(),
                               256 * 192 * sizeof(uint32_t));
                    } else {
                        memcpy(&buffers.framebuffer[0], m_core->gpu2D[1].GetFramebuffer(), 256 * 192 * sizeof(uint32_t));
                        memcpy(&buffers.framebuffer[256 * 192], m_core->gpu2D[0].GetFramebuffer(),
                               256 * 192 * sizeof(uint32_t));
                    }
                }
                if (m_core->settings.highRes3D && (m_core->gpu2D[0].ReadDispCnt() & BIT(3))) {
                    buffers.hiRes3D.assign(m_core->gpu3DRenderer.GetLine(0), m_core->gpu3DRenderer.GetLine(0) + 256 * 192 * 4);
                    buffers.top3D = (m_powCnt1 & BIT(15));
                }
                m_mutex.lock();
                m_framebuffers.push(std::move(buffers));
                m_ready.store(true);
                m_mutex.unlock();
            }
            break;
        }
        case 262: {
            for (int i = 0; i < 2; i++)
                m_dispStat[i] = static_cast<uint16_t>(m_dispStat[i] & ~BIT(0));
            m_core->actionReplay.ApplyCheats();
            m_core->EndFrame();
            break;
        }
        case 263: {
            m_vCount = 0;
            if (m_core->settings.threaded2D && !m_thread) {
                m_running = true;
                m_thread  = std::make_unique<std::thread>(&Gpu::DrawThreaded, this);
                uint64_t id = std::hash<std::thread::id>{}(m_thread->get_id());
                LOG("Create thread to run virtual GPU (id:%lu)", id);
            }
            break;
        }
    }
    if (m_vCount < 192 && m_thread)
        m_drawing.store(1);
    for (int i = 0; i < 2; i++) {
        if (m_vCount == ((m_dispStat[i] >> 8) | ((m_dispStat[i] & BIT(7)) << 1))) {
            m_dispStat[i] |= BIT(2);
            if (m_dispStat[i] & BIT(5))
                m_core->interpreter[i].SendInterrupt(2);
        } else if (m_dispStat[i] & BIT(2)) {
            m_dispStat[i] = static_cast<uint16_t>(m_dispStat[i] & ~BIT(2));
        }
        m_dispStat[i] = static_cast<uint16_t>(m_dispStat[i] & ~BIT(1));
    }
    m_core->Schedule(Task(&m_scanline355Task, 355 * 6));
}

void Gpu::DrawGbaThreaded() {
    while (m_running) {
        while (m_drawing.load() != 1) {
            if (!m_running)
                return;
            std::this_thread::yield();
        }
        m_core->gpu2D[0].DrawGbaScanline(m_vCount);
        m_drawing.store(0);
    }
}

void Gpu::DrawThreaded() {
    while (m_running) {
        while (m_drawing.load() != 1) {
            if (!m_running)
                return;
            std::this_thread::yield();
        }
        m_drawing.store(2);
        m_core->gpu2D[0].DrawScanline(m_vCount);
        if (m_drawing.exchange(3) == 2)
            m_core->gpu2D[1].DrawScanline(m_vCount);
        m_drawing.store(0);
    }
}

void Gpu::WriteDispStat(bool cpu, uint16_t mask, uint16_t value) {
    mask &= 0xFFB8;
    m_dispStat[cpu] = (m_dispStat[cpu] & ~mask) | (value & mask);
}

void Gpu::WriteDispCapCnt(uint32_t mask, uint32_t value) {
    mask &= 0xEF3F1F1F;
    m_dispCapCnt = (m_dispCapCnt & ~mask) | (value & mask);
}

void Gpu::WritePowCnt1(uint16_t mask, uint16_t value) {
    mask &= 0x820F;
    m_powCnt1 = (m_powCnt1 & ~mask) | (value & mask);
}

} // namespace consoles::arm

