// Définitions de sdl3/dialog.hpp
#include "sdl3/io/dialog.hpp"

#include <memory>

namespace sdl3 {

namespace dialog {

namespace detail {

void SDLCALL Trampoline(void *userdata, const char *const *filelist, int filter) {
    const std::unique_ptr<DialogCtx> ctx(static_cast<DialogCtx *>(userdata)); // appelé une seule fois
    DialogResult res;
    if (!filelist) {
        res.error = Some(String(GetError()));
    } else {
        for (auto p = filelist; *p; ++p)
            res.files.push_back(String(*p));
    }
    if (ctx->fn)
        ctx->fn(res, filter);
}

std::vector<SDL_DialogFileFilter> ToSdlFilters(const std::vector<DialogFilter> &filters) {
    std::vector<SDL_DialogFileFilter> out;
    out.reserve(filters.size());
    for (auto &f : filters)
        out.push_back({f.name.c_str(), f.pattern.c_str()});
    return out;
}

namespace {
/// Contexte possédant le callback et une copie des filtres (valides jusqu'au trampoline).
std::unique_ptr<DialogCtx> MakeCtx(Callback cb, const std::vector<DialogFilter> &filters) {
    auto ctx = std::make_unique<DialogCtx>();
    ctx->fn = std::move(cb);
    ctx->filters = filters;
    ctx->sdlFilters = ToSdlFilters(ctx->filters);
    return ctx;
}
} // namespace

void ShowOpenFileImpl(Callback cb, SDL_Window *window, const std::vector<DialogFilter> &filters,
		const String &defaultLocation, bool allowMany) {
    auto ctx = MakeCtx(std::move(cb), filters);
    const SDL_DialogFileFilter *sdlFilters = ctx->sdlFilters.empty() ? nullptr : ctx->sdlFilters.data();
    const int count = static_cast<int>(ctx->sdlFilters.size());
    SDL_ShowOpenFileDialog(Trampoline, ctx.release(), window, sdlFilters, count,
                           defaultLocation.IsEmpty() ? nullptr : defaultLocation.c_str(), allowMany);
}

void ShowSaveFileImpl(Callback cb, SDL_Window *window, const std::vector<DialogFilter> &filters,
		const String &defaultLocation) {
    auto ctx = MakeCtx(std::move(cb), filters);
    const SDL_DialogFileFilter *sdlFilters = ctx->sdlFilters.empty() ? nullptr : ctx->sdlFilters.data();
    const int count = static_cast<int>(ctx->sdlFilters.size());
    SDL_ShowSaveFileDialog(Trampoline, ctx.release(), window, sdlFilters, count,
                           defaultLocation.IsEmpty() ? nullptr : defaultLocation.c_str());
}

void ShowOpenFolderImpl(Callback cb, SDL_Window *window, const String &defaultLocation, bool allowMany) {
    auto ctx = MakeCtx(std::move(cb), {});
    SDL_ShowOpenFolderDialog(Trampoline, ctx.release(), window,
                             defaultLocation.IsEmpty() ? nullptr : defaultLocation.c_str(), allowMany);
}

} // namespace detail

void ShowOpenFile(Callback cb, const std::vector<DialogFilter> &filters, const String &defaultLocation, bool allowMany) {
    detail::ShowOpenFileImpl(std::move(cb), nullptr, filters, defaultLocation, allowMany);
}

void ShowSaveFile(Callback cb, const std::vector<DialogFilter> &filters, const String &defaultLocation) {
    detail::ShowSaveFileImpl(std::move(cb), nullptr, filters, defaultLocation);
}

void ShowOpenFolder(Callback cb, const String &defaultLocation, bool allowMany) {
    detail::ShowOpenFolderImpl(std::move(cb), nullptr, defaultLocation, allowMany);
}

void ShowOpenFile(Callback cb, Window &window, const std::vector<DialogFilter> &filters,
		const String &defaultLocation, bool allowMany) {
    detail::ShowOpenFileImpl(std::move(cb), window.Get(), filters, defaultLocation, allowMany);
}

void ShowSaveFile(Callback cb, Window &window, const std::vector<DialogFilter> &filters, const String &defaultLocation) {
    detail::ShowSaveFileImpl(std::move(cb), window.Get(), filters, defaultLocation);
}

void ShowOpenFolder(Callback cb, Window &window, const String &defaultLocation, bool allowMany) {
    detail::ShowOpenFolderImpl(std::move(cb), window.Get(), defaultLocation, allowMany);
}

} // namespace dialog

} // namespace sdl3
