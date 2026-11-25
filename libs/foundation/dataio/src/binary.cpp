#include "dataio/binary.hpp"

#include <algorithm>

namespace dataio {

void RleEncode16(std::span<const uint16_t> in, ByteWriter &out) {
	for (size_t i = 0; i < in.size();) {
		const uint16_t v = in[i];
		size_t run = 1;
		while (i + run < in.size() && in[i + run] == v)
			++run;
		out.PutVarUint(run);
		out.Put<uint16_t>(v);
		i += run;
	}
}

bool RleDecode16(ByteReader &in, std::span<uint16_t> out) {
	size_t pos = 0;
	while (pos < out.size()) {
		const uint64_t run = in.GetVarUint();
		const uint16_t v = in.Get<uint16_t>();
		if (!in.Ok() || run == 0 || run > out.size() - pos)
			return false;
		std::fill_n(out.begin() + static_cast<std::ptrdiff_t>(pos), run, v);
		pos += run;
	}
	return true;
}

} // namespace dataio
