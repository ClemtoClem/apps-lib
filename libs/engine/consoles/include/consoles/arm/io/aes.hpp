#pragma once
#include <cstdint>
#include <deque>
#include <functional>

namespace consoles::arm {

class Core;
class StateArchive;

class Aes {
public:
    Aes(Core *core);
    void IoState(StateArchive &archive);
    void Update();

    uint32_t ReadCnt() { return m_aesCnt; }
    uint32_t ReadRdfifo();

    void WriteCnt(uint32_t mask, uint32_t value);
    void WriteBlkcnt(uint32_t mask, uint32_t value);
    void WriteWrfifo(uint32_t mask, uint32_t value);
    void WriteIv(int i, uint32_t mask, uint32_t value);
    void WriteMac(int i, uint32_t mask, uint32_t value);
    void WriteKey(int i, int j, uint32_t mask, uint32_t value);
    void WriteKeyx(int i, int j, uint32_t mask, uint32_t value);
    void WriteKeyy(int i, int j, uint32_t mask, uint32_t value);

private:
    Core *m_core;
    bool m_scheduled = false;
    std::function<void()> m_updateTask;

    uint8_t m_fsBox[0x100];
    uint8_t m_rsBox[0x100];
    uint32_t m_fTable[0x100];
    uint32_t m_rTable[0x100];

    uint32_t m_keys[4][4] = {};
    uint32_t m_keysX[4][4] = {};
    uint32_t m_keysY[4][4] = {};
    uint32_t m_rKey[44] = {};
    uint32_t m_ctr[4] = {};
    uint32_t m_cbc[4] = {};
    uint16_t m_curBlock = 0;
    uint16_t m_curExtra = 0;
    uint8_t m_curKey = 0;

    std::deque<uint32_t> m_writeFifo;
    std::deque<uint32_t> m_readFifo;

    uint32_t m_aesCnt = 0;
    uint32_t m_aesBlkcnt = 0;
    uint32_t m_aesRdfifo = 0;
    uint32_t m_aesIv[4] = {};
    uint32_t m_aesMac[4] = {};

    static uint32_t Scatter8(uint8_t *t, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    static uint32_t Scatter32(uint32_t *t, uint32_t a, uint32_t b, uint32_t c, uint32_t d);

    static void RolKey(uint32_t *key, uint8_t rol);
    static void XorKey(uint32_t *dst, uint32_t *src);
    static void AddKey(uint32_t *dst, uint32_t *src);

    void CryptBlock(uint32_t *src, uint32_t *dst);
    void InitFifo();
    void TriggerFifo();
};

} // namespace consoles::arm

