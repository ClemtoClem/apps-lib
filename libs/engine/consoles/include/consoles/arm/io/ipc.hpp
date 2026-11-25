#pragma once
#include <cstdint>
#include <queue>
#include <memory>
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {

class Core;

/**
 * @brief Gestion de l'IPC (Inter-Processor Communication).
 *
 * Cette classe simule le mécanisme d'échange de données entre les deux CPU
 * d'une console via les registres IPC (synchronisation et FIFOs).
 */
class Ipc {
  public:
    /**
     * @brief Constructeur de la classe Ipc.
     *
     * Initialise les registres IPC et associe le cœur principal.
     *
     * @param core Pointeur vers le cœur principal de l’émulateur.
     */
    Ipc(Core *core_) : m_core(core_) {
    }

    /**
     * @brief Lit le registre IPCSYNC.
     *
     * Ce registre contient des bits de synchronisation entre les deux CPU.
     *
     * @param cpu Identifiant du CPU (false = CPU0, true = CPU1).
     * @return Valeur du registre ipcSync[cpu].
     */
    uint16_t ReadIpcSync(bool cpu) {
      return m_ipcSync[cpu];
    }

    /**
     * @brief Lit le registre IPCFIFOCNT.
     *
     * Ce registre contient les flags de gestion de la FIFO IPC
     * (état vide, plein, erreurs, interruptions).
     *
     * @param cpu Identifiant du CPU (false = CPU0, true = CPU1).
     * @return Valeur du registre ipcFifoCnt[cpu].
     */
    uint16_t ReadIpcFifoCnt(bool cpu) {
      return m_ipcFifoCnt[cpu];
    }

    /**
     * @brief Lit le registre IPCFIFORECV.
     *
     * Récupère une valeur depuis la FIFO de l'autre CPU si disponible.
     *
     * @param cpu Identifiant du CPU (false = CPU0, true = CPU1).
     * @return Dernière valeur reçue via la FIFO.
     */
    uint32_t ReadIpcFifoRecv(bool cpu);

    /**
     * @brief Écrit dans le registre IPCSYNC.
     *
     * Met à jour les bits de synchronisation et déclenche éventuellement
     * une interruption sur l'autre CPU.
     *
     * @param cpu Identifiant du CPU (false = CPU0, true = CPU1).
     * @param mask Masque des bits modifiables.
     * @param value Valeur à appliquer.
     */
    void WriteIpcSync(bool cpu, uint16_t mask, uint16_t value);

    /**
     * @brief Écrit dans le registre IPCFIFOCNT.
     *
     * Met à jour les flags de la FIFO et déclenche les interruptions associées
     * (vidage, erreurs, réception...).
     *
     * @param cpu Identifiant du CPU (false = CPU0, true = CPU1).
     * @param mask Masque des bits modifiables.
     * @param value Valeur à appliquer.
     */
    void WriteIpcFifoCnt(bool cpu, uint16_t mask, uint16_t value);

    /**
     * @brief Écrit dans le registre IPCFIFOSEND.
     *
     * Envoie une donnée dans la FIFO de l'autre CPU si elle n'est pas pleine,
     * ou déclenche un flag d'erreur si la FIFO est pleine.
     *
     * @param cpu Identifiant du CPU (false = CPU0, true = CPU1).
     * @param mask Masque des bits applicables.
     * @param value Valeur à envoyer.
     */
    void WriteIpcFifoSend(bool cpu, uint32_t mask, uint32_t value);

    /**
     * @brief Sérialise/désérialise l'état de l'IPC (registres et FIFOs) pour les save states.
     * @param archive Archive de sauvegarde/chargement.
     */
    void IoState(StateArchive &archive) {
        archive.IoQueue(m_fifos[0]);
        archive.IoQueue(m_fifos[1]);
        archive.Io(m_ipcSync);
        archive.Io(m_ipcFifoCnt);
        archive.Io(m_ipcFifoRecv);
    }

  private:
    Core *m_core;                /**< Pointeur vers le cœur principal. */
    std::queue<uint32_t> m_fifos[2];            /**< Files FIFO (une par CPU). */
    uint16_t             m_ipcSync[2]     = {}; /**< Registres IPCSYNC (synchronisation). */
    uint16_t             m_ipcFifoCnt[2]  = {0x0101, 0x0101}; /**< Registres IPCFIFOCNT (gestion FIFO). */
    uint32_t             m_ipcFifoRecv[2] = {}; /**< Dernières valeurs reçues depuis la FIFO. */
};

} // namespace consoles::arm

