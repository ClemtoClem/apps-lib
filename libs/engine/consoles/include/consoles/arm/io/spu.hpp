
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <queue>
#include <mutex>
#include <memory>
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {

class Core;

/**
 * @brief Sound Processing Unit (SPU) emulator class.
 * 
 * Cette classe gère l'émulation audio des systèmes supportés (GBA, etc.).
 * Elle traite les registres sonores, les buffers audio, les mixages, 
 * et la génération des échantillons.
 */
class Spu {
  public:
    /**
     * @brief Construit une instance du SPU.
     * @param core Pointeur vers l'instance principale du noyau (Core).
     */
    Spu(Core *core);

    /**
     * @brief Destructeur, libère les buffers audio.
     */
    ~Spu();

    /**
     * @brief Planifie l'initialisation de la tâche audio principale.
     */
    void ScheduleInit();

    /**
     * @brief Planifie l'initialisation de la tâche audio spécifique GBA.
     */
    void GbaScheduleInit();

    /// Reçoit chaque trame stéréo produite (32 768 Hz en mode NDS, 65 536 Hz en mode GBA),
    /// sur le fil d'émulation.
    std::function<void(int16_t left, int16_t right)> onSample;

    /**
     * @brief Déclenche le traitement FIFO du GBA pour un timer donné.
     * @param timer Numéro du timer GBA utilisé.
     */
    void GbaFifoTimer(int timer);

    /**
     * @brief Lecture du registre GBA SOUNDxCNT_L.
     * @param channel Canal sonore (0–3).
     * @return Valeur du registre.
     */
    uint8_t ReadGbaSoundCntL(int channel);

    /**
     * @brief Lecture du registre GBA SOUNDxCNT_H.
     * @param channel Canal sonore (0–3).
     * @return Valeur du registre.
     */
    uint16_t ReadGbaSoundCntH(int channel);

    /**
     * @brief Lecture du registre GBA SOUNDxCNT_X.
     * @param channel Canal sonore (0–3).
     * @return Valeur du registre.
     */
    uint16_t ReadGbaSoundCntX(int channel);

    /**
     * @brief Lecture du registre GBA SOUNDCNT_L (global).
     * @return Valeur du registre.
     */
    uint16_t ReadGbaMainSoundCntL();

    /**
     * @brief Lecture du registre GBA SOUNDCNT_H (global).
     * @return Valeur du registre.
     */
    uint16_t ReadGbaMainSoundCntH();

    /**
     * @brief Lecture du registre GBA SOUNDCNT_X (global).
     * @return Valeur du registre.
     */
    uint8_t ReadGbaMainSoundCntX();

    /**
     * @brief Lecture du registre GBA SOUNDBIAS.
     * @return Valeur du registre.
     */
    uint16_t ReadGbaSoundBias();

    /**
     * @brief Lecture de la Wave RAM GBA.
     * @param index Index (0–31).
     * @return Octet stocké.
     */
    uint8_t ReadGbaWaveRam(int index);

    /**
     * @brief Lecture du registre SOUNDxCNT.
     * @param channel Canal sonore (0–15).
     * @return Valeur du registre.
     */
    uint32_t ReadSoundCnt(int channel);

    /**
     * @brief Lecture du registre MAIN_SOUNDCNT.
     * @return Valeur du registre.
     */
    uint16_t ReadMainSoundCnt();

    /**
     * @brief Lecture du registre SOUNDBIAS.
     * @return Valeur du registre.
     */
    uint16_t ReadSoundBias();

    /**
     * @brief Lecture du registre SNDCAPCNT (Sound Capture).
     * @param channel Canal de capture (0 ou 1).
     * @return Valeur du registre.
     */
    uint8_t ReadSndCapCnt(int channel);

    /**
     * @brief Lecture du registre SNDCAPDAD (destination address).
     * @param channel Canal de capture (0 ou 1).
     * @return Valeur du registre.
     */
    uint32_t ReadSndCapDad(int channel);

    /**
     * @brief Écriture dans le registre GBA SOUNDxCNT_L.
     * @param channel Canal sonore (0–3).
     * @param value Valeur à écrire.
     */
    void WriteGbaSoundCntL(int channel, uint8_t value);

    /**
     * @brief Écriture dans le registre GBA SOUNDxCNT_H.
     * @param channel Canal sonore (0–3).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteGbaSoundCntH(int channel, uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre GBA SOUNDxCNT_X.
     * @param channel Canal sonore (0–3).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteGbaSoundCntX(int channel, uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre global GBA SOUNDCNT_L.
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteGbaMainSoundCntL(uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre global GBA SOUNDCNT_H.
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteGbaMainSoundCntH(uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre global GBA SOUNDCNT_X.
     * @param value Valeur à écrire.
     */
    void WriteGbaMainSoundCntX(uint8_t value);

    /**
     * @brief Écriture dans le registre global GBA SOUNDBIAS.
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteGbaSoundBias(uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans la Wave RAM GBA.
     * @param index Index (0–31).
     * @param value Octet à écrire.
     */
    void WriteGbaWaveRam(int index, uint8_t value);

    /**
     * @brief Écriture dans le FIFO A du GBA.
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteGbaFifoA(uint32_t mask, uint32_t value);

    /**
     * @brief Écriture dans le FIFO B du GBA.
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteGbaFifoB(uint32_t mask, uint32_t value);

    /**
     * @brief Écriture dans le registre SOUNDxCNT.
     * @param channel Canal sonore (0–15).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteSoundCnt(int channel, uint32_t mask, uint32_t value);

    /**
     * @brief Écriture dans le registre SOUNDxSAD.
     * @param channel Canal sonore (0–15).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteSoundSad(int channel, uint32_t mask, uint32_t value);

    /**
     * @brief Écriture dans le registre SOUNDxTMR.
     * @param channel Canal sonore (0–15).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteSoundTmr(int channel, uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre SOUNDxPNT.
     * @param channel Canal sonore (0–15).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteSoundPnt(int channel, uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre SOUNDxLEN.
     * @param channel Canal sonore (0–15).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteSoundLen(int channel, uint32_t mask, uint32_t value);

    /**
     * @brief Écriture dans le registre MAIN_SOUNDCNT.
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteMainSoundCnt(uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre SOUNDBIAS.
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteSoundBias(uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre SNDCAPCNT (Sound Capture).
     * @param channel Canal de capture (0 ou 1).
     * @param value Valeur à écrire.
     */
    void WriteSndCapCnt(int channel, uint8_t value);

    /**
     * @brief Écriture dans le registre SNDCAPDAD (Sound Capture).
     * @param channel Canal de capture (0 ou 1).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteSndCapDad(int channel, uint32_t mask, uint32_t value);

    /**
     * @brief Écriture dans le registre SNDCAPLEN (Sound Capture).
     * @param channel Canal de capture (0 ou 1).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void WriteSndCapLen(int channel, uint16_t mask, uint16_t value);

    /**
     * @brief Sérialise/désérialise l'état du SPU pour les save states.
     *
     * Les échantillons déjà remis à `onSample` ne font pas partie de l'état.
     * @param archive Archive de sauvegarde/chargement.
     */
    void IoState(StateArchive &archive);

  private:
    Core*     m_core; //!< Pointeur vers le noyau principal.

    // État interne du GBA (timers, enveloppes, RAM audio, etc.)
    int                     m_gbaFrameSequencer = 0;
    int                     m_gbaSoundTimers[4] = {};
    int                     m_gbaEnvelopes[3]   = {};
    int                     m_gbaEnvTimers[3]   = {};
    int                     m_gbaSweepTimer     = 0;
    int                     m_gbaWaveDigit      = 0;
    uint16_t                m_gbaNoiseValue     = 0;
    uint8_t                 m_gbaWaveRam[2][16] = {};
    std::queue<int8_t>      m_gbaFifoA, m_gbaFifoB;
    int8_t                  m_gbaSampleA = 0, m_gbaSampleB = 0;
    uint16_t                m_enabled = 0;
    static const int        indexTable[8];
    static const int16_t    adpcmTable[89];
    int32_t                 m_adpcmValue[16] = {}, m_adpcmLoopValue[16] = {};
    int                     m_adpcmIndex[16] = {}, m_adpcmLoopIndex[16] = {};
    bool                    m_adpcmToggle[16]  = {};
    int                     m_dutyCycles[6]    = {};
    uint16_t                m_noiseValues[2]   = {};
    uint32_t                m_soundCurrent[16] = {};
    uint16_t                m_soundTimers[16]  = {};
    uint32_t                m_sndCapCurrent[2] = {};
    uint16_t                m_sndCapTimers[2]  = {};
    uint8_t                 m_gbaSoundCntL[2]  = {};
    uint16_t                m_gbaSoundCntH[4]  = {};
    uint16_t                m_gbaSoundCntX[4]  = {};
    uint16_t                m_gbaMainSoundCntL = 0;
    uint16_t                m_gbaMainSoundCntH = 0;
    uint8_t                 m_gbaMainSoundCntX = 0;
    uint16_t                m_gbaSoundBias     = 0;
    uint32_t                m_soundCnt[16]     = {};
    uint32_t                m_soundSad[16]     = {};
    uint16_t                m_soundTmr[16]     = {};
    uint16_t                m_soundPnt[16]     = {};
    uint32_t                m_soundLen[16]     = {};
    uint16_t                m_mainSoundCnt     = 0;
    uint16_t                m_soundBias        = 0;
    uint8_t                 m_sndCapCnt[2]     = {};
    uint32_t                m_sndCapDad[2]     = {};
    uint16_t                m_sndCapLen[2]     = {};
    std::function<void()>   m_runGbaSampleTask;
    std::function<void()>   m_runSampleTask;
    
    /**
     * @brief Tâche périodique : génère les échantillons GBA.
     */
    void RunGbaSample();

    /**
     * @brief Tâche périodique : génère les échantillons audio généraux.
     */
    void RunSample();

    /**
     * @brief Échange les buffers audio (double buffering).
     */

    /**
     * @brief Active un canal sonore donné.
     * @param channel Index du canal (0–15).
     */
    void StartChannel(int channel);
};

} // namespace consoles::arm

