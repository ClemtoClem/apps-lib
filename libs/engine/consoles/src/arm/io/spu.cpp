#include "consoles/arm/io/spu.hpp"

#include "consoles/arm/core.hpp"
#include "consoles/arm/settings.hpp"

#include <chrono>
#include <cstring>

namespace consoles::arm {


const int     Spu::indexTable[] = {-1, -1, -1, -1, 2, 4, 6, 8};
const int16_t Spu::adpcmTable[] = {
    0x0007, 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x0010, 0x0011, 0x0013, 0x0015, 0x0017,
    0x0019, 0x001C, 0x001F, 0x0022, 0x0025, 0x0029, 0x002D, 0x0032, 0x0037, 0x003C, 0x0042, 0x0049, 0x0050,
    0x0058, 0x0061, 0x006B, 0x0076, 0x0082, 0x008F, 0x009D, 0x00AD, 0x00BE, 0x00D1, 0x00E6, 0x00FD, 0x0117,
    0x0133, 0x0151, 0x0173, 0x0198, 0x01C1, 0x01EE, 0x0220, 0x0256, 0x0292, 0x02D4, 0x031C, 0x036C, 0x03C3,
    0x0424, 0x048E, 0x0502, 0x0583, 0x0610, 0x06AB, 0x0756, 0x0812, 0x08E0, 0x09C3, 0x0ABD, 0x0BD0, 0x0CFF,
    0x0E4C, 0x0FBA, 0x114C, 0x1307, 0x14EE, 0x1706, 0x1954, 0x1BDC, 0x1EA5, 0x21B6, 0x2515, 0x28CA, 0x2CDF,
    0x315B, 0x364B, 0x3BB9, 0x41B2, 0x4844, 0x4F7E, 0x5771, 0x602F, 0x69CE, 0x7462, 0x7FFF
};


Spu::Spu(Core *core_) : m_core(core_) {
    m_runGbaSampleTask = std::bind(&Spu::RunGbaSample, this);
    m_runSampleTask    = std::bind(&Spu::RunSample, this);
}


Spu::~Spu() = default;


void Spu::ScheduleInit() {
    m_core->Schedule(Task(&m_runSampleTask, 512 * 2));
}


void Spu::GbaScheduleInit() {
    m_core->Schedule(Task(&m_runGbaSampleTask, 512));
}

void Spu::IoState(StateArchive &archive) {
    archive.Io(m_gbaFrameSequencer);
    archive.Io(m_gbaSoundTimers);
    archive.Io(m_gbaEnvelopes);
    archive.Io(m_gbaEnvTimers);
    archive.Io(m_gbaSweepTimer);
    archive.Io(m_gbaWaveDigit);
    archive.Io(m_gbaNoiseValue);
    archive.Io(m_gbaWaveRam);
    archive.IoQueue(m_gbaFifoA);
    archive.IoQueue(m_gbaFifoB);
    archive.Io(m_gbaSampleA);
    archive.Io(m_gbaSampleB);
    archive.Io(m_enabled);
    archive.Io(m_adpcmValue);
    archive.Io(m_adpcmLoopValue);
    archive.Io(m_adpcmIndex);
    archive.Io(m_adpcmLoopIndex);
    archive.Io(m_adpcmToggle);
    archive.Io(m_dutyCycles);
    archive.Io(m_noiseValues);
    archive.Io(m_soundCurrent);
    archive.Io(m_soundTimers);
    archive.Io(m_sndCapCurrent);
    archive.Io(m_sndCapTimers);
    archive.Io(m_gbaSoundCntL);
    archive.Io(m_gbaSoundCntH);
    archive.Io(m_gbaSoundCntX);
    archive.Io(m_gbaMainSoundCntL);
    archive.Io(m_gbaMainSoundCntH);
    archive.Io(m_gbaMainSoundCntX);
    archive.Io(m_gbaSoundBias);
    archive.Io(m_soundCnt);
    archive.Io(m_soundSad);
    archive.Io(m_soundTmr);
    archive.Io(m_soundPnt);
    archive.Io(m_soundLen);
    archive.Io(m_mainSoundCnt);
    archive.Io(m_soundBias);
    archive.Io(m_sndCapCnt);
    archive.Io(m_sndCapDad);
    archive.Io(m_sndCapLen);
}


void Spu::RunGbaSample() {
    int64_t sampleLeft  = 0;
    int64_t sampleRight = 0;
    if (m_gbaMainSoundCntX & BIT(7)) {
        int32_t data[4] = {};
        for (int i = 0; i < 2; i++) {
            if (!(m_gbaMainSoundCntX & BIT(i)))
                continue;
            if (i == 0 && m_gbaFrameSequencer % 256 == 128 && (m_gbaSoundCntL[i] & 0x70) && --m_gbaSweepTimer <= 0) {
                uint16_t frequency = (m_gbaSoundCntX[i] & 0x07FF);
                int      sweep     = frequency >> (m_gbaSoundCntL[i] & 0x07);
                if (m_gbaSoundCntL[i] & BIT(3))
                    sweep = -sweep;
                frequency = static_cast<uint16_t>(frequency + (sweep));
                if (frequency < 0x800) {
                    m_gbaSoundCntX[i] = static_cast<uint16_t>((m_gbaSoundCntX[i] & ~0x07FF) | frequency);
                    m_gbaSweepTimer   = (m_gbaSoundCntL[i] & 0x70) >> 4;
                } else {
                    m_gbaMainSoundCntX = static_cast<uint8_t>(m_gbaMainSoundCntX & ~BIT(i));
                    continue;
                }
            }
            m_gbaSoundTimers[i] -= 4;
            while ((m_gbaSoundTimers[i]) <= 0)
                m_gbaSoundTimers[i] += 2048 - (m_gbaSoundCntX[i] & 0x07FF);
            int duty;
            switch ((m_gbaSoundCntH[i] & 0x00C0) >> 6) {
                case 0:
                    duty = (2048 - (m_gbaSoundCntX[i] & 0x07FF)) * 7 / 8;
                    break;
                case 1:
                    duty = (2048 - (m_gbaSoundCntX[i] & 0x07FF)) * 6 / 8;
                    break;
                case 2:
                    duty = (2048 - (m_gbaSoundCntX[i] & 0x07FF)) * 4 / 8;
                    break;
                case 3:
                    duty = (2048 - (m_gbaSoundCntX[i] & 0x07FF)) * 2 / 8;
                    break;
            }
            data[i] = (m_gbaSoundTimers[i] < duty) ? -0x80 : 0x80;
            if (m_gbaFrameSequencer % 128 == 0 && (m_gbaSoundCntX[i] & BIT(14)) && (m_gbaSoundCntH[i] & 0x003F)) {
                m_gbaSoundCntH[i] = static_cast<uint16_t>((m_gbaSoundCntH[i] & ~0x003F) | ((m_gbaSoundCntH[i] & 0x003F) - 1));
                if ((m_gbaSoundCntH[i] & 0x003F) == 0)
                    m_gbaMainSoundCntX = static_cast<uint8_t>(m_gbaMainSoundCntX & ~BIT(i));
            }
            if (m_gbaFrameSequencer == 448 && --m_gbaEnvTimers[i] <= 0) {
                if (m_gbaEnvTimers[i] == 0) {
                    if ((m_gbaSoundCntH[i] & BIT(11)) && m_gbaEnvelopes[i] < 15)
                        m_gbaEnvelopes[i]++;
                    else if (!(m_gbaSoundCntH[i] & BIT(11)) && m_gbaEnvelopes[i] > 0)
                        m_gbaEnvelopes[i]--;
                } else {
                    m_gbaEnvelopes[i] = (m_gbaSoundCntH[i] & 0xF000) >> 12;
                }
                m_gbaEnvTimers[i] = (m_gbaSoundCntH[i] & 0x0700) >> 8;
            }
            data[i] = data[i] * m_gbaEnvelopes[i] / 15;
        }
        if ((m_gbaMainSoundCntX & BIT(2)) && (m_gbaSoundCntL[1] & BIT(7))) {
            m_gbaSoundTimers[2] -= 64;
            while ((m_gbaSoundTimers[2]) <= 0) {
                m_gbaSoundTimers[2] += (2048 - (m_gbaSoundCntX[2] & 0x07FF));
                m_gbaWaveDigit = (m_gbaWaveDigit + 1) % 64;
            }
            int bank = (m_gbaSoundCntL[1] & BIT(6)) >> 6;
            if ((m_gbaSoundCntL[1] & BIT(5)) && m_gbaWaveDigit >= 32)
                bank = !bank;
            data[2] = m_gbaWaveRam[bank][(m_gbaWaveDigit % 32) / 2];
            if (m_gbaWaveDigit & 1)
                data[2] &= 0x0F;
            else
                data[2] >>= 4;
            if (m_gbaFrameSequencer % 128 == 0 && (m_gbaSoundCntX[2] & BIT(14)) && (m_gbaSoundCntH[2] & 0x00FF)) {
                m_gbaSoundCntH[2] = static_cast<uint16_t>((m_gbaSoundCntH[2] & ~0x00FF) | ((m_gbaSoundCntH[2] & 0x00FF) - 1));
                if ((m_gbaSoundCntH[2] & 0x00FF) == 0)
                    m_gbaMainSoundCntX = static_cast<uint8_t>(m_gbaMainSoundCntX & ~BIT(2));
            }
            switch ((m_gbaSoundCntH[2] & 0xE000) >> 13) {
                case 0:
                    data[2] >>= 4;
                    break;
                case 1:
                    data[2] >>= 0;
                    break;
                case 2:
                    data[2] >>= 1;
                    break;
                case 3:
                    data[2] >>= 2;
                    break;
                default:
                    data[2] = data[2] * 3 / 4;
                    break;
            }
            data[2] = (data[2] * 0x100 / 0xF);
        }
        if (m_gbaMainSoundCntX & BIT(3)) {
            m_gbaSoundTimers[3] -= 16;
            while ((m_gbaSoundTimers[3]) <= 0) {
                int divisor = (m_gbaSoundCntX[3] & 0x0007) * 16;
                if (divisor == 0)
                    divisor = 8;
                m_gbaSoundTimers[3] += (divisor << ((m_gbaSoundCntX[3] & 0x00F0) >> 4));
                m_gbaNoiseValue = static_cast<uint16_t>(m_gbaNoiseValue & ~BIT(15));
                if (m_gbaNoiseValue & BIT(0))
                    m_gbaNoiseValue = static_cast<uint16_t>(BIT(15) | ((m_gbaNoiseValue >> 1) ^ ((m_gbaSoundCntH[3] & BIT(3)) ? 0x60 : 0x6000)));
                else
                    m_gbaNoiseValue >>= 1;
            }
            data[3] = (m_gbaNoiseValue & BIT(15)) ? 0x80 : -0x80;
            if (m_gbaFrameSequencer % 128 == 0 && (m_gbaSoundCntX[3] & BIT(14)) && (m_gbaSoundCntH[3] & 0x003F)) {
                m_gbaSoundCntH[3] = static_cast<uint16_t>((m_gbaSoundCntH[3] & ~0x003F) | ((m_gbaSoundCntH[3] & 0x003F) - 1));
                if ((m_gbaSoundCntH[3] & 0x003F) == 0)
                    m_gbaMainSoundCntX = static_cast<uint8_t>(m_gbaMainSoundCntX & ~BIT(3));
            }
            if (m_gbaFrameSequencer == 448 && --m_gbaEnvTimers[2] <= 0) {
                if (m_gbaEnvTimers[2] == 0) {
                    if ((m_gbaSoundCntH[3] & BIT(11)) && m_gbaEnvelopes[2] < 15)
                        m_gbaEnvelopes[2]++;
                    else if (!(m_gbaSoundCntH[3] & BIT(11)) && m_gbaEnvelopes[2] > 0)
                        m_gbaEnvelopes[2]--;
                } else {
                    m_gbaEnvelopes[2] = (m_gbaSoundCntH[3] & 0xF000) >> 12;
                }
                m_gbaEnvTimers[2] = (m_gbaSoundCntH[3] & 0x0700) >> 8;
            }
            data[3] = data[3] * m_gbaEnvelopes[2] / 15;
        }
        for (int i = 0; i < 4; i++) {
            switch (m_gbaMainSoundCntH & 0x0003) {
                case 0:
                    data[i] >>= 2;
                    break;
                case 1:
                    data[i] >>= 1;
                    break;
                case 2:
                    data[i] >>= 0;
                    break;
            }
            if (m_gbaMainSoundCntL & BIT(12 + i))
                sampleLeft += data[i] * ((m_gbaMainSoundCntL & 0x0070) >> 4) / 7;
            if (m_gbaMainSoundCntL & BIT(8 + i))
                sampleRight += data[i] * (m_gbaMainSoundCntL & 0x0007) / 7;
        }
        if (m_gbaMainSoundCntH & BIT(9))
            sampleLeft += m_gbaSampleA << ((m_gbaMainSoundCntH & BIT(2)) ? 2 : 1);
        if (m_gbaMainSoundCntH & BIT(8))
            sampleRight += m_gbaSampleA << ((m_gbaMainSoundCntH & BIT(2)) ? 2 : 1);
        if (m_gbaMainSoundCntH & BIT(13))
            sampleLeft += m_gbaSampleB << ((m_gbaMainSoundCntH & BIT(3)) ? 2 : 1);
        if (m_gbaMainSoundCntH & BIT(12))
            sampleRight += m_gbaSampleB << ((m_gbaMainSoundCntH & BIT(3)) ? 2 : 1);
        m_gbaFrameSequencer = (m_gbaFrameSequencer + 1) % 512;
    }
    sampleLeft += (m_gbaSoundBias & 0x03FF);
    sampleRight += (m_gbaSoundBias & 0x03FF);
    if (sampleLeft < 0x000)
        sampleLeft = 0x000;
    if (sampleLeft > 0x3FF)
        sampleLeft = 0x3FF;
    if (sampleRight < 0x000)
        sampleRight = 0x000;
    if (sampleRight > 0x3FF)
        sampleRight = 0x3FF;
    sampleLeft  = (sampleLeft - 0x200) << 5;
    sampleRight = (sampleRight - 0x200) << 5;
    if (onSample)
        onSample(static_cast<int16_t>(sampleLeft), static_cast<int16_t>(sampleRight));
    m_core->Schedule(Task(&m_runGbaSampleTask, 512));
}


void Spu::RunSample() {
    int64_t mixerLeft = 0, mixerRight = 0;
    int64_t channelsLeft[2] = {}, channelsRight[2] = {};
    for (int i = 0; i < 16; i++) {
        if (!(m_enabled & BIT(i)))
            continue;
        int format = (m_soundCnt[i] & 0x60000000) >> 29;
        int64_t data = 0;
        switch (format) {
            case 0: {
                data = (int8_t)m_core->memory.Read<uint8_t>(1, m_soundCurrent[i]) << 8;
                break;
            }
            case 1: {
                data = (int16_t)m_core->memory.Read<uint16_t>(1, m_soundCurrent[i]);
                break;
            }
            case 2: {
                data = m_adpcmValue[i];
                break;
            }
            case 3: {
                if (i >= 8 && i <= 13) {
                    int duty = 7 - ((m_soundCnt[i] & 0x07000000) >> 24);
                    data     = (m_dutyCycles[i - 8] < duty) ? -0x7FFF : 0x7FFF;
                } else if (i >= 14) {
                    data = (m_noiseValues[i - 14] & BIT(15)) ? -0x7FFF : 0x7FFF;
                }
                break;
            }
        }
        m_soundTimers[i] += 512;
        bool overflow = (m_soundTimers[i] < 512);
        while (overflow) {
            m_soundTimers[i] += m_soundTmr[i];
            overflow = (m_soundTimers[i] < m_soundTmr[i]);
            switch (format) {
                case 0:
                case 1: {
                    m_soundCurrent[i] += static_cast<uint32_t>(1 + format);
                    break;
                }
                case 2: {
                    if (m_soundCurrent[i] == m_soundSad[i] + m_soundPnt[i] * 4 && !m_adpcmToggle[i]) {
                        m_adpcmLoopValue[i] = m_adpcmValue[i];
                        m_adpcmLoopIndex[i] = m_adpcmIndex[i];
                    }
                    uint8_t adpcmData = m_core->memory.Read<uint8_t>(1, m_soundCurrent[i]);
                    adpcmData         = m_adpcmToggle[i] ? ((adpcmData & 0xF0) >> 4) : (adpcmData & 0x0F);
                    int32_t diff      = adpcmTable[m_adpcmIndex[i]] / 8;
                    if (adpcmData & BIT(0))
                        diff += adpcmTable[m_adpcmIndex[i]] / 4;
                    if (adpcmData & BIT(1))
                        diff += adpcmTable[m_adpcmIndex[i]] / 2;
                    if (adpcmData & BIT(2))
                        diff += adpcmTable[m_adpcmIndex[i]] / 1;
                    if (adpcmData & BIT(3)) {
                        m_adpcmValue[i] += diff;
                        if (m_adpcmValue[i] > 0x7FFF)
                            m_adpcmValue[i] = 0x7FFF;
                    } else {
                        m_adpcmValue[i] -= diff;
                        if (m_adpcmValue[i] < -0x7FFF)
                            m_adpcmValue[i] = -0x7FFF;
                    }
                    m_adpcmIndex[i] += indexTable[adpcmData & 0x7];
                    if (m_adpcmIndex[i] < 0)
                        m_adpcmIndex[i] = 0;
                    if (m_adpcmIndex[i] > 88)
                        m_adpcmIndex[i] = 88;
                    m_adpcmToggle[i] = !m_adpcmToggle[i];
                    if (!m_adpcmToggle[i])
                        m_soundCurrent[i]++;
                    break;
                }
                case 3: {
                    if (i >= 8 && i <= 13) {
                        m_dutyCycles[i - 8] = (m_dutyCycles[i - 8] + 1) % 8;
                    } else if (i >= 14) {
                        m_noiseValues[i - 14] = static_cast<uint16_t>(m_noiseValues[i - 14] & ~BIT(15));
                        if (m_noiseValues[i - 14] & BIT(0))
                            m_noiseValues[i - 14] = BIT(15) | ((m_noiseValues[i - 14] >> 1) ^ 0x6000);
                        else
                            m_noiseValues[i - 14] >>= 1;
                    }
                    break;
                }
            }
            if (format != 3 && m_soundCurrent[i] >= m_soundSad[i] + (m_soundPnt[i] + m_soundLen[i]) * 4) {
                if ((m_soundCnt[i] & 0x18000000) >> 27 == 1) {
                    m_soundCurrent[i] = m_soundSad[i] + m_soundPnt[i] * 4;
                    if (format == 2) {
                        m_adpcmValue[i]  = m_adpcmLoopValue[i];
                        m_adpcmIndex[i]  = m_adpcmLoopIndex[i];
                        m_adpcmToggle[i] = false;
                    }
                } else {
                    m_soundCnt[i] &= ~BIT(31);
                    m_enabled = static_cast<uint16_t>(m_enabled & ~BIT(i));
                    break;
                }
            }
        }
        int divShift = (m_soundCnt[i] & 0x00000300) >> 8;
        if (divShift == 3)
            divShift++;
        data <<= 4 - divShift;
        int mulFactor = (m_soundCnt[i] & 0x0000007F);
        if (mulFactor == 127)
            mulFactor++;
        data         = (data << 7) * mulFactor / 128;
        int panValue = (m_soundCnt[i] & 0x007F0000) >> 16;
        if (panValue == 127)
            panValue++;
        int64_t dataLeft  = (data * (128 - panValue) / 128) >> 3;
        int64_t dataRight = (data * panValue / 128) >> 3;
        if (i == 1 || i == 3) {
            channelsLeft[i >> 1]  = dataLeft;
            channelsRight[i >> 1] = dataRight;
            if (m_mainSoundCnt & BIT(12 + (i >> 1)))
                continue;
        }
        mixerLeft += dataLeft;
        mixerRight += dataRight;
    }
    for (int i = 0; i < 2; i++) {
        if (!(m_sndCapCnt[i] & BIT(7)))
            continue;
        m_sndCapTimers[i] += 512;
        bool overflow = (m_sndCapTimers[i] < 512);
        while (overflow) {
            m_sndCapTimers[i] += m_soundTmr[1 + (i << 1)];
            overflow       = (m_sndCapTimers[i] < m_soundTmr[1 + (i << 1)]);
            int64_t sample = ((i == 0) ? mixerLeft : mixerRight);
            if (sample > 0x7FFFFF)
                sample = 0x7FFFFF;
            if (sample < -0x800000)
                sample = -0x800000;
            if (m_sndCapCnt[i] & BIT(3)) {
                m_core->memory.Write<uint8_t>(1, m_sndCapCurrent[i], static_cast<unsigned char>(sample >> 16));
                m_sndCapCurrent[i]++;
            } else {
                m_core->memory.Write<uint16_t>(1, m_sndCapCurrent[i], static_cast<unsigned short>(sample >> 8));
                m_sndCapCurrent[i] += 2;
            }
            if (m_sndCapCurrent[i] >= m_sndCapDad[i] + m_sndCapLen[i] * 4) {
                if (m_sndCapCnt[i] & BIT(2)) {
                    m_sndCapCnt[i] = static_cast<uint8_t>(m_sndCapCnt[i] & ~BIT(7));
                    continue;
                } else {
                    m_sndCapCurrent[i] = m_sndCapDad[i];
                }
            }
        }
    }
    int64_t sampleLeft;
    switch ((m_mainSoundCnt & 0x0300) >> 8) {
        case 0:
            sampleLeft = mixerLeft;
            break;
        case 1:
            sampleLeft = channelsLeft[0];
            break;
        case 2:
            sampleLeft = channelsLeft[1];
            break;
        case 3:
            sampleLeft = channelsLeft[0] + channelsLeft[1];
            break;
    }
    int64_t sampleRight;
    switch ((m_mainSoundCnt & 0x0C00) >> 10) {
        case 0:
            sampleRight = mixerRight;
            break;
        case 1:
            sampleRight = channelsRight[0];
            break;
        case 2:
            sampleRight = channelsRight[1];
            break;
        case 3:
            sampleRight = channelsRight[0] + channelsRight[1];
            break;
    }
    int masterVol = (m_mainSoundCnt & 0x007F);
    if (masterVol == 127)
        masterVol++;
    sampleLeft  = (sampleLeft * masterVol / 128) >> 8;
    sampleRight = (sampleRight * masterVol / 128) >> 8;
    sampleLeft  = (sampleLeft >> 6) + m_soundBias;
    sampleRight = (sampleRight >> 6) + m_soundBias;
    if (sampleLeft < 0x000)
        sampleLeft = 0x000;
    if (sampleLeft > 0x3FF)
        sampleLeft = 0x3FF;
    if (sampleRight < 0x000)
        sampleRight = 0x000;
    if (sampleRight > 0x3FF)
        sampleRight = 0x3FF;
    sampleLeft  = (sampleLeft - 0x200) << 5;
    sampleRight = (sampleRight - 0x200) << 5;
    if (onSample)
        onSample(static_cast<int16_t>(sampleLeft), static_cast<int16_t>(sampleRight));
    m_core->Schedule(Task(&m_runSampleTask, 512 * 2));
}


void Spu::StartChannel(int channel) {
    m_soundCurrent[channel] = m_soundSad[channel];
    m_soundTimers[channel]  = m_soundTmr[channel];
    switch ((m_soundCnt[channel] & 0x60000000) >> 29) {
        case 2: {
            uint32_t header     = m_core->memory.Read<uint32_t>(1, m_soundSad[channel]);
            m_adpcmValue[channel] = (int16_t)header;
            m_adpcmIndex[channel] = (header & 0x007F0000) >> 16;
            if (m_adpcmIndex[channel] > 88)
                m_adpcmIndex[channel] = 88;
            m_adpcmToggle[channel] = false;
            m_soundCurrent[channel] += 4;
            break;
        }
        case 3: {
            if (channel >= 8 && channel <= 13)
                m_dutyCycles[channel - 8] = 0;
            else if (channel >= 14)
                m_noiseValues[channel - 14] = 0x7FFF;
            break;
        }
    }
    m_enabled |= BIT(channel);
}


void Spu::GbaFifoTimer(int timer) {
    if (((m_gbaMainSoundCntH & BIT(10)) >> 10) == static_cast<unsigned>(timer)) {
        if (!m_gbaFifoA.empty()) {
            m_gbaSampleA = m_gbaFifoA.front();
            m_gbaFifoA.pop();
        }
        if (m_gbaFifoA.size() <= 16)
            m_core->dma[1].Trigger(3, 0x02);
    }
    if (((m_gbaMainSoundCntH & BIT(14)) >> 14) == static_cast<unsigned>(timer)) {
        if (!m_gbaFifoB.empty()) {
            m_gbaSampleB = m_gbaFifoB.front();
            m_gbaFifoB.pop();
        }
        if (m_gbaFifoB.size() <= 16)
            m_core->dma[1].Trigger(3, 0x04);
    }
}


void Spu::WriteGbaSoundCntL(int channel, uint8_t value) {
    if (!(m_gbaMainSoundCntX & BIT(7)))
        return;
    uint8_t mask              = (channel == 0) ? 0x7F : 0xE0;
    m_gbaSoundCntL[channel / 2] = (m_gbaSoundCntL[channel / 2] & ~mask) | (value & mask);
}


void Spu::WriteGbaSoundCntH(int channel, uint16_t mask, uint16_t value) {
    if (!(m_gbaMainSoundCntX & BIT(7)))
        return;
    switch (channel) {
        case 2:
            mask &= 0xE0FF;
            break;
        case 3:
            mask &= 0xFF3F;
            break;
    }
    m_gbaSoundCntH[channel] = (m_gbaSoundCntH[channel] & ~mask) | (value & mask);
}


void Spu::WriteGbaSoundCntX(int channel, uint16_t mask, uint16_t value) {
    if (!(m_gbaMainSoundCntX & BIT(7)))
        return;
    mask &= (channel == 3) ? 0x40FF : 0x47FF;
    m_gbaSoundCntX[channel] = (m_gbaSoundCntX[channel] & ~mask) | (value & mask);
    if ((value & BIT(15))) {
        if (channel < 2) {
            if (channel == 0)
                m_gbaSweepTimer = (m_gbaSoundCntL[0] & 0x70) >> 4;
            m_gbaEnvelopes[channel]   = (m_gbaSoundCntH[channel] & 0xF000) >> 12;
            m_gbaEnvTimers[channel]   = (m_gbaSoundCntH[channel] & 0x0700) >> 8;
            m_gbaSoundTimers[channel] = 2048 - (m_gbaSoundCntX[channel] & 0x07FF);
        } else if (channel == 2) {
            m_gbaWaveDigit      = 0;
            m_gbaSoundTimers[2] = 2048 - (m_gbaSoundCntX[2] & 0x07FF);
        } else {
            m_gbaNoiseValue   = (m_gbaSoundCntH[3] & BIT(3)) ? 0x40 : 0x4000;
            m_gbaEnvelopes[2] = (m_gbaSoundCntH[3] & 0xF000) >> 12;
            m_gbaEnvTimers[2] = (m_gbaSoundCntH[3] & 0x0700) >> 8;
            int divisor     = (m_gbaSoundCntX[3] & 0x0007) * 16;
            if (divisor == 0)
                divisor = 8;
            m_gbaSoundTimers[3] = (divisor << ((m_gbaSoundCntX[3] & 0x00F0) >> 4));
        }
        m_gbaMainSoundCntX |= BIT(channel);
    }
}


void Spu::WriteGbaMainSoundCntL(uint16_t mask, uint16_t value) {
    if (!(m_gbaMainSoundCntX & BIT(7)))
        return;
    mask &= 0xFF77;
    m_gbaMainSoundCntL = (m_gbaMainSoundCntL & ~mask) | (value & mask);
}


void Spu::WriteGbaMainSoundCntH(uint16_t mask, uint16_t value) {
    mask &= 0x770F;
    m_gbaMainSoundCntH = (m_gbaMainSoundCntH & ~mask) | (value & mask);
    if (value & BIT(11)) {
        while (!m_gbaFifoA.empty())
            m_gbaFifoA.pop();
    }
    if (value & BIT(15)) {
        while (!m_gbaFifoB.empty())
            m_gbaFifoB.pop();
    }
}


void Spu::WriteGbaMainSoundCntX(uint8_t value) {
    m_gbaMainSoundCntX = static_cast<uint8_t>((m_gbaMainSoundCntX & ~0x80) | (value & 0x80));
    if (!(m_gbaMainSoundCntX & BIT(7))) {
        for (int i = 0; i < 4; i++) {
            if (i < 2)
                m_gbaSoundCntL[i] = 0;
            m_gbaSoundCntH[i] = 0;
            m_gbaSoundCntX[i] = 0;
        }
        m_gbaMainSoundCntL = 0;
        m_gbaMainSoundCntX = static_cast<uint8_t>(m_gbaMainSoundCntX & (~0x0F));
        m_gbaFrameSequencer = 0;
    }
}


void Spu::WriteGbaSoundBias(uint16_t mask, uint16_t value) {
    mask &= 0xC3FE;
    m_gbaSoundBias = (m_gbaSoundBias & ~mask) | (value & mask);
}


void Spu::WriteGbaWaveRam(int index, uint8_t value) {
    m_gbaWaveRam[!(m_gbaSoundCntL[1] & BIT(6))][index] = value;
}


void Spu::WriteGbaFifoA(uint32_t mask, uint32_t value) {
    for (int i = 0; i < 32; i += 8) {
        if (m_gbaFifoA.size() < 32 && (mask & (0xFF << i)))
            m_gbaFifoA.push(static_cast<std::queue<signed char>::value_type>(value >> i));
    }
}


void Spu::WriteGbaFifoB(uint32_t mask, uint32_t value) {
    for (int i = 0; i < 32; i += 8) {
        if (m_gbaFifoB.size() < 32 && (mask & (0xFF << i)))
            m_gbaFifoB.push(static_cast<std::queue<signed char>::value_type>(value >> i));
    }
}


void Spu::WriteSoundCnt(int channel, uint32_t mask, uint32_t value) {
    bool enable = (!(m_soundCnt[channel] & BIT(31)) && (value & BIT(31)));
    mask &= 0xFF7F837F;
    m_soundCnt[channel] = (m_soundCnt[channel] & ~mask) | (value & mask);
    if (enable && (m_mainSoundCnt & BIT(15)) && (m_soundSad[channel] != 0 || ((m_soundCnt[channel] & 0x60000000) >> 29) == 3))
        StartChannel(channel);
    else if (!(m_soundCnt[channel] & BIT(31)))
        m_enabled = static_cast<uint16_t>(m_enabled & ~BIT(channel));
}


void Spu::WriteSoundSad(int channel, uint32_t mask, uint32_t value) {
    mask &= 0x07FFFFFC;
    m_soundSad[channel] = (m_soundSad[channel] & ~mask) | (value & mask);
    if (((m_soundCnt[channel] & 0x60000000) >> 29) != 3) {
        if (m_soundSad[channel] != 0 && (m_mainSoundCnt & BIT(15)) && (m_soundCnt[channel] & BIT(31)))
            StartChannel(channel);
        else
            m_enabled = static_cast<uint16_t>(m_enabled & ~BIT(channel));
    }
}


void Spu::WriteSoundTmr(int channel, uint16_t mask, uint16_t value) {
    m_soundTmr[channel] = (m_soundTmr[channel] & ~mask) | (value & mask);
}


void Spu::WriteSoundPnt(int channel, uint16_t mask, uint16_t value) {
    m_soundPnt[channel] = (m_soundPnt[channel] & ~mask) | (value & mask);
}


void Spu::WriteSoundLen(int channel, uint32_t mask, uint32_t value) {
    mask &= 0x003FFFFF;
    m_soundLen[channel] = (m_soundLen[channel] & ~mask) | (value & mask);
}


void Spu::WriteMainSoundCnt(uint16_t mask, uint16_t value) {
    bool enable = (!(m_mainSoundCnt & BIT(15)) && (value & BIT(15)));
    mask &= 0xBF7F;
    m_mainSoundCnt = (m_mainSoundCnt & ~mask) | (value & mask);
    if (enable) {
        for (int i = 0; i < 16; i++) {
            if ((m_soundCnt[i] & BIT(31)) && (m_soundSad[i] != 0 || ((m_soundCnt[i] & 0x60000000) >> 29) == 3))
                StartChannel(i);
        }
    } else if (!(m_mainSoundCnt & BIT(15))) {
        m_enabled = 0;
    }
}


void Spu::WriteSoundBias(uint16_t mask, uint16_t value) {
    mask &= 0x03FF;
    m_soundBias = (m_soundBias & ~mask) | (value & mask);
}


void Spu::WriteSndCapCnt(int channel, uint8_t value) {
    if (!(m_sndCapCnt[channel] & BIT(7)) && (value & BIT(7))) {
        m_sndCapCurrent[channel] = m_sndCapDad[channel];
        m_sndCapTimers[channel]  = m_soundTmr[1 + (channel << 1)];
    }
    m_sndCapCnt[channel] = (value & 0x8F);
}


void Spu::WriteSndCapDad(int channel, uint32_t mask, uint32_t value) {
    mask &= 0x07FFFFFC;
    m_sndCapDad[channel]     = (m_sndCapDad[channel] & ~mask) | (value & mask);
    m_sndCapCurrent[channel] = m_sndCapDad[channel];
    m_sndCapTimers[channel]  = m_soundTmr[1 + (channel << 1)];
}


void Spu::WriteSndCapLen(int channel, uint16_t mask, uint16_t value) {
    m_sndCapLen[channel] = (m_sndCapLen[channel] & ~mask) | (value & mask);
}


uint8_t Spu::ReadGbaSoundCntL(int channel) {
    return m_gbaSoundCntL[channel / 2];
}


uint16_t Spu::ReadGbaSoundCntH(int channel) {
    return static_cast<uint16_t>(m_gbaSoundCntH[channel] & ~((channel == 2) ? 0x00FF : 0x003F));
}


uint16_t Spu::ReadGbaSoundCntX(int channel) {
    return static_cast<uint16_t>(m_gbaSoundCntX[channel] & ~((channel == 3) ? 0x0000 : 0x07FF));
}


uint16_t Spu::ReadGbaMainSoundCntL() {
    return m_gbaMainSoundCntL;
}


uint16_t Spu::ReadGbaMainSoundCntH() {
    return m_gbaMainSoundCntH;
}


uint8_t Spu::ReadGbaMainSoundCntX() {
    return m_gbaMainSoundCntX;
}


uint16_t Spu::ReadGbaSoundBias() {
    return m_gbaSoundBias;
}


uint8_t Spu::ReadGbaWaveRam(int index) {
    return m_gbaWaveRam[!(m_gbaSoundCntL[1] & BIT(6))][index];
}


uint32_t Spu::ReadSoundCnt(int channel) {
    return m_soundCnt[channel];
}


uint16_t Spu::ReadMainSoundCnt() {
    return m_mainSoundCnt;
}


uint16_t Spu::ReadSoundBias() {
    return m_soundBias;
}


uint8_t Spu::ReadSndCapCnt(int channel) {
    return m_sndCapCnt[channel];
}


uint32_t Spu::ReadSndCapDad(int channel) {
    return m_sndCapDad[channel];
}

} // namespace consoles::arm
