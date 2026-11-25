#include "consoles/arm/io/div_sqrt.hpp"

#include "consoles/arm/core.hpp"

#include <cmath>

namespace consoles::arm {


void DivSqrt::Divide() {
    if (m_divDenom == 0)
        m_divCnt |= BIT(14);
    else
        m_divCnt = static_cast<uint16_t>(m_divCnt & ~BIT(14));

    switch (m_divCnt & 0x0003) {
        case 0: {
            if ((int32_t)m_divNumer == INT32_MIN && (int32_t)m_divDenom == -1) {
                m_divResult    = static_cast<int64_t>(static_cast<unsigned long long>(int64_t((int32_t)m_divNumer)) ^ (0xFFFFFFFFULL << 32));
                m_divRemResult = 0;
            } else if ((int32_t)m_divDenom != 0) {
                m_divResult    = (int32_t)m_divNumer / (int32_t)m_divDenom;
                m_divRemResult = (int32_t)m_divNumer % (int32_t)m_divDenom;
            } else {
                m_divResult    = static_cast<int64_t>(static_cast<unsigned long long>((((int32_t)m_divNumer < 0) ? 1 : -1)) ^ (0xFFFFFFFFULL << 32));
                m_divRemResult = (int32_t)m_divNumer;
            }
            break;
        }
        case 1:
        case 3: {
            if (m_divNumer == INT64_MIN && (int32_t)m_divDenom == -1) {
                m_divResult    = m_divNumer;
                m_divRemResult = 0;
            } else if ((int32_t)m_divDenom != 0) {
                m_divResult    = m_divNumer / (int32_t)m_divDenom;
                m_divRemResult = m_divNumer % (int32_t)m_divDenom;
            } else {
                m_divResult    = (m_divNumer < 0) ? 1 : -1;
                m_divRemResult = m_divNumer;
            }
            break;
        }
        case 2: {
            if (m_divNumer == INT64_MIN && m_divDenom == -1) {
                m_divResult    = m_divNumer;
                m_divRemResult = 0;
            } else if (m_divDenom != 0) {
                m_divResult    = m_divNumer / m_divDenom;
                m_divRemResult = m_divNumer % m_divDenom;
            } else {
                m_divResult    = (m_divNumer < 0) ? 1 : -1;
                m_divRemResult = m_divNumer;
            }
            break;
        }
    }
}

void DivSqrt::SquareRoot() {
    switch (m_sqrtCnt & 0x0001) {
        case 0:
            m_sqrtResult = static_cast<uint32_t>(sqrt((uint32_t)m_sqrtParam));
            break;
        case 1:
            m_sqrtResult = static_cast<uint32_t>(sqrtl(m_sqrtParam));
            break;
    }
}

void DivSqrt::WriteDivCnt(uint16_t mask, uint16_t value) {
    mask &= 0x0003;
    m_divCnt = (m_divCnt & ~mask) | (value & mask);
    Divide();
}

void DivSqrt::WriteDivNumerL(uint32_t mask, uint32_t value) {
    m_divNumer = static_cast<int64_t>((static_cast<size_t>(m_divNumer) & ~((uint64_t)mask)) | (value & mask));
    Divide();
}

void DivSqrt::WriteDivNumerH(uint32_t mask, uint32_t value) {
    m_divNumer = static_cast<int64_t>((static_cast<size_t>(m_divNumer) & ~((uint64_t)mask << 32)) | ((uint64_t)(value & mask) << 32));
    Divide();
}

void DivSqrt::WriteDivDenomL(uint32_t mask, uint32_t value) {
    m_divDenom = static_cast<int64_t>((static_cast<size_t>(m_divDenom) & ~((uint64_t)mask)) | (value & mask));
    Divide();
}

void DivSqrt::WriteDivDenomH(uint32_t mask, uint32_t value) {
    m_divDenom = static_cast<int64_t>((static_cast<size_t>(m_divDenom) & ~((uint64_t)mask << 32)) | ((uint64_t)(value & mask) << 32));
    Divide();
}

void DivSqrt::WriteSqrtCnt(uint16_t mask, uint16_t value) {
    mask &= 0x0001;
    m_sqrtCnt = (m_sqrtCnt & ~mask) | (value & mask);
    SquareRoot();
}

void DivSqrt::WriteSqrtParamL(uint32_t mask, uint32_t value) {
    m_sqrtParam = (m_sqrtParam & ~((uint64_t)mask)) | (value & mask);
    SquareRoot();
}

void DivSqrt::WriteSqrtParamH(uint32_t mask, uint32_t value) {
    m_sqrtParam = (m_sqrtParam & ~((uint64_t)mask << 32)) | ((uint64_t)(value & mask) << 32);
    SquareRoot();
}

} // namespace consoles::arm

