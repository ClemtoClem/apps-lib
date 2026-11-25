// Définitions de sdl3/process.hpp
#include "sdl3/io/process.hpp"

namespace sdl3 {

// ── Process ──────────────────────────────────────────────────────────────────

Result<Process, Error> Process::Create(const std::vector<String> &args, bool pipeStdio) {
    std::vector<const char *> argv;
    argv.reserve(args.size() + 1);
    for (auto &a : args)
        argv.push_back(a.c_str());
    argv.push_back(nullptr);

    auto *p = SDL_CreateProcess(argv.data(), pipeStdio);
    if (!p)
        return Err(GetError());
    return Ok(Process(p));
}

Result<Process, StringView> Process::CreateWithProperties(SDL_PropertiesID props) {
    auto *p = SDL_CreateProcessWithProperties(props);
    if (!p)
        return Err(GetError());
    return Ok(Process(p));
}

Result<Process, Error> Process::Create(const ProcessOptions &o) {
    if (o.args.empty())
        return Err(StringView("commande vide"));
    std::vector<const char *> argv;
    argv.reserve(o.args.size() + 1);
    for (auto &a : o.args)
        argv.push_back(a.c_str());
    argv.push_back(nullptr);
    SDL_Environment *env = nullptr;
    if (!o.environment.empty()) {
        env = SDL_CreateEnvironment(true);
        for (const auto &[name, value] : o.environment)
            SDL_SetEnvironmentVariable(env, name.c_str(), value.c_str(), true);
    }
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    if (env)
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, detail::ToSDL(o.stdinMode));
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, detail::ToSDL(o.stdoutMode));
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, o.stderrToStdout);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, o.background);
    SDL_Process *p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (env)
        SDL_DestroyEnvironment(env);
    if (!p)
        return Err(GetError());
    return Ok(Process(p));
}

Result<ProcessOutput, Error> Process::Run(const ProcessOptions &options) {
    auto process = Create(options);
    if (!process)
        return Err(process.Error());
    auto out = process.Value().Read();
    if (!out)
        return Err(out.Error());
    return Ok(std::move(out.Value()));
}

SDL_PropertiesID Process::Properties() const noexcept {
    return mHandle ? SDL_GetProcessProperties(mHandle) : 0;
}

Option<IOStreamView> Process::StdinStream() const noexcept {
    auto *io = mHandle ? SDL_GetProcessInput(mHandle) : nullptr;
    if (!io)
        return NONE;
    return Some(IOStreamView(io));
}

Option<IOStreamView> Process::StdoutStream() const noexcept {
    auto *io = mHandle ? SDL_GetProcessOutput(mHandle) : nullptr;
    if (!io)
        return NONE;
    return Some(IOStreamView(io));
}

Result<ProcessOutput, StringView> Process::Read() {
    if (!mHandle)
        return Err(StringView("process invalide"));
    size_t size = 0;
    int exitCode = 0;
    void *data = SDL_ReadProcess(mHandle, &size, &exitCode);
    if (!data)
        return Err(GetError());

    ProcessOutput out;
    out.data.assign(static_cast<uint8_t *>(data), static_cast<uint8_t *>(data) + size);
    out.exitCode = exitCode;
    SDL_free(data);
    return Ok(std::move(out));
}

Option<int> Process::Wait(bool block) noexcept {
    if (!mHandle)
        return NONE;
    int exitCode = 0;
    if (!SDL_WaitProcess(mHandle, block, &exitCode))
        return NONE;
    return Some(exitCode);
}

} // namespace sdl3
