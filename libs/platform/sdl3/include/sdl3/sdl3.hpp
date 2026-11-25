#pragma once
/**
 * @file sdl3.hpp
 * @brief En-tête « parapluie » du module sdl3 : inclut tout le wrapper C++23 de SDL3
 *        (core + SDL_image/mixer/net/ttf/shadercross) en une seule inclusion.
 */

// Stdinc: math/memory/ctype/environment wrappers over SDL_stdinc.h
#include "sdl3/core/stdinc.hpp"

// Core SDL3 error
#include "sdl3/core/error.hpp"

// Assertions: AssertState, AssertionHandler (SDL_assert.h)
#include "sdl3/core/assert.hpp"

// Hints: HintPriority, HintCallback (SDL_hints.h)
#include "sdl3/core/hints.hpp"

// Log: LogRouter (fan-out SDL_LogOutputFunction) + threaded FileLogSink
#include "sdl3/core/log.hpp"

// Thread: SDL_Thread/SDL_Mutex/SDL_RWLock/SDL_Semaphore/SDL_Condition/atomics
#include "sdl3/core/thread.hpp"

// SDL3: SDL_Init / SDL_Quit RAII guard
#include "sdl3/core/sdl_context.hpp"

// Core SDL3 colors, geometry
#include "sdl3/core/structs.hpp"

// Core SDL3 time
#include "sdl3/core/time.hpp"

// Rendering: Window, Surface, Texture, Renderer
#include "sdl3/render/render.hpp"

// Events
#include "sdl3/core/events.hpp"

// EventsManager: états d'évènements figés par image + liaisons (Bind/Unbind)
#include "sdl3/core/events_manager.hpp"

// Input: Keyboard, Mouse, Cursor, Gamepad
#include "sdl3/input/input.hpp"

// Audio: AudioStream, WavData
#include "sdl3/input/audio.hpp"

// File I/O: IOStream, readFile, writeFile
#include "sdl3/io/iostream.hpp"

// Async I/O: AsyncIO, AsyncIOQueue (SDL_asyncio.h)
#include "sdl3/core/asyncio.hpp"

// Shared object: SharedObject (SDL_loadso.h)
#include "sdl3/io/loadso.hpp"

// Misc: Properties, MessageBox, Clipboard, system info, CPU info
#include "sdl3/core/misc.hpp"

// Notification: system notifications (SDL_notification.h)
#include "sdl3/core/notification.hpp"

// System: platform integration (SDL_system.h)
#include "sdl3/core/system.hpp"

// Dialog: native open/save file & folder dialogs (async)
#include "sdl3/io/dialog.hpp"

// Camera: device enumeration, frame acquisition
#include "sdl3/input/camera.hpp"

// Sensor: accelerometer/gyroscope enumeration & reading
#include "sdl3/input/sensor.hpp"

// Process: subprocess creation, stdio piping
#include "sdl3/io/process.hpp"

// Storage: sandboxed title/user/file storage abstraction
#include "sdl3/io/storage.hpp"

// Joystick: raw device enumeration & queries (see input.hpp for Gamepad)
#include "sdl3/input/joystick.hpp"

// Haptic: force-feedback device enumeration, rumble & effects
#include "sdl3/input/haptic.hpp"

// Touch: touch device enumeration & active fingers
#include "sdl3/input/touch.hpp"

// Pen: stylus device type queries (event data lives in events.hpp)
#include "sdl3/input/pen.hpp"

// Power: battery/AC status query
#include "sdl3/core/power.hpp"

// Locale: user preferred language/country
#include "sdl3/core/locale.hpp"

// Guid: 128-bit device identifiers (joystick/gamepad/sensor)
#include "sdl3/core/guid.hpp"

// Filesystem: unsandboxed base/pref/user paths, directory ops, glob
#include "sdl3/io/filesystem.hpp"

// Tray: system tray icon + menu
#include "sdl3/io/tray.hpp"

// HID: raw USB/Bluetooth HID device access
#include "sdl3/input/hidapi.hpp"

// MIDI: transport-agnostic message (de)serialization + streaming parser
#include "sdl3/input/midi.hpp"

// DMX512: universe buffer + generic HID-widget transport helper
#include "sdl3/input/dmx.hpp"

// SDL3_ttf: Font, TextEngine, Text
#include "sdl3/render/ttf.hpp"

// SDL3_image: ImgContext, imgLoad, imgLoadTexture
#include "sdl3/render/image.hpp"

// SVG: analyse XML via dataio + rastérisation (nanosvg)
#include "sdl3/render/svg.hpp"

// SDL3_mixer: MixerContext, Mixer, MixAudio, MixTrack
#include "sdl3/input/mixer.hpp"

// SDL3_net: NetContext, IpAddress, TcpSocket, TcpServer, UdpSocket
#include "sdl3/io/net.hpp"

// GPU: GpuDevice, GpuBuffer, GpuTexture, GpuShader, GpuGraphicsPipeline, ...
#include "sdl3/render/gpu.hpp"

// Shader cross-compiler: ShaderCross, SPIR-V <-> HLSL/MSL/DXBC/DXIL
#include "sdl3/render/shadercross.hpp"

// OpenGL: GlContext + gl:: helpers
#include "sdl3/render/gl.hpp"

// Metal: MetalView (SDL_metal.h, plateformes Apple)
#include "sdl3/render/metal.hpp"

// Vulkan: VkLibrary + vulkan:: helpers
#include "sdl3/render/vulkan.hpp"

// Rendu 3D sur SDL_GPU (maillages procéduraux, matériaux physiques, caméra,
// lumières, ciel analytique, carte d'ombre) — voir render3d.hpp.
#include "sdl3/render/render3d.hpp"

// Canvas 2D par lots (polices, icônes), contexte graphique (fenêtre + device +
// renderer) et opérations d'images RGBA8 sur Surface.
#include "sdl3/render/canvas.hpp"
#include "sdl3/render/graphics_context.hpp"
#include "sdl3/render/image_ops.hpp"
