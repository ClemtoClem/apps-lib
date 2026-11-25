#include "emu/state.hpp"

namespace emu {

void StateArchive::Bytes(void *data, size_t size) {
	if (size == 0)
		return;
	if (m_saving) {
		const auto *bytes = static_cast<const uint8_t *>(data);
		m_buffer.insert(m_buffer.end(), bytes, bytes + size);
		return;
	}
	if (m_failed || size > Remaining()) {
		m_failed = true;
		std::memset(data, 0, size);
		return;
	}
	std::memcpy(data, m_buffer.data() + m_cursor, size);
	m_cursor += size;
}

void StateArchive::IoString(std::string &text, size_t maxLength) {
	uint32_t length = uint32_t(text.size());
	Io(length);
	if (!m_saving) {
		if (m_failed || length > maxLength || length > Remaining()) {
			m_failed = true;
			text.clear();
			return;
		}
		text.resize(length);
	}
	Bytes(text.data(), text.size());
}

void StateArchive::Marker(uint32_t tag) {
	uint32_t value = tag;
	Io(value);
	if (!m_saving && value != tag)
		m_failed = true;
}

} // namespace emu
