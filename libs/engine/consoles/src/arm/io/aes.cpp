#include "consoles/arm/io/aes.hpp"

#include "consoles/arm/core.hpp"

#include <algorithm>
#include <cstring>

namespace consoles::arm {

namespace {
// Bit-rotation helpers for the Rijndael math below — not used anywhere else
// in the codebase, so kept local rather than added to defines.hpp.
inline uint8_t ROL8(uint8_t v, int n) {
    return uint8_t((v << n) | (v >> (8 - n)));
}
inline uint32_t ROL32(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}
inline uint32_t ROR32(uint32_t v, int n) {
    return (v >> n) | (v << (32 - n));
}
} // namespace

Aes::Aes(Core *core_): m_core(core_) {
    m_updateTask = std::bind(&Aes::Update, this);

    // Generate temporary pow and log tables
    uint8_t pow[0x100], log[0x100];
    for (int i = 0, x = 1; i < 0x100; i++) {
        pow[i] = static_cast<uint8_t>(x); log[x] = static_cast<uint8_t>(i);
        x ^= (x << 1) ^ ((x & 0x80) ? 0x11B : 0);
    }

    // Generate the AES forward and reverse S-boxes
    for (int i = 0; i < 0x100; i++) {
        uint8_t x = i ? pow[0xFF - log[i]] : 0;
        x = x ^ ROL8(x, 1) ^ ROL8(x, 2) ^ ROL8(x, 3) ^ ROL8(x, 4) ^ 0x63;
        m_fsBox[i] = x; m_rsBox[x] = static_cast<uint8_t>(i);
    }

    // Generate the AES forward and reverse tables
    for (int i = 0; i < 0x100; i++) {
        uint8_t x = (m_fsBox[i] << 1) ^ ((m_fsBox[i] & 0x80) ? 0x11B : 0);
        m_fTable[i] = (m_fsBox[i] * 0x10101) ^ (x * 0x1000001);
        m_rTable[i] = (x = m_rsBox[i]) ? (pow[(log[x] + log[0xE]) % 0xFF] << 24) | (pow[(log[x] + log[0x9]) % 0xFF] << 16) |
            (pow[(log[x] + log[0xD]) % 0xFF] << 8) | pow[(log[x] + log[0xB]) % 0xFF] : 0;
    }
}

void Aes::IoState(StateArchive &archive) {
    // DSi-only state — this fork never sets core->dsiMode, so this is a
    // no-op today, but keeps Aes serializable the same way as every other
    // component if DSi support ever lands.
    if (!m_core->dsiMode) return;
    archive.Io(m_scheduled);
    archive.Io(m_keys);
    archive.Io(m_keysX);
    archive.Io(m_keysY);
    archive.Io(m_rKey);
    archive.Io(m_ctr);
    archive.Io(m_cbc);
    archive.Io(m_curBlock);
    archive.Io(m_curExtra);
    archive.Io(m_curKey);
    archive.Io(m_aesCnt);
    archive.Io(m_aesBlkcnt);
    archive.Io(m_aesRdfifo);
    archive.Io(m_aesIv);
    archive.Io(m_aesMac);

    // StateArchive has no std::deque overload (only IoQueue() for
    // std::queue) — serialize manually, mirroring IoQueue()'s own format.
    auto ioDeque = [&archive](std::deque<uint32_t> &d) {
        if (archive.saving) {
            uint32_t n = uint32_t(d.size());
            archive.Io(n);
            for (uint32_t v : d)
                archive.Io(v);
        } else {
            uint32_t n = 0;
            archive.Io(n);
            d.clear();
            for (uint32_t i = 0; i < n; i++) {
                uint32_t v = 0;
                archive.Io(v);
                d.push_back(v);
            }
        }
    };
    ioDeque(m_writeFifo);
    ioDeque(m_readFifo);
}

uint32_t Aes::Scatter8(uint8_t *t, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    // Perform a scatter operation with an 8-bit table
    return (t[(d >> 24) & 0xFF] << 24) | (t[(c >> 16) & 0xFF] << 16) | (t[(b >> 8) & 0xFF] << 8) | t[a & 0xFF];
}

uint32_t Aes::Scatter32(uint32_t *t, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    // Perform a scatter operation with a 32-bit table
    return t[(d >> 24) & 0xFF] ^ ROR32(t[(c >> 16) & 0xFF], 8) ^ ROR32(t[(b >> 8) & 0xFF], 16) ^ ROR32(t[a & 0xFF], 24);
}

void Aes::RolKey(uint32_t *key, uint8_t rol) {
    // Rotate a key left by entire words
    while (rol >= 32) {
        uint32_t temp = key[3];
        key[3] = key[2];
        key[2] = key[1];
        key[1] = key[0];
        key[0] = temp;
        rol -= 32;
    }

    // Rotate a key left by less than a word
    if (!rol) return;
    uint32_t temp = key[3];
    key[3] = (key[3] << rol) | (key[2] >> (32 - rol));
    key[2] = (key[2] << rol) | (key[1] >> (32 - rol));
    key[1] = (key[1] << rol) | (key[0] >> (32 - rol));
    key[0] = (key[0] << rol) | (temp >> (32 - rol));
}

void Aes::XorKey(uint32_t *dst, uint32_t *src) {
    // Bitwise exclusive or one key with another
    for (int i = 0; i < 4; i++)
        dst[i] ^= src[i];
}

void Aes::AddKey(uint32_t *dst, uint32_t *src) {
    // Add one key to another
    bool over = false;
    for (int i = 0; i < 4; i++) {
        uint32_t res = dst[i] + src[i] + over;
        over = (res < dst[i] || (res == dst[i] && over));
        dst[i] = res;
    }
}

void Aes::CryptBlock(uint32_t *src, uint32_t *dst) {
    // Set initial values for encryption/decryption
    uint32_t y0 = m_rKey[0] ^ src[0];
    uint32_t y1 = m_rKey[1] ^ src[1];
    uint32_t y2 = m_rKey[2] ^ src[2];
    uint32_t y3 = m_rKey[3] ^ src[3];

    // Encrypt/decrypt a block of data
    for (int i = 4; i < 40; i += 4) {
        uint32_t x0 = m_rKey[i + 0] ^ Scatter32(m_fTable, y1, y2, y3, y0);
        uint32_t x1 = m_rKey[i + 1] ^ Scatter32(m_fTable, y2, y3, y0, y1);
        uint32_t x2 = m_rKey[i + 2] ^ Scatter32(m_fTable, y3, y0, y1, y2);
        uint32_t x3 = m_rKey[i + 3] ^ Scatter32(m_fTable, y0, y1, y2, y3);
        y0 = x0; y1 = x1; y2 = x2; y3 = x3;
    }

    // Output the final encrypted/decrypted values
    dst[0] = m_rKey[40] ^ Scatter8(m_fsBox, y1, y2, y3, y0);
    dst[1] = m_rKey[41] ^ Scatter8(m_fsBox, y2, y3, y0, y1);
    dst[2] = m_rKey[42] ^ Scatter8(m_fsBox, y3, y0, y1, y2);
    dst[3] = m_rKey[43] ^ Scatter8(m_fsBox, y0, y1, y2, y3);
}

void Aes::InitFifo() {
    // Get the current AES mode and reload block counters
    uint8_t mode = (m_aesCnt >> 28) & 0x3;
    m_curBlock = static_cast<uint16_t>((m_aesBlkcnt >> 16));
    m_curExtra = (mode < 2) ? (m_aesBlkcnt & 0xFFFF) : 0;
    m_aesCnt &= ~BIT(21); // CCM verify
    LOG("AES FIFO starting in mode %d\n", mode);

    // Initialize the ring key values
    // `y` porte la dernière valeur d'un tour au suivant : déclaré DANS la
    // boucle (comme à l'origine), sa lecture au tour suivant était indéfinie.
    uint32_t y = 0;
    for (int i = 0, x = 1; i < 44; i += 4) {
        for (int j = 3; j >= 0; j--)
            m_rKey[i + j] = y = (i ? (y ^ m_rKey[i + j - 4]) : m_keys[m_curKey][j]);
        y = Scatter8(m_fsBox, y, y, y, y);
        y = ROL32(y, 8) ^ static_cast<uint32_t>((x << 24));
        x = (x << 1) ^ ((x & 0x80) ? 0x11B : 0);
    }

    // Initialize encryption/decryption based on the mode
    switch (mode) {
    case 0: case 1: // CCM
        m_ctr[0] = m_cbc[0] = (m_aesIv[0] << 24);
        m_ctr[1] = m_cbc[1] = (m_aesIv[1] << 24) | (m_aesIv[0] >> 8);
        m_ctr[2] = m_cbc[2] = (m_aesIv[2] << 24) | (m_aesIv[1] >> 8);
        m_ctr[3] = m_cbc[3] = (0x2 << 24) | (m_aesIv[2] >> 8);
        m_cbc[0] |= (m_curBlock << 4);
        m_cbc[3] |= ((m_curExtra > 0) << 30) | ((m_aesCnt << 11) & 0x38000000);
        CryptBlock(m_cbc, m_cbc);
        return;

    default: // CTR
        memcpy(m_ctr, m_aesIv, sizeof(m_ctr));
        return;
    }
}

void Aes::TriggerFifo() {
    // Schedule a FIFO update if one hasn't been already
    if (m_scheduled) return;
    m_core->Schedule(Task(&m_updateTask, 1));
    m_scheduled = true;
}

void Aes::Update() {
    // Process FIFO blocks when active and available
    while ((m_aesCnt & BIT(31)) && m_writeFifo.size() >= 4 && m_readFifo.size() <= 12) {
        // Receive an input block from the write FIFO
        uint32_t src[4], dst[4];
        for (int i = 0; i < 4; i++) {
            src[i] = m_writeFifo.front();
            m_writeFifo.pop_front();
        }

        // Process a CCM extra block and send it to the read FIFO if enabled
        if (m_curExtra) {
            for (int i = 0; i < 4; i++)
                m_cbc[i] ^= src[i];
            CryptBlock(m_cbc, m_cbc);
            if (m_aesCnt & BIT(19))
                for (int i = 0; i < 4; i++)
                    m_readFifo.push_back(src[i]);
            m_curExtra--;
            continue;
        }

        // Handle the block based on the selected mode
        uint8_t mode = (m_aesCnt >> 28) & 0x3;
        switch (mode) {
        case 0: // CCM decrypt
            for (int i = 0, c = 1; i < 4; i++)
                m_ctr[i] += static_cast<uint32_t>(c), c &= (m_ctr[i] == 0);
            CryptBlock(m_ctr, dst);
            for (int i = 0; i < 4; i++)
                m_cbc[i] ^= (dst[i] ^= src[i]);
            CryptBlock(m_cbc, m_cbc);
            break;

        case 1: // CCM encrypt
            for (int i = 0, c = 1; i < 4; i++)
                m_ctr[i] += static_cast<uint32_t>(c), c &= (m_ctr[i] == 0);
            CryptBlock(m_ctr, dst);
            for (int i = 0; i < 4; i++)
                m_cbc[i] ^= src[i], dst[i] ^= src[i];
            CryptBlock(m_cbc, m_cbc);
            break;

        default: // CTR
            CryptBlock(m_ctr, dst);
            for (int i = 0, c = 1; i < 4; i++) {
                m_ctr[i] += static_cast<uint32_t>(c);
                c &= (m_ctr[i] == 0);
                dst[i] ^= src[i];
            }
            break;
        }

        // Send an output block to the read FIFO
        for (int i = 0; i < 4; i++)
            m_readFifo.push_back(dst[i]);

        // Disable the FIFO once all blocks are processed and trigger an interrupt if enabled
        if (--m_curBlock > 0) continue;
        m_aesCnt &= ~BIT(31);
        if (m_aesCnt & BIT(30))
            m_core->interpreter[1].SendInterrupt(44);
        LOG("AES FIFO finished processing\n");

        // Calculate a CCM MAC for applicable modes
        if (mode > 1) continue;
        m_ctr[0] &= ~0xFFFFFFu;
        CryptBlock(m_ctr, m_ctr);
        for (int i = 0; i < 4; i++)
            m_cbc[i] ^= m_ctr[i];

        // Create a mask based on the MAC length
        uint32_t mask[4] = {};
        uint8_t len = static_cast<uint8_t>(std::max(4U, ((m_aesCnt >> 15) & 0xE) + 2));
        for (int i = 0; i < len; i += 2)
            mask[i / 4] |= 0xFFFF << ((i << 3) & 0x10);

        // Handle final verification steps for CCM modes
        if (mode == 1) { // Encrypt
            // Append the MAC to the output data (possibly overflowing the FIFO, but that's fine)
            for (int i = 0; i < 4; i++)
                m_readFifo.push_back(m_cbc[i] & mask[i]);
        }
        else if (~m_aesCnt & BIT(20)) {
            // Catch unhandled verification using the write FIFO
            LOG("Unhandled AES-CCM MAC verification source used: FIFO\n");
        }
        else { // Decrypt
            // Verify decryption by comparing the MAC with one provided via registers
            bool fail = false;
            for (int i = 0; i < 4; i++)
                if ((fail = (m_cbc[i] ^ m_aesMac[i]) & mask[i])) break;
            m_aesCnt |= (!fail << 21);
        }
    }

    // Update the AES FIFO sizes
    m_aesCnt = static_cast<uint32_t>((m_aesCnt & ~0x3FFu) | (std::min<uint8_t>(16, static_cast<unsigned char>(m_readFifo.size())) << 5) | m_writeFifo.size());
    m_scheduled = false;

    // NOTE: upstream sets/clears NDMA DRQs 0xA/0xB here to auto-feed the AES
    // FIFOs via DMA. This fork never ported NDMA (it's DSi-only, and DSi
    // mode is never enabled here — see core->dsiMode), so there's no NDMA
    // channel to signal. Left unimplemented rather than faking a stub.
}

uint32_t Aes::ReadRdfifo() {
    // Pop a value from the read FIFO
    if (m_readFifo.empty()) return m_aesRdfifo;
    m_aesRdfifo = m_readFifo.front();
    m_readFifo.pop_front();
    TriggerFifo();
    return m_aesRdfifo;
}

void Aes::WriteCnt(uint32_t mask, uint32_t value) {
    // Write to the AES_CNT register
    uint32_t mask2 = (mask & 0xFC1FF000);
    bool start = (value & mask2 & ~m_aesCnt & BIT(31));
    m_aesCnt = (m_aesCnt & ~mask2) | (value & mask2);

    // Handle write-only bits that trigger events
    if (value & mask & BIT(10)) m_writeFifo = {}; // Empty write FIFO
    if (value & mask & BIT(11)) m_readFifo = {}; // Empty read FIFO
    if (value & mask & BIT(24)) m_curKey = (m_aesCnt >> 26) & 0x3; // Apply key slot

    // Start processing a new set of FIFO blocks if triggered
    if (!start) return;
    InitFifo();
    TriggerFifo();
}

void Aes::WriteBlkcnt(uint32_t mask, uint32_t value) {
    // Write to the AES_BLKCNT register
    m_aesBlkcnt = (m_aesBlkcnt & ~mask) | (value & mask);
}

void Aes::WriteWrfifo(uint32_t mask, uint32_t value) {
    // Push a value to the write FIFO
    if (m_writeFifo.size() == 16) return;
    m_writeFifo.push_back(value & mask);
    TriggerFifo();
}

void Aes::WriteIv(int i, uint32_t mask, uint32_t value) {
    // Write to part of the AES_IV value
    m_aesIv[i] = (m_aesIv[i] & ~mask) | (value & mask);
}

void Aes::WriteMac(int i, uint32_t mask, uint32_t value) {
    // Write to part of the AES_MAC value
    m_aesMac[i] = (m_aesMac[i] & ~mask) | (value & mask);
}

void Aes::WriteKey(int i, int j, uint32_t mask, uint32_t value) {
    // Write to part of one of the AES key slots
    m_keys[i][j] = (m_keys[i][j] & ~mask) | (value & mask);
}

void Aes::WriteKeyx(int i, int j, uint32_t mask, uint32_t value) {
    // Write to part of one of the AES key X slots
    m_keysX[i][j] = (m_keys[i][j] & ~mask) | (value & mask);
}

void Aes::WriteKeyy(int i, int j, uint32_t mask, uint32_t value) {
    // Write to part of one of the AES key Y slots
    m_keysY[i][j] = (m_keys[i][j] & ~mask) | (value & mask);

    // Generate a key using X and Y when the last word is written to
    if (j < 3) return;
    memcpy(m_keys[i], m_keysX[i], sizeof(m_keys[0]));
    uint32_t seed[4] = { 0x1A4F3E79, 0x2A680F5F, 0x29590258, 0xFFFEFB4E };
    XorKey(m_keys[i], m_keysY[i]);
    AddKey(m_keys[i], seed);
    RolKey(m_keys[i], 42);
}

} // namespace consoles::arm