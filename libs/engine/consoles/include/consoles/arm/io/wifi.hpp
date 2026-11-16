/**
 * @file wifi.h
 * @brief Émulation du module WiFi NDS/GBA (MAC + BB) utilisé par le core.
 *
 * Cette classe gère :
 *  - la simulation des registres WiFi (MAC/BB)
 *  - les compteurs internes (timings microsecondes, beacons…)
 *  - les buffers RX/TX circulaires
 *  - la gestion des interruptions WiFi
 *  - l'émulation des transferts de paquets entre instances Wifi
 *
 * Elle représente une interface WiFi pour une instance de Core.
 */

#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>
#include <memory>
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {

class Core;

/**
 * @class Wifi
 * @brief Module d'émulation WiFi pour une instance Core.
 *
 * Chaque Core possède son module Wifi, capable de communiquer avec
 * les autres Wifi via AddConnection() / RemConnection().
 */
class Wifi {
  public:
  /**
     * @brief Constructeur.
     * @param core Pointeur vers le Core possédant ce module WiFi.
     */
    Wifi(Core *core);

    /**
     * @brief Indique si le module doit reprogrammer son scheduler.
     */
    bool ShouldSchedule() {
        return (!m_connections.empty() || m_wUsCountcnt) && !m_scheduled;
    }

     /**
     * @brief Programme la tâche WiFi dans le scheduler du Core.
     */
    void ScheduleInit();

    /**
     * @brief Ajoute une connexion WiFi bidirectionnelle avec un autre Core.
     * @param core Core distant à relier.
     */
    void AddConnection(Core *peer);

    /**
     * @brief Supprime une connexion WiFi bidirectionnelle.
     * @param core Core distant.
     */
    void RemConnection(Core *peer);
    
    /** @name Lectures des registres MAC/BB */
    ///@{
    uint16_t ReadWModeWep() const {
        return m_wModeWep;
    }
    uint16_t ReadWIrf() const {
        return m_wIrf;
    }
    uint16_t ReadWIe() const {
        return m_wIe;
    }
    uint16_t ReadWMacaddr(int index) const {
        return m_wMacaddr[index];
    }
    uint16_t ReadWBssid(int index) const {
        return m_wBssid[index];
    }
    uint16_t ReadWAidFull() const {
        return m_wAidFull;
    }
    uint16_t ReadWRxcnt() const {
        return m_wRxcnt;
    }
    uint16_t ReadWPowerstate() const {
        return m_wPowerstate;
    }
    uint16_t ReadWPowerforce() const {
        return m_wPowerforce;
    }
    uint16_t ReadWRxbufBegin() const {
        return m_wRxbufBegin;
    }
    uint16_t ReadWRxbufEnd() const {
        return m_wRxbufEnd;
    }
    uint16_t ReadWRxbufWrcsr() const {
        return m_wRxbufWrcsr >> 1;
    }
    uint16_t ReadWRxbufWrAddr() const {
        return m_wRxbufWrAddr;
    }
    uint16_t ReadWRxbufRdAddr() const {
        return m_wRxbufRdAddr;
    }
    uint16_t ReadWRxbufReadcsr() const {
        return m_wRxbufReadcsr;
    }
    uint16_t ReadWRxbufGap() const {
        return m_wRxbufGap;
    }
    uint16_t ReadWRxbufGapdisp() const {
        return m_wRxbufGapdisp;
    }
    uint16_t ReadWRxbufCount() const {
        return m_wRxbufCount;
    }
    uint16_t ReadWTxbufWrAddr() const {
        return m_wTxbufWrAddr;
    }
    uint16_t ReadWTxbufCount() const {
        return m_wTxbufCount;
    }
    uint16_t ReadWTxbufGap() const {
        return m_wTxbufGap;
    }
    uint16_t ReadWTxbufGapdisp() const {
        return m_wTxbufGapdisp;
    }
    uint16_t ReadWTxbufLoc(int index) const {
        return m_wTxbufLoc[index];
    }
    uint16_t ReadWBeaconInt() const {
        return m_wBeaconInt;
    }
    uint16_t ReadWTxreqRead() const {
        return m_wTxreqRead;
    }
    uint16_t ReadWUsCountcnt() const {
        return m_wUsCountcnt;
    }
    uint16_t ReadWUsComparecnt() const {
        return m_wUsComparecnt;
    }
    uint16_t ReadWPreBeacon() const {
        return m_wPreBeacon;
    }
    uint16_t ReadWBeaconCount() const {
        return m_wBeaconCount;
    }
    uint16_t ReadWConfig(int index) const {
        return m_wConfig[index];
    }
    uint16_t ReadWPostBeacon() const {
        return m_wPostBeacon;
    }
    uint16_t ReadWBbRead() const {
        return m_wBbRead;
    }
    uint16_t ReadWRxbufRdData();
    ///@}

    /** @name Écritures des registres MAC/BB */
    ///@{
    void WriteWModeWep(uint16_t mask, uint16_t value);
    void WriteWIrf(uint16_t mask, uint16_t value);
    void WriteWIe(uint16_t mask, uint16_t value);
    void WriteWMacaddr(int index, uint16_t mask, uint16_t value);
    void WriteWBssid(int index, uint16_t mask, uint16_t value);
    void WriteWAidFull(uint16_t mask, uint16_t value);
    void WriteWRxcnt(uint16_t mask, uint16_t value);
    void WriteWPowerstate(uint16_t mask, uint16_t value);
    void WriteWPowerforce(uint16_t mask, uint16_t value);
    void WriteWRxbufBegin(uint16_t mask, uint16_t value);
    void WriteWRxbufEnd(uint16_t mask, uint16_t value);
    void WriteWRxbufWrAddr(uint16_t mask, uint16_t value);
    void WriteWRxbufRdAddr(uint16_t mask, uint16_t value);
    void WriteWRxbufReadcsr(uint16_t mask, uint16_t value);
    void WriteWRxbufGap(uint16_t mask, uint16_t value);
    void WriteWRxbufGapdisp(uint16_t mask, uint16_t value);
    void WriteWTxbufLoc(int index, uint16_t mask, uint16_t value);
    void WriteWBeaconInt(uint16_t mask, uint16_t value);
    void WriteWTxreqReset(uint16_t mask, uint16_t value);
    void WriteWTxreqSet(uint16_t mask, uint16_t value);
    void WriteWUsCountcnt(uint16_t mask, uint16_t value);
    void WriteWUsComparecnt(uint16_t mask, uint16_t value);
    void WriteWPreBeacon(uint16_t mask, uint16_t value);
    void WriteWBeaconCount(uint16_t mask, uint16_t value);
    void WriteWRxbufCount(uint16_t mask, uint16_t value);
    void WriteWTxbufWrAddr(uint16_t mask, uint16_t value);
    void WriteWTxbufCount(uint16_t mask, uint16_t value);
    void WriteWTxbufWrData(uint16_t mask, uint16_t value);
    void WriteWTxbufGap(uint16_t mask, uint16_t value);
    void WriteWTxbufGapdisp(uint16_t mask, uint16_t value);
    void WriteWConfig(int index, uint16_t mask, uint16_t value);
    void WriteWPostBeacon(uint16_t mask, uint16_t value);
    void WriteWBbCnt(uint16_t mask, uint16_t value);
    void WriteWBbWrite(uint16_t mask, uint16_t value);
    void WriteWIrfSet(uint16_t mask, uint16_t value);
    ///@}

    /**
     * @brief Sérialise/désérialise les registres WiFi pour les save states.
     *
     * Les connexions multijoueur (connections) et les paquets en attente
     * (packets) sont des pointeurs vers d'autres instances vivantes et des
     * données de session : ils ne font pas partie d'un instantané et sont
     * reconstruits par la couche applicative si besoin.
     * @param archive Archive de sauvegarde/chargement.
     */
    void IoState(StateArchive &archive) {
        archive.Io(m_bbRegisters);
        archive.Io(m_wModeWep);
        archive.Io(m_wIrf);
        archive.Io(m_wIe);
        archive.Io(m_wMacaddr);
        archive.Io(m_wBssid);
        archive.Io(m_wAidFull);
        archive.Io(m_wRxcnt);
        archive.Io(m_wPowerstate);
        archive.Io(m_wPowerforce);
        archive.Io(m_wRxbufBegin);
        archive.Io(m_wRxbufEnd);
        archive.Io(m_wRxbufWrcsr);
        archive.Io(m_wRxbufWrAddr);
        archive.Io(m_wRxbufRdAddr);
        archive.Io(m_wRxbufReadcsr);
        archive.Io(m_wRxbufGap);
        archive.Io(m_wRxbufGapdisp);
        archive.Io(m_wTxbufLoc);
        archive.Io(m_wBeaconInt);
        archive.Io(m_wTxreqRead);
        archive.Io(m_wUsCountcnt);
        archive.Io(m_wUsComparecnt);
        archive.Io(m_wPreBeacon);
        archive.Io(m_wBeaconCount);
        archive.Io(m_wRxbufCount);
        archive.Io(m_wTxbufWrAddr);
        archive.Io(m_wTxbufCount);
        archive.Io(m_wTxbufGap);
        archive.Io(m_wTxbufGapdisp);
        archive.Io(m_wPostBeacon);
        archive.Io(m_wBbWrite);
        archive.Io(m_wBbRead);
        archive.Io(m_wConfig);
    }

    /**
     * @brief Réinitialise le drapeau de planification interne après un chargement d'état.
     *
     * `scheduled` n'est pas sérialisé : le scheduler (Core::tasks) est
     * entièrement reconstruit lors d'un chargement, donc la tâche countMs
     * n'est plus programmée. Ce drapeau doit être remis à false pour que
     * ShouldSchedule()/ScheduleInit() puissent la reprogrammer normalement.
     */
    void ResetSchedule() {
        m_scheduled = false;
    }

    /**
     * @brief Pont réseau optionnel (app::NetBridge) : reçoit une copie de
     * chaque paquet émis par Transfer(), en plus de la diffusion locale vers
     * `connections`. Laissé vide si aucune session réseau n'est active.
     * @param data Paquet brut (mêmes octets que ceux placés dans le TX buffer).
     * @param len Taille en octets.
     */
    std::function<void(const uint8_t *data, size_t len)> onPacketSent;

    /**
     * @brief Injecte un paquet reçu depuis le pont réseau (app::NetBridge),
     * comme s'il avait été livré par une connexion locale (AddConnection()).
     * @param data Paquet brut.
     * @param len Taille en octets (doit être paire).
     */
    void InjectPacket(const uint8_t *data, size_t len);

private:
    Core *m_core;                             ///< Instance `Core` parent.
    std::vector<Wifi *>     m_connections;    ///< Connexions `WiFi` actives
    std::vector<std::vector<uint16_t>> m_packets;        ///< File de paquets en attente.
    std::mutex              m_mutex;          ///< Mutex de protection des buffers.

    bool     m_scheduled          = false;
    uint8_t  m_bbRegisters[0x100] = {};
    uint16_t m_wModeWep           = 0;
    uint16_t m_wIrf               = 0;
    uint16_t m_wIe                = 0;
    uint16_t m_wMacaddr[3]        = {};
    uint16_t m_wBssid[3]          = {};
    uint16_t m_wAidFull           = 0;
    uint16_t m_wRxcnt             = 0;
    uint16_t m_wPowerstate        = 0x0200;
    uint16_t m_wPowerforce        = 0;
    uint16_t m_wRxbufBegin        = 0;
    uint16_t m_wRxbufEnd          = 0;
    uint16_t m_wRxbufWrcsr        = 0;
    uint16_t m_wRxbufWrAddr       = 0;
    uint16_t m_wRxbufRdAddr       = 0;
    uint16_t m_wRxbufReadcsr      = 0;
    uint16_t m_wRxbufGap          = 0;
    uint16_t m_wRxbufGapdisp      = 0;
    uint16_t m_wTxbufLoc[5]       = {};
    uint16_t m_wBeaconInt         = 0;
    uint16_t m_wTxreqRead         = 0x0010;
    uint16_t m_wUsCountcnt        = 0;
    uint16_t m_wUsComparecnt      = 0;
    uint16_t m_wPreBeacon         = 0;
    uint16_t m_wBeaconCount       = 0;
    uint16_t m_wRxbufCount        = 0;
    uint16_t m_wTxbufWrAddr       = 0;
    uint16_t m_wTxbufCount        = 0;
    uint16_t m_wTxbufGap          = 0;
    uint16_t m_wTxbufGapdisp      = 0;
    uint16_t m_wPostBeacon        = 0;
    uint16_t m_wBbWrite           = 0;
    uint16_t m_wBbRead            = 0;
    uint16_t m_wConfig[15]        = {0x0048, 0x4840, 0x0000, 0x0000, 0x0142, 0x8064, 0x0000, 0x2443,
                                   0x0042, 0x0016, 0x0016, 0x0016, 0x162C, 0x0204, 0x0058};

    std::function<void()> m_countMsTask; ///< Tâche pour le scheduler (tick WiFi).

    /**
     * @brief Déclenche un bit d'interruption WiFi.
     * @param bit Numéro de bit dans wIrf.
     */
    void SendInterrupt(int bit);

    /**
     * @brief Tick périodique microseconde / beacon / timers.
     */
    void CountMs();

    /**
     * @brief Défile et traite les paquets reçus.
     */
    void ProcessPackets();

    /**
     * @brief Envoie un paquet depuis un TX buffer.
     * @param index Indice du TX buffer (0–4).
     */
    void Transfer(int index);
};

} // namespace consoles::arm

