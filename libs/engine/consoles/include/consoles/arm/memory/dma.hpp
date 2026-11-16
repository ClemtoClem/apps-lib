
#pragma once
#include <memory>
#include <cstdint>
#include <functional>
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {

class Core;

/**
 * @brief Gestionnaire de transfert DMA (Direct Memory Access).
 *
 * Cette classe implémente la logique des canaux DMA (jusqu'à 4),
 * permettant le transfert direct de données entre la mémoire et les périphériques
 * sans passer par le CPU.
 */
class Dma {
  private:
    Core *m_core;              /**< Pointeur vers le cœur principal de l'émulateur. */
    bool                  m_cpu;               /**< Indique le CPU concerné (0 ou 1). */
    uint32_t              m_srcAddrs[4]   = {};/**< Adresses sources courantes des transferts DMA. */
    uint32_t              m_dstAddrs[4]   = {};/**< Adresses destinations courantes des transferts DMA. */
    uint32_t              m_wordCounts[4] = {};/**< Compteurs de mots restants à transférer. */
    uint32_t              m_dmaSad[4]     = {};/**< Registres DMA Source Address. */
    uint32_t              m_dmaDad[4]     = {};/**< Registres DMA Destination Address. */
    uint32_t              m_dmaCnt[4]     = {};/**< Registres DMA Control. */
    std::function<void()> m_transferTask[4];   /**< Tâches planifiées pour gérer les transferts DMA. */
    
    /**
     * @brief Exécute un transfert DMA pour un canal donné.
     *
     * Cette fonction effectue le transfert mémoire-mémoire ou mémoire-périphérique
     * selon les modes configurés dans les registres de contrôle du DMA.
     *
     * @param channel Index du canal DMA (0 à 3).
     */ 
    void Transfer(int channel);

  public:
    /**
     * @brief Constructeur du gestionnaire DMA.
     *
     * Initialise les canaux DMA et associe les tâches de transfert.
     *
     * @param core Pointeur vers le cœur principal de l’émulateur.
     * @param cpu Identifiant du CPU (0 = ARM9, 1 = ARM7).
     */
    Dma(Core *core, bool cpu);

    /**
     * @brief Déclenche les transferts DMA pour les canaux sélectionnés.
     *
     * Vérifie les canaux actifs et planifie les tâches de transfert si les conditions sont remplies.
     *
     * @param mode Mode de déclenchement (ex: VBlank, HBlank, FIFO...).
     * @param channels Masque des canaux concernés (par défaut 0x0F = tous).
     */
    void Trigger(int mode, uint8_t channels = 0x0F);

    /**
     * @brief Lit le registre DMA Source Address.
     *
     * @param channel Index du canal DMA (0 à 3).
     * @return Valeur du registre dmaSad[channel].
     */
    uint32_t ReadDmaSad(int channel) {
      return m_dmaSad[channel];
    }

    /**
     * @brief Lit le registre DMA Destination Address.
     *
     * @param channel Index du canal DMA (0 à 3).
     * @return Valeur du registre dmaDad[channel].
     */
    uint32_t ReadDmaDad(int channel) {
      return m_dmaDad[channel];
    }

    /**
     * @brief Lit le registre DMA Control.
     *
     * @param channel Index du canal DMA (0 à 3).
     * @return Valeur du registre dmaCnt[channel], avec certains bits masqués selon le mode.
     */
    uint32_t ReadDmaCnt(int channel);

    /**
     * @brief Écrit dans le registre DMA Source Address.
     *
     * @param channel Index du canal DMA (0 à 3).
     * @param mask Masque des bits modifiables.
     * @param value Nouvelle valeur à appliquer.
     */
    void WriteDmaSad(int channel, uint32_t mask, uint32_t value);
   
    /**
     * @brief Écrit dans le registre DMA Destination Address.
     *
     * @param channel Index du canal DMA (0 à 3).
     * @param mask Masque des bits modifiables.
     * @param value Nouvelle valeur à appliquer.
     */
    void WriteDmaDad(int channel, uint32_t mask, uint32_t value);
    
    /**
     * @brief Écrit dans le registre DMA Control.
     *
     * Met à jour les paramètres du canal DMA et déclenche éventuellement un transfert immédiat.
     *
     * @param channel Index du canal DMA (0 à 3).
     * @param mask Masque des bits modifiables.
     * @param value Nouvelle valeur à appliquer.
     */
    void WriteDmaCnt(int channel, uint32_t mask, uint32_t value);

    /**
     * @brief Sérialise/désérialise l'état des 4 canaux DMA pour les save states.
     * @param archive Archive de sauvegarde/chargement.
     */
    void IoState(StateArchive &archive) {
        archive.Io(m_srcAddrs);
        archive.Io(m_dstAddrs);
        archive.Io(m_wordCounts);
        archive.Io(m_dmaSad);
        archive.Io(m_dmaDad);
        archive.Io(m_dmaCnt);
    }

    /**
     * @brief Replanifie les transferts en cours après un chargement d'état.
     *
     * Le scheduler (Core::tasks) n'est pas sérialisé : un transfert GXFIFO
     * (mode 7) en attente de place dans la FIFO doit donc être reprogrammé
     * manuellement, à l'identique de ce que ferait une écriture normale du
     * registre. Les autres modes (immédiat déjà terminé au moment du save,
     * ou déclenchés par un événement externe comme VBlank/HBlank) n'ont pas
     * besoin d'être relancés : ils repartiront naturellement au prochain
     * événement qui les déclenche.
     */
    void Rearm();
};

} // namespace consoles::arm

