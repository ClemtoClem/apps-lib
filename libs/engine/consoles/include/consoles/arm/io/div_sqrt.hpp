
#pragma once
#include <cstdint>
#include <memory>
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {


class Core;
class DivSqrt {
  public:
    DivSqrt(Core *core_) : m_core(core_) {
    }
    uint16_t ReadDivCnt() {
        return m_divCnt;
    }
    uint32_t ReadDivNumerL() {
        return static_cast<uint32_t>(m_divNumer);
    }
    uint32_t ReadDivNumerH() {
        return static_cast<uint32_t>(m_divNumer >> 32);
    }
    uint32_t ReadDivDenomL() {
        return static_cast<uint32_t>(m_divDenom);
    }
    uint32_t ReadDivDenomH() {
        return static_cast<uint32_t>(m_divDenom >> 32);
    }
    uint32_t ReadDivResultL() {
        return static_cast<uint32_t>(m_divResult);
    }
    uint32_t ReadDivResultH() {
        return static_cast<uint32_t>(m_divResult >> 32);
    }
    uint32_t ReadDivRemResultL() {
        return static_cast<uint32_t>(m_divRemResult);
    }
    uint32_t ReadDivRemResultH() {
        return static_cast<uint32_t>(m_divRemResult >> 32);
    }
    uint16_t ReadSqrtCnt() {
        return m_sqrtCnt;
    }
    uint32_t ReadSqrtResult() {
        return m_sqrtResult;
    }
    uint32_t ReadSqrtParamL() {
        return static_cast<uint32_t>(m_sqrtParam);
    }
    uint32_t ReadSqrtParamH() {
        return static_cast<uint32_t>(m_sqrtParam >> 32);
    }
    void WriteDivCnt(uint16_t mask, uint16_t value);
    void WriteDivNumerL(uint32_t mask, uint32_t value);
    void WriteDivNumerH(uint32_t mask, uint32_t value);
    void WriteDivDenomL(uint32_t mask, uint32_t value);
    void WriteDivDenomH(uint32_t mask, uint32_t value);
    void WriteSqrtCnt(uint16_t mask, uint16_t value);
    void WriteSqrtParamL(uint32_t mask, uint32_t value);
    void WriteSqrtParamH(uint32_t mask, uint32_t value);

    void IoState(StateArchive &archive) {
        archive.Io(m_divCnt);
        archive.Io(m_divNumer);
        archive.Io(m_divDenom);
        archive.Io(m_divResult);
        archive.Io(m_divRemResult);
        archive.Io(m_sqrtCnt);
        archive.Io(m_sqrtResult);
        archive.Io(m_sqrtParam);
    }

  private:
    Core *m_core;
    uint16_t m_divCnt       = 0;
    int64_t  m_divNumer     = 0;
    int64_t  m_divDenom     = 0;
    int64_t  m_divResult    = 0;
    int64_t  m_divRemResult = 0;
    uint16_t m_sqrtCnt      = 0;
    uint32_t m_sqrtResult   = 0;
    uint64_t m_sqrtParam    = 0;
    void     Divide();
    void     SquareRoot();
};

} // namespace consoles::arm

