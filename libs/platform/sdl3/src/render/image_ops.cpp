#include "sdl3/render/image_ops.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

#include <SDL3/SDL.h>

#include <core/string.hpp>
#include <sdl3/render/image.hpp>

namespace sdl3 {

namespace {

/// Pointeur du pixel (x, y) d'une surface RGBA32 (4 octets par pixel).
uint8_t *PixelAt(Surface &s, int x, int y) { return s.Pixels() + size_t(y) * size_t(s.Pitch()) + size_t(x) * 4; }
const uint8_t *PixelAt(const Surface &s, int x, int y) { return s.Pixels() + size_t(y) * size_t(s.Pitch()) + size_t(x) * 4; }

/// Convertit la surface en RGBA32 si nécessaire (opérations in-place).
bool EnsureRgba32(Surface &s) {
	if (!s)
		return false;
	if (s.Get()->format == SDL_PIXELFORMAT_RGBA32)
		return true;
	auto c = s.Convert(PixelFormat::RGBA32);
	if (!c)
		return false;
	s = std::move(c.Value());
	return true;
}

/// Surface RGBA32 transparente `w`×`h`, ou nulle en cas d'échec.
Surface Transparent(int w, int h) {
	auto r = Surface::Solid(w, h, 0, 0, 0, 0);
	return r ? std::move(r.Value()) : Surface{};
}

/// Redimensionne (filtre boîte) vers des dimensions arbitraires.
Surface ResizeImage(const Surface &src, int w, int h) {
	Surface out = Transparent(w, h);
	if (!out || w <= 0 || h <= 0)
		return {};
	const int sw = src.GetWidth(), sh = src.GetHeight();
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const int x0 = x * sw / w, x1 = std::max(x0 + 1, (x + 1) * sw / w);
			const int y0 = y * sh / h, y1 = std::max(y0 + 1, (y + 1) * sh / h);
			uint32_t acc[4] = {}, n = 0;
			for (int sy = y0; sy < y1; ++sy)
				for (int sx = x0; sx < x1; ++sx) {
					const uint8_t *p = PixelAt(src, sx, sy);
					for (int c = 0; c < 4; ++c)
						acc[c] += p[c];
					++n;
				}
			uint8_t *d = PixelAt(out, x, y);
			for (int c = 0; c < 4; ++c)
				d[c] = static_cast<uint8_t>(acc[c] / n);
		}
	return out;
}

/// Recadre une région (x, y, largeur, hauteur) ; les zones hors image restent transparentes.
Surface CropImage(const Surface &src, int x, int y, int w, int h) {
	Surface out = Transparent(w, h);
	if (!out || w <= 0 || h <= 0)
		return {};
	for (int j = 0; j < h; ++j) {
		const int sy = y + j;
		if (sy < 0 || sy >= src.GetHeight())
			continue;
		for (int i = 0; i < w; ++i) {
			const int sx = x + i;
			if (sx < 0 || sx >= src.GetWidth())
				continue;
			const uint8_t *sp = PixelAt(src, sx, sy);
			uint8_t *dp = PixelAt(out, i, j);
			dp[0] = sp[0], dp[1] = sp[1], dp[2] = sp[2], dp[3] = sp[3];
		}
	}
	return out;
}

/// Extrait la `index`-ième trame d'une bande verticale de `count` trames égales.
Surface ExtractFrame(const Surface &strip, int index, int count) {
	const int frameH = count > 0 ? strip.GetHeight() / count : 0;
	if (frameH <= 0) { // bande sans trame découpable : copie telle quelle
		auto d = strip.Duplicate();
		return d ? std::move(d.Value()) : Surface{};
	}
	const int y0 = std::clamp(index, 0, count - 1) * frameH;
	Surface result = Transparent(strip.GetWidth(), frameH);
	if (!result)
		return {};
	const size_t row = size_t(strip.GetWidth()) * 4;
	for (int y = 0; y < frameH; ++y)
		std::memcpy(PixelAt(result, 0, y), PixelAt(strip, 0, y0 + y), row);
	return result;
}

// Grammaire : terme ( '^' (terme | op) )* ; terme = chemin | '(' expr ')' ; op = nom '(' nombres ')'.
class ComposeParser {
public:
	ComposeParser(std::string_view s, const std::filesystem::path &dir, int size, const SurfaceResolver &resolve)
		: m_s(s), m_dir(dir), m_size(size), m_resolve(resolve) {}

	Result<Surface, StringView> Expr() {
		auto term = Term();
		if (!term)
			return Err(term.Error());
		Surface img = std::move(term.Value());
		while (m_i < m_s.size() && m_s[m_i] == '^') {
			++m_i;
			if (!ApplyOp(img)) {
				auto top = Term();
				if (!top)
					return Err(top.Error());
				Surface topImg = std::move(top.Value());
				if (img.GetWidth() != topImg.GetWidth() || img.GetHeight() != topImg.GetHeight()) {
					auto a = SquareThumbnail(img, m_size);
					auto b = SquareThumbnail(topImg, m_size);
					if (!a || !b)
						return Err((a ? b : a).Error());
					img = std::move(a.Value());
					topImg = std::move(b.Value());
				}
				Overlay(img, topImg);
			}
		}
		if (img.GetWidth() != m_size || img.GetHeight() != m_size) {
			auto a = SquareThumbnail(img, m_size);
			if (!a)
				return Err(a.Error());
			img = std::move(a.Value());
		}
		return Ok(std::move(img));
	}

private:
	Result<Surface, StringView> Term() {
		if (m_i < m_s.size() && m_s[m_i] == '(') {
			++m_i;
			auto img = Expr();
			if (m_i < m_s.size() && m_s[m_i] == ')')
				++m_i;
			return img;
		}
		const size_t b = m_i;
		while (m_i < m_s.size() && m_s[m_i] != '^' && m_s[m_i] != ')')
			++m_i;
		const std::string term(StringView(m_s.substr(b, m_i - b)).Trim());
		if (m_resolve) {
			auto img = m_resolve(term);
			if (img)
				return img;
		}
		return LoadImageRgba(m_dir / term);
	}
	bool ApplyOp(Surface &img) {
		static constexpr std::string_view ops[] = {"tint(", "colorize(", "grayscale(", "darken(",
												   "opacity(", "lighting(", "frame(", "resize(", "crop(",
												   "flip(", "rotate(", "invert(", "contrast(", "saturate(",
												   "sepia(", "blur(", "pixelate(", "outline(",
												   "noise(", "threshold(", "chromakey(", "shift("};
		for (auto op : ops) {
			if (m_s.substr(m_i, op.size()) != op)
				continue;
			m_i += op.size();
			std::vector<float> args;
			while (m_i < m_s.size() && m_s[m_i] != ')') {
				while (m_i < m_s.size() && (m_s[m_i] == ' ' || m_s[m_i] == ','))
					++m_i;
				float v = 0;
				auto [p, ec] = std::from_chars(m_s.data() + m_i, m_s.data() + m_s.size(), v);
				if (ec != std::errc{})
					break;
				args.push_back(v);
				m_i = static_cast<size_t>(p - m_s.data());
			}
			if (m_i < m_s.size())
				++m_i; // ')'
			if (op == "tint(" && args.size() >= 3)
				Tint(img, uint8_t(args[0]), uint8_t(args[1]), uint8_t(args[2]));
			else if (op == "colorize(" && args.size() >= 4)
				Colorize(img, uint8_t(args[0]), uint8_t(args[1]), uint8_t(args[2]), args[3]);
			else if (op == "grayscale(")
				Grayscale(img);
			else if (op == "darken(" && !args.empty())
				ScaleBrightness(img, args[0]);
			else if (op == "opacity(" && !args.empty())
				ScaleAlpha(img, args[0]);
			else if (op == "lighting(" && !args.empty())
				ScaleBrightness(img, args[0]);
			else if (op == "frame(" && args.size() >= 2)
				img = ExtractFrame(img, int(args[0]), int(args[1]));
			else if (op == "resize(" && args.size() >= 2)
				img = ResizeImage(img, int(args[0]), int(args[1]));
			else if (op == "crop(" && args.size() >= 4)
				img = CropImage(img, int(args[0]), int(args[1]), int(args[2]), int(args[3]));
			else if (op == "flip(" && args.size() >= 2) {
				if (auto r = FlipImage(img, args[0] > 0, args[1] > 0)) img = std::move(r.Value());
			} else if (op == "rotate(" && !args.empty()) {
				if (auto r = Rotate90(img, int(args[0]))) img = std::move(r.Value());
			} else if (op == "invert(")
				Invert(img);
			else if (op == "contrast(" && !args.empty())
				AdjustContrast(img, args[0]);
			else if (op == "saturate(" && !args.empty())
				AdjustSaturation(img, args[0]);
			else if (op == "sepia(")
				Sepia(img);
			else if (op == "blur(" && !args.empty()) {
				if (auto r = Blur(img, std::max(1, int(args[0])))) img = std::move(r.Value());
			} else if (op == "pixelate(" && !args.empty())
				Pixelate(img, std::max(1, int(args[0])));
			else if (op == "outline(" && args.size() >= 4) {
				if (auto r = Outline(img, uint8_t(args[0]), uint8_t(args[1]), uint8_t(args[2]), int(args[3]))) img = std::move(r.Value());
			}
			else if (op == "noise(" && !args.empty())
				AddNoise(img, args[0]);
			else if (op == "threshold(" && !args.empty())
				Threshold(img, static_cast<uint8_t>(std::clamp(args[0], 0.f, 255.f)));
			else if (op == "chromakey(" && args.size() >= 4)
				ChromaKey(img, uint8_t(args[0]), uint8_t(args[1]), uint8_t(args[2]), args[3]);
			else if (op == "shift(" && args.size() >= 2) {
				if (auto r = Shift(img, int(args[0]), int(args[1]))) img = std::move(r.Value());
			}
			return true;
		}
		return false;
	}
	
	std::string_view m_s;
	const std::filesystem::path &m_dir;
	int m_size;
	const SurfaceResolver &m_resolve;
	size_t m_i = 0;
};

} // namespace

Result<Surface, StringView> LoadImageRgba(const std::filesystem::path &path) {
	auto surf = ImgLoad(String(path.string()));
	if (!surf)
		return Err(surf.Error());
	auto rgba = surf.Value().Convert(PixelFormat::RGBA32);
	if (!rgba)
		return Err(rgba.Error());
	return Ok(std::move(rgba.Value()));
}

Result<Surface, StringView> SquareThumbnail(const Surface &src, int size) {
	if (!src || size <= 0 || src.GetWidth() <= 0 || src.GetHeight() <= 0)
		return Surface::Solid(size > 0 ? size : 0, size > 0 ? size : 0, 0, 0, 0, 0);
	const int side = std::min(src.GetWidth(), src.GetHeight()); // première image d'une bande verticale
	Surface out = Transparent(size, size);
	if (!out)
		return Err(StringView("SquareThumbnail : allocation"));
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x) {
			const int x0 = x * side / size, x1 = std::max(x0 + 1, (x + 1) * side / size);
			const int y0 = y * side / size, y1 = std::max(y0 + 1, (y + 1) * side / size);
			uint32_t acc[4] = {}, n = 0;
			for (int sy = y0; sy < y1; ++sy)
				for (int sx = x0; sx < x1; ++sx) {
					const uint8_t *p = PixelAt(src, sx, sy);
					for (int c = 0; c < 4; ++c)
						acc[c] += p[c];
					++n;
				}
			uint8_t *d = PixelAt(out, x, y);
			for (int c = 0; c < 4; ++c)
				d[c] = static_cast<uint8_t>(acc[c] / n);
		}
	return Ok(std::move(out));
}

void Tint(Surface &img, uint8_t r, uint8_t g, uint8_t b) {
	if (!EnsureRgba32(img))
		return;
	for (int y = 0; y < img.GetHeight(); ++y)
		for (int x = 0; x < img.GetWidth(); ++x) {
			uint8_t *p = PixelAt(img, x, y);
			p[0] = static_cast<uint8_t>(p[0] * r / 255);
			p[1] = static_cast<uint8_t>(p[1] * g / 255);
			p[2] = static_cast<uint8_t>(p[2] * b / 255);
		}
}

void Overlay(Surface &dst, const Surface &top) {
	if (!EnsureRgba32(dst) || dst.GetWidth() != top.GetWidth() || dst.GetHeight() != top.GetHeight())
		return;
	for (int y = 0; y < dst.GetHeight(); ++y)
		for (int x = 0; x < dst.GetWidth(); ++x) {
			uint8_t *d = PixelAt(dst, x, y);
			const uint8_t *t = PixelAt(top, x, y);
			const float a = float(t[3]) / 255.f;
			for (int c = 0; c < 3; ++c)
				d[c] = static_cast<uint8_t>(std::lround(float(t[c]) * a + float(d[c]) * (1.f - a)));
			d[3] = std::max(d[3], t[3]);
		}
}

void ScaleAlpha(Surface &img, float f) {
	if (!EnsureRgba32(img))
		return;
	for (int y = 0; y < img.GetHeight(); ++y)
		for (int x = 0; x < img.GetWidth(); ++x) {
			uint8_t *p = PixelAt(img, x, y);
			p[3] = static_cast<uint8_t>(std::clamp(float(p[3]) * f, 0.f, 255.f));
		}
}

void ScaleBrightness(Surface &img, float f) {
	if (!EnsureRgba32(img))
		return;
	for (int y = 0; y < img.GetHeight(); ++y)
		for (int x = 0; x < img.GetWidth(); ++x) {
			uint8_t *p = PixelAt(img, x, y);
			p[0] = static_cast<uint8_t>(std::clamp(float(p[0]) * f, 0.f, 255.f));
			p[1] = static_cast<uint8_t>(std::clamp(float(p[1]) * f, 0.f, 255.f));
			p[2] = static_cast<uint8_t>(std::clamp(float(p[2]) * f, 0.f, 255.f));
		}
}

void Colorize(Surface &img, uint8_t r, uint8_t g, uint8_t b, float part) {
	if (!EnsureRgba32(img))
		return;
	const float p = std::clamp(part, 0.f, 1.f), q = 1.f - p;
	for (int y = 0; y < img.GetHeight(); ++y)
		for (int x = 0; x < img.GetWidth(); ++x) {
			uint8_t *d = PixelAt(img, x, y);
			d[0] = static_cast<uint8_t>(std::lround(float(d[0]) * q + float(r) * p));
			d[1] = static_cast<uint8_t>(std::lround(float(d[1]) * q + float(g) * p));
			d[2] = static_cast<uint8_t>(std::lround(float(d[2]) * q + float(b) * p));
		}
}

void Grayscale(Surface &img) {
	if (!EnsureRgba32(img))
		return;
	for (int y = 0; y < img.GetHeight(); ++y)
		for (int x = 0; x < img.GetWidth(); ++x) {
			uint8_t *p = PixelAt(img, x, y);
			const uint8_t l = static_cast<uint8_t>(std::lround(0.299f * float(p[0]) + 0.587f * float(p[1]) + 0.114f * float(p[2])));
			p[0] = p[1] = p[2] = l;
		}
}

Result<Surface, StringView> FlipImage(const Surface &src, bool horizontal, bool vertical) {
	Surface out = Transparent(src.GetWidth(), src.GetHeight());
	if (!out) return Err(StringView("FlipImage : allocation"));
	for (int y = 0; y < src.GetHeight(); ++y) {
		for (int x = 0; x < src.GetWidth(); ++x) {
			int dx = horizontal ? (src.GetWidth() - 1 - x) : x;
			int dy = vertical ? (src.GetHeight() - 1 - y) : y;
			const uint8_t *sp = PixelAt(src, x, y);
			uint8_t *dp = PixelAt(out, dx, dy);
			std::memcpy(dp, sp, 4);
		}
	}
	return Ok(std::move(out));
}

Result<Surface, StringView> Rotate90(const Surface &src, int angle) {
    angle = (angle % 360 + 360) % 360;
    if (angle == 0) return src.Duplicate();
    
    bool swapDims = (angle == 90 || angle == 270);
    int w = swapDims ? src.GetHeight() : src.GetWidth();
    int h = swapDims ? src.GetWidth() : src.GetHeight();
    Surface out = Transparent(w, h);
    if (!out) return Err(StringView("Rotate90 : allocation"));

    for (int y = 0; y < src.GetHeight(); ++y) {
        for (int x = 0; x < src.GetWidth(); ++x) {
            int dx = x, dy = y;
            if (angle == 90) { dx = src.GetHeight() - 1 - y; dy = x; }
            else if (angle == 180) { dx = src.GetWidth() - 1 - x; dy = src.GetHeight() - 1 - y; }
            else if (angle == 270) { dx = y; dy = src.GetWidth() - 1 - x; }
            
            const uint8_t *sp = PixelAt(src, x, y);
            uint8_t *dp = PixelAt(out, dx, dy);
            std::memcpy(dp, sp, 4);
        }
    }
    return Ok(std::move(out));
}

void Invert(Surface &img) {
	if (!EnsureRgba32(img)) return;
	for (int y = 0; y < img.GetHeight(); ++y)
		for (int x = 0; x < img.GetWidth(); ++x) {
			uint8_t *p = PixelAt(img, x, y);
			p[0] = 255 - p[0];
			p[1] = 255 - p[1];
			p[2] = 255 - p[2];
		}
}

void AdjustContrast(Surface &img, float factor) {
	if (!EnsureRgba32(img)) return;
	for (int y = 0; y < img.GetHeight(); ++y)
		for (int x = 0; x < img.GetWidth(); ++x) {
			uint8_t *p = PixelAt(img, x, y);
			for (int c = 0; c < 3; ++c) {
				float val = float(p[c]) / 255.0f;
				val = (val - 0.5f) * factor + 0.5f;
				p[c] = static_cast<uint8_t>(std::clamp(val * 255.0f, 0.0f, 255.0f));
			}
		}
}

void AdjustSaturation(Surface &img, float factor) {
	if (!EnsureRgba32(img)) return;
	for (int y = 0; y < img.GetHeight(); ++y)
		for (int x = 0; x < img.GetWidth(); ++x) {
			uint8_t *p = PixelAt(img, x, y);
			float r = p[0], g = p[1], b = p[2];
			float l = 0.299f * r + 0.587f * g + 0.114f * b;
			p[0] = static_cast<uint8_t>(std::clamp(l + (r - l) * factor, 0.f, 255.f));
			p[1] = static_cast<uint8_t>(std::clamp(l + (g - l) * factor, 0.f, 255.f));
			p[2] = static_cast<uint8_t>(std::clamp(l + (b - l) * factor, 0.f, 255.f));
		}
}

void Sepia(Surface &img) {
	if (!EnsureRgba32(img)) return;
	for (int y = 0; y < img.GetHeight(); ++y)
		for (int x = 0; x < img.GetWidth(); ++x) {
			uint8_t *p = PixelAt(img, x, y);
			float r = p[0], g = p[1], b = p[2];
			p[0] = static_cast<uint8_t>(std::clamp(0.393f * r + 0.769f * g + 0.189f * b, 0.f, 255.f));
			p[1] = static_cast<uint8_t>(std::clamp(0.349f * r + 0.686f * g + 0.168f * b, 0.f, 255.f));
			p[2] = static_cast<uint8_t>(std::clamp(0.272f * r + 0.534f * g + 0.131f * b, 0.f, 255.f));
		}
}

void Pixelate(Surface &img, int blockSize) {
	if (!EnsureRgba32(img) || blockSize <= 1) return;
	for (int y = 0; y < img.GetHeight(); y += blockSize) {
		for (int x = 0; x < img.GetWidth(); x += blockSize) {
			int bx = std::min(x + blockSize, img.GetWidth());
			int by = std::min(y + blockSize, img.GetHeight());
			uint32_t acc[4] = {0}, n = 0;
			
			// Moyenne du bloc
			for (int sy = y; sy < by; ++sy)
				for (int sx = x; sx < bx; ++sx) {
					const uint8_t *p = PixelAt(img, sx, sy);
					for (int c = 0; c < 4; ++c) acc[c] += p[c];
					++n;
				}
				
			// Application
			for (int sy = y; sy < by; ++sy)
				for (int sx = x; sx < bx; ++sx) {
					uint8_t *p = PixelAt(img, sx, sy);
					for (int c = 0; c < 4; ++c) p[c] = static_cast<uint8_t>(acc[c] / n);
				}
		}
	}
}

Result<Surface, StringView> Blur(const Surface &src, int radius) {
    Surface out = Transparent(src.GetWidth(), src.GetHeight());
    if (!out || radius <= 0) return Err(StringView("Blur : allocation ou rayon"));
    
    for (int y = 0; y < src.GetHeight(); ++y) {
        for (int x = 0; x < src.GetWidth(); ++x) {
            uint32_t acc[4] = {0};
            uint32_t n = 0;
            
            for (int dy = -radius; dy <= radius; ++dy) {
                int sy = std::clamp(y + dy, 0, src.GetHeight() - 1);
                for (int dx = -radius; dx <= radius; ++dx) {
                    int sx = std::clamp(x + dx, 0, src.GetWidth() - 1);
                    const uint8_t *sp = PixelAt(src, sx, sy);
                    for (int c = 0; c < 4; ++c) acc[c] += sp[c];
                    ++n;
                }
            }
            uint8_t *dp = PixelAt(out, x, y);
            for (int c = 0; c < 4; ++c) dp[c] = static_cast<uint8_t>(acc[c] / n);
        }
    }
    return Ok(std::move(out));
}

Result<Surface, StringView> Outline(const Surface &src, uint8_t r, uint8_t g, uint8_t b, int thickness) {
    auto dup = src.Duplicate();
    if (!dup) return Err(dup.Error());
    Surface out = std::move(dup.Value());
    
    for (int y = 0; y < src.GetHeight(); ++y) {
        for (int x = 0; x < src.GetWidth(); ++x) {
            const uint8_t *sp = PixelAt(src, x, y);
            if (sp[3] > 0) continue; // Uniquement sur les pixels transparents
            
            bool nearOpaque = false;
            for (int dy = -thickness; dy <= thickness && !nearOpaque; ++dy) {
                for (int dx = -thickness; dx <= thickness; ++dx) {
                    int sx = x + dx, sy = y + dy;
                    if (sx >= 0 && sx < src.GetWidth() && sy >= 0 && sy < src.GetHeight()) {
                        if (PixelAt(src, sx, sy)[3] > 128) { // Si un pixel voisin est opaque
                            nearOpaque = true;
                            break;
                        }
                    }
                }
            }
            if (nearOpaque) {
                uint8_t *dp = PixelAt(out, x, y);
                dp[0] = r; dp[1] = g; dp[2] = b; dp[3] = 255;
            }
        }
    }
    return Ok(std::move(out));
}

void AddNoise(Surface &img, float amount) {
	if (!EnsureRgba32(img) || amount <= 0.f) return;
	for (int y = 0; y < img.GetHeight(); ++y) {
		for (int x = 0; x < img.GetWidth(); ++x) {
			uint8_t *p = PixelAt(img, x, y);
			if (p[3] == 0) continue; // Ne pas bruiter les zones transparentes
			
			for (int c = 0; c < 3; ++c) {
				// Bruit entre -amount*255 et +amount*255
				float noise = (float(std::rand() % 2000) / 1000.f - 1.f) * amount * 255.f;
				p[c] = static_cast<uint8_t>(std::clamp(float(p[c]) + noise, 0.f, 255.f));
			}
		}
	}
}

void Threshold(Surface &img, uint8_t level) {
	if (!EnsureRgba32(img)) return;
	for (int y = 0; y < img.GetHeight(); ++y) {
		for (int x = 0; x < img.GetWidth(); ++x) {
			uint8_t *p = PixelAt(img, x, y);
			float luminance = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
			uint8_t val = (luminance >= level) ? 255 : 0;
			p[0] = p[1] = p[2] = val;
		}
	}
}

void ChromaKey(Surface &img, uint8_t r, uint8_t g, uint8_t b, float tolerance) {
	if (!EnsureRgba32(img)) return;
	for (int y = 0; y < img.GetHeight(); ++y) {
		for (int x = 0; x < img.GetWidth(); ++x) {
			uint8_t *p = PixelAt(img, x, y);
			// Distance euclidienne dans l'espace RGB
			const float dr = float(p[0]) - float(r), dg = float(p[1]) - float(g), db = float(p[2]) - float(b);
			float dist = std::sqrt(dr * dr + dg * dg + db * db);
			if (dist <= tolerance) {
				p[3] = 0; // Rend le pixel totalement transparent
			}
		}
	}
}

Result<Surface, StringView> Shift(const Surface &src, int dx, int dy) {
	Surface out = Transparent(src.GetWidth(), src.GetHeight());
	if (!out) return Err(StringView("Shift : allocation"));
	
	for (int y = 0; y < src.GetHeight(); ++y) {
		for (int x = 0; x < src.GetWidth(); ++x) {
			int nx = x + dx;
			int ny = y + dy;
			// Si le pixel décalé tombe dans les limites de l'image
			if (nx >= 0 && nx < src.GetWidth() && ny >= 0 && ny < src.GetHeight()) {
				const uint8_t *sp = PixelAt(src, x, y);
				uint8_t *dp = PixelAt(out, nx, ny);
				std::memcpy(dp, sp, 4);
			}
		}
	}
	return Ok(std::move(out));
}

Result<Surface, StringView> ComposeSurface(std::string_view expr, const std::filesystem::path &baseDir, int size,
											const SurfaceResolver &resolve) {
	return ComposeParser(expr, baseDir, size, resolve).Expr();
}

} // namespace sdl3
