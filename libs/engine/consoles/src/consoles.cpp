#include "consoles/consoles.hpp"

#include "consoles/gba/gba.hpp"
#include "consoles/gbc/gbc.hpp"
#include "consoles/nds/nds.hpp"

namespace consoles {

//void RegisterAll(emu::SystemRegistry &registry) { (void)registry.Register(nes::System()); }
void RegisterAll(emu::SystemRegistry &registry) {
	(void)registry.Register(gbc::System());
	(void)registry.Register(gba::System());
	(void)registry.Register(nds::System());
}
//void RegisterAll(emu::SystemRegistry &registry) { (void)registry.Register(gba::System()); }
//void RegisterAll(emu::SystemRegistry &registry) { (void)registry.Register(nds::System()); }
//void RegisterAll(emu::SystemRegistry &registry) { (void)registry.Register(ds3::System()); }

} // namespace consoles
