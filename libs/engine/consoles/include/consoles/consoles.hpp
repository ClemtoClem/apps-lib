#pragma once
/**
 * @file consoles.hpp
 * @brief Systèmes émulés bâtis sur le cadre `emu`.
 */
#include <emu/system.hpp>

namespace consoles {

/// Enregistre tous les systèmes disponibles dans `registry`.
void RegisterAll(emu::SystemRegistry &registry);

} // namespace consoles
