// Définitions de sdl3/misc.hpp
#include "sdl3/core/misc.hpp"
#include "sdl3/core/error.hpp"

namespace sdl3 {

// ── PropertiesLock ───────────────────────────────────────────────────────────

PropertiesLock::PropertiesLock(SDL_PropertiesID id) : m_id(id) {
    if (m_id)
        SDL_LockProperties(m_id);
}

PropertiesLock::~PropertiesLock() {
    if (m_id)
        SDL_UnlockProperties(m_id);
}

// ── PropertiesBase ───────────────────────────────────────────────────────────

bool PropertiesBase::SetPointerWithCleanup(const char *name, void *val, SDL_CleanupPropertyCallback cleanup, void *userdata) {
    return SDL_SetPointerPropertyWithCleanup(mId, name, val, cleanup, userdata);
}

bool PropertiesBase::SetPointerWithCleanup(const char *name, void *val, std::function<void(void *)> cleanup) {
    auto *fn = new std::function<void(void *)>(std::move(cleanup));
    auto cb = [](void *ud, void *v) {
        auto *f = static_cast<std::function<void(void *)> *>(ud);
        (*f)(v);
        delete f;
    };
    return SDL_SetPointerPropertyWithCleanup(mId, name, val, cb, fn);
}

const char * PropertiesBase::GetString(const char *name, const char *def) const {
    return SDL_GetStringProperty(mId, name, def);
}

Sint64 PropertiesBase::GetNumber(const char *name, Sint64 def) const {
    return SDL_GetNumberProperty(mId, name, def);
}

float PropertiesBase::GetFloat(const char *name, float def) const {
    return SDL_GetFloatProperty(mId, name, def);
}

void * PropertiesBase::GetPointer(const char *name, void *def) const {
    return SDL_GetPointerProperty(mId, name, def);
}

sdl3::PropertyType PropertiesBase::PropertyType(const char *name) const noexcept {
    return sdl3::PropertyType(SDL_GetPropertyType(mId, name));
}

void PropertiesBase::Enumerate(std::function<void(const char *name)> fn) const {
    auto cb = [](void *ud, SDL_PropertiesID, const char *name) {
        (*static_cast<std::function<void(const char *)> *>(ud))(name);
    };
    SDL_EnumerateProperties(mId, cb, &fn);
}

uint64_t PropertiesBase::Count() const {
    uint64_t n = 0;
    Enumerate([&n](const char *) { ++n; });
    return n;
}

// ── Properties ───────────────────────────────────────────────────────────────

Properties::~Properties() {
    if (mId)
        SDL_DestroyProperties(mId);
}

Properties Properties::Clone() const {
    Properties dst = Properties::Create();
    SDL_CopyProperties(mId, dst.mId);
    return dst;
}

bool ShowMessage(MsgBoxKind kind, const String &title, const String &msg, SDL_Window *parent) {
    return SDL_ShowSimpleMessageBox(uint32_t(kind), title.CStr(), msg.CStr(), parent);
}

bool ShowInfo(const String &title, const String &msg, SDL_Window *parent) {
    return ShowMessage(MsgBoxKind::INFO, title, msg, parent);
}

bool ShowWarning(const String &title, const String &msg, SDL_Window *parent) {
    return ShowMessage(MsgBoxKind::WARNING, title, msg, parent);
}

bool ShowError(const String &title, const String &msg, SDL_Window *parent) {
    return ShowMessage(MsgBoxKind::ERROR, title, msg, parent);
}

namespace clipboard {

String GetText() {
    char *t = SDL_GetClipboardText();
    String s(t ? t : "");
    SDL_free(t);
    return s;
}

String GetPrimarySelectionText() {
    char *t = SDL_GetPrimarySelectionText();
    String s(t ? t : "");
    SDL_free(t);
    return s;
}

Option<std::vector<uint8_t>> GetData(const char *mimeType) {
    size_t size = 0;
    void *data = SDL_GetClipboardData(mimeType, &size);
    if (!data)
        return NONE;
    const auto *bytes = static_cast<const uint8_t *>(data);
    std::vector<uint8_t> out(bytes, bytes + size);
    SDL_free(data);
    return Some(std::move(out));
}

std::vector<String> GetMimeTypes() {
    size_t count = 0;
    char **types = SDL_GetClipboardMimeTypes(&count);
    if (!types)
        return {};
    std::vector<String> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i)
        out.emplace_back(types[i]);
    SDL_free(types);
    return out;
}

bool SetData(SDL_ClipboardDataCallback callback, SDL_ClipboardCleanupCallback cleanup,
             void *userdata, const std::vector<String> &mimeTypes) {
    std::vector<const char *> raw;
    raw.reserve(mimeTypes.size());
    for (const String &m : mimeTypes)
        raw.push_back(m.CStr());
    return SDL_SetClipboardData(callback, cleanup, userdata, raw.data(), raw.size());
}

} // namespace clipboard

// ── MessageBox::Show ─────────────────────────────────────────────────────────

Result<int, Error> MessageBox::Show() const {    std::vector<SDL_MessageBoxButtonData> rawButtons;
    rawButtons.reserve(buttons.size());
    for (const MessageBoxButton &b : buttons)
        rawButtons.push_back({b.flags, b.id, b.text.CStr()});

    SDL_MessageBoxData data{};
    data.flags = flags;
    data.window = window;
    data.title = title.CStr();
    data.message = message.CStr();
    data.numbuttons = int(rawButtons.size());
    data.buttons = rawButtons.empty() ? nullptr : rawButtons.data();

    SDL_MessageBoxColorScheme scheme{};
    if (colorScheme) {
        scheme = colorScheme->ToSDL();
        data.colorScheme = &scheme;
    }

    int buttonId = -1;
    if (!SDL_ShowMessageBox(&data, &buttonId))
        return Err(GetError());
    return Ok(buttonId);
}

String EncodeUrl(const String &url, const char *noEncodeChars) {
    char *encoded = SDL_EncodeURL(url.CStr(), noEncodeChars);
    if (!encoded)
        return {};
    String result(encoded);
    SDL_free(encoded);
    return result;
}

} // namespace sdl3
