#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include "consoles/arm/defines.hpp"
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {

class Core;
class Bios;
class CPU {
  private:
    Core *m_core;
    bool  m_cpu;
    Bios *m_bios = nullptr;

    uint32_t  m_pipeline[2]      = {};
    uint32_t *m_registers[16]    = {};
    uint32_t  m_registersUsr[16] = {};
    uint32_t  m_registersFiq[7]  = {};
    uint32_t  m_registersSvc[2]  = {};
    uint32_t  m_registersAbt[2]  = {};
    uint32_t  m_registersIrq[2]  = {};
    uint32_t  m_registersUnd[2]  = {};

    uint32_t m_cpsr = 0, *m_spsr = nullptr;
    uint32_t m_spsrFiq = 0, m_spsrSvc = 0, m_spsrAbt = 0, m_spsrIrq = 0, m_spsrUnd = 0;
    uint8_t  m_halted = 0;
    uint8_t  m_ime    = 0;
    uint32_t m_ie = 0, m_irf = 0;
    uint8_t  m_postFlg = 0;

  public:
    CPU(Core *core, bool cpu);

    void Init();
    void DirectBoot();
    /// GBA : état laissé par le BIOS au saut vers la cartouche (piles par mode, mode système, PC = 0x08000000).
    void DirectBootGba();
    int  RunOpcode();

    void Halt(int bit) {
        m_halted |= BIT(bit);
    }
    void Unhalt(int bit) {
        m_halted = static_cast<uint8_t>(m_halted & ~BIT(bit));
    }
    void SendInterrupt(int bit);
    bool ShouldRun() {
        return !m_halted;
    }
    bool IsThumb() {
        return m_cpsr & BIT(5);
    }
    uint32_t GetPC() {
        return *m_registers[15];
    }
    /// Registre visible dans le mode courant (r15 : en avance de deux instructions, pipeline).
    [[nodiscard]] uint32_t GetRegister(int index) const {
        return *m_registers[index & 15];
    }
    [[nodiscard]] uint32_t GetCpsr() const {
        return m_cpsr;
    }
    [[nodiscard]] bool IsHalted() const {
        return m_halted != 0;
    }
    void SetBios(Bios *newBios) {
        this->m_bios = newBios;
    }
    int     HandleHleIrq();
    uint8_t ReadIme() {
        return m_ime;
    }
    uint32_t ReadIe() {
        return m_ie;
    }
    uint32_t ReadIrf() {
        return m_irf;
    }
    uint8_t ReadPostFlg() {
        return m_postFlg;
    }

    void WriteIme(uint8_t value);
    void WriteIe(uint32_t mask, uint32_t value);
    void WriteIrf(uint32_t mask, uint32_t value);
    void WritePostFlg(uint8_t value);

    /**
     * @brief Sérialise/désérialise l'état du CPU (registres, CPSR/SPSR, IRQ) pour les save states.
     * @param archive Archive de sauvegarde/chargement.
     */
    void IoState(StateArchive &archive);

  private:
    std::function<void()> m_interruptTask;

    void Interrupt();
    int  Exception(uint8_t vector);
    void FlushPipeline();
    void SetCpsr(uint32_t value, bool save = false);
    int  HandleReserved(uint32_t opcode);
    int  FinishHleIrq();
    int  UnkArm(uint32_t opcode);
    int  UnkThumb(uint16_t opcode);

    /** @name */
    ///@{
    uint32_t Lli(uint32_t opcode);
    uint32_t Llr(uint32_t opcode);
    uint32_t Lri(uint32_t opcode);
    uint32_t Lrr(uint32_t opcode);
    uint32_t Ari(uint32_t opcode);
    uint32_t Arr(uint32_t opcode);
    uint32_t Rri(uint32_t opcode);
    uint32_t Rrr(uint32_t opcode);
    uint32_t Imm(uint32_t opcode);
    uint32_t LliS(uint32_t opcode);
    uint32_t LlrS(uint32_t opcode);
    uint32_t LriS(uint32_t opcode);
    uint32_t LrrS(uint32_t opcode);
    uint32_t AriS(uint32_t opcode);
    uint32_t ArrS(uint32_t opcode);
    uint32_t RriS(uint32_t opcode);
    uint32_t RrrS(uint32_t opcode);
    uint32_t ImmS(uint32_t opcode);
    ///@}

    /** @name ALU Operations */
    ///@{
    int And(uint32_t opcode, uint32_t op2);    ///< AND{cond}{S}   Rd,Rn,Op2   Nzc-    Rd = Rn AND Op2
    int Eor(uint32_t opcode, uint32_t op2);     ///< EOR{cond}{S}   Rd,Rn,Op2   NZc-    Rd = Rn XOR Op2
    int Sub(uint32_t opcode, uint32_t op2);     ///< SUB{cond}{S}   Rd,Rn,Op2   NZcV    Rd = Rn-Op2
    int Rsb(uint32_t opcode, uint32_t op2);     ///< RSB{cond}{S}   Rd,Rn,Op2   NZcV    Rd = Op2-Rn
    int Add(uint32_t opcode, uint32_t op2);     ///< ADD{cond}{S}   Rd,Rn,Op2   NZcV    Rd = Rn+Op2
    int Adc(uint32_t opcode, uint32_t op2);     ///< ADC{cond}{S}   Rd,Rn,Op2   NZcV    Rd = Rn+Op2+Cy
    int Sbc(uint32_t opcode, uint32_t op2);     ///< SBC{cond}{S}   Rd,Rn,Op2   NZcV    Rd = Rn-Op2+Cy-1
    int Rsc(uint32_t opcode, uint32_t op2);     ///< RSC{cond}{S}   Rd,Rn,Op2   NZcV    Rd = Op2-Rn+Cy-1
    int Tst(uint32_t opcode, uint32_t op2);     ///< TST{cond}{P}      Rn,Op2   NZc-    Void = Rn AND Op2
    int Teq(uint32_t opcode, uint32_t op2);     ///< TEQ{cond}{P}      Rn,Op2   NZc-    Void = Rn XOR Op2
    int Cmp(uint32_t opcode, uint32_t op2);     ///< CMP{cond}{P}      Rn,Op2   NZcV    Void = Rn-Op2
    int Cmn(uint32_t opcode, uint32_t op2);     ///< CMN{cond}{P}      Rn,Op2   NZcV    Void = Rn+Op2
    int Orr(uint32_t opcode, uint32_t op2);
    int Mov(uint32_t opcode, uint32_t op2);     ///< MOV{cond}{S}   Rd,Op2      NZc-    Rd = Op2
    int Bic(uint32_t opcode, uint32_t op2);     ///< BIC{cond}{S}   Rd,Rn,Op2   NZc-    Rd = Rn AND NOT Op2
    int Mvn(uint32_t opcode, uint32_t op2);     ///< MVN{cond}{S}   Rd,Op2      NZc-    Rd = NOT Op2
    int Ands(uint32_t opcode, uint32_t op2);
    int Eors(uint32_t opcode, uint32_t op2);
    int Subs(uint32_t opcode, uint32_t op2);
    int Rsbs(uint32_t opcode, uint32_t op2);
    int Adds(uint32_t opcode, uint32_t op2);
    int Adcs(uint32_t opcode, uint32_t op2);
    int Sbcs(uint32_t opcode, uint32_t op2);
    int Rscs(uint32_t opcode, uint32_t op2);
    int Orrs(uint32_t opcode, uint32_t op2);    ///< ORR{cond}{S}   Rd,Rn,Op2     NZc-  Rd = Rn OR Op2
    int Movs(uint32_t opcode, uint32_t op2);
    int Bics(uint32_t opcode, uint32_t op2);
    int Mvns(uint32_t opcode, uint32_t op2);
    ///@}

    /** @name */
    ///@{
    int AndLli(uint32_t opcode);
    int AndLlr(uint32_t opcode);
    int AndLri(uint32_t opcode);
    int AndLrr(uint32_t opcode);
    int AndAri(uint32_t opcode);
    int AndArr(uint32_t opcode);
    int AndRri(uint32_t opcode);
    int AndRrr(uint32_t opcode);
    int AndImm(uint32_t opcode);
    int AndsLli(uint32_t opcode);
    int AndsLlr(uint32_t opcode);
    int AndsLri(uint32_t opcode);
    int AndsLrr(uint32_t opcode);
    int AndsAri(uint32_t opcode);
    int AndsArr(uint32_t opcode);
    int AndsRri(uint32_t opcode);
    int AndsRrr(uint32_t opcode);
    int AndsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int EorLli(uint32_t opcode);
    int EorLlr(uint32_t opcode);
    int EorLri(uint32_t opcode);
    int EorLrr(uint32_t opcode);
    int EorAri(uint32_t opcode);
    int EorArr(uint32_t opcode);
    int EorRri(uint32_t opcode);
    int EorRrr(uint32_t opcode);
    int EorImm(uint32_t opcode);
    int EorsLli(uint32_t opcode);
    int EorsLlr(uint32_t opcode);
    int EorsLri(uint32_t opcode);
    int EorsLrr(uint32_t opcode);
    int EorsAri(uint32_t opcode);
    int EorsArr(uint32_t opcode);
    int EorsRri(uint32_t opcode);
    int EorsRrr(uint32_t opcode);
    int EorsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int SubLli(uint32_t opcode);
    int SubLlr(uint32_t opcode);
    int SubLri(uint32_t opcode);
    int SubLrr(uint32_t opcode);
    int SubAri(uint32_t opcode);
    int SubArr(uint32_t opcode);
    int SubRri(uint32_t opcode);
    int SubRrr(uint32_t opcode);
    int SubImm(uint32_t opcode);
    int SubsLli(uint32_t opcode);
    int SubsLlr(uint32_t opcode);
    int SubsLri(uint32_t opcode);
    int SubsLrr(uint32_t opcode);
    int SubsAri(uint32_t opcode);
    int SubsArr(uint32_t opcode);
    int SubsRri(uint32_t opcode);
    int SubsRrr(uint32_t opcode);
    int SubsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int RsbLli(uint32_t opcode);
    int RsbLlr(uint32_t opcode);
    int RsbLri(uint32_t opcode);
    int RsbLrr(uint32_t opcode);
    int RsbAri(uint32_t opcode);
    int RsbArr(uint32_t opcode);
    int RsbRri(uint32_t opcode);
    int RsbRrr(uint32_t opcode);
    int RsbImm(uint32_t opcode);
    int RsbsLli(uint32_t opcode);
    int RsbsLlr(uint32_t opcode);
    int RsbsLri(uint32_t opcode);
    int RsbsLrr(uint32_t opcode);
    int RsbsAri(uint32_t opcode);
    int RsbsArr(uint32_t opcode);
    int RsbsRri(uint32_t opcode);
    int RsbsRrr(uint32_t opcode);
    int RsbsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int AddLli(uint32_t opcode);
    int AddLlr(uint32_t opcode);
    int AddLri(uint32_t opcode);
    int AddLrr(uint32_t opcode);
    int AddAri(uint32_t opcode);
    int AddArr(uint32_t opcode);
    int AddRri(uint32_t opcode);
    int AddRrr(uint32_t opcode);
    int AddImm(uint32_t opcode);
    int AddsLli(uint32_t opcode);
    int AddsLlr(uint32_t opcode);
    int AddsLri(uint32_t opcode);
    int AddsLrr(uint32_t opcode);
    int AddsAri(uint32_t opcode);
    int AddsArr(uint32_t opcode);
    int AddsRri(uint32_t opcode);
    int AddsRrr(uint32_t opcode);
    int AddsImm(uint32_t opcode);
    int AdcLli(uint32_t opcode);
    int AdcLlr(uint32_t opcode);
    int AdcLri(uint32_t opcode);
    int AdcLrr(uint32_t opcode);
    int AdcAri(uint32_t opcode);
    int AdcArr(uint32_t opcode);
    int AdcRri(uint32_t opcode);
    int AdcRrr(uint32_t opcode);
    int AdcImm(uint32_t opcode);
    int AdcsLli(uint32_t opcode);
    int AdcsLlr(uint32_t opcode);
    int AdcsLri(uint32_t opcode);
    int AdcsLrr(uint32_t opcode);
    int AdcsAri(uint32_t opcode);
    int AdcsArr(uint32_t opcode);
    int AdcsRri(uint32_t opcode);
    int AdcsRrr(uint32_t opcode);
    int AdcsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int SbcLli(uint32_t opcode);
    int SbcLlr(uint32_t opcode);
    int SbcLri(uint32_t opcode);
    int SbcLrr(uint32_t opcode);
    int SbcAri(uint32_t opcode);
    int SbcArr(uint32_t opcode);
    int SbcRri(uint32_t opcode);
    int SbcRrr(uint32_t opcode);
    int SbcImm(uint32_t opcode);
    int SbcsLli(uint32_t opcode);
    int SbcsLlr(uint32_t opcode);
    int SbcsLri(uint32_t opcode);
    int SbcsLrr(uint32_t opcode);
    int SbcsAri(uint32_t opcode);
    int SbcsArr(uint32_t opcode);
    int SbcsRri(uint32_t opcode);
    int SbcsRrr(uint32_t opcode);
    int SbcsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int RscLli(uint32_t opcode);
    int RscLlr(uint32_t opcode);
    int RscLri(uint32_t opcode);
    int RscLrr(uint32_t opcode);
    int RscAri(uint32_t opcode);
    int RscArr(uint32_t opcode);
    int RscRri(uint32_t opcode);
    int RscRrr(uint32_t opcode);
    int RscImm(uint32_t opcode);
    int RscsLli(uint32_t opcode);
    int RscsLlr(uint32_t opcode);
    int RscsLri(uint32_t opcode);
    int RscsLrr(uint32_t opcode);
    int RscsAri(uint32_t opcode);
    int RscsArr(uint32_t opcode);
    int RscsRri(uint32_t opcode);
    int RscsRrr(uint32_t opcode);
    int RscsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int TstLli(uint32_t opcode);
    int TstLlr(uint32_t opcode);
    int TstLri(uint32_t opcode);
    int TstLrr(uint32_t opcode);
    int TstAri(uint32_t opcode);
    int TstArr(uint32_t opcode);
    int TstRri(uint32_t opcode);
    int TstRrr(uint32_t opcode);
    int TstImm(uint32_t opcode);
    int TeqLli(uint32_t opcode);
    int TeqLlr(uint32_t opcode);
    int TeqLri(uint32_t opcode);
    int TeqLrr(uint32_t opcode);
    int TeqAri(uint32_t opcode);
    int TeqArr(uint32_t opcode);
    int TeqRri(uint32_t opcode);
    int TeqRrr(uint32_t opcode);
    int TeqImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int CmpLli(uint32_t opcode);
    int CmpLlr(uint32_t opcode);
    int CmpLri(uint32_t opcode);
    int CmpLrr(uint32_t opcode);
    int CmpAri(uint32_t opcode);
    int CmpArr(uint32_t opcode);
    int CmpRri(uint32_t opcode);
    int CmpRrr(uint32_t opcode);
    int CmpImm(uint32_t opcode);
    int CmnLli(uint32_t opcode);
    int CmnLlr(uint32_t opcode);
    int CmnLri(uint32_t opcode);
    int CmnLrr(uint32_t opcode);
    int CmnAri(uint32_t opcode);
    int CmnArr(uint32_t opcode);
    int CmnRri(uint32_t opcode);
    int CmnRrr(uint32_t opcode);
    int CmnImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int OrrLli(uint32_t opcode);
    int OrrLlr(uint32_t opcode);
    int OrrLri(uint32_t opcode);
    int OrrLrr(uint32_t opcode);
    int OrrAri(uint32_t opcode);
    int OrrArr(uint32_t opcode);
    int OrrRri(uint32_t opcode);
    int OrrRrr(uint32_t opcode);
    int OrrImm(uint32_t opcode);
    int OrrsLli(uint32_t opcode);
    int OrrsLlr(uint32_t opcode);
    int OrrsLri(uint32_t opcode);
    int OrrsLrr(uint32_t opcode);
    int OrrsAri(uint32_t opcode);
    int OrrsArr(uint32_t opcode);
    int OrrsRri(uint32_t opcode);
    int OrrsRrr(uint32_t opcode);
    int OrrsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int MovLli(uint32_t opcode);
    int MovLlr(uint32_t opcode);
    int MovLri(uint32_t opcode);
    int MovLrr(uint32_t opcode);
    int MovAri(uint32_t opcode);
    int MovArr(uint32_t opcode);
    int MovRri(uint32_t opcode);
    int MovRrr(uint32_t opcode);
    int MovImm(uint32_t opcode);
    int MovsLli(uint32_t opcode);
    int MovsLlr(uint32_t opcode);
    int MovsLri(uint32_t opcode);
    int MovsLrr(uint32_t opcode);
    int MovsAri(uint32_t opcode);
    int MovsArr(uint32_t opcode);
    int MovsRri(uint32_t opcode);
    int MovsRrr(uint32_t opcode);
    int MovsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int BicLli(uint32_t opcode);
    int BicLlr(uint32_t opcode);
    int BicLri(uint32_t opcode);
    int BicLrr(uint32_t opcode);
    int BicAri(uint32_t opcode);
    int BicArr(uint32_t opcode);
    int BicRri(uint32_t opcode);
    int BicRrr(uint32_t opcode);
    int BicImm(uint32_t opcode);
    int BicsLli(uint32_t opcode);
    int BicsLlr(uint32_t opcode);
    int BicsLri(uint32_t opcode);
    int BicsLrr(uint32_t opcode);
    int BicsAri(uint32_t opcode);
    int BicsArr(uint32_t opcode);
    int BicsRri(uint32_t opcode);
    int BicsRrr(uint32_t opcode);
    int BicsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int MvnLli(uint32_t opcode);
    int MvnLlr(uint32_t opcode);
    int MvnLri(uint32_t opcode);
    int MvnLrr(uint32_t opcode);
    int MvnAri(uint32_t opcode);
    int MvnArr(uint32_t opcode);
    int MvnRri(uint32_t opcode);
    int MvnRrr(uint32_t opcode);
    int MvnImm(uint32_t opcode);
    int MvnsLli(uint32_t opcode);
    int MvnsLlr(uint32_t opcode);
    int MvnsLri(uint32_t opcode);
    int MvnsLrr(uint32_t opcode);
    int MvnsAri(uint32_t opcode);
    int MvnsArr(uint32_t opcode);
    int MvnsRri(uint32_t opcode);
    int MvnsRrr(uint32_t opcode);
    int MvnsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int Mul(uint32_t opcode);       ///< MUL{cond}{S}   Rd,Rm,Rs        NZx-    Rd = Rm*Rs
    int Mla(uint32_t opcode);       ///< MLA{cond}{S}   Rd,Rm,Rs,Rn     NZx-    Rd = Rm*Rs+Rn
    int Umull(uint32_t opcode);     ///< UMULL{cond}{S} RdLo,RdHi,Rm,R  NZx-    RdHiLo = Rm*Rs
    int Umlal(uint32_t opcode);     ///< UMLAL{cond}{S} RdLo,RdHi,Rm,Rs NZx-    RdHiLo = Rm*Rs+RdHiLo
    int Smull(uint32_t opcode);     ///< SMULL{cond}{S} RdLo,RdHi,Rm,Rs NZx-    RdHiLo = Rm*Rs
    int Smlal(uint32_t opcode);     ///< SMLAL{cond}{S} RdLo,RdHi,Rm,Rs NZx-    RdHiLo = Rm*Rs+RdHiLo
    int Muls(uint32_t opcode);
    int Mlas(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int Umulls(uint32_t opcode);
    int Umlals(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int Smulls(uint32_t opcode);
    int Smlals(uint32_t opcode);
    int Smulbb(uint32_t opcode);
    int Smulbt(uint32_t opcode);
    int Smultb(uint32_t opcode);
    int Smultt(uint32_t opcode);
    int Smulwb(uint32_t opcode);
    int Smulwt(uint32_t opcode);
    int Smlabb(uint32_t opcode);
    int Smlabt(uint32_t opcode);
    int Smlatb(uint32_t opcode);
    int Smlatt(uint32_t opcode);
    int Smlawb(uint32_t opcode);
    int Smlawt(uint32_t opcode);
    int Smlalbb(uint32_t opcode);
    int Smlalbt(uint32_t opcode);
    int Smlaltb(uint32_t opcode);
    int Smlaltt(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int Clz(uint32_t opcode);       ///< CLZ{cond}      Rd,Rm       ----    Count Leading Zeros
    int Qadd(uint32_t opcode);      ///< QADD{cond}     Rd,Rm,Rn    ----q   Rd=Rm+Rn
    int Qsub(uint32_t opcode);      ///< QSUB{cond}     Rd,Rm,Rn    ----q   Rd=Rm-Rn
    int Qdadd(uint32_t opcode);     ///< QDADD{cond}    Rd,Rm,Rn    ----q   Rd=Rm+Rn*2
    int Qdsub(uint32_t opcode);     ///< QDSUB{cond}    Rd,Rm,Rn    ----q   Rd=Rm-Rn*2
    ///@}

    /** @name */
    ///@{
    int AddRegT(uint16_t opcode);
    int SubRegT(uint16_t opcode);
    int AddHT(uint16_t opcode);
    int CmpHT(uint16_t opcode);
    int MovHT(uint16_t opcode);
    int AddPcT(uint16_t opcode);
    int AddSpT(uint16_t opcode);
    int AddSpImmT(uint16_t opcode);
    ///@}

    /** @name */
    ///@{
    int LslImmT(uint16_t opcode);
    int LsrImmT(uint16_t opcode);
    int AsrImmT(uint16_t opcode);
    int AddImm3T(uint16_t opcode);
    int SubImm3T(uint16_t opcode);
    int AddImm8T(uint16_t opcode);
    int SubImm8T(uint16_t opcode);
    int CmpImm8T(uint16_t opcode);
    int MovImm8T(uint16_t opcode);
    ///@}

    /** @name */
    ///@{
    int LslDpT(uint16_t opcode);
    int LsrDpT(uint16_t opcode);
    int AsrDpT(uint16_t opcode);
    int RorDpT(uint16_t opcode);
    int AndDpT(uint16_t opcode);
    int EorDpT(uint16_t opcode);
    int AdcDpT(uint16_t opcode);
    int SbcDpT(uint16_t opcode);
    int TstDpT(uint16_t opcode);
    int CmpDpT(uint16_t opcode);
    int CmnDpT(uint16_t opcode);
    int OrrDpT(uint16_t opcode);
    int BicDpT(uint16_t opcode);
    int MvnDpT(uint16_t opcode);
    int NegDpT(uint16_t opcode);
    int MulDpT(uint16_t opcode);
    ///@}

    /** @name */
    ///@{
    uint32_t Ip(uint32_t opcode);
    uint32_t IpH(uint32_t opcode);
    uint32_t Rp(uint32_t opcode);
    uint32_t Rpll(uint32_t opcode);
    uint32_t Rplr(uint32_t opcode);
    uint32_t Rpar(uint32_t opcode);
    uint32_t Rprr(uint32_t opcode);
    ///@}

    /** @name Memory Load/Store */
    ///@{
    int LdrsbOf(uint32_t opcode, uint32_t op2);
    int LdrshOf(uint32_t opcode, uint32_t op2);
    int LdrbOf(uint32_t opcode, uint32_t op2);
    int StrbOf(uint32_t opcode, uint32_t op2);
    int LdrhOf(uint32_t opcode, uint32_t op2);
    int StrhOf(uint32_t opcode, uint32_t op2);
    int LdrOf(uint32_t opcode, uint32_t op2);
    int StrOf(uint32_t opcode, uint32_t op2);
    int LdrdOf(uint32_t opcode, uint32_t op2);
    int StrdOf(uint32_t opcode, uint32_t op2);
    int LdrsbPr(uint32_t opcode, uint32_t op2);
    int LdrshPr(uint32_t opcode, uint32_t op2);
    int LdrbPr(uint32_t opcode, uint32_t op2);
    int StrbPr(uint32_t opcode, uint32_t op2);
    int LdrhPr(uint32_t opcode, uint32_t op2);
    int StrhPr(uint32_t opcode, uint32_t op2);
    int LdrPr(uint32_t opcode, uint32_t op2);
    int StrPr(uint32_t opcode, uint32_t op2);
    int LdrdPr(uint32_t opcode, uint32_t op2);
    int StrdPr(uint32_t opcode, uint32_t op2);
    int LdrsbPt(uint32_t opcode, uint32_t op2);
    int LdrshPt(uint32_t opcode, uint32_t op2);
    int LdrbPt(uint32_t opcode, uint32_t op2);
    int StrbPt(uint32_t opcode, uint32_t op2);
    int LdrhPt(uint32_t opcode, uint32_t op2);
    int StrhPt(uint32_t opcode, uint32_t op2);
    int LdrPt(uint32_t opcode, uint32_t op2);
    int StrPt(uint32_t opcode, uint32_t op2);
    int LdrdPt(uint32_t opcode, uint32_t op2);
    int StrdPt(uint32_t opcode, uint32_t op2);
    ///@}

    /** @name */
    ///@{
    int LdrsbOfrm(uint32_t opcode);
    int LdrsbOfim(uint32_t opcode);
    int LdrsbOfrp(uint32_t opcode);
    int LdrsbOfip(uint32_t opcode);
    int LdrsbPrrm(uint32_t opcode);
    int LdrsbPrim(uint32_t opcode);
    int LdrsbPrrp(uint32_t opcode);
    int LdrsbPrip(uint32_t opcode);
    int LdrsbPtrm(uint32_t opcode);
    int LdrsbPtim(uint32_t opcode);
    int LdrsbPtrp(uint32_t opcode);
    int LdrsbPtip(uint32_t opcode);
    int LdrshOfrm(uint32_t opcode);
    int LdrshOfim(uint32_t opcode);
    int LdrshOfrp(uint32_t opcode);
    int LdrshOfip(uint32_t opcode);
    int LdrshPrrm(uint32_t opcode);
    int LdrshPrim(uint32_t opcode);
    int LdrshPrrp(uint32_t opcode);
    int LdrshPrip(uint32_t opcode);
    int LdrshPtrm(uint32_t opcode);
    int LdrshPtim(uint32_t opcode);
    int LdrshPtrp(uint32_t opcode);
    int LdrshPtip(uint32_t opcode);
    int LdrbOfim(uint32_t opcode);
    int LdrbOfip(uint32_t opcode);
    int LdrbOfrmll(uint32_t opcode);
    int LdrbOfrmlr(uint32_t opcode);
    int LdrbOfrmar(uint32_t opcode);
    int LdrbOfrmrr(uint32_t opcode);
    int LdrbOfrpll(uint32_t opcode);
    int LdrbOfrplr(uint32_t opcode);
    int LdrbOfrpar(uint32_t opcode);
    int LdrbOfrprr(uint32_t opcode);
    int LdrbPrim(uint32_t opcode);
    int LdrbPrip(uint32_t opcode);
    int LdrbPrrmll(uint32_t opcode);
    int LdrbPrrmlr(uint32_t opcode);
    int LdrbPrrmar(uint32_t opcode);
    int LdrbPrrmrr(uint32_t opcode);
    int LdrbPrrpll(uint32_t opcode);
    int LdrbPrrplr(uint32_t opcode);
    int LdrbPrrpar(uint32_t opcode);
    int LdrbPrrprr(uint32_t opcode);
    int LdrbPtim(uint32_t opcode);
    int LdrbPtip(uint32_t opcode);
    int LdrbPtrmll(uint32_t opcode);
    int LdrbPtrmlr(uint32_t opcode);
    int LdrbPtrmar(uint32_t opcode);
    int LdrbPtrmrr(uint32_t opcode);
    int LdrbPtrpll(uint32_t opcode);
    int LdrbPtrplr(uint32_t opcode);
    int LdrbPtrpar(uint32_t opcode);
    int LdrbPtrprr(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int StrbOfim(uint32_t opcode);
    int StrbOfip(uint32_t opcode);
    int StrbOfrmll(uint32_t opcode);
    int StrbOfrmlr(uint32_t opcode);
    int StrbOfrmar(uint32_t opcode);
    int StrbOfrmrr(uint32_t opcode);
    int StrbOfrpll(uint32_t opcode);
    int StrbOfrplr(uint32_t opcode);
    int StrbOfrpar(uint32_t opcode);
    int StrbOfrprr(uint32_t opcode);
    int StrbPrim(uint32_t opcode);
    int StrbPrip(uint32_t opcode);
    int StrbPrrmll(uint32_t opcode);
    int StrbPrrmlr(uint32_t opcode);
    int StrbPrrmar(uint32_t opcode);
    int StrbPrrmrr(uint32_t opcode);
    int StrbPrrpll(uint32_t opcode);
    int StrbPrrplr(uint32_t opcode);
    int StrbPrrpar(uint32_t opcode);
    int StrbPrrprr(uint32_t opcode);
    int StrbPtim(uint32_t opcode);
    int StrbPtip(uint32_t opcode);
    int StrbPtrmll(uint32_t opcode);
    int StrbPtrmlr(uint32_t opcode);
    int StrbPtrmar(uint32_t opcode);
    int StrbPtrmrr(uint32_t opcode);
    int StrbPtrpll(uint32_t opcode);
    int StrbPtrplr(uint32_t opcode);
    int StrbPtrpar(uint32_t opcode);
    int StrbPtrprr(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int LdrhOfrm(uint32_t opcode);
    int LdrhOfim(uint32_t opcode);
    int LdrhOfrp(uint32_t opcode);
    int LdrhOfip(uint32_t opcode);
    int LdrhPrrm(uint32_t opcode);
    int LdrhPrim(uint32_t opcode);
    int LdrhPrrp(uint32_t opcode);
    int LdrhPrip(uint32_t opcode);
    int LdrhPtrm(uint32_t opcode);
    int LdrhPtim(uint32_t opcode);
    int LdrhPtrp(uint32_t opcode);
    int LdrhPtip(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int StrhOfrm(uint32_t opcode);
    int StrhOfim(uint32_t opcode);
    int StrhOfrp(uint32_t opcode);
    int StrhOfip(uint32_t opcode);
    int StrhPrrm(uint32_t opcode);
    int StrhPrim(uint32_t opcode);
    int StrhPrrp(uint32_t opcode);
    int StrhPrip(uint32_t opcode);
    int StrhPtrm(uint32_t opcode);
    int StrhPtim(uint32_t opcode);
    int StrhPtrp(uint32_t opcode);
    int StrhPtip(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int LdrOfim(uint32_t opcode);
    int LdrOfip(uint32_t opcode);
    int LdrOfrmll(uint32_t opcode);
    int LdrOfrmlr(uint32_t opcode);
    int LdrOfrmar(uint32_t opcode);
    int LdrOfrmrr(uint32_t opcode);
    int LdrOfrpll(uint32_t opcode);
    int LdrOfrplr(uint32_t opcode);
    int LdrOfrpar(uint32_t opcode);
    int LdrOfrprr(uint32_t opcode);
    int LdrPrim(uint32_t opcode);
    int LdrPrip(uint32_t opcode);
    int LdrPrrmll(uint32_t opcode);
    int LdrPrrmlr(uint32_t opcode);
    int LdrPrrmar(uint32_t opcode);
    int LdrPrrmrr(uint32_t opcode);
    int LdrPrrpll(uint32_t opcode);
    int LdrPrrplr(uint32_t opcode);
    int LdrPrrpar(uint32_t opcode);
    int LdrPrrprr(uint32_t opcode);
    int LdrPtim(uint32_t opcode);
    int LdrPtip(uint32_t opcode);
    int LdrPtrmll(uint32_t opcode);
    int LdrPtrmlr(uint32_t opcode);
    int LdrPtrmar(uint32_t opcode);
    int LdrPtrmrr(uint32_t opcode);
    int LdrPtrpll(uint32_t opcode);
    int LdrPtrplr(uint32_t opcode);
    int LdrPtrpar(uint32_t opcode);
    int LdrPtrprr(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int StrOfim(uint32_t opcode);
    int StrOfip(uint32_t opcode);
    int StrOfrmll(uint32_t opcode);
    int StrOfrmlr(uint32_t opcode);
    int StrOfrmar(uint32_t opcode);
    int StrOfrmrr(uint32_t opcode);
    int StrOfrpll(uint32_t opcode);
    int StrOfrplr(uint32_t opcode);
    int StrOfrpar(uint32_t opcode);
    int StrOfrprr(uint32_t opcode);
    int StrPrim(uint32_t opcode);
    int StrPrip(uint32_t opcode);
    int StrPrrmll(uint32_t opcode);
    int StrPrrmlr(uint32_t opcode);
    int StrPrrmar(uint32_t opcode);
    int StrPrrmrr(uint32_t opcode);
    int StrPrrpll(uint32_t opcode);
    int StrPrrplr(uint32_t opcode);
    int StrPrrpar(uint32_t opcode);
    int StrPrrprr(uint32_t opcode);
    int StrPtim(uint32_t opcode);
    int StrPtip(uint32_t opcode);
    int StrPtrmll(uint32_t opcode);
    int StrPtrmlr(uint32_t opcode);
    int StrPtrmar(uint32_t opcode);
    int StrPtrmrr(uint32_t opcode);
    int StrPtrpll(uint32_t opcode);
    int StrPtrplr(uint32_t opcode);
    int StrPtrpar(uint32_t opcode);
    int StrPtrprr(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int LdrdOfrm(uint32_t opcode);
    int LdrdOfim(uint32_t opcode);
    int LdrdOfrp(uint32_t opcode);
    int LdrdOfip(uint32_t opcode);
    int LdrdPrrm(uint32_t opcode);
    int LdrdPrim(uint32_t opcode);
    int LdrdPrrp(uint32_t opcode);
    int LdrdPrip(uint32_t opcode);
    int LdrdPtrm(uint32_t opcode);
    int LdrdPtim(uint32_t opcode);
    int LdrdPtrp(uint32_t opcode);
    int LdrdPtip(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int StrdOfrm(uint32_t opcode);
    int StrdOfim(uint32_t opcode);
    int StrdOfrp(uint32_t opcode);
    int StrdOfip(uint32_t opcode);
    int StrdPrrm(uint32_t opcode);
    int StrdPrim(uint32_t opcode);
    int StrdPrrp(uint32_t opcode);
    int StrdPrip(uint32_t opcode);
    int StrdPtrm(uint32_t opcode);
    int StrdPtim(uint32_t opcode);
    int StrdPtrp(uint32_t opcode);
    int StrdPtip(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int Swpb(uint32_t opcode);
    int Swp(uint32_t opcode);
    int Ldmda(uint32_t opcode);
    int Stmda(uint32_t opcode);
    int Ldmia(uint32_t opcode);
    int Stmia(uint32_t opcode);
    int Ldmdb(uint32_t opcode);
    int Stmdb(uint32_t opcode);
    int Ldmib(uint32_t opcode);
    int Stmib(uint32_t opcode);
    int LdmdaW(uint32_t opcode);
    int StmdaW(uint32_t opcode);
    int LdmiaW(uint32_t opcode);
    int StmiaW(uint32_t opcode);
    int LdmdbW(uint32_t opcode);
    int StmdbW(uint32_t opcode);
    int LdmibW(uint32_t opcode);
    int StmibW(uint32_t opcode);
    int LdmdaU(uint32_t opcode);
    int StmdaU(uint32_t opcode);
    int LdmiaU(uint32_t opcode);
    int StmiaU(uint32_t opcode);
    int LdmdbU(uint32_t opcode);
    int StmdbU(uint32_t opcode);
    int LdmibU(uint32_t opcode);
    int StmibU(uint32_t opcode);
    int LdmdaUW(uint32_t opcode);
    int StmdaUW(uint32_t opcode);
    int LdmiaUW(uint32_t opcode);
    int StmiaUW(uint32_t opcode);
    int LdmdbUW(uint32_t opcode);
    int StmdbUW(uint32_t opcode);
    int LdmibUW(uint32_t opcode);
    int StmibUW(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int MsrRc(uint32_t opcode);
    int MsrRs(uint32_t opcode);
    int MsrIc(uint32_t opcode);
    int MsrIs(uint32_t opcode);
    int MrsRc(uint32_t opcode);
    int MrsRs(uint32_t opcode);
    int Mrc(uint32_t opcode);
    int Mcr(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int LdrsbRegT(uint16_t opcode);
    int LdrshRegT(uint16_t opcode);
    int LdrbRegT(uint16_t opcode);
    int StrbRegT(uint16_t opcode);
    int LdrhRegT(uint16_t opcode);
    int StrhRegT(uint16_t opcode);
    int LdrRegT(uint16_t opcode);
    int StrRegT(uint16_t opcode);
    ///@}

    /** @name */
    ///@{
    int LdrbImm5T(uint16_t opcode);
    int StrbImm5T(uint16_t opcode);
    int LdrhImm5T(uint16_t opcode);
    int StrhImm5T(uint16_t opcode);
    int LdrImm5T(uint16_t opcode);
    int StrImm5T(uint16_t opcode);
    int LdrPcT(uint16_t opcode);
    int LdrSpT(uint16_t opcode);
    int StrSpT(uint16_t opcode);
    int LdmiaT(uint16_t opcode);
    int StmiaT(uint16_t opcode);
    int PopT(uint16_t opcode);
    int PushT(uint16_t opcode);
    int PopPcT(uint16_t opcode);
    int PushLrT(uint16_t opcode);
    int Bx(uint32_t opcode);
    int BlxReg(uint32_t opcode);
    int B(uint32_t opcode);
    int Bl(uint32_t opcode);
    int Blx(uint32_t opcode);
    int Swi(uint32_t opcode);
    int BxRegT(uint16_t opcode);
    int BlxRegT(uint16_t opcode);
    int BT(uint16_t opcode);
    int BeqT(uint16_t opcode);
    int BneT(uint16_t opcode);
    int BcsT(uint16_t opcode);
    int BccT(uint16_t opcode);
    int BmiT(uint16_t opcode);
    int BplT(uint16_t opcode);
    int BvsT(uint16_t opcode);
    int BvcT(uint16_t opcode);
    int BhiT(uint16_t opcode);
    int BlsT(uint16_t opcode);
    int BgeT(uint16_t opcode);
    int BltT(uint16_t opcode);
    int BgtT(uint16_t opcode);
    int BleT(uint16_t opcode);
    int BlSetupT(uint16_t opcode);
    int BlOffT(uint16_t opcode);
    int BlxOffT(uint16_t opcode);
    int SwiT(uint16_t opcode);
    ///@}

  private:
    int (CPU::*m_armInstrs[0x1000])(uint32_t) = {
        &CPU::AndLli,     &CPU::AndLlr,    &CPU::AndLri,     &CPU::AndLrr,    &CPU::AndAri,     &CPU::AndArr,
        &CPU::AndRri,     &CPU::AndRrr,    &CPU::AndLli,     &CPU::Mul,       &CPU::AndLri,     &CPU::StrhPtrm,
        &CPU::AndAri,     &CPU::LdrdPtrm,  &CPU::AndRri,     &CPU::StrdPtrm,  &CPU::AndsLli,    &CPU::AndsLlr,
        &CPU::AndsLri,    &CPU::AndsLrr,   &CPU::AndsAri,    &CPU::AndsArr,   &CPU::AndsRri,    &CPU::AndsRrr,
        &CPU::AndsLli,    &CPU::Muls,      &CPU::AndsLri,    &CPU::LdrhPtrm,  &CPU::AndsAri,    &CPU::LdrsbPtrm,
        &CPU::AndsRri,    &CPU::LdrshPtrm, &CPU::EorLli,     &CPU::EorLlr,    &CPU::EorLri,     &CPU::EorLrr,
        &CPU::EorAri,     &CPU::EorArr,    &CPU::EorRri,     &CPU::EorRrr,    &CPU::EorLli,     &CPU::Mla,
        &CPU::EorLri,     &CPU::StrhPtrm,  &CPU::EorAri,     &CPU::LdrdPtrm,  &CPU::EorRri,     &CPU::StrdPtrm,
        &CPU::EorsLli,    &CPU::EorsLlr,   &CPU::EorsLri,    &CPU::EorsLrr,   &CPU::EorsAri,    &CPU::EorsArr,
        &CPU::EorsRri,    &CPU::EorsRrr,   &CPU::EorsLli,    &CPU::Mlas,      &CPU::EorsLri,    &CPU::LdrhPtrm,
        &CPU::EorsAri,    &CPU::LdrsbPtrm, &CPU::EorsRri,    &CPU::LdrshPtrm, &CPU::SubLli,     &CPU::SubLlr,
        &CPU::SubLri,     &CPU::SubLrr,    &CPU::SubAri,     &CPU::SubArr,    &CPU::SubRri,     &CPU::SubRrr,
        &CPU::SubLli,     &CPU::UnkArm,    &CPU::SubLri,     &CPU::StrhPtim,  &CPU::SubAri,     &CPU::LdrdPtim,
        &CPU::SubRri,     &CPU::StrdPtim,  &CPU::SubsLli,    &CPU::SubsLlr,   &CPU::SubsLri,    &CPU::SubsLrr,
        &CPU::SubsAri,    &CPU::SubsArr,   &CPU::SubsRri,    &CPU::SubsRrr,   &CPU::SubsLli,    &CPU::UnkArm,
        &CPU::SubsLri,    &CPU::LdrhPtim,  &CPU::SubsAri,    &CPU::LdrsbPtim, &CPU::SubsRri,    &CPU::LdrshPtim,
        &CPU::RsbLli,     &CPU::RsbLlr,    &CPU::RsbLri,     &CPU::RsbLrr,    &CPU::RsbAri,     &CPU::RsbArr,
        &CPU::RsbRri,     &CPU::RsbRrr,    &CPU::RsbLli,     &CPU::UnkArm,    &CPU::RsbLri,     &CPU::StrhPtim,
        &CPU::RsbAri,     &CPU::LdrdPtim,  &CPU::RsbRri,     &CPU::StrdPtim,  &CPU::RsbsLli,    &CPU::RsbsLlr,
        &CPU::RsbsLri,    &CPU::RsbsLrr,   &CPU::RsbsAri,    &CPU::RsbsArr,   &CPU::RsbsRri,    &CPU::RsbsRrr,
        &CPU::RsbsLli,    &CPU::UnkArm,    &CPU::RsbsLri,    &CPU::LdrhPtim,  &CPU::RsbsAri,    &CPU::LdrsbPtim,
        &CPU::RsbsRri,    &CPU::LdrshPtim, &CPU::AddLli,     &CPU::AddLlr,    &CPU::AddLri,     &CPU::AddLrr,
        &CPU::AddAri,     &CPU::AddArr,    &CPU::AddRri,     &CPU::AddRrr,    &CPU::AddLli,     &CPU::Umull,
        &CPU::AddLri,     &CPU::StrhPtrp,  &CPU::AddAri,     &CPU::LdrdPtrp,  &CPU::AddRri,     &CPU::StrdPtrp,
        &CPU::AddsLli,    &CPU::AddsLlr,   &CPU::AddsLri,    &CPU::AddsLrr,   &CPU::AddsAri,    &CPU::AddsArr,
        &CPU::AddsRri,    &CPU::AddsRrr,   &CPU::AddsLli,    &CPU::Umulls,    &CPU::AddsLri,    &CPU::LdrhPtrp,
        &CPU::AddsAri,    &CPU::LdrsbPtrp, &CPU::AddsRri,    &CPU::LdrshPtrp, &CPU::AdcLli,     &CPU::AdcLlr,
        &CPU::AdcLri,     &CPU::AdcLrr,    &CPU::AdcAri,     &CPU::AdcArr,    &CPU::AdcRri,     &CPU::AdcRrr,
        &CPU::AdcLli,     &CPU::Umlal,     &CPU::AdcLri,     &CPU::StrhPtrp,  &CPU::AdcAri,     &CPU::LdrdPtrp,
        &CPU::AdcRri,     &CPU::StrdPtrp,  &CPU::AdcsLli,    &CPU::AdcsLlr,   &CPU::AdcsLri,    &CPU::AdcsLrr,
        &CPU::AdcsAri,    &CPU::AdcsArr,   &CPU::AdcsRri,    &CPU::AdcsRrr,   &CPU::AdcsLli,    &CPU::Umlals,
        &CPU::AdcsLri,    &CPU::LdrhPtrp,  &CPU::AdcsAri,    &CPU::LdrsbPtrp, &CPU::AdcsRri,    &CPU::LdrshPtrp,
        &CPU::SbcLli,     &CPU::SbcLlr,    &CPU::SbcLri,     &CPU::SbcLrr,    &CPU::SbcAri,     &CPU::SbcArr,
        &CPU::SbcRri,     &CPU::SbcRrr,    &CPU::SbcLli,     &CPU::Smull,     &CPU::SbcLri,     &CPU::StrhPtip,
        &CPU::SbcAri,     &CPU::LdrdPtip,  &CPU::SbcRri,     &CPU::StrdPtip,  &CPU::SbcsLli,    &CPU::SbcsLlr,
        &CPU::SbcsLri,    &CPU::SbcsLrr,   &CPU::SbcsAri,    &CPU::SbcsArr,   &CPU::SbcsRri,    &CPU::SbcsRrr,
        &CPU::SbcsLli,    &CPU::Smulls,    &CPU::SbcsLri,    &CPU::LdrhPtip,  &CPU::SbcsAri,    &CPU::LdrsbPtip,
        &CPU::SbcsRri,    &CPU::LdrshPtip, &CPU::RscLli,     &CPU::RscLlr,    &CPU::RscLri,     &CPU::RscLrr,
        &CPU::RscAri,     &CPU::RscArr,    &CPU::RscRri,     &CPU::RscRrr,    &CPU::RscLli,     &CPU::Smlal,
        &CPU::RscLri,     &CPU::StrhPtip,  &CPU::RscAri,     &CPU::LdrdPtip,  &CPU::RscRri,     &CPU::StrdPtip,
        &CPU::RscsLli,    &CPU::RscsLlr,   &CPU::RscsLri,    &CPU::RscsLrr,   &CPU::RscsAri,    &CPU::RscsArr,
        &CPU::RscsRri,    &CPU::RscsRrr,   &CPU::RscsLli,    &CPU::Smlals,    &CPU::RscsLri,    &CPU::LdrhPtip,
        &CPU::RscsAri,    &CPU::LdrsbPtip, &CPU::RscsRri,    &CPU::LdrshPtip, &CPU::MrsRc,      &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::Qadd,      &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::Smlabb,     &CPU::Swp,       &CPU::Smlatb,     &CPU::StrhOfrm,  &CPU::Smlabt,     &CPU::LdrdOfrm,
        &CPU::Smlatt,     &CPU::StrdOfrm,  &CPU::TstLli,     &CPU::TstLlr,    &CPU::TstLri,     &CPU::TstLrr,
        &CPU::TstAri,     &CPU::TstArr,    &CPU::TstRri,     &CPU::TstRrr,    &CPU::TstLli,     &CPU::UnkArm,
        &CPU::TstLri,     &CPU::LdrhOfrm,  &CPU::TstAri,     &CPU::LdrsbOfrm, &CPU::TstRri,     &CPU::LdrshOfrm,
        &CPU::MsrRc,      &CPU::Bx,        &CPU::UnkArm,     &CPU::BlxReg,    &CPU::UnkArm,     &CPU::Qsub,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::Smlawb,     &CPU::UnkArm,    &CPU::Smulwb,     &CPU::StrhPrrm,
        &CPU::Smlawt,     &CPU::LdrdPrrm,  &CPU::Smulwt,     &CPU::StrdPrrm,  &CPU::TeqLli,     &CPU::TeqLlr,
        &CPU::TeqLri,     &CPU::TeqLrr,    &CPU::TeqAri,     &CPU::TeqArr,    &CPU::TeqRri,     &CPU::TeqRrr,
        &CPU::TeqLli,     &CPU::UnkArm,    &CPU::TeqLri,     &CPU::LdrhPrrm,  &CPU::TeqAri,     &CPU::LdrsbPrrm,
        &CPU::TeqRri,     &CPU::LdrshPrrm, &CPU::MrsRs,      &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::Qdadd,     &CPU::UnkArm,     &CPU::UnkArm,    &CPU::Smlalbb,    &CPU::Swpb,
        &CPU::Smlaltb,    &CPU::StrhOfim,  &CPU::Smlalbt,    &CPU::LdrdOfim,  &CPU::Smlaltt,    &CPU::StrdOfim,
        &CPU::CmpLli,     &CPU::CmpLlr,    &CPU::CmpLri,     &CPU::CmpLrr,    &CPU::CmpAri,     &CPU::CmpArr,
        &CPU::CmpRri,     &CPU::CmpRrr,    &CPU::CmpLli,     &CPU::UnkArm,    &CPU::CmpLri,     &CPU::LdrhOfim,
        &CPU::CmpAri,     &CPU::LdrsbOfim, &CPU::CmpRri,     &CPU::LdrshOfim, &CPU::MsrRs,      &CPU::Clz,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::Qdsub,     &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::Smulbb,     &CPU::UnkArm,    &CPU::Smultb,     &CPU::StrhPrim,  &CPU::Smulbt,     &CPU::LdrdPrim,
        &CPU::Smultt,     &CPU::StrdPrim,  &CPU::CmnLli,     &CPU::CmnLlr,    &CPU::CmnLri,     &CPU::CmnLrr,
        &CPU::CmnAri,     &CPU::CmnArr,    &CPU::CmnRri,     &CPU::CmnRrr,    &CPU::CmnLli,     &CPU::UnkArm,
        &CPU::CmnLri,     &CPU::LdrhPrim,  &CPU::CmnAri,     &CPU::LdrsbPrim, &CPU::CmnRri,     &CPU::LdrshPrim,
        &CPU::OrrLli,     &CPU::OrrLlr,    &CPU::OrrLri,     &CPU::OrrLrr,    &CPU::OrrAri,     &CPU::OrrArr,
        &CPU::OrrRri,     &CPU::OrrRrr,    &CPU::OrrLli,     &CPU::UnkArm,    &CPU::OrrLri,     &CPU::StrhOfrp,
        &CPU::OrrAri,     &CPU::LdrdOfrp,  &CPU::OrrRri,     &CPU::StrdOfrp,  &CPU::OrrsLli,    &CPU::OrrsLlr,
        &CPU::OrrsLri,    &CPU::OrrsLrr,   &CPU::OrrsAri,    &CPU::OrrsArr,   &CPU::OrrsRri,    &CPU::OrrsRrr,
        &CPU::OrrsLli,    &CPU::UnkArm,    &CPU::OrrsLri,    &CPU::LdrhOfrp,  &CPU::OrrsAri,    &CPU::LdrsbOfrp,
        &CPU::OrrsRri,    &CPU::LdrshOfrp, &CPU::MovLli,     &CPU::MovLlr,    &CPU::MovLri,     &CPU::MovLrr,
        &CPU::MovAri,     &CPU::MovArr,    &CPU::MovRri,     &CPU::MovRrr,    &CPU::MovLli,     &CPU::UnkArm,
        &CPU::MovLri,     &CPU::StrhPrrp,  &CPU::MovAri,     &CPU::LdrdPrrp,  &CPU::MovRri,     &CPU::StrdPrrp,
        &CPU::MovsLli,    &CPU::MovsLlr,   &CPU::MovsLri,    &CPU::MovsLrr,   &CPU::MovsAri,    &CPU::MovsArr,
        &CPU::MovsRri,    &CPU::MovsRrr,   &CPU::MovsLli,    &CPU::UnkArm,    &CPU::MovsLri,    &CPU::LdrhPrrp,
        &CPU::MovsAri,    &CPU::LdrsbPrrp, &CPU::MovsRri,    &CPU::LdrshPrrp, &CPU::BicLli,     &CPU::BicLlr,
        &CPU::BicLri,     &CPU::BicLrr,    &CPU::BicAri,     &CPU::BicArr,    &CPU::BicRri,     &CPU::BicRrr,
        &CPU::BicLli,     &CPU::UnkArm,    &CPU::BicLri,     &CPU::StrhOfip,  &CPU::BicAri,     &CPU::LdrdOfip,
        &CPU::BicRri,     &CPU::StrdOfip,  &CPU::BicsLli,    &CPU::BicsLlr,   &CPU::BicsLri,    &CPU::BicsLrr,
        &CPU::BicsAri,    &CPU::BicsArr,   &CPU::BicsRri,    &CPU::BicsRrr,   &CPU::BicsLli,    &CPU::UnkArm,
        &CPU::BicsLri,    &CPU::LdrhOfip,  &CPU::BicsAri,    &CPU::LdrsbOfip, &CPU::BicsRri,    &CPU::LdrshOfip,
        &CPU::MvnLli,     &CPU::MvnLlr,    &CPU::MvnLri,     &CPU::MvnLrr,    &CPU::MvnAri,     &CPU::MvnArr,
        &CPU::MvnRri,     &CPU::MvnRrr,    &CPU::MvnLli,     &CPU::UnkArm,    &CPU::MvnLri,     &CPU::StrhPrip,
        &CPU::MvnAri,     &CPU::LdrdPrip,  &CPU::MvnRri,     &CPU::StrdPrip,  &CPU::MvnsLli,    &CPU::MvnsLlr,
        &CPU::MvnsLri,    &CPU::MvnsLrr,   &CPU::MvnsAri,    &CPU::MvnsArr,   &CPU::MvnsRri,    &CPU::MvnsRrr,
        &CPU::MvnsLli,    &CPU::UnkArm,    &CPU::MvnsLri,    &CPU::LdrhPrip,  &CPU::MvnsAri,    &CPU::LdrsbPrip,
        &CPU::MvnsRri,    &CPU::LdrshPrip, &CPU::AndImm,     &CPU::AndImm,    &CPU::AndImm,     &CPU::AndImm,
        &CPU::AndImm,     &CPU::AndImm,    &CPU::AndImm,     &CPU::AndImm,    &CPU::AndImm,     &CPU::AndImm,
        &CPU::AndImm,     &CPU::AndImm,    &CPU::AndImm,     &CPU::AndImm,    &CPU::AndImm,     &CPU::AndImm,
        &CPU::AndsImm,    &CPU::AndsImm,   &CPU::AndsImm,    &CPU::AndsImm,   &CPU::AndsImm,    &CPU::AndsImm,
        &CPU::AndsImm,    &CPU::AndsImm,   &CPU::AndsImm,    &CPU::AndsImm,   &CPU::AndsImm,    &CPU::AndsImm,
        &CPU::AndsImm,    &CPU::AndsImm,   &CPU::AndsImm,    &CPU::AndsImm,   &CPU::EorImm,     &CPU::EorImm,
        &CPU::EorImm,     &CPU::EorImm,    &CPU::EorImm,     &CPU::EorImm,    &CPU::EorImm,     &CPU::EorImm,
        &CPU::EorImm,     &CPU::EorImm,    &CPU::EorImm,     &CPU::EorImm,    &CPU::EorImm,     &CPU::EorImm,
        &CPU::EorImm,     &CPU::EorImm,    &CPU::EorsImm,    &CPU::EorsImm,   &CPU::EorsImm,    &CPU::EorsImm,
        &CPU::EorsImm,    &CPU::EorsImm,   &CPU::EorsImm,    &CPU::EorsImm,   &CPU::EorsImm,    &CPU::EorsImm,
        &CPU::EorsImm,    &CPU::EorsImm,   &CPU::EorsImm,    &CPU::EorsImm,   &CPU::EorsImm,    &CPU::EorsImm,
        &CPU::SubImm,     &CPU::SubImm,    &CPU::SubImm,     &CPU::SubImm,    &CPU::SubImm,     &CPU::SubImm,
        &CPU::SubImm,     &CPU::SubImm,    &CPU::SubImm,     &CPU::SubImm,    &CPU::SubImm,     &CPU::SubImm,
        &CPU::SubImm,     &CPU::SubImm,    &CPU::SubImm,     &CPU::SubImm,    &CPU::SubsImm,    &CPU::SubsImm,
        &CPU::SubsImm,    &CPU::SubsImm,   &CPU::SubsImm,    &CPU::SubsImm,   &CPU::SubsImm,    &CPU::SubsImm,
        &CPU::SubsImm,    &CPU::SubsImm,   &CPU::SubsImm,    &CPU::SubsImm,   &CPU::SubsImm,    &CPU::SubsImm,
        &CPU::SubsImm,    &CPU::SubsImm,   &CPU::RsbImm,     &CPU::RsbImm,    &CPU::RsbImm,     &CPU::RsbImm,
        &CPU::RsbImm,     &CPU::RsbImm,    &CPU::RsbImm,     &CPU::RsbImm,    &CPU::RsbImm,     &CPU::RsbImm,
        &CPU::RsbImm,     &CPU::RsbImm,    &CPU::RsbImm,     &CPU::RsbImm,    &CPU::RsbImm,     &CPU::RsbImm,
        &CPU::RsbsImm,    &CPU::RsbsImm,   &CPU::RsbsImm,    &CPU::RsbsImm,   &CPU::RsbsImm,    &CPU::RsbsImm,
        &CPU::RsbsImm,    &CPU::RsbsImm,   &CPU::RsbsImm,    &CPU::RsbsImm,   &CPU::RsbsImm,    &CPU::RsbsImm,
        &CPU::RsbsImm,    &CPU::RsbsImm,   &CPU::RsbsImm,    &CPU::RsbsImm,   &CPU::AddImm,     &CPU::AddImm,
        &CPU::AddImm,     &CPU::AddImm,    &CPU::AddImm,     &CPU::AddImm,    &CPU::AddImm,     &CPU::AddImm,
        &CPU::AddImm,     &CPU::AddImm,    &CPU::AddImm,     &CPU::AddImm,    &CPU::AddImm,     &CPU::AddImm,
        &CPU::AddImm,     &CPU::AddImm,    &CPU::AddsImm,    &CPU::AddsImm,   &CPU::AddsImm,    &CPU::AddsImm,
        &CPU::AddsImm,    &CPU::AddsImm,   &CPU::AddsImm,    &CPU::AddsImm,   &CPU::AddsImm,    &CPU::AddsImm,
        &CPU::AddsImm,    &CPU::AddsImm,   &CPU::AddsImm,    &CPU::AddsImm,   &CPU::AddsImm,    &CPU::AddsImm,
        &CPU::AdcImm,     &CPU::AdcImm,    &CPU::AdcImm,     &CPU::AdcImm,    &CPU::AdcImm,     &CPU::AdcImm,
        &CPU::AdcImm,     &CPU::AdcImm,    &CPU::AdcImm,     &CPU::AdcImm,    &CPU::AdcImm,     &CPU::AdcImm,
        &CPU::AdcImm,     &CPU::AdcImm,    &CPU::AdcImm,     &CPU::AdcImm,    &CPU::AdcsImm,    &CPU::AdcsImm,
        &CPU::AdcsImm,    &CPU::AdcsImm,   &CPU::AdcsImm,    &CPU::AdcsImm,   &CPU::AdcsImm,    &CPU::AdcsImm,
        &CPU::AdcsImm,    &CPU::AdcsImm,   &CPU::AdcsImm,    &CPU::AdcsImm,   &CPU::AdcsImm,    &CPU::AdcsImm,
        &CPU::AdcsImm,    &CPU::AdcsImm,   &CPU::SbcImm,     &CPU::SbcImm,    &CPU::SbcImm,     &CPU::SbcImm,
        &CPU::SbcImm,     &CPU::SbcImm,    &CPU::SbcImm,     &CPU::SbcImm,    &CPU::SbcImm,     &CPU::SbcImm,
        &CPU::SbcImm,     &CPU::SbcImm,    &CPU::SbcImm,     &CPU::SbcImm,    &CPU::SbcImm,     &CPU::SbcImm,
        &CPU::SbcsImm,    &CPU::SbcsImm,   &CPU::SbcsImm,    &CPU::SbcsImm,   &CPU::SbcsImm,    &CPU::SbcsImm,
        &CPU::SbcsImm,    &CPU::SbcsImm,   &CPU::SbcsImm,    &CPU::SbcsImm,   &CPU::SbcsImm,    &CPU::SbcsImm,
        &CPU::SbcsImm,    &CPU::SbcsImm,   &CPU::SbcsImm,    &CPU::SbcsImm,   &CPU::RscImm,     &CPU::RscImm,
        &CPU::RscImm,     &CPU::RscImm,    &CPU::RscImm,     &CPU::RscImm,    &CPU::RscImm,     &CPU::RscImm,
        &CPU::RscImm,     &CPU::RscImm,    &CPU::RscImm,     &CPU::RscImm,    &CPU::RscImm,     &CPU::RscImm,
        &CPU::RscImm,     &CPU::RscImm,    &CPU::RscsImm,    &CPU::RscsImm,   &CPU::RscsImm,    &CPU::RscsImm,
        &CPU::RscsImm,    &CPU::RscsImm,   &CPU::RscsImm,    &CPU::RscsImm,   &CPU::RscsImm,    &CPU::RscsImm,
        &CPU::RscsImm,    &CPU::RscsImm,   &CPU::RscsImm,    &CPU::RscsImm,   &CPU::RscsImm,    &CPU::RscsImm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::TstImm,     &CPU::TstImm,
        &CPU::TstImm,     &CPU::TstImm,    &CPU::TstImm,     &CPU::TstImm,    &CPU::TstImm,     &CPU::TstImm,
        &CPU::TstImm,     &CPU::TstImm,    &CPU::TstImm,     &CPU::TstImm,    &CPU::TstImm,     &CPU::TstImm,
        &CPU::TstImm,     &CPU::TstImm,    &CPU::MsrIc,      &CPU::MsrIc,     &CPU::MsrIc,      &CPU::MsrIc,
        &CPU::MsrIc,      &CPU::MsrIc,     &CPU::MsrIc,      &CPU::MsrIc,     &CPU::MsrIc,      &CPU::MsrIc,
        &CPU::MsrIc,      &CPU::MsrIc,     &CPU::MsrIc,      &CPU::MsrIc,     &CPU::MsrIc,      &CPU::MsrIc,
        &CPU::TeqImm,     &CPU::TeqImm,    &CPU::TeqImm,     &CPU::TeqImm,    &CPU::TeqImm,     &CPU::TeqImm,
        &CPU::TeqImm,     &CPU::TeqImm,    &CPU::TeqImm,     &CPU::TeqImm,    &CPU::TeqImm,     &CPU::TeqImm,
        &CPU::TeqImm,     &CPU::TeqImm,    &CPU::TeqImm,     &CPU::TeqImm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::CmpImm,     &CPU::CmpImm,    &CPU::CmpImm,     &CPU::CmpImm,
        &CPU::CmpImm,     &CPU::CmpImm,    &CPU::CmpImm,     &CPU::CmpImm,    &CPU::CmpImm,     &CPU::CmpImm,
        &CPU::CmpImm,     &CPU::CmpImm,    &CPU::CmpImm,     &CPU::CmpImm,    &CPU::CmpImm,     &CPU::CmpImm,
        &CPU::MsrIs,      &CPU::MsrIs,     &CPU::MsrIs,      &CPU::MsrIs,     &CPU::MsrIs,      &CPU::MsrIs,
        &CPU::MsrIs,      &CPU::MsrIs,     &CPU::MsrIs,      &CPU::MsrIs,     &CPU::MsrIs,      &CPU::MsrIs,
        &CPU::MsrIs,      &CPU::MsrIs,     &CPU::MsrIs,      &CPU::MsrIs,     &CPU::CmnImm,     &CPU::CmnImm,
        &CPU::CmnImm,     &CPU::CmnImm,    &CPU::CmnImm,     &CPU::CmnImm,    &CPU::CmnImm,     &CPU::CmnImm,
        &CPU::CmnImm,     &CPU::CmnImm,    &CPU::CmnImm,     &CPU::CmnImm,    &CPU::CmnImm,     &CPU::CmnImm,
        &CPU::CmnImm,     &CPU::CmnImm,    &CPU::OrrImm,     &CPU::OrrImm,    &CPU::OrrImm,     &CPU::OrrImm,
        &CPU::OrrImm,     &CPU::OrrImm,    &CPU::OrrImm,     &CPU::OrrImm,    &CPU::OrrImm,     &CPU::OrrImm,
        &CPU::OrrImm,     &CPU::OrrImm,    &CPU::OrrImm,     &CPU::OrrImm,    &CPU::OrrImm,     &CPU::OrrImm,
        &CPU::OrrsImm,    &CPU::OrrsImm,   &CPU::OrrsImm,    &CPU::OrrsImm,   &CPU::OrrsImm,    &CPU::OrrsImm,
        &CPU::OrrsImm,    &CPU::OrrsImm,   &CPU::OrrsImm,    &CPU::OrrsImm,   &CPU::OrrsImm,    &CPU::OrrsImm,
        &CPU::OrrsImm,    &CPU::OrrsImm,   &CPU::OrrsImm,    &CPU::OrrsImm,   &CPU::MovImm,     &CPU::MovImm,
        &CPU::MovImm,     &CPU::MovImm,    &CPU::MovImm,     &CPU::MovImm,    &CPU::MovImm,     &CPU::MovImm,
        &CPU::MovImm,     &CPU::MovImm,    &CPU::MovImm,     &CPU::MovImm,    &CPU::MovImm,     &CPU::MovImm,
        &CPU::MovImm,     &CPU::MovImm,    &CPU::MovsImm,    &CPU::MovsImm,   &CPU::MovsImm,    &CPU::MovsImm,
        &CPU::MovsImm,    &CPU::MovsImm,   &CPU::MovsImm,    &CPU::MovsImm,   &CPU::MovsImm,    &CPU::MovsImm,
        &CPU::MovsImm,    &CPU::MovsImm,   &CPU::MovsImm,    &CPU::MovsImm,   &CPU::MovsImm,    &CPU::MovsImm,
        &CPU::BicImm,     &CPU::BicImm,    &CPU::BicImm,     &CPU::BicImm,    &CPU::BicImm,     &CPU::BicImm,
        &CPU::BicImm,     &CPU::BicImm,    &CPU::BicImm,     &CPU::BicImm,    &CPU::BicImm,     &CPU::BicImm,
        &CPU::BicImm,     &CPU::BicImm,    &CPU::BicImm,     &CPU::BicImm,    &CPU::BicsImm,    &CPU::BicsImm,
        &CPU::BicsImm,    &CPU::BicsImm,   &CPU::BicsImm,    &CPU::BicsImm,   &CPU::BicsImm,    &CPU::BicsImm,
        &CPU::BicsImm,    &CPU::BicsImm,   &CPU::BicsImm,    &CPU::BicsImm,   &CPU::BicsImm,    &CPU::BicsImm,
        &CPU::BicsImm,    &CPU::BicsImm,   &CPU::MvnImm,     &CPU::MvnImm,    &CPU::MvnImm,     &CPU::MvnImm,
        &CPU::MvnImm,     &CPU::MvnImm,    &CPU::MvnImm,     &CPU::MvnImm,    &CPU::MvnImm,     &CPU::MvnImm,
        &CPU::MvnImm,     &CPU::MvnImm,    &CPU::MvnImm,     &CPU::MvnImm,    &CPU::MvnImm,     &CPU::MvnImm,
        &CPU::MvnsImm,    &CPU::MvnsImm,   &CPU::MvnsImm,    &CPU::MvnsImm,   &CPU::MvnsImm,    &CPU::MvnsImm,
        &CPU::MvnsImm,    &CPU::MvnsImm,   &CPU::MvnsImm,    &CPU::MvnsImm,   &CPU::MvnsImm,    &CPU::MvnsImm,
        &CPU::MvnsImm,    &CPU::MvnsImm,   &CPU::MvnsImm,    &CPU::MvnsImm,   &CPU::StrPtim,    &CPU::StrPtim,
        &CPU::StrPtim,    &CPU::StrPtim,   &CPU::StrPtim,    &CPU::StrPtim,   &CPU::StrPtim,    &CPU::StrPtim,
        &CPU::StrPtim,    &CPU::StrPtim,   &CPU::StrPtim,    &CPU::StrPtim,   &CPU::StrPtim,    &CPU::StrPtim,
        &CPU::StrPtim,    &CPU::StrPtim,   &CPU::LdrPtim,    &CPU::LdrPtim,   &CPU::LdrPtim,    &CPU::LdrPtim,
        &CPU::LdrPtim,    &CPU::LdrPtim,   &CPU::LdrPtim,    &CPU::LdrPtim,   &CPU::LdrPtim,    &CPU::LdrPtim,
        &CPU::LdrPtim,    &CPU::LdrPtim,   &CPU::LdrPtim,    &CPU::LdrPtim,   &CPU::LdrPtim,    &CPU::LdrPtim,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::StrbPtim,   &CPU::StrbPtim,  &CPU::StrbPtim,   &CPU::StrbPtim,
        &CPU::StrbPtim,   &CPU::StrbPtim,  &CPU::StrbPtim,   &CPU::StrbPtim,  &CPU::StrbPtim,   &CPU::StrbPtim,
        &CPU::StrbPtim,   &CPU::StrbPtim,  &CPU::StrbPtim,   &CPU::StrbPtim,  &CPU::StrbPtim,   &CPU::StrbPtim,
        &CPU::LdrbPtim,   &CPU::LdrbPtim,  &CPU::LdrbPtim,   &CPU::LdrbPtim,  &CPU::LdrbPtim,   &CPU::LdrbPtim,
        &CPU::LdrbPtim,   &CPU::LdrbPtim,  &CPU::LdrbPtim,   &CPU::LdrbPtim,  &CPU::LdrbPtim,   &CPU::LdrbPtim,
        &CPU::LdrbPtim,   &CPU::LdrbPtim,  &CPU::LdrbPtim,   &CPU::LdrbPtim,  &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::StrPtip,    &CPU::StrPtip,   &CPU::StrPtip,    &CPU::StrPtip,   &CPU::StrPtip,    &CPU::StrPtip,
        &CPU::StrPtip,    &CPU::StrPtip,   &CPU::StrPtip,    &CPU::StrPtip,   &CPU::StrPtip,    &CPU::StrPtip,
        &CPU::StrPtip,    &CPU::StrPtip,   &CPU::StrPtip,    &CPU::StrPtip,   &CPU::LdrPtip,    &CPU::LdrPtip,
        &CPU::LdrPtip,    &CPU::LdrPtip,   &CPU::LdrPtip,    &CPU::LdrPtip,   &CPU::LdrPtip,    &CPU::LdrPtip,
        &CPU::LdrPtip,    &CPU::LdrPtip,   &CPU::LdrPtip,    &CPU::LdrPtip,   &CPU::LdrPtip,    &CPU::LdrPtip,
        &CPU::LdrPtip,    &CPU::LdrPtip,   &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::StrbPtip,   &CPU::StrbPtip,
        &CPU::StrbPtip,   &CPU::StrbPtip,  &CPU::StrbPtip,   &CPU::StrbPtip,  &CPU::StrbPtip,   &CPU::StrbPtip,
        &CPU::StrbPtip,   &CPU::StrbPtip,  &CPU::StrbPtip,   &CPU::StrbPtip,  &CPU::StrbPtip,   &CPU::StrbPtip,
        &CPU::StrbPtip,   &CPU::StrbPtip,  &CPU::LdrbPtip,   &CPU::LdrbPtip,  &CPU::LdrbPtip,   &CPU::LdrbPtip,
        &CPU::LdrbPtip,   &CPU::LdrbPtip,  &CPU::LdrbPtip,   &CPU::LdrbPtip,  &CPU::LdrbPtip,   &CPU::LdrbPtip,
        &CPU::LdrbPtip,   &CPU::LdrbPtip,  &CPU::LdrbPtip,   &CPU::LdrbPtip,  &CPU::LdrbPtip,   &CPU::LdrbPtip,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::StrOfim,    &CPU::StrOfim,   &CPU::StrOfim,    &CPU::StrOfim,
        &CPU::StrOfim,    &CPU::StrOfim,   &CPU::StrOfim,    &CPU::StrOfim,   &CPU::StrOfim,    &CPU::StrOfim,
        &CPU::StrOfim,    &CPU::StrOfim,   &CPU::StrOfim,    &CPU::StrOfim,   &CPU::StrOfim,    &CPU::StrOfim,
        &CPU::LdrOfim,    &CPU::LdrOfim,   &CPU::LdrOfim,    &CPU::LdrOfim,   &CPU::LdrOfim,    &CPU::LdrOfim,
        &CPU::LdrOfim,    &CPU::LdrOfim,   &CPU::LdrOfim,    &CPU::LdrOfim,   &CPU::LdrOfim,    &CPU::LdrOfim,
        &CPU::LdrOfim,    &CPU::LdrOfim,   &CPU::LdrOfim,    &CPU::LdrOfim,   &CPU::StrPrim,    &CPU::StrPrim,
        &CPU::StrPrim,    &CPU::StrPrim,   &CPU::StrPrim,    &CPU::StrPrim,   &CPU::StrPrim,    &CPU::StrPrim,
        &CPU::StrPrim,    &CPU::StrPrim,   &CPU::StrPrim,    &CPU::StrPrim,   &CPU::StrPrim,    &CPU::StrPrim,
        &CPU::StrPrim,    &CPU::StrPrim,   &CPU::LdrPrim,    &CPU::LdrPrim,   &CPU::LdrPrim,    &CPU::LdrPrim,
        &CPU::LdrPrim,    &CPU::LdrPrim,   &CPU::LdrPrim,    &CPU::LdrPrim,   &CPU::LdrPrim,    &CPU::LdrPrim,
        &CPU::LdrPrim,    &CPU::LdrPrim,   &CPU::LdrPrim,    &CPU::LdrPrim,   &CPU::LdrPrim,    &CPU::LdrPrim,
        &CPU::StrbOfim,   &CPU::StrbOfim,  &CPU::StrbOfim,   &CPU::StrbOfim,  &CPU::StrbOfim,   &CPU::StrbOfim,
        &CPU::StrbOfim,   &CPU::StrbOfim,  &CPU::StrbOfim,   &CPU::StrbOfim,  &CPU::StrbOfim,   &CPU::StrbOfim,
        &CPU::StrbOfim,   &CPU::StrbOfim,  &CPU::StrbOfim,   &CPU::StrbOfim,  &CPU::LdrbOfim,   &CPU::LdrbOfim,
        &CPU::LdrbOfim,   &CPU::LdrbOfim,  &CPU::LdrbOfim,   &CPU::LdrbOfim,  &CPU::LdrbOfim,   &CPU::LdrbOfim,
        &CPU::LdrbOfim,   &CPU::LdrbOfim,  &CPU::LdrbOfim,   &CPU::LdrbOfim,  &CPU::LdrbOfim,   &CPU::LdrbOfim,
        &CPU::LdrbOfim,   &CPU::LdrbOfim,  &CPU::StrbPrim,   &CPU::StrbPrim,  &CPU::StrbPrim,   &CPU::StrbPrim,
        &CPU::StrbPrim,   &CPU::StrbPrim,  &CPU::StrbPrim,   &CPU::StrbPrim,  &CPU::StrbPrim,   &CPU::StrbPrim,
        &CPU::StrbPrim,   &CPU::StrbPrim,  &CPU::StrbPrim,   &CPU::StrbPrim,  &CPU::StrbPrim,   &CPU::StrbPrim,
        &CPU::LdrbPrim,   &CPU::LdrbPrim,  &CPU::LdrbPrim,   &CPU::LdrbPrim,  &CPU::LdrbPrim,   &CPU::LdrbPrim,
        &CPU::LdrbPrim,   &CPU::LdrbPrim,  &CPU::LdrbPrim,   &CPU::LdrbPrim,  &CPU::LdrbPrim,   &CPU::LdrbPrim,
        &CPU::LdrbPrim,   &CPU::LdrbPrim,  &CPU::LdrbPrim,   &CPU::LdrbPrim,  &CPU::StrOfip,    &CPU::StrOfip,
        &CPU::StrOfip,    &CPU::StrOfip,   &CPU::StrOfip,    &CPU::StrOfip,   &CPU::StrOfip,    &CPU::StrOfip,
        &CPU::StrOfip,    &CPU::StrOfip,   &CPU::StrOfip,    &CPU::StrOfip,   &CPU::StrOfip,    &CPU::StrOfip,
        &CPU::StrOfip,    &CPU::StrOfip,   &CPU::LdrOfip,    &CPU::LdrOfip,   &CPU::LdrOfip,    &CPU::LdrOfip,
        &CPU::LdrOfip,    &CPU::LdrOfip,   &CPU::LdrOfip,    &CPU::LdrOfip,   &CPU::LdrOfip,    &CPU::LdrOfip,
        &CPU::LdrOfip,    &CPU::LdrOfip,   &CPU::LdrOfip,    &CPU::LdrOfip,   &CPU::LdrOfip,    &CPU::LdrOfip,
        &CPU::StrPrip,    &CPU::StrPrip,   &CPU::StrPrip,    &CPU::StrPrip,   &CPU::StrPrip,    &CPU::StrPrip,
        &CPU::StrPrip,    &CPU::StrPrip,   &CPU::StrPrip,    &CPU::StrPrip,   &CPU::StrPrip,    &CPU::StrPrip,
        &CPU::StrPrip,    &CPU::StrPrip,   &CPU::StrPrip,    &CPU::StrPrip,   &CPU::LdrPrip,    &CPU::LdrPrip,
        &CPU::LdrPrip,    &CPU::LdrPrip,   &CPU::LdrPrip,    &CPU::LdrPrip,   &CPU::LdrPrip,    &CPU::LdrPrip,
        &CPU::LdrPrip,    &CPU::LdrPrip,   &CPU::LdrPrip,    &CPU::LdrPrip,   &CPU::LdrPrip,    &CPU::LdrPrip,
        &CPU::LdrPrip,    &CPU::LdrPrip,   &CPU::StrbOfip,   &CPU::StrbOfip,  &CPU::StrbOfip,   &CPU::StrbOfip,
        &CPU::StrbOfip,   &CPU::StrbOfip,  &CPU::StrbOfip,   &CPU::StrbOfip,  &CPU::StrbOfip,   &CPU::StrbOfip,
        &CPU::StrbOfip,   &CPU::StrbOfip,  &CPU::StrbOfip,   &CPU::StrbOfip,  &CPU::StrbOfip,   &CPU::StrbOfip,
        &CPU::LdrbOfip,   &CPU::LdrbOfip,  &CPU::LdrbOfip,   &CPU::LdrbOfip,  &CPU::LdrbOfip,   &CPU::LdrbOfip,
        &CPU::LdrbOfip,   &CPU::LdrbOfip,  &CPU::LdrbOfip,   &CPU::LdrbOfip,  &CPU::LdrbOfip,   &CPU::LdrbOfip,
        &CPU::LdrbOfip,   &CPU::LdrbOfip,  &CPU::LdrbOfip,   &CPU::LdrbOfip,  &CPU::StrbPrip,   &CPU::StrbPrip,
        &CPU::StrbPrip,   &CPU::StrbPrip,  &CPU::StrbPrip,   &CPU::StrbPrip,  &CPU::StrbPrip,   &CPU::StrbPrip,
        &CPU::StrbPrip,   &CPU::StrbPrip,  &CPU::StrbPrip,   &CPU::StrbPrip,  &CPU::StrbPrip,   &CPU::StrbPrip,
        &CPU::StrbPrip,   &CPU::StrbPrip,  &CPU::LdrbPrip,   &CPU::LdrbPrip,  &CPU::LdrbPrip,   &CPU::LdrbPrip,
        &CPU::LdrbPrip,   &CPU::LdrbPrip,  &CPU::LdrbPrip,   &CPU::LdrbPrip,  &CPU::LdrbPrip,   &CPU::LdrbPrip,
        &CPU::LdrbPrip,   &CPU::LdrbPrip,  &CPU::LdrbPrip,   &CPU::LdrbPrip,  &CPU::LdrbPrip,   &CPU::LdrbPrip,
        &CPU::StrPtrmll,  &CPU::UnkArm,    &CPU::StrPtrmlr,  &CPU::UnkArm,    &CPU::StrPtrmar,  &CPU::UnkArm,
        &CPU::StrPtrmrr,  &CPU::UnkArm,    &CPU::StrPtrmll,  &CPU::UnkArm,    &CPU::StrPtrmlr,  &CPU::UnkArm,
        &CPU::StrPtrmar,  &CPU::UnkArm,    &CPU::StrPtrmrr,  &CPU::UnkArm,    &CPU::LdrPtrmll,  &CPU::UnkArm,
        &CPU::LdrPtrmlr,  &CPU::UnkArm,    &CPU::LdrPtrmar,  &CPU::UnkArm,    &CPU::LdrPtrmrr,  &CPU::UnkArm,
        &CPU::LdrPtrmll,  &CPU::UnkArm,    &CPU::LdrPtrmlr,  &CPU::UnkArm,    &CPU::LdrPtrmar,  &CPU::UnkArm,
        &CPU::LdrPtrmrr,  &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::StrbPtrmll, &CPU::UnkArm,
        &CPU::StrbPtrmlr, &CPU::UnkArm,    &CPU::StrbPtrmar, &CPU::UnkArm,    &CPU::StrbPtrmrr, &CPU::UnkArm,
        &CPU::StrbPtrmll, &CPU::UnkArm,    &CPU::StrbPtrmlr, &CPU::UnkArm,    &CPU::StrbPtrmar, &CPU::UnkArm,
        &CPU::StrbPtrmrr, &CPU::UnkArm,    &CPU::LdrbPtrmll, &CPU::UnkArm,    &CPU::LdrbPtrmlr, &CPU::UnkArm,
        &CPU::LdrbPtrmar, &CPU::UnkArm,    &CPU::LdrbPtrmrr, &CPU::UnkArm,    &CPU::LdrbPtrmll, &CPU::UnkArm,
        &CPU::LdrbPtrmlr, &CPU::UnkArm,    &CPU::LdrbPtrmar, &CPU::UnkArm,    &CPU::LdrbPtrmrr, &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::StrPtrpll,  &CPU::UnkArm,    &CPU::StrPtrplr,  &CPU::UnkArm,
        &CPU::StrPtrpar,  &CPU::UnkArm,    &CPU::StrPtrprr,  &CPU::UnkArm,    &CPU::StrPtrpll,  &CPU::UnkArm,
        &CPU::StrPtrplr,  &CPU::UnkArm,    &CPU::StrPtrpar,  &CPU::UnkArm,    &CPU::StrPtrprr,  &CPU::UnkArm,
        &CPU::LdrPtrpll,  &CPU::UnkArm,    &CPU::LdrPtrplr,  &CPU::UnkArm,    &CPU::LdrPtrpar,  &CPU::UnkArm,
        &CPU::LdrPtrprr,  &CPU::UnkArm,    &CPU::LdrPtrpll,  &CPU::UnkArm,    &CPU::LdrPtrplr,  &CPU::UnkArm,
        &CPU::LdrPtrpar,  &CPU::UnkArm,    &CPU::LdrPtrprr,  &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::StrbPtrpll, &CPU::UnkArm,    &CPU::StrbPtrplr, &CPU::UnkArm,    &CPU::StrbPtrpar, &CPU::UnkArm,
        &CPU::StrbPtrprr, &CPU::UnkArm,    &CPU::StrbPtrpll, &CPU::UnkArm,    &CPU::StrbPtrplr, &CPU::UnkArm,
        &CPU::StrbPtrpar, &CPU::UnkArm,    &CPU::StrbPtrprr, &CPU::UnkArm,    &CPU::LdrbPtrpll, &CPU::UnkArm,
        &CPU::LdrbPtrplr, &CPU::UnkArm,    &CPU::LdrbPtrpar, &CPU::UnkArm,    &CPU::LdrbPtrprr, &CPU::UnkArm,
        &CPU::LdrbPtrpll, &CPU::UnkArm,    &CPU::LdrbPtrplr, &CPU::UnkArm,    &CPU::LdrbPtrpar, &CPU::UnkArm,
        &CPU::LdrbPtrprr, &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::StrOfrmll,  &CPU::UnkArm,
        &CPU::StrOfrmlr,  &CPU::UnkArm,    &CPU::StrOfrmar,  &CPU::UnkArm,    &CPU::StrOfrmrr,  &CPU::UnkArm,
        &CPU::StrOfrmll,  &CPU::UnkArm,    &CPU::StrOfrmlr,  &CPU::UnkArm,    &CPU::StrOfrmar,  &CPU::UnkArm,
        &CPU::StrOfrmrr,  &CPU::UnkArm,    &CPU::LdrOfrmll,  &CPU::UnkArm,    &CPU::LdrOfrmlr,  &CPU::UnkArm,
        &CPU::LdrOfrmar,  &CPU::UnkArm,    &CPU::LdrOfrmrr,  &CPU::UnkArm,    &CPU::LdrOfrmll,  &CPU::UnkArm,
        &CPU::LdrOfrmlr,  &CPU::UnkArm,    &CPU::LdrOfrmar,  &CPU::UnkArm,    &CPU::LdrOfrmrr,  &CPU::UnkArm,
        &CPU::StrPrrmll,  &CPU::UnkArm,    &CPU::StrPrrmlr,  &CPU::UnkArm,    &CPU::StrPrrmar,  &CPU::UnkArm,
        &CPU::StrPrrmrr,  &CPU::UnkArm,    &CPU::StrPrrmll,  &CPU::UnkArm,    &CPU::StrPrrmlr,  &CPU::UnkArm,
        &CPU::StrPrrmar,  &CPU::UnkArm,    &CPU::StrPrrmrr,  &CPU::UnkArm,    &CPU::LdrPrrmll,  &CPU::UnkArm,
        &CPU::LdrPrrmlr,  &CPU::UnkArm,    &CPU::LdrPrrmar,  &CPU::UnkArm,    &CPU::LdrPrrmrr,  &CPU::UnkArm,
        &CPU::LdrPrrmll,  &CPU::UnkArm,    &CPU::LdrPrrmlr,  &CPU::UnkArm,    &CPU::LdrPrrmar,  &CPU::UnkArm,
        &CPU::LdrPrrmrr,  &CPU::UnkArm,    &CPU::StrbOfrmll, &CPU::UnkArm,    &CPU::StrbOfrmlr, &CPU::UnkArm,
        &CPU::StrbOfrmar, &CPU::UnkArm,    &CPU::StrbOfrmrr, &CPU::UnkArm,    &CPU::StrbOfrmll, &CPU::UnkArm,
        &CPU::StrbOfrmlr, &CPU::UnkArm,    &CPU::StrbOfrmar, &CPU::UnkArm,    &CPU::StrbOfrmrr, &CPU::UnkArm,
        &CPU::LdrbOfrmll, &CPU::UnkArm,    &CPU::LdrbOfrmlr, &CPU::UnkArm,    &CPU::LdrbOfrmar, &CPU::UnkArm,
        &CPU::LdrbOfrmrr, &CPU::UnkArm,    &CPU::LdrbOfrmll, &CPU::UnkArm,    &CPU::LdrbOfrmlr, &CPU::UnkArm,
        &CPU::LdrbOfrmar, &CPU::UnkArm,    &CPU::LdrbOfrmrr, &CPU::UnkArm,    &CPU::StrbPrrmll, &CPU::UnkArm,
        &CPU::StrbPrrmlr, &CPU::UnkArm,    &CPU::StrbPrrmar, &CPU::UnkArm,    &CPU::StrbPrrmrr, &CPU::UnkArm,
        &CPU::StrbPrrmll, &CPU::UnkArm,    &CPU::StrbPrrmlr, &CPU::UnkArm,    &CPU::StrbPrrmar, &CPU::UnkArm,
        &CPU::StrbPrrmrr, &CPU::UnkArm,    &CPU::LdrbPrrmll, &CPU::UnkArm,    &CPU::LdrbPrrmlr, &CPU::UnkArm,
        &CPU::LdrbPrrmar, &CPU::UnkArm,    &CPU::LdrbPrrmrr, &CPU::UnkArm,    &CPU::LdrbPrrmll, &CPU::UnkArm,
        &CPU::LdrbPrrmlr, &CPU::UnkArm,    &CPU::LdrbPrrmar, &CPU::UnkArm,    &CPU::LdrbPrrmrr, &CPU::UnkArm,
        &CPU::StrOfrpll,  &CPU::UnkArm,    &CPU::StrOfrplr,  &CPU::UnkArm,    &CPU::StrOfrpar,  &CPU::UnkArm,
        &CPU::StrOfrprr,  &CPU::UnkArm,    &CPU::StrOfrpll,  &CPU::UnkArm,    &CPU::StrOfrplr,  &CPU::UnkArm,
        &CPU::StrOfrpar,  &CPU::UnkArm,    &CPU::StrOfrprr,  &CPU::UnkArm,    &CPU::LdrOfrpll,  &CPU::UnkArm,
        &CPU::LdrOfrplr,  &CPU::UnkArm,    &CPU::LdrOfrpar,  &CPU::UnkArm,    &CPU::LdrOfrprr,  &CPU::UnkArm,
        &CPU::LdrOfrpll,  &CPU::UnkArm,    &CPU::LdrOfrplr,  &CPU::UnkArm,    &CPU::LdrOfrpar,  &CPU::UnkArm,
        &CPU::LdrOfrprr,  &CPU::UnkArm,    &CPU::StrPrrpll,  &CPU::UnkArm,    &CPU::StrPrrplr,  &CPU::UnkArm,
        &CPU::StrPrrpar,  &CPU::UnkArm,    &CPU::StrPrrprr,  &CPU::UnkArm,    &CPU::StrPrrpll,  &CPU::UnkArm,
        &CPU::StrPrrplr,  &CPU::UnkArm,    &CPU::StrPrrpar,  &CPU::UnkArm,    &CPU::StrPrrprr,  &CPU::UnkArm,
        &CPU::LdrPrrpll,  &CPU::UnkArm,    &CPU::LdrPrrplr,  &CPU::UnkArm,    &CPU::LdrPrrpar,  &CPU::UnkArm,
        &CPU::LdrPrrprr,  &CPU::UnkArm,    &CPU::LdrPrrpll,  &CPU::UnkArm,    &CPU::LdrPrrplr,  &CPU::UnkArm,
        &CPU::LdrPrrpar,  &CPU::UnkArm,    &CPU::LdrPrrprr,  &CPU::UnkArm,    &CPU::StrbOfrpll, &CPU::UnkArm,
        &CPU::StrbOfrplr, &CPU::UnkArm,    &CPU::StrbOfrpar, &CPU::UnkArm,    &CPU::StrbOfrprr, &CPU::UnkArm,
        &CPU::StrbOfrpll, &CPU::UnkArm,    &CPU::StrbOfrplr, &CPU::UnkArm,    &CPU::StrbOfrpar, &CPU::UnkArm,
        &CPU::StrbOfrprr, &CPU::UnkArm,    &CPU::LdrbOfrpll, &CPU::UnkArm,    &CPU::LdrbOfrplr, &CPU::UnkArm,
        &CPU::LdrbOfrpar, &CPU::UnkArm,    &CPU::LdrbOfrprr, &CPU::UnkArm,    &CPU::LdrbOfrpll, &CPU::UnkArm,
        &CPU::LdrbOfrplr, &CPU::UnkArm,    &CPU::LdrbOfrpar, &CPU::UnkArm,    &CPU::LdrbOfrprr, &CPU::UnkArm,
        &CPU::StrbPrrpll, &CPU::UnkArm,    &CPU::StrbPrrplr, &CPU::UnkArm,    &CPU::StrbPrrpar, &CPU::UnkArm,
        &CPU::StrbPrrprr, &CPU::UnkArm,    &CPU::StrbPrrpll, &CPU::UnkArm,    &CPU::StrbPrrplr, &CPU::UnkArm,
        &CPU::StrbPrrpar, &CPU::UnkArm,    &CPU::StrbPrrprr, &CPU::UnkArm,    &CPU::LdrbPrrpll, &CPU::UnkArm,
        &CPU::LdrbPrrplr, &CPU::UnkArm,    &CPU::LdrbPrrpar, &CPU::UnkArm,    &CPU::LdrbPrrprr, &CPU::UnkArm,
        &CPU::LdrbPrrpll, &CPU::UnkArm,    &CPU::LdrbPrrplr, &CPU::UnkArm,    &CPU::LdrbPrrpar, &CPU::UnkArm,
        &CPU::LdrbPrrprr, &CPU::UnkArm,    &CPU::Stmda,      &CPU::Stmda,     &CPU::Stmda,      &CPU::Stmda,
        &CPU::Stmda,      &CPU::Stmda,     &CPU::Stmda,      &CPU::Stmda,     &CPU::Stmda,      &CPU::Stmda,
        &CPU::Stmda,      &CPU::Stmda,     &CPU::Stmda,      &CPU::Stmda,     &CPU::Stmda,      &CPU::Stmda,
        &CPU::Ldmda,      &CPU::Ldmda,     &CPU::Ldmda,      &CPU::Ldmda,     &CPU::Ldmda,      &CPU::Ldmda,
        &CPU::Ldmda,      &CPU::Ldmda,     &CPU::Ldmda,      &CPU::Ldmda,     &CPU::Ldmda,      &CPU::Ldmda,
        &CPU::Ldmda,      &CPU::Ldmda,     &CPU::Ldmda,      &CPU::Ldmda,     &CPU::StmdaW,     &CPU::StmdaW,
        &CPU::StmdaW,     &CPU::StmdaW,    &CPU::StmdaW,     &CPU::StmdaW,    &CPU::StmdaW,     &CPU::StmdaW,
        &CPU::StmdaW,     &CPU::StmdaW,    &CPU::StmdaW,     &CPU::StmdaW,    &CPU::StmdaW,     &CPU::StmdaW,
        &CPU::StmdaW,     &CPU::StmdaW,    &CPU::LdmdaW,     &CPU::LdmdaW,    &CPU::LdmdaW,     &CPU::LdmdaW,
        &CPU::LdmdaW,     &CPU::LdmdaW,    &CPU::LdmdaW,     &CPU::LdmdaW,    &CPU::LdmdaW,     &CPU::LdmdaW,
        &CPU::LdmdaW,     &CPU::LdmdaW,    &CPU::LdmdaW,     &CPU::LdmdaW,    &CPU::LdmdaW,     &CPU::LdmdaW,
        &CPU::StmdaU,     &CPU::StmdaU,    &CPU::StmdaU,     &CPU::StmdaU,    &CPU::StmdaU,     &CPU::StmdaU,
        &CPU::StmdaU,     &CPU::StmdaU,    &CPU::StmdaU,     &CPU::StmdaU,    &CPU::StmdaU,     &CPU::StmdaU,
        &CPU::StmdaU,     &CPU::StmdaU,    &CPU::StmdaU,     &CPU::StmdaU,    &CPU::LdmdaU,     &CPU::LdmdaU,
        &CPU::LdmdaU,     &CPU::LdmdaU,    &CPU::LdmdaU,     &CPU::LdmdaU,    &CPU::LdmdaU,     &CPU::LdmdaU,
        &CPU::LdmdaU,     &CPU::LdmdaU,    &CPU::LdmdaU,     &CPU::LdmdaU,    &CPU::LdmdaU,     &CPU::LdmdaU,
        &CPU::LdmdaU,     &CPU::LdmdaU,    &CPU::StmdaUW,    &CPU::StmdaUW,   &CPU::StmdaUW,    &CPU::StmdaUW,
        &CPU::StmdaUW,    &CPU::StmdaUW,   &CPU::StmdaUW,    &CPU::StmdaUW,   &CPU::StmdaUW,    &CPU::StmdaUW,
        &CPU::StmdaUW,    &CPU::StmdaUW,   &CPU::StmdaUW,    &CPU::StmdaUW,   &CPU::StmdaUW,    &CPU::StmdaUW,
        &CPU::LdmdaUW,    &CPU::LdmdaUW,   &CPU::LdmdaUW,    &CPU::LdmdaUW,   &CPU::LdmdaUW,    &CPU::LdmdaUW,
        &CPU::LdmdaUW,    &CPU::LdmdaUW,   &CPU::LdmdaUW,    &CPU::LdmdaUW,   &CPU::LdmdaUW,    &CPU::LdmdaUW,
        &CPU::LdmdaUW,    &CPU::LdmdaUW,   &CPU::LdmdaUW,    &CPU::LdmdaUW,   &CPU::Stmia,      &CPU::Stmia,
        &CPU::Stmia,      &CPU::Stmia,     &CPU::Stmia,      &CPU::Stmia,     &CPU::Stmia,      &CPU::Stmia,
        &CPU::Stmia,      &CPU::Stmia,     &CPU::Stmia,      &CPU::Stmia,     &CPU::Stmia,      &CPU::Stmia,
        &CPU::Stmia,      &CPU::Stmia,     &CPU::Ldmia,      &CPU::Ldmia,     &CPU::Ldmia,      &CPU::Ldmia,
        &CPU::Ldmia,      &CPU::Ldmia,     &CPU::Ldmia,      &CPU::Ldmia,     &CPU::Ldmia,      &CPU::Ldmia,
        &CPU::Ldmia,      &CPU::Ldmia,     &CPU::Ldmia,      &CPU::Ldmia,     &CPU::Ldmia,      &CPU::Ldmia,
        &CPU::StmiaW,     &CPU::StmiaW,    &CPU::StmiaW,     &CPU::StmiaW,    &CPU::StmiaW,     &CPU::StmiaW,
        &CPU::StmiaW,     &CPU::StmiaW,    &CPU::StmiaW,     &CPU::StmiaW,    &CPU::StmiaW,     &CPU::StmiaW,
        &CPU::StmiaW,     &CPU::StmiaW,    &CPU::StmiaW,     &CPU::StmiaW,    &CPU::LdmiaW,     &CPU::LdmiaW,
        &CPU::LdmiaW,     &CPU::LdmiaW,    &CPU::LdmiaW,     &CPU::LdmiaW,    &CPU::LdmiaW,     &CPU::LdmiaW,
        &CPU::LdmiaW,     &CPU::LdmiaW,    &CPU::LdmiaW,     &CPU::LdmiaW,    &CPU::LdmiaW,     &CPU::LdmiaW,
        &CPU::LdmiaW,     &CPU::LdmiaW,    &CPU::StmiaU,     &CPU::StmiaU,    &CPU::StmiaU,     &CPU::StmiaU,
        &CPU::StmiaU,     &CPU::StmiaU,    &CPU::StmiaU,     &CPU::StmiaU,    &CPU::StmiaU,     &CPU::StmiaU,
        &CPU::StmiaU,     &CPU::StmiaU,    &CPU::StmiaU,     &CPU::StmiaU,    &CPU::StmiaU,     &CPU::StmiaU,
        &CPU::LdmiaU,     &CPU::LdmiaU,    &CPU::LdmiaU,     &CPU::LdmiaU,    &CPU::LdmiaU,     &CPU::LdmiaU,
        &CPU::LdmiaU,     &CPU::LdmiaU,    &CPU::LdmiaU,     &CPU::LdmiaU,    &CPU::LdmiaU,     &CPU::LdmiaU,
        &CPU::LdmiaU,     &CPU::LdmiaU,    &CPU::LdmiaU,     &CPU::LdmiaU,    &CPU::StmiaUW,    &CPU::StmiaUW,
        &CPU::StmiaUW,    &CPU::StmiaUW,   &CPU::StmiaUW,    &CPU::StmiaUW,   &CPU::StmiaUW,    &CPU::StmiaUW,
        &CPU::StmiaUW,    &CPU::StmiaUW,   &CPU::StmiaUW,    &CPU::StmiaUW,   &CPU::StmiaUW,    &CPU::StmiaUW,
        &CPU::StmiaUW,    &CPU::StmiaUW,   &CPU::LdmiaUW,    &CPU::LdmiaUW,   &CPU::LdmiaUW,    &CPU::LdmiaUW,
        &CPU::LdmiaUW,    &CPU::LdmiaUW,   &CPU::LdmiaUW,    &CPU::LdmiaUW,   &CPU::LdmiaUW,    &CPU::LdmiaUW,
        &CPU::LdmiaUW,    &CPU::LdmiaUW,   &CPU::LdmiaUW,    &CPU::LdmiaUW,   &CPU::LdmiaUW,    &CPU::LdmiaUW,
        &CPU::Stmdb,      &CPU::Stmdb,     &CPU::Stmdb,      &CPU::Stmdb,     &CPU::Stmdb,      &CPU::Stmdb,
        &CPU::Stmdb,      &CPU::Stmdb,     &CPU::Stmdb,      &CPU::Stmdb,     &CPU::Stmdb,      &CPU::Stmdb,
        &CPU::Stmdb,      &CPU::Stmdb,     &CPU::Stmdb,      &CPU::Stmdb,     &CPU::Ldmdb,      &CPU::Ldmdb,
        &CPU::Ldmdb,      &CPU::Ldmdb,     &CPU::Ldmdb,      &CPU::Ldmdb,     &CPU::Ldmdb,      &CPU::Ldmdb,
        &CPU::Ldmdb,      &CPU::Ldmdb,     &CPU::Ldmdb,      &CPU::Ldmdb,     &CPU::Ldmdb,      &CPU::Ldmdb,
        &CPU::Ldmdb,      &CPU::Ldmdb,     &CPU::StmdbW,     &CPU::StmdbW,    &CPU::StmdbW,     &CPU::StmdbW,
        &CPU::StmdbW,     &CPU::StmdbW,    &CPU::StmdbW,     &CPU::StmdbW,    &CPU::StmdbW,     &CPU::StmdbW,
        &CPU::StmdbW,     &CPU::StmdbW,    &CPU::StmdbW,     &CPU::StmdbW,    &CPU::StmdbW,     &CPU::StmdbW,
        &CPU::LdmdbW,     &CPU::LdmdbW,    &CPU::LdmdbW,     &CPU::LdmdbW,    &CPU::LdmdbW,     &CPU::LdmdbW,
        &CPU::LdmdbW,     &CPU::LdmdbW,    &CPU::LdmdbW,     &CPU::LdmdbW,    &CPU::LdmdbW,     &CPU::LdmdbW,
        &CPU::LdmdbW,     &CPU::LdmdbW,    &CPU::LdmdbW,     &CPU::LdmdbW,    &CPU::StmdbU,     &CPU::StmdbU,
        &CPU::StmdbU,     &CPU::StmdbU,    &CPU::StmdbU,     &CPU::StmdbU,    &CPU::StmdbU,     &CPU::StmdbU,
        &CPU::StmdbU,     &CPU::StmdbU,    &CPU::StmdbU,     &CPU::StmdbU,    &CPU::StmdbU,     &CPU::StmdbU,
        &CPU::StmdbU,     &CPU::StmdbU,    &CPU::LdmdbU,     &CPU::LdmdbU,    &CPU::LdmdbU,     &CPU::LdmdbU,
        &CPU::LdmdbU,     &CPU::LdmdbU,    &CPU::LdmdbU,     &CPU::LdmdbU,    &CPU::LdmdbU,     &CPU::LdmdbU,
        &CPU::LdmdbU,     &CPU::LdmdbU,    &CPU::LdmdbU,     &CPU::LdmdbU,    &CPU::LdmdbU,     &CPU::LdmdbU,
        &CPU::StmdbUW,    &CPU::StmdbUW,   &CPU::StmdbUW,    &CPU::StmdbUW,   &CPU::StmdbUW,    &CPU::StmdbUW,
        &CPU::StmdbUW,    &CPU::StmdbUW,   &CPU::StmdbUW,    &CPU::StmdbUW,   &CPU::StmdbUW,    &CPU::StmdbUW,
        &CPU::StmdbUW,    &CPU::StmdbUW,   &CPU::StmdbUW,    &CPU::StmdbUW,   &CPU::LdmdbUW,    &CPU::LdmdbUW,
        &CPU::LdmdbUW,    &CPU::LdmdbUW,   &CPU::LdmdbUW,    &CPU::LdmdbUW,   &CPU::LdmdbUW,    &CPU::LdmdbUW,
        &CPU::LdmdbUW,    &CPU::LdmdbUW,   &CPU::LdmdbUW,    &CPU::LdmdbUW,   &CPU::LdmdbUW,    &CPU::LdmdbUW,
        &CPU::LdmdbUW,    &CPU::LdmdbUW,   &CPU::Stmib,      &CPU::Stmib,     &CPU::Stmib,      &CPU::Stmib,
        &CPU::Stmib,      &CPU::Stmib,     &CPU::Stmib,      &CPU::Stmib,     &CPU::Stmib,      &CPU::Stmib,
        &CPU::Stmib,      &CPU::Stmib,     &CPU::Stmib,      &CPU::Stmib,     &CPU::Stmib,      &CPU::Stmib,
        &CPU::Ldmib,      &CPU::Ldmib,     &CPU::Ldmib,      &CPU::Ldmib,     &CPU::Ldmib,      &CPU::Ldmib,
        &CPU::Ldmib,      &CPU::Ldmib,     &CPU::Ldmib,      &CPU::Ldmib,     &CPU::Ldmib,      &CPU::Ldmib,
        &CPU::Ldmib,      &CPU::Ldmib,     &CPU::Ldmib,      &CPU::Ldmib,     &CPU::StmibW,     &CPU::StmibW,
        &CPU::StmibW,     &CPU::StmibW,    &CPU::StmibW,     &CPU::StmibW,    &CPU::StmibW,     &CPU::StmibW,
        &CPU::StmibW,     &CPU::StmibW,    &CPU::StmibW,     &CPU::StmibW,    &CPU::StmibW,     &CPU::StmibW,
        &CPU::StmibW,     &CPU::StmibW,    &CPU::LdmibW,     &CPU::LdmibW,    &CPU::LdmibW,     &CPU::LdmibW,
        &CPU::LdmibW,     &CPU::LdmibW,    &CPU::LdmibW,     &CPU::LdmibW,    &CPU::LdmibW,     &CPU::LdmibW,
        &CPU::LdmibW,     &CPU::LdmibW,    &CPU::LdmibW,     &CPU::LdmibW,    &CPU::LdmibW,     &CPU::LdmibW,
        &CPU::StmibU,     &CPU::StmibU,    &CPU::StmibU,     &CPU::StmibU,    &CPU::StmibU,     &CPU::StmibU,
        &CPU::StmibU,     &CPU::StmibU,    &CPU::StmibU,     &CPU::StmibU,    &CPU::StmibU,     &CPU::StmibU,
        &CPU::StmibU,     &CPU::StmibU,    &CPU::StmibU,     &CPU::StmibU,    &CPU::LdmibU,     &CPU::LdmibU,
        &CPU::LdmibU,     &CPU::LdmibU,    &CPU::LdmibU,     &CPU::LdmibU,    &CPU::LdmibU,     &CPU::LdmibU,
        &CPU::LdmibU,     &CPU::LdmibU,    &CPU::LdmibU,     &CPU::LdmibU,    &CPU::LdmibU,     &CPU::LdmibU,
        &CPU::LdmibU,     &CPU::LdmibU,    &CPU::StmibUW,    &CPU::StmibUW,   &CPU::StmibUW,    &CPU::StmibUW,
        &CPU::StmibUW,    &CPU::StmibUW,   &CPU::StmibUW,    &CPU::StmibUW,   &CPU::StmibUW,    &CPU::StmibUW,
        &CPU::StmibUW,    &CPU::StmibUW,   &CPU::StmibUW,    &CPU::StmibUW,   &CPU::StmibUW,    &CPU::StmibUW,
        &CPU::LdmibUW,    &CPU::LdmibUW,   &CPU::LdmibUW,    &CPU::LdmibUW,   &CPU::LdmibUW,    &CPU::LdmibUW,
        &CPU::LdmibUW,    &CPU::LdmibUW,   &CPU::LdmibUW,    &CPU::LdmibUW,   &CPU::LdmibUW,    &CPU::LdmibUW,
        &CPU::LdmibUW,    &CPU::LdmibUW,   &CPU::LdmibUW,    &CPU::LdmibUW,   &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,         &CPU::B,          &CPU::B,
        &CPU::B,          &CPU::B,         &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,        &CPU::Bl,         &CPU::Bl,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::UnkArm,
        &CPU::UnkArm,     &CPU::UnkArm,    &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mcr,
        &CPU::UnkArm,     &CPU::Mcr,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,       &CPU::UnkArm,     &CPU::Mrc,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi,
        &CPU::Swi,        &CPU::Swi,       &CPU::Swi,        &CPU::Swi};
    int (CPU::*m_thumbInstrs[0x400])(uint16_t) = {
        &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,
        &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,
        &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,
        &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,
        &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LslImmT,
        &CPU::LslImmT,   &CPU::LslImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,
        &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,
        &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,
        &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,
        &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,
        &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::LsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,
        &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,
        &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,
        &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,
        &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,
        &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,   &CPU::AsrImmT,
        &CPU::AddRegT,   &CPU::AddRegT,   &CPU::AddRegT,   &CPU::AddRegT,   &CPU::AddRegT,   &CPU::AddRegT,
        &CPU::AddRegT,   &CPU::AddRegT,   &CPU::SubRegT,   &CPU::SubRegT,   &CPU::SubRegT,   &CPU::SubRegT,
        &CPU::SubRegT,   &CPU::SubRegT,   &CPU::SubRegT,   &CPU::SubRegT,   &CPU::AddImm3T,  &CPU::AddImm3T,
        &CPU::AddImm3T,  &CPU::AddImm3T,  &CPU::AddImm3T,  &CPU::AddImm3T,  &CPU::AddImm3T,  &CPU::AddImm3T,
        &CPU::SubImm3T,  &CPU::SubImm3T,  &CPU::SubImm3T,  &CPU::SubImm3T,  &CPU::SubImm3T,  &CPU::SubImm3T,
        &CPU::SubImm3T,  &CPU::SubImm3T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,
        &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,
        &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,
        &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,
        &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,
        &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::MovImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,
        &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,
        &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,
        &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,
        &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,
        &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,  &CPU::CmpImm8T,
        &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,
        &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,
        &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,
        &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,
        &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::AddImm8T,
        &CPU::AddImm8T,  &CPU::AddImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,
        &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,
        &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,
        &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,
        &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,
        &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::SubImm8T,  &CPU::AndDpT,    &CPU::EorDpT,
        &CPU::LslDpT,    &CPU::LsrDpT,    &CPU::AsrDpT,    &CPU::AdcDpT,    &CPU::SbcDpT,    &CPU::RorDpT,
        &CPU::TstDpT,    &CPU::NegDpT,    &CPU::CmpDpT,    &CPU::CmnDpT,    &CPU::OrrDpT,    &CPU::MulDpT,
        &CPU::BicDpT,    &CPU::MvnDpT,    &CPU::AddHT,     &CPU::AddHT,     &CPU::AddHT,     &CPU::AddHT,
        &CPU::CmpHT,     &CPU::CmpHT,     &CPU::CmpHT,     &CPU::CmpHT,     &CPU::MovHT,     &CPU::MovHT,
        &CPU::MovHT,     &CPU::MovHT,     &CPU::BxRegT,    &CPU::BxRegT,    &CPU::BlxRegT,   &CPU::BlxRegT,
        &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,
        &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,
        &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,
        &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,
        &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::LdrPcT,
        &CPU::LdrPcT,    &CPU::LdrPcT,    &CPU::StrRegT,   &CPU::StrRegT,   &CPU::StrRegT,   &CPU::StrRegT,
        &CPU::StrRegT,   &CPU::StrRegT,   &CPU::StrRegT,   &CPU::StrRegT,   &CPU::StrhRegT,  &CPU::StrhRegT,
        &CPU::StrhRegT,  &CPU::StrhRegT,  &CPU::StrhRegT,  &CPU::StrhRegT,  &CPU::StrhRegT,  &CPU::StrhRegT,
        &CPU::StrbRegT,  &CPU::StrbRegT,  &CPU::StrbRegT,  &CPU::StrbRegT,  &CPU::StrbRegT,  &CPU::StrbRegT,
        &CPU::StrbRegT,  &CPU::StrbRegT,  &CPU::LdrsbRegT, &CPU::LdrsbRegT, &CPU::LdrsbRegT, &CPU::LdrsbRegT,
        &CPU::LdrsbRegT, &CPU::LdrsbRegT, &CPU::LdrsbRegT, &CPU::LdrsbRegT, &CPU::LdrRegT,   &CPU::LdrRegT,
        &CPU::LdrRegT,   &CPU::LdrRegT,   &CPU::LdrRegT,   &CPU::LdrRegT,   &CPU::LdrRegT,   &CPU::LdrRegT,
        &CPU::LdrhRegT,  &CPU::LdrhRegT,  &CPU::LdrhRegT,  &CPU::LdrhRegT,  &CPU::LdrhRegT,  &CPU::LdrhRegT,
        &CPU::LdrhRegT,  &CPU::LdrhRegT,  &CPU::LdrbRegT,  &CPU::LdrbRegT,  &CPU::LdrbRegT,  &CPU::LdrbRegT,
        &CPU::LdrbRegT,  &CPU::LdrbRegT,  &CPU::LdrbRegT,  &CPU::LdrbRegT,  &CPU::LdrshRegT, &CPU::LdrshRegT,
        &CPU::LdrshRegT, &CPU::LdrshRegT, &CPU::LdrshRegT, &CPU::LdrshRegT, &CPU::LdrshRegT, &CPU::LdrshRegT,
        &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,
        &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,
        &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,
        &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,
        &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::StrImm5T,
        &CPU::StrImm5T,  &CPU::StrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,
        &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,
        &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,
        &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,
        &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,
        &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::LdrImm5T,  &CPU::StrbImm5T, &CPU::StrbImm5T,
        &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T,
        &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T,
        &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T,
        &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T,
        &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T, &CPU::StrbImm5T,
        &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T,
        &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T,
        &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T,
        &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T,
        &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::LdrbImm5T,
        &CPU::LdrbImm5T, &CPU::LdrbImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T,
        &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T,
        &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T,
        &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T,
        &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T,
        &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::StrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T,
        &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T,
        &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T,
        &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T,
        &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T,
        &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T, &CPU::LdrhImm5T,
        &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,
        &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,
        &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,
        &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,
        &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,    &CPU::StrSpT,
        &CPU::StrSpT,    &CPU::StrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,
        &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,
        &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,
        &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,
        &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,
        &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::LdrSpT,    &CPU::AddPcT,    &CPU::AddPcT,
        &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,
        &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,
        &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,
        &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,
        &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,    &CPU::AddPcT,
        &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,
        &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,
        &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,
        &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,
        &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpT,
        &CPU::AddSpT,    &CPU::AddSpT,    &CPU::AddSpImmT, &CPU::AddSpImmT, &CPU::AddSpImmT, &CPU::AddSpImmT,
        &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,
        &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,
        &CPU::PushT,     &CPU::PushT,     &CPU::PushT,     &CPU::PushT,     &CPU::PushLrT,   &CPU::PushLrT,
        &CPU::PushLrT,   &CPU::PushLrT,   &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,
        &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,
        &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,
        &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,
        &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::PopT,      &CPU::PopT,      &CPU::PopT,      &CPU::PopT,
        &CPU::PopPcT,    &CPU::PopPcT,    &CPU::PopPcT,    &CPU::PopPcT,    &CPU::UnkThumb,  &CPU::UnkThumb,
        &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,
        &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,
        &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,
        &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,
        &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,
        &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,    &CPU::StmiaT,
        &CPU::StmiaT,    &CPU::StmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,
        &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,
        &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,
        &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,
        &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,
        &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::LdmiaT,    &CPU::BeqT,      &CPU::BeqT,
        &CPU::BeqT,      &CPU::BeqT,      &CPU::BneT,      &CPU::BneT,      &CPU::BneT,      &CPU::BneT,
        &CPU::BcsT,      &CPU::BcsT,      &CPU::BcsT,      &CPU::BcsT,      &CPU::BccT,      &CPU::BccT,
        &CPU::BccT,      &CPU::BccT,      &CPU::BmiT,      &CPU::BmiT,      &CPU::BmiT,      &CPU::BmiT,
        &CPU::BplT,      &CPU::BplT,      &CPU::BplT,      &CPU::BplT,      &CPU::BvsT,      &CPU::BvsT,
        &CPU::BvsT,      &CPU::BvsT,      &CPU::BvcT,      &CPU::BvcT,      &CPU::BvcT,      &CPU::BvcT,
        &CPU::BhiT,      &CPU::BhiT,      &CPU::BhiT,      &CPU::BhiT,      &CPU::BlsT,      &CPU::BlsT,
        &CPU::BlsT,      &CPU::BlsT,      &CPU::BgeT,      &CPU::BgeT,      &CPU::BgeT,      &CPU::BgeT,
        &CPU::BltT,      &CPU::BltT,      &CPU::BltT,      &CPU::BltT,      &CPU::BgtT,      &CPU::BgtT,
        &CPU::BgtT,      &CPU::BgtT,      &CPU::BleT,      &CPU::BleT,      &CPU::BleT,      &CPU::BleT,
        &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::UnkThumb,  &CPU::SwiT,      &CPU::SwiT,
        &CPU::SwiT,      &CPU::SwiT,      &CPU::BT,        &CPU::BT,        &CPU::BT,        &CPU::BT,
        &CPU::BT,        &CPU::BT,        &CPU::BT,        &CPU::BT,        &CPU::BT,        &CPU::BT,
        &CPU::BT,        &CPU::BT,        &CPU::BT,        &CPU::BT,        &CPU::BT,        &CPU::BT,
        &CPU::BT,        &CPU::BT,        &CPU::BT,        &CPU::BT,        &CPU::BT,        &CPU::BT,
        &CPU::BT,        &CPU::BT,        &CPU::BT,        &CPU::BT,        &CPU::BT,        &CPU::BT,
        &CPU::BT,        &CPU::BT,        &CPU::BT,        &CPU::BT,        &CPU::BlxOffT,   &CPU::BlxOffT,
        &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,
        &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,
        &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,
        &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,
        &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,   &CPU::BlxOffT,
        &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,
        &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,
        &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,
        &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,
        &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlSetupT,
        &CPU::BlSetupT,  &CPU::BlSetupT,  &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,
        &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,
        &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,
        &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,
        &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,
        &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT,    &CPU::BlOffT};

    const uint8_t condition[0x100] = {
        0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0,
        0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,
        1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0,
        1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1,
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};


    const uint8_t bitCount[0x100] = {
        0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2,
        3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3,
        3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5,
        6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4,
        3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4,
        5, 5, 6, 5, 6, 6, 7, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6,
        6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8};
};

} // namespace consoles::arm

