#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {

class Core;

/**
 * @brief Gestionnaire des minuteries (timers) matériels.
 *
 * Cette classe implémente les 4 minuteries disponibles pour chaque CPU.
 * Elles peuvent être utilisées pour générer des interruptions, synchroniser
 * des événements (comme l'audio), ou chaîner plusieurs timers.
 */
class Timers {
  public:
    /**
     * @brief Constructeur de la classe Timers.
     *
     * Initialise les structures internes et associe les tâches de débordement.
     *
     * @param core Pointeur vers le cœur principal de l’émulateur.
     * @param cpu Identifiant du CPU concerné (false = CPU0, true = CPU1).
     */
    Timers(Core *core, bool cpu);

    /**
     * @brief Réinitialise le décompte des cycles.
     *
     * Ajuste les compteurs internes en soustrayant le nombre
     * total de cycles globaux écoulés.
     */
    void ResetCycles();

    /**
     * @brief Lit le registre TMCNT_H.
     *
     * Ce registre contient le contrôle du timer (activation, mode, IRQ...).
     *
     * @param timer Index du timer (0 à 3).
     * @return Valeur du registre TMCNT_H du timer.
     */
    uint16_t ReadTmCntH(int timer) {
        return m_tmCntH[timer];
    }

    /**
     * @brief Lit le registre TMCNT_L (valeur courante du timer).
     *
     * Si le timer est actif, la valeur courante est recalculée
     * en fonction des cycles globaux et du prescaler.
     *
     * @param timer Index du timer (0 à 3).
     * @return Valeur courante du compteur du timer.
     */
    uint16_t ReadTmCntL(int timer);

    /**
     * @brief Écrit dans le registre TMCNT_L (valeur de départ).
     *
     * @param timer Index du timer (0 à 3).
     * @param mask Masque des bits modifiables.
     * @param value Nouvelle valeur à écrire.
     */
    void WriteTmCntL(int timer, uint16_t mask, uint16_t value);

    /**
     * @brief Écrit dans le registre TMCNT_H (contrôle du timer).
     *
     * Met à jour la configuration du timer (activation, mode cascade,
     * prescaler, génération d’interruption).
     *
     * @param timer Index du timer (0 à 3).
     * @param mask Masque des bits modifiables.
     * @param value Nouvelle valeur à écrire.
     */
    void WriteTmCntH(int timer, uint16_t mask, uint16_t value);

    /**
     * @brief Sérialise/désérialise l'état des 4 minuteries pour les save states.
     * @param archive Archive de sauvegarde/chargement.
     */
    void IoState(StateArchive &archive) {
        archive.Io(m_timers);
        archive.Io(m_shifts);
        archive.Io(m_endCycles);
        archive.Io(m_tmCntL);
        archive.Io(m_tmCntH);
    }

    /**
     * @brief Replanifie les débordements des minuteries actives après un chargement d'état.
     *
     * Le scheduler n'étant pas sérialisé, chaque timer actif (non cascadé, ou
     * timer 0) doit être reprogrammé pour déborder au cycle indiqué par
     * endCycles[], qui vient d'être restauré.
     */
    void Rearm();

  private:
    Core *m_core;                 /**< Pointeur vers le cœur principal de l’émulateur. */
    bool                  m_cpu;                  /**< CPU concerné (false = CPU0, true = CPU1). */
    uint16_t              m_timers[4]    = {};    /**< Valeurs courantes des compteurs de timers. */
    uint8_t               m_shifts[4]    = {};    /**< Prescaler appliqué à chaque timer (en puissance de 2). */
    uint32_t              m_endCycles[4] = {};    /**< Cycle global prévu pour le prochain débordement. */
    uint16_t              m_tmCntL[4]    = {};    /**< Registres TMCNT_L (valeurs de départ). */
    uint16_t              m_tmCntH[4]    = {};    /**< Registres TMCNT_H (contrôle des timers). */
    std::function<void()> m_overflowTask[4];      /**< Tâches déclenchées lors d’un débordement. */

    /**
     * @brief Gère un débordement de timer.
     *
     * Réinitialise le compteur, planifie le prochain débordement,
     * déclenche une interruption si nécessaire, et gère le chaînage
     * avec le timer suivant.
     *
     * @param timer Index du timer (0 à 3).
     */
    void Overflow(int timer);
};

} // namespace consoles::arm

