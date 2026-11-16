// Définitions de sdl3/camera.hpp
#include "sdl3/input/camera.hpp"

namespace sdl3 {

namespace camera {

std::vector<CameraID> Enumerated() {
    int count = 0;
    CameraID *ids = SDL_GetCameras(&count);
    if (!ids)
        return {};
    std::vector<CameraID> v(ids, ids + count);
    SDL_free(ids);
    return v;
}

std::vector<CameraSpec> SupportedFormats(CameraID id) {
    int count = 0;
    SDL_CameraSpec **specs = SDL_GetCameraSupportedFormats(id, &count);
    if (!specs)
        return {};
    std::vector<CameraSpec> out;
    out.reserve(size_t(count));
    for (int i = 0; i < count; ++i)
        out.emplace_back(*specs[i]);
    SDL_free(specs);
    return out;
}

} // namespace camera

// ── CameraFrame ──────────────────────────────────────────────────────────────

void CameraFrame::Release() noexcept {
    if (m_camera && m_surface)
        SDL_ReleaseCameraFrame(m_camera, m_surface);
    m_camera = nullptr;
    m_surface = nullptr;
}

// ── Camera ───────────────────────────────────────────────────────────────────

Result<Camera, StringView> Camera::Open(CameraID id, Option<CameraSpec> spec) {
    SDL_CameraSpec sdlSpec{};
    if (spec.IsSome())
        sdlSpec = spec.Unwrap();

    auto *c = SDL_OpenCamera(id, spec.IsSome() ? &sdlSpec : nullptr);
    if (!c)
        return Err(GetError());
    return Ok(Camera(c));
}

CameraPermission Camera::Permission() const noexcept {
    return mHandle ? CameraPermission(SDL_GetCameraPermissionState(mHandle)) : CameraPermission::DENIED;
}

SDL_PropertiesID Camera::Properties() const noexcept {
    return mHandle ? SDL_GetCameraProperties(mHandle) : 0;
}

Option<CameraSpec> Camera::Format() const {
    SDL_CameraSpec spec{};
    if (!mHandle || !SDL_GetCameraFormat(mHandle, &spec))
        return NONE;
    return Some(CameraSpec(spec));
}

Option<CameraFrame> Camera::AcquireFrame() noexcept {
    if (!mHandle)
        return NONE;
    uint64_t ts = 0;
    SDL_Surface *frame = SDL_AcquireCameraFrame(mHandle, &ts);
    if (!frame)
        return NONE;
    return Some(CameraFrame(mHandle, frame, ts));
}

} // namespace sdl3
