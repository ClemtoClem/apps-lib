/**
 * @file rtc.h
 * @brief Déclaration de la classe Rtc, simulant l’horloge temps réel (RTC) et le port GPIO RTC.
 */

#pragma once
#include <cstdint>
#include <memory>
#include "consoles/arm/defines.hpp"
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {

class Core;

/**
 * @class Rtc
 * @brief Simulation RTC pour GBA/NDS : communication série, registres, date/heure et GPIO associés.
 *
 * Cette classe gère :
 * - Le protocole série RTC (CS/SCK/SIO)
 * - Les registres internes du RTC
 * - La lecture/écriture de l'heure en BCD
 * - Le fonctionnement différent entre mode NDS et GBA
 * - Les lignes GPIO (gpDirection, gpControl, gpData)
 */
class Rtc {
  public:
    /**
     * @brief Constructeur.
     * @param core Pointeur vers l'objet Core.
     */
    Rtc(Core *core_) : m_core(core_) { }

    /**
     * @brief Active la gestion du RTC via le GPIO (mode NDS).
     */
    void EnableGpRtc() {
        m_gpRtc = true;
    }

    /**
     * @brief Réinitialise les registres RTC internes.
     */
    void Reset();

    /**
     * @brief Lit la valeur du registre RTC principal.
     * @return Octet contenant CS/SCK/SIO ainsi que les flags RTC.
     */
    uint8_t ReadRtc();

    /**
     * @brief Lit l'état du port GPIO (lignes CS/SIO/SCK).
     * @return Valeur bit à bit.
     */
    uint16_t ReadGpData();

    /**
     * @brief Lit la direction GPIO.
     * @return Masque de direction : 1 = sortie, 0 = entrée.
     */
    uint16_t ReadGpDirection() {
        return m_gpDirection;
    }

    /**
     * @brief Lit le registre de contrôle GPIO.
     * @return Valeur du registre.
     */
    uint16_t ReadGpControl() {
        return m_gpControl;
    }

    /**
     * @brief Écrit dans le registre RTC principal (affecte CS/SCK/SIO).
     * @param value Valeur écrite.
     */
    void WriteRtc(uint8_t value);

    /**
     * @brief Écrit dans le port GPIO RTC (bits CS/SIO/SCK).
     * @param value Nouveaux bits.
     * @param mask Masque des bits à modifier.
     */
    void WriteGpData(uint16_t value, uint16_t mask);

    /**
     * @brief Configure la direction des lignes GPIO.
     * @param value Direction : 1 = sortie.
     * @param mask Masque des bits concernés.
     */
    void WriteGpDirection(uint16_t value, uint16_t mask);

    /**
     * @brief Écrit dans le registre de contrôle GPIO.
     * @param value Nouvelle valeur.
     * @param mask Masque des bits modifiables.
     */
    void WriteGpControl(uint16_t value, uint16_t mask);

    /**
     * @brief Sérialise/désérialise l'état du RTC pour les save states.
     * @param archive Archive de sauvegarde/chargement.
     */
    void IoState(StateArchive &archive) {
        archive.Io(m_gpRtc);
        archive.Io(m_csCur);
        archive.Io(m_sckCur);
        archive.Io(m_sioCur);
        archive.Io(m_writeCount);
        archive.Io(m_command);
        archive.Io(m_control);
        archive.Io(m_dateTime);
        archive.Io(m_rtc);
        archive.Io(m_gpDirection);
        archive.Io(m_gpControl);
        archive.Io(m_status2);
        archive.Io(m_int1);
        archive.Io(m_int2);
        archive.Io(m_clockAdjust);
        archive.Io(m_freeRegister);
        archive.Io(m_timeOffset);
        archive.Io(m_pending);
    }

  private:
    Core *m_core;                   ///< Référence au cœur principal.
    bool m_gpRtc           = false; ///< Le RTC est-il piloté par le GPIO ?
    bool m_csCur           = false; ///< Ligne CS actuelle.
    bool m_sckCur          = false; ///< Ligne SCK actuelle.
    bool m_sioCur          = false; ///< Ligne SIO actuelle.
    uint8_t m_writeCount   = 0;     ///< Nombre de bits écrits dans la commande.
    uint8_t m_command      = 0;     ///< Dernière commande reçue.
    uint8_t m_control      = 0;     ///< Registre de contrôle RTC.
    uint8_t m_dateTime[7]  = {};    ///< Données date/heure encodées en BCD.
    uint8_t m_rtc          = 0;     ///< Registre RTC principal.
    uint16_t m_gpDirection = 0;     ///< Direction des GPIO.
    uint16_t m_gpControl   = 0;     ///< Contrôle GPIO.

    // Registres NDS (S-35199A01) au-delà de Status 1 : les jeux du kit
    // officiel les écrivent puis les relisent pour vérifier l'horloge ;
    // les ignorer (lecture à 0) faisait boucler leur initialisation RTC.
    uint8_t m_status2      = 0;     ///< Registre 1 : Status 2 (modes INT1/INT2, 24 h…).
    uint8_t m_int1[3]      = {};    ///< Registre 4 : INT1 (fréquence ou alarme 1).
    uint8_t m_int2[3]      = {};    ///< Registre 5 : INT2 (alarme 2).
    uint8_t m_clockAdjust  = 0;     ///< Registre 6 : réglage de la fréquence.
    uint8_t m_freeRegister = 0;     ///< Registre 7 : octet libre.
    int64_t m_timeOffset   = 0;     ///< Écart (s) entre l'heure écrite par le jeu et l'horloge de l'hôte.
    uint8_t m_pending[7]   = {};    ///< Date/heure en cours d'écriture (BCD).

    /**
     * @brief Met à jour le protocole série RTC en fonction de CS/SCK/SIO.
     * @details Gère l’écriture de commandes 8 bits, puis :
     * - Lecture de registres si le bit 0 de la commande vaut 1
     * - Écriture sinon
     * @param cs État ligne CS
     * @param sck État ligne SCK
     * @param sio État ligne SIO
     */
    void UpdateRtc(bool cs, bool sck, bool sio);

    /**
     * @brief Met à jour la date/heure à partir de l'heure système.
     * @details Format utilisé : BCD (année/mois/jour/heure/min/sec)
     * Comportement 12h/24h dépend du mode GBA/NDS.
     */
    void UpdateDateTime();

    /// Applique la date/heure BCD de `pending` (octets `first`..6) : l'heure
    /// lue ensuite repart de cette valeur et avance avec l'horloge de l'hôte.
    void ApplyPendingDateTime(int first);

    void ResetRegisters();

    /**
     * @brief Lit un registre interne du RTC.
     * @param index Index du registre (différent en mode GBA).
     * @return Bit lu.
     */
    bool ReadRegister(uint8_t index);

    /**
     * @brief Écrit un bit dans un registre interne RTC.
     * @param index Index du registre.
     * @param value Valeur du bit.
     */
    void WriteRegister(uint8_t index, bool value);
};

} // namespace consoles::arm

