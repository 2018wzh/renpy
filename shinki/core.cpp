// ShinkiEMU ABI v3 adapter for the original Ren'Py runtime.
#define PY_SSIZE_T_CLEAN
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
#include "vendor/shinki-emu-abi/include/shinki_emu.h"
#include <Python.h>
#include <SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" void init_librenpy(void);

bool push_event(const SDL_Event &event) {
  return SDL_PushEvent(const_cast<SDL_Event *>(&event)) == 1;
}

namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

ShinkiEmuBytes bytes(const char *s) noexcept {
  return {reinterpret_cast<const uint8_t *>(s), std::strlen(s)};
}
ShinkiEmuBytes bytes(const std::string &s) noexcept {
  return {reinterpret_cast<const uint8_t *>(s.data()), s.size()};
}
void clear(ShinkiEmuError *e) noexcept {
  if (e)
    *e = {};
}
ShinkiEmuStatus fail(ShinkiEmuError *e, ShinkiEmuStatus status, const char *code,
                    const char *message) noexcept {
  if (e) {
    *e = {};
    e->code_len =
        static_cast<uint32_t>((std::min)(std::strlen(code), sizeof(e->code)));
    e->message_len = static_cast<uint32_t>(
        (std::min)(std::strlen(message), sizeof(e->message)));
    // Do not leave a truncated UTF-8 sequence at the end of the message.
    const size_t length = std::strlen(message);
    if (e->message_len < length) {
      while (e->message_len &&
             (static_cast<unsigned char>(message[e->message_len]) & 0xc0) ==
                 0x80)
        --e->message_len;
    }
    std::memcpy(e->code, code, e->code_len);
    std::memcpy(e->message, message, e->message_len);
  }
  return status;
}
struct Failure : std::runtime_error {
  ShinkiEmuStatus status;
  const char *code;
  Failure(ShinkiEmuStatus s, const char *c, const std::string &m)
      : std::runtime_error(m), status(s), code(c) {}
};
[[noreturn]] void invalid(const char *m) {
  throw Failure(ShinkiEmuINVALID_ARGUMENT, "RENPY_INVALID_ARGUMENT", m);
}
template <class F> ShinkiEmuStatus boundary(ShinkiEmuError *e, F &&f) noexcept {
  clear(e);
  try {
    return f();
  } catch (const Failure &x) {
    return fail(e, x.status, x.code, x.what());
  } catch (const std::bad_alloc &) {
    return fail(e, ShinkiEmuCORE_ERROR, "RENPY_OUT_OF_MEMORY",
                "Native allocation failed");
  } catch (const std::exception &x) {
    return fail(e, ShinkiEmuCORE_ERROR, "RENPY_NATIVE_ERROR", x.what());
  } catch (...) {
    return fail(e, ShinkiEmuPANICKED, "RENPY_NATIVE_EXCEPTION",
                "Unknown native exception");
  }
}
std::mutex diagnostics_mutex;
ShinkiEmuDiagnostics diagnostics{};
void log(uint32_t level, const char *event,
         const std::string &message) noexcept {
  ShinkiEmuDiagnostics sink;
  {
    std::lock_guard<std::mutex> l(diagnostics_mutex);
    sink = diagnostics;
  }
  if (!sink.emit)
    return;
  const ShinkiEmuLogRecord record{
      level, 0, bytes("shinki.renpy"), bytes(event), bytes(message), nullptr, 0};
  try {
    sink.emit(sink.context, &record);
  } catch (...) {
  }
}
bool header(const ShinkiEmuHeader &h, size_t size) noexcept {
  return h.version == ShinkiEmuABI_VERSION && h.size >= size;
}
std::string text(ShinkiEmuBytes b, size_t maximum, bool empty = true) {
  if (b.len > maximum || (b.len && !b.data) || (!empty && !b.len)) invalid("Invalid UTF-8 byte span");
  if (!b.len) return {};
  if (std::memchr(b.data, 0, b.len)) invalid("Embedded NUL is not permitted");
  PyObject *value = PyUnicode_DecodeUTF8(reinterpret_cast<const char *>(b.data), static_cast<Py_ssize_t>(b.len), "strict");
  if (!value) { PyErr_Clear(); invalid("Invalid UTF-8 encoding"); }
  Py_DECREF(value);
  return {reinterpret_cast<const char *>(b.data), b.len};
}
fs::path canonical_directory(const std::string &value) {
  fs::path p = fs::u8path(value);
  if (!p.is_absolute()) invalid("Game path must be an absolute directory path");
  std::error_code ec; p = fs::canonical(p, ec);
  if (ec || !fs::is_directory(p)) invalid("Game path is not an accessible directory");
  return p;
}
std::string path_key(const fs::path &p) {
  std::string value = p.lexically_normal().u8string();
  std::replace(value.begin(), value.end(), '\\', '/');
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c); });
  return value;
}
std::string game_hash(const fs::path &p) {
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char ch : path_key(p)) hash = (hash ^ ch) * 1099511628211ull;
  std::ostringstream out; out << std::hex << std::setfill('0') << std::setw(16) << hash; return out.str();
}
fs::path package_root() {
#ifdef _WIN32
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&package_root), &module)) throw Failure(ShinkiEmuCORE_ERROR, "RENPY_MODULE_PATH", "Cannot locate adapter library");
  std::vector<wchar_t> name(32768); const DWORD n = GetModuleFileNameW(module, name.data(), static_cast<DWORD>(name.size()));
  if (!n || n >= name.size()) throw Failure(ShinkiEmuCORE_ERROR, "RENPY_MODULE_PATH", "Cannot read adapter library path");
  return fs::path(std::wstring(name.data(), n)).parent_path();
#else
  Dl_info info{};
  if (!dladdr(reinterpret_cast<const void *>(&package_root), &info) || !info.dli_fname) throw Failure(ShinkiEmuCORE_ERROR, "RENPY_MODULE_PATH", "Cannot locate adapter library");
  return fs::absolute(fs::u8path(info.dli_fname)).parent_path();
#endif
}
struct Detection {
  bool matched = false;
  const char *format = "";
};
Detection detect(const fs::path &root) {
  const fs::path game = fs::is_directory(root / "game") ? root / "game" : root;
  std::error_code ec;
  for (fs::directory_iterator i(game, ec), end; !ec && i != end; i.increment(ec)) {
    if (!i->is_regular_file(ec)) continue;
    auto extension = i->path().extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c); });
    if (extension != ".rpyc" && extension != ".rpa" && extension != ".rpy") continue;
    std::ifstream file(i->path(), std::ios::binary);
    std::array<unsigned char, 16> magic{};
    file.read(reinterpret_cast<char *>(magic.data()), magic.size());
    const auto count = file.gcount();
    if (extension == ".rpa" && count >= 7 &&
        (std::memcmp(magic.data(), "RPA-3.0", 7) == 0 ||
         std::memcmp(magic.data(), "RPA-2.0", 7) == 0 ||
         std::memcmp(magic.data(), "RPA-1.0", 7) == 0))
      return {true, "rpa"};
    if (extension == ".rpyc" &&
        ((count >= 10 && std::memcmp(magic.data(), "RENPY RPC2", 10) == 0) ||
         (count >= 2 && (magic[0] & 15) == 8 && (magic[0] >> 4) <= 7 &&
          ((magic[0] << 8) + magic[1]) % 31 == 0)))
      return {true, "rpyc"};
    if (extension == ".rpy" && count > 0 && game != root)
      return {true, "rpy"};
  }
  return {};
}

struct Slot {
  std::atomic<uint32_t> references{1};
  std::unique_ptr<uint8_t[]> pixels;
  size_t size = 0;
  ShinkiEmuSurfaceInfo info{};
  ShinkiEmuFrameIdentity identity{};
  uint64_t timestamp = 0;
  bool writing = false;
  void release() noexcept {
    if (references.fetch_sub(1, std::memory_order_acq_rel) == 1)
      delete this;
  }
};
void release_pixels(void *value) noexcept {
  if (value)
    static_cast<Slot *>(value)->release();
}
struct QueuedInput {
  ShinkiEmuInputEvent event{};
  std::string physical, logical;
};
struct Owner;
struct Session {
  std::mutex mutex;
  std::condition_variable changed;
  std::atomic<bool> cancel{false};
  ShinkiEmuHostServices host{};
  Owner *owner = nullptr;
  uint64_t epoch = 0, context = 0, output = 0, generation = 0, request = 0,
           wake = 0;
  std::string game_root;
  ShinkiEmuWindowState window{};
  bool user_paused = false, effective_pause = false, configured = false,
       done = false, failed = false;
  Clock::time_point pause_start{};
  double pause_total = 0;
  ShinkiEmuError failure{};
  ShinkiEmuSurfaceInfo surface{};
  std::array<Slot *, 4> slots{};
  Slot *latest = nullptr, *pending = nullptr;
  std::vector<QueuedInput> inputs, pumping;
  bool sent_focus = false, last_focus = false;
  SDL_Window *input_window = nullptr;
  Session() {
    try {
      for (auto &slot : slots)
        slot = new Slot;
    } catch (...) {
      for (auto slot : slots)
        if (slot)
          slot->release();
      throw;
    }
  }
  ~Session() {
    for (auto slot : slots)
      if (slot)
        slot->release();
  }
  bool cancelled() const noexcept {
    return cancel.load(std::memory_order_acquire) ||
           host.cancellation.is_cancelled(host.cancellation.context, epoch) !=
               0;
  }
  void update_pause() noexcept {
    const bool value = user_paused || window.suspended;
    if (value == effective_pause)
      return;
    const auto now = Clock::now();
    if (value)
      pause_start = now;
    else
      pause_total += std::chrono::duration<double>(now - pause_start).count();
    effective_pause = value;
  }
  void signal() noexcept {
    ++wake;
    changed.notify_all();
  }
};
thread_local Session *current = nullptr;
std::atomic<uint64_t> next_context{1};
std::mutex handles_mutex;
std::unordered_map<void *, std::shared_ptr<Session>> sessions;
struct Service;
struct Owner {
  std::shared_ptr<Service> service;
};
std::unordered_map<void *, std::shared_ptr<Owner>> owners;
std::shared_ptr<Session> session_handle(void *p) {
  std::lock_guard<std::mutex> lock(handles_mutex);
  const auto i = sessions.find(p);
  if (i == sessions.end())
    invalid("Invalid session handle");
  return i->second;
}
std::shared_ptr<Owner> owner_handle(void *p) {
  std::lock_guard<std::mutex> lock(handles_mutex);
  const auto i = owners.find(p);
  if (i == owners.end())
    invalid("A live module owner is required");
  return i->second;
}

bool dimensions(int rw, int rh, int lw, int lh) noexcept {
  return rw > 0 && rh > 0 && lw > 0 && lh > 0 &&
         rw <= ShinkiEmuMAX_RENDER_EXTENT && rh <= ShinkiEmuMAX_RENDER_EXTENT &&
         lw <= ShinkiEmuMAX_RENDER_EXTENT && lh <= ShinkiEmuMAX_RENDER_EXTENT &&
         static_cast<uint64_t>(rw) * rh * 4 <= ShinkiEmuMAX_FRAME_BYTES;
}
void configure(Session &s, int rw, int rh, int lw, int lh) {
  if (!dimensions(rw, rh, lw, lh))
    invalid("Invalid renderer surface dimensions");
  if (s.pending)
    throw Failure(ShinkiEmuINVALID_STATE, "RENPY_FRAME_IN_PROGRESS",
                  "Cannot reconfigure a reserved frame");
  if (!s.configured || s.surface.raster.width != static_cast<uint32_t>(rw) ||
      s.surface.raster.height != static_cast<uint32_t>(rh) ||
      s.surface.logical.width != static_cast<uint32_t>(lw) ||
      s.surface.logical.height != static_cast<uint32_t>(lh)) {
    s.surface = {ShinkiEmuSURFACE_CPU,
                 ShinkiEmuPIXEL_RGBA8,
                 ShinkiEmuCOLOR_SRGB,
                 ShinkiEmuALPHA_IGNORED,
                 ShinkiEmuROWS_BOTTOM_UP,
                 {static_cast<uint32_t>(lw), static_cast<uint32_t>(lh)},
                 {static_cast<uint32_t>(rw), static_cast<uint32_t>(rh)},
                 static_cast<uint64_t>(rw) * 4};
    ++s.output;
    s.latest = nullptr;
    s.configured = true;
    s.signal();
  }
}
Session &python_session() {
  if (!current)
    throw Failure(ShinkiEmuINVALID_STATE, "RENPY_NO_SESSION",
                  "No active Ren'Py session on this thread");
  return *current;
}
template <class F> PyObject *python_boundary(F &&f) noexcept {
  try {
    return f();
  } catch (const std::bad_alloc &) {
    return PyErr_NoMemory();
  } catch (const std::exception &x) {
    PyErr_SetString(PyExc_RuntimeError, x.what());
    return nullptr;
  } catch (...) {
    PyErr_SetString(PyExc_RuntimeError, "Unknown native bridge exception");
    return nullptr;
  }
}
PyObject *py_cancelled(PyObject *, PyObject *) {
  return python_boundary(
      [] { return PyBool_FromLong(python_session().cancelled()); });
}
PyObject *py_paused(PyObject *, PyObject *) {
  return python_boundary([] {
    auto &s = python_session();
    std::lock_guard<std::mutex> l(s.mutex);
    return PyBool_FromLong(s.effective_pause);
  });
}
PyObject *py_paused_seconds(PyObject *, PyObject *) {
  return python_boundary([] {
    auto &s = python_session();
    std::lock_guard<std::mutex> l(s.mutex);
    return PyFloat_FromDouble(
        s.pause_total + (s.effective_pause ? std::chrono::duration<double>(
                                                 Clock::now() - s.pause_start)
                                                 .count()
                                           : 0.0));
  });
}
PyObject *py_active(PyObject *, PyObject *) {
  return python_boundary([] {
    auto &s = python_session();
    std::lock_guard<std::mutex> l(s.mutex);
    return PyBool_FromLong(!s.window.suspended);
  });
}
PyObject *py_focused(PyObject *, PyObject *) {
  return python_boundary([] {
    auto &s = python_session();
    std::lock_guard<std::mutex> l(s.mutex);
    return PyBool_FromLong(s.window.focused);
  });
}
struct AllowThreads {
  PyThreadState *state = PyEval_SaveThread();
  ~AllowThreads() { PyEval_RestoreThread(state); }
};
PyObject *py_wait(PyObject *, PyObject *args) {
  return python_boundary([&]() -> PyObject * {
    int milliseconds;
    if (!PyArg_ParseTuple(args, "i:wait", &milliseconds))
      return nullptr;
    if (milliseconds < 0)
      invalid("Negative wait duration");
    auto &s = python_session();
    {
      AllowThreads released;
      std::unique_lock<std::mutex> lock(s.mutex);
      const auto wake = s.wake;
      const auto deadline =
          Clock::now() + std::chrono::milliseconds(milliseconds);
      while (wake == s.wake && !s.cancelled() && Clock::now() < deadline)
        s.changed.wait_until(
            lock,
            (std::min)(deadline, Clock::now() + std::chrono::milliseconds(25)));
    }
    Py_RETURN_NONE;
  });
}
PyObject *py_configure_surface(PyObject *, PyObject *args) {
  return python_boundary([&]() -> PyObject * {
    int rw, rh, lw, lh;
    if (!PyArg_ParseTuple(args, "iiii:configure_surface", &rw, &rh, &lw, &lh))
      return nullptr;
    auto &s = python_session();
    std::lock_guard<std::mutex> lock(s.mutex);
    configure(s, rw, rh, lw, lh);
    Py_RETURN_NONE;
  });
}
PyObject *py_begin_frame(PyObject *, PyObject *args) {
  return python_boundary([&]() -> PyObject * {
    int rw, rh, lw, lh;
    if (!PyArg_ParseTuple(args, "iiii:begin_frame", &rw, &rh, &lw, &lh))
      return nullptr;
    auto &s = python_session();
    std::lock_guard<std::mutex> lock(s.mutex);
    configure(s, rw, rh, lw, lh);
    if (s.cancelled() || s.effective_pause)
      Py_RETURN_NONE;
    const size_t size = static_cast<size_t>(rw) * rh * 4;
    for (auto slot : s.slots) {
      if (slot == s.latest || slot->writing ||
          slot->references.load(std::memory_order_acquire) != 1)
        continue;
      if (slot->size != size) {
        std::unique_ptr<uint8_t[]> storage(new uint8_t[size]);
        slot->pixels = std::move(storage);
        slot->size = size;
      }
      auto view =
          PyMemoryView_FromMemory(reinterpret_cast<char *>(slot->pixels.get()),
                                  static_cast<Py_ssize_t>(size), PyBUF_WRITE);
      if (!view)
        return nullptr;
      slot->writing = true;
      s.pending = slot;
      return view;
    }
    Py_RETURN_NONE;
  });
}
PyObject *py_end_frame(PyObject *, PyObject *) {
  return python_boundary([]() -> PyObject * {
    auto &s = python_session();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.pending)
      throw Failure(ShinkiEmuINVALID_STATE, "RENPY_NO_FRAME_RESERVATION",
                    "No frame is reserved");
    Slot *slot = s.pending;
    slot->info = s.surface;
    slot->identity = {s.epoch, s.output, ++s.generation};
    slot->timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch())
            .count());
    slot->writing = false;
    s.pending = nullptr;
    s.latest = slot;
    s.signal();
    Py_RETURN_NONE;
  });
}
PyObject *py_abort_frame(PyObject *, PyObject *) {
  return python_boundary([]() -> PyObject * {
    auto &s = python_session();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.pending) {
      s.pending->writing = false;
      s.pending = nullptr;
    }
    Py_RETURN_NONE;
  });
}

SDL_Scancode scancode(const std::string &key) noexcept {
  if (key.size() == 4 && key.compare(0, 3, "Key") == 0 && key[3] >= 'A' &&
      key[3] <= 'Z')
    return static_cast<SDL_Scancode>(SDL_SCANCODE_A + key[3] - 'A');
  if (key.size() == 6 && key.compare(0, 5, "Digit") == 0 && key[5] >= '0' &&
      key[5] <= '9')
    return key[5] == '0'
               ? SDL_SCANCODE_0
               : static_cast<SDL_Scancode>(SDL_SCANCODE_1 + key[5] - '1');
  if (key.size() >= 2 && key.size() <= 3 && key[0] == 'F') {
    unsigned value = 0;
    for (size_t i = 1; i < key.size(); ++i) {
      if (key[i] < '0' || key[i] > '9')
        return SDL_SCANCODE_UNKNOWN;
      value = value * 10 + key[i] - '0';
    }
    if (value >= 1 && value <= 12)
      return static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + value - 1);
    if (value >= 13 && value <= 24)
      return static_cast<SDL_Scancode>(SDL_SCANCODE_F13 + value - 13);
  }
  struct Key {
    const char *name;
    SDL_Scancode code;
  };
  static constexpr Key keys[] = {{"Enter", SDL_SCANCODE_RETURN},
                                 {"NumpadEnter", SDL_SCANCODE_KP_ENTER},
                                 {"Escape", SDL_SCANCODE_ESCAPE},
                                 {"Space", SDL_SCANCODE_SPACE},
                                 {"Tab", SDL_SCANCODE_TAB},
                                 {"Backspace", SDL_SCANCODE_BACKSPACE},
                                 {"Delete", SDL_SCANCODE_DELETE},
                                 {"Insert", SDL_SCANCODE_INSERT},
                                 {"Home", SDL_SCANCODE_HOME},
                                 {"End", SDL_SCANCODE_END},
                                 {"PageUp", SDL_SCANCODE_PAGEUP},
                                 {"PageDown", SDL_SCANCODE_PAGEDOWN},
                                 {"ArrowLeft", SDL_SCANCODE_LEFT},
                                 {"ArrowRight", SDL_SCANCODE_RIGHT},
                                 {"ArrowUp", SDL_SCANCODE_UP},
                                 {"ArrowDown", SDL_SCANCODE_DOWN},
                                 {"ShiftLeft", SDL_SCANCODE_LSHIFT},
                                 {"ShiftRight", SDL_SCANCODE_RSHIFT},
                                 {"ControlLeft", SDL_SCANCODE_LCTRL},
                                 {"ControlRight", SDL_SCANCODE_RCTRL},
                                 {"AltLeft", SDL_SCANCODE_LALT},
                                 {"AltRight", SDL_SCANCODE_RALT},
                                 {"SuperLeft", SDL_SCANCODE_LGUI},
                                 {"SuperRight", SDL_SCANCODE_RGUI},
                                 {"MetaLeft", SDL_SCANCODE_LGUI},
                                 {"MetaRight", SDL_SCANCODE_RGUI},
                                 {"Minus", SDL_SCANCODE_MINUS},
                                 {"Equal", SDL_SCANCODE_EQUALS},
                                 {"BracketLeft", SDL_SCANCODE_LEFTBRACKET},
                                 {"BracketRight", SDL_SCANCODE_RIGHTBRACKET},
                                 {"Backslash", SDL_SCANCODE_BACKSLASH},
                                 {"IntlBackslash", SDL_SCANCODE_NONUSBACKSLASH},
                                 {"Semicolon", SDL_SCANCODE_SEMICOLON},
                                 {"Quote", SDL_SCANCODE_APOSTROPHE},
                                 {"Backquote", SDL_SCANCODE_GRAVE},
                                 {"Comma", SDL_SCANCODE_COMMA},
                                 {"Period", SDL_SCANCODE_PERIOD},
                                 {"Slash", SDL_SCANCODE_SLASH},
                                 {"CapsLock", SDL_SCANCODE_CAPSLOCK},
                                 {"NumLock", SDL_SCANCODE_NUMLOCKCLEAR},
                                 {"ScrollLock", SDL_SCANCODE_SCROLLLOCK},
                                 {"PrintScreen", SDL_SCANCODE_PRINTSCREEN},
                                 {"Pause", SDL_SCANCODE_PAUSE},
                                 {"ContextMenu", SDL_SCANCODE_APPLICATION},
                                 {"NumpadAdd", SDL_SCANCODE_KP_PLUS},
                                 {"NumpadSubtract", SDL_SCANCODE_KP_MINUS},
                                 {"NumpadMultiply", SDL_SCANCODE_KP_MULTIPLY},
                                 {"NumpadDivide", SDL_SCANCODE_KP_DIVIDE},
                                 {"NumpadDecimal", SDL_SCANCODE_KP_PERIOD},
                                 {"NumpadEqual", SDL_SCANCODE_KP_EQUALS},
                                 {"NumpadComma", SDL_SCANCODE_KP_COMMA},
                                 {"IntlRo", SDL_SCANCODE_INTERNATIONAL1},
                                 {"KanaMode", SDL_SCANCODE_INTERNATIONAL2},
                                 {"IntlYen", SDL_SCANCODE_INTERNATIONAL3},
                                 {"Convert", SDL_SCANCODE_INTERNATIONAL4},
                                 {"NonConvert", SDL_SCANCODE_INTERNATIONAL5},
                                 {"Lang1", SDL_SCANCODE_LANG1},
                                 {"Lang2", SDL_SCANCODE_LANG2},
                                 {"Lang3", SDL_SCANCODE_LANG3},
                                 {"Lang4", SDL_SCANCODE_LANG4},
                                 {"Lang5", SDL_SCANCODE_LANG5},
                                 {"AudioVolumeMute", SDL_SCANCODE_AUDIOMUTE},
                                 {"AudioVolumeDown", SDL_SCANCODE_VOLUMEDOWN},
                                 {"AudioVolumeUp", SDL_SCANCODE_VOLUMEUP}};
  if (key.size() == 7 && key.compare(0, 6, "Numpad") == 0 && key[6] >= '0' &&
      key[6] <= '9')
    return key[6] == '0'
               ? SDL_SCANCODE_KP_0
               : static_cast<SDL_Scancode>(SDL_SCANCODE_KP_1 + key[6] - '1');
  for (const auto &entry : keys)
    if (key == entry.name)
      return entry.code;
  return SDL_SCANCODE_UNKNOWN;
}
bool logical_text(const std::string &value) noexcept {
  if (value.empty()) return false;
  for (unsigned char c : value) if (c < 32 || c == 127) return false;
  static constexpr const char *named[] = {"Enter", "Escape", "Tab", "Backspace", "Delete", "Insert", "Home", "End", "PageUp", "PageDown", "ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Shift", "Control", "Alt", "AltGraph", "Meta", "Super", "CapsLock", "NumLock", "ScrollLock", "Pause", "PrintScreen", "ContextMenu", "Unidentified", "Dead", "Clear", "Copy", "Cut", "Paste", "Undo", "Redo", "Help", "Fn", "FnLock", "Hyper", "Symbol", "SymbolLock"};
  for (const char *name : named) if (value == name) return false;
  if (value.size() >= 2 && value.size() <= 3 && value[0] == 'F' && value[1] >= '0' && value[1] <= '9') return false;
  return true;
}
void send_text(const std::string &value) {
  size_t offset = 0;
  while (offset < value.size()) {
    size_t count = (std::min)(value.size() - offset, static_cast<size_t>(SDL_TEXTINPUTEVENT_TEXT_SIZE - 1));
    if (offset + count < value.size()) while (count && (static_cast<unsigned char>(value[offset + count]) & 0xc0) == 0x80) --count;
    SDL_Event injected{};
    injected.type = SDL_TEXTINPUT;
    std::memcpy(injected.text.text, value.data() + offset, count);
    if (!push_event(injected)) throw Failure(ShinkiEmuCORE_ERROR, "RENPY_INPUT_QUEUE", "SDL input queue rejected text");
    offset += count;
  }
}
PyObject *py_pump_events(PyObject *, PyObject *args) {
  return python_boundary([&]() -> PyObject * {
    double scale_x, scale_y;
    if (!PyArg_ParseTuple(args, "dd:pump_events", &scale_x, &scale_y)) return nullptr;
    if (!std::isfinite(scale_x) || !std::isfinite(scale_y) || scale_x <= 0 || scale_y <= 0) invalid("Invalid renderer coordinate scale");
    auto &s = python_session();
    auto *window = SDL_GL_GetCurrentWindow();
    if (!window) Py_RETURN_NONE;
    bool focused; uint64_t output_epoch;
    { std::lock_guard<std::mutex> lock(s.mutex); s.pumping.clear(); s.pumping.swap(s.inputs); focused = s.window.focused != 0; output_epoch = s.output; }
    const Uint32 window_id = SDL_GetWindowID(window);
    if (!s.sent_focus || s.last_focus != focused || s.input_window != window) {
      SDL_Event focus{}; focus.type = SDL_WINDOWEVENT; focus.window.windowID = window_id; focus.window.event = focused ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
      if (!push_event(focus)) throw Failure(ShinkiEmuCORE_ERROR, "RENPY_INPUT_QUEUE", "SDL input queue rejected focus");
      s.last_focus = focused; s.sent_focus = true; s.input_window = window;
    }
    for (const auto &item : s.pumping) {
      const auto &event = item.event;
      if (s.cancelled()) break;
      if (event.kind == ShinkiEmuINPUT_WINDOW || event.displayed.output_epoch != output_epoch) continue;
      SDL_Event injected{};
      if (event.kind == ShinkiEmuINPUT_KEY) {
        const auto code = scancode(item.physical.empty() ? item.logical : item.physical);
        if (code != SDL_SCANCODE_UNKNOWN) { injected.type = event.pressed ? SDL_KEYDOWN : SDL_KEYUP; injected.key.windowID = window_id; injected.key.state = event.pressed ? SDL_PRESSED : SDL_RELEASED; injected.key.repeat = event.repeat; injected.key.keysym.scancode = code; injected.key.keysym.sym = SDL_GetKeyFromScancode(code); if (!push_event(injected)) throw Failure(ShinkiEmuCORE_ERROR, "RENPY_INPUT_QUEUE", "SDL input queue rejected key"); }
        if (event.pressed && logical_text(item.logical)) send_text(item.logical);
      } else if (event.kind == ShinkiEmuINPUT_POINTER_MOVE || event.kind == ShinkiEmuINPUT_POINTER_BUTTON) {
        const double px = event.x / scale_x, py = event.y / scale_y;
        if (px < std::numeric_limits<int>::min() || px > std::numeric_limits<int>::max() || py < std::numeric_limits<int>::min() || py > std::numeric_limits<int>::max()) invalid("Pointer coordinate exceeds SDL range");
        injected.type = SDL_MOUSEMOTION; injected.motion.windowID = window_id; injected.motion.x = static_cast<int>(px); injected.motion.y = static_cast<int>(py); if (!push_event(injected)) throw Failure(ShinkiEmuCORE_ERROR, "RENPY_INPUT_QUEUE", "SDL input queue rejected pointer");
        if (event.kind == ShinkiEmuINPUT_POINTER_BUTTON) { injected = {}; injected.type = event.pressed ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP; injected.button.windowID = window_id; injected.button.state = event.pressed ? SDL_PRESSED : SDL_RELEASED; injected.button.button = event.button == ShinkiEmuBUTTON_PRIMARY ? SDL_BUTTON_LEFT : event.button == ShinkiEmuBUTTON_SECONDARY ? SDL_BUTTON_RIGHT : SDL_BUTTON_MIDDLE; injected.button.x = static_cast<int>(px); injected.button.y = static_cast<int>(py); if (!push_event(injected)) throw Failure(ShinkiEmuCORE_ERROR, "RENPY_INPUT_QUEUE", "SDL input queue rejected button"); }
      } else if (event.kind == ShinkiEmuINPUT_WHEEL) { injected.type = SDL_MOUSEWHEEL; injected.wheel.windowID = window_id; injected.wheel.x = static_cast<int>(event.x); injected.wheel.y = static_cast<int>(event.y); if (!push_event(injected)) throw Failure(ShinkiEmuCORE_ERROR, "RENPY_INPUT_QUEUE", "SDL input queue rejected wheel"); }
    }
    s.pumping.clear(); Py_RETURN_NONE;
  });
}
PyObject *py_window(PyObject *, PyObject *args, PyObject *kwargs) {
  return python_boundary([&]() -> PyObject * {
    int kind, flags = 0, width = 0, height = 0, x = 0, y = 0;
    const char *value = "";
    Py_ssize_t length = 0;
    static const char *names[] = {"kind",   "flags", "text", "width",
                                  "height", "x",     "y",    nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|is#iiii:window",
                                     const_cast<char **>(names), &kind, &flags,
                                     &value, &length, &width, &height, &x, &y))
      return nullptr;
    if (kind < ShinkiEmuWINDOW_TITLE || kind > ShinkiEmuWINDOW_QUERY_STATE ||
        flags < 0 || width < 0 || height < 0)
      invalid("Invalid typed window request");
    const auto title = text(
        {reinterpret_cast<const uint8_t *>(value), static_cast<size_t>(length)},
        ShinkiEmuMAX_MODAL_TEXT);
    auto &s = python_session();
    ShinkiEmuWindowResult result{};
    ShinkiEmuStatus status = ShinkiEmuUNSUPPORTED;
    if (s.cancelled())
      status = ShinkiEmuCANCELLED;
    else if (s.host.window.request) {
      ShinkiEmuWindowRequest request{};
      request.session_epoch = s.epoch;
      request.context_epoch = s.context;
      request.request_id = ++s.request;
      request.kind = static_cast<uint32_t>(kind);
      request.flags = static_cast<uint32_t>(flags);
      request.text = bytes(title);
      request.width = static_cast<uint32_t>(width);
      request.height = static_cast<uint32_t>(height);
      request.x = x;
      request.y = y;
      ShinkiEmuError error{};
      {
        AllowThreads released;
        status = s.host.window.request(s.host.window.context, &request, &result,
                                       &error);
      }
      if (status != ShinkiEmuOK && status != ShinkiEmuCANCELLED)
        log(ShinkiEmuLOG_WARN, "window.request_failed",
            "Host rejected Ren'Py window request kind=" + std::to_string(kind));
    }
    if (status != ShinkiEmuOK)
      result = {};
    return Py_BuildValue("(iIIIIiiII)", status, result.valid_fields,
                         result.fullscreen, result.width, result.height,
                         result.x, result.y, result.visible, result.minimized);
  });
}

PyMethodDef shinki_methods[] = {
    {"cancelled", py_cancelled, METH_NOARGS, nullptr},
    {"paused", py_paused, METH_NOARGS, nullptr},
    {"paused_seconds", py_paused_seconds, METH_NOARGS, nullptr},
    {"wait", py_wait, METH_VARARGS, nullptr},
    {"active", py_active, METH_NOARGS, nullptr},
    {"focused", py_focused, METH_NOARGS, nullptr},
    {"pump_events", py_pump_events, METH_VARARGS, nullptr},
    {"configure_surface", py_configure_surface, METH_VARARGS, nullptr},
    {"begin_frame", py_begin_frame, METH_VARARGS, nullptr},
    {"end_frame", py_end_frame, METH_NOARGS, nullptr},
    {"abort_frame", py_abort_frame, METH_NOARGS, nullptr},
    {"window", reinterpret_cast<PyCFunction>(py_window),
     METH_VARARGS | METH_KEYWORDS, nullptr},
    {nullptr, nullptr, 0, nullptr}};
PyModuleDef shinki_module = {PyModuleDef_HEAD_INIT,
                            "_shinki",
                            nullptr,
                            -1,
                            shinki_methods,
                            nullptr,
                            nullptr,
                            nullptr,
                            nullptr};
PyObject *PyInit__shinki() { return PyModule_Create(&shinki_module); }

std::string python_error() {
  // Never PyErr_Print: its SystemExit handling can exit the embedding host.
  PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
  PyErr_Fetch(&type, &value, &traceback);
  PyErr_NormalizeException(&type, &value, &traceback);
  std::string message = "Ren'Py Python exception";
  PyObject *formatted = nullptr;
  PyObject *module = PyImport_ImportModule("traceback");
  if (module) {
    PyObject *lines = PyObject_CallMethod(
        module, "format_exception", "OOO", type ? type : Py_None,
        value ? value : Py_None, traceback ? traceback : Py_None);
    if (lines) {
      PyObject *separator = PyUnicode_FromString("");
      if (separator) {
        formatted = PyUnicode_Join(separator, lines);
        Py_DECREF(separator);
      }
      Py_DECREF(lines);
    }
    Py_DECREF(module);
  }
  if (!formatted && value) {
    PyErr_Clear();
    formatted = PyObject_Str(value);
  }
  if (formatted) {
    const char *s = PyUnicode_AsUTF8(formatted);
    if (s)
      message = s;
    Py_DECREF(formatted);
  }
  Py_XDECREF(type);
  Py_XDECREF(value);
  Py_XDECREF(traceback);
  PyErr_Clear();
  return message;
}
void check(PyStatus status) {
  if (PyStatus_Exception(status))
    throw Failure(ShinkiEmuCORE_ERROR, "RENPY_PYTHON_INITIALIZATION",
                  status.err_msg ? status.err_msg
                                 : "CPython initialization requested an exit");
}
struct PythonConfig {
  PyConfig value;
  PythonConfig() { PyConfig_InitIsolatedConfig(&value); }
  ~PythonConfig() { PyConfig_Clear(&value); }
};
void initialize_python(bool &owns_python) {
  const fs::path root = package_root(), runtime = root / "Resources";
#ifdef _WIN32
  constexpr const char *display_module = "display.pyd";
#else
  constexpr const char *display_module = "display.so";
#endif
  if (!fs::is_regular_file(runtime / "shinki_renpy_bridge.py") || !fs::is_regular_file(runtime / "renpy" / "__init__.py") || !fs::is_regular_file(runtime / "overrides" / display_module))
    throw Failure(ShinkiEmuCORE_ERROR, "RENPY_RUNTIME_MISSING", "Packaged Ren'Py runtime, bridge or display override is missing");
  if (Py_IsInitialized()) throw Failure(ShinkiEmuINVALID_STATE, "RENPY_FOREIGN_INTERPRETER", "SDK CPython was initialized outside the Ren'Py owner service");
  if (PyImport_AppendInittab("_shinki", &PyInit__shinki) < 0) throw Failure(ShinkiEmuCORE_ERROR, "RENPY_BUILTIN_REGISTRATION", "Cannot register the native _shinki bridge");
  init_librenpy();
  PyPreConfig pre; PyPreConfig_InitIsolatedConfig(&pre); pre.utf8_mode = 1; check(Py_PreInitialize(&pre));
  PythonConfig config; auto &c = config.value; c.use_environment = 0; c.site_import = 0; c.write_bytecode = 0; c.parse_argv = 0; c.install_signal_handlers = 0; c.module_search_paths_set = 1;
  const auto set_path = [&](wchar_t **slot, const fs::path &path) { const std::string value = path.u8string(); check(PyConfig_SetBytesString(&c, slot, value.c_str())); };
  set_path(&c.home, runtime); set_path(&c.prefix, runtime); set_path(&c.exec_prefix, runtime);
  const std::string executable = "shinki-emu"; check(PyConfig_SetBytesString(&c, &c.program_name, executable.c_str())); check(PyConfig_SetBytesString(&c, &c.executable, executable.c_str()));
  const fs::path paths[] = {runtime, runtime / "lib" / "python3.12" / "site-packages", runtime / "lib" / "python3.12", runtime / "lib" / "python312.zip", root};
  for (const auto &path : paths) { const std::string value = path.u8string(); wchar_t *wide = Py_DecodeLocale(value.c_str(), nullptr); if (!wide) throw Failure(ShinkiEmuCORE_ERROR, "RENPY_PYTHON_INITIALIZATION", "Cannot encode runtime path"); check(PyWideStringList_Append(&c.module_search_paths, wide)); PyMem_RawFree(wide); }
  const auto initialized = Py_InitializeFromConfig(&c); owns_python = Py_IsInitialized() != 0; check(initialized);
  PyObject *platform = PyUnicode_FromString("portable"); PyObject *os = PyImport_ImportModule("os"); PyObject *environment = os ? PyObject_GetAttrString(os, "environ") : nullptr;
  const int set = environment && platform ? PyMapping_SetItemString(environment, "RENPY_PLATFORM", platform) : -1; Py_XDECREF(environment); Py_XDECREF(os); Py_XDECREF(platform);
  if (set < 0) throw Failure(ShinkiEmuCORE_ERROR, "RENPY_PYTHON_INITIALIZATION", python_error());
}

struct Service {
  std::mutex mutex;
  std::condition_variable changed;
  bool ready = false;
  ShinkiEmuStatus initialization_status = ShinkiEmuOK;
  ShinkiEmuError initialization_error{};
  bool occupied = false;
  std::shared_ptr<Session> queued;
  void work() noexcept {
    bool owns_python = false;
    try {
      initialize_python(owns_python);
    } catch (const Failure &e) {
      initialization_status =
          fail(&initialization_error, e.status, e.code, e.what());
    } catch (const std::exception &e) {
      initialization_status = fail(&initialization_error, ShinkiEmuCORE_ERROR,
                                   "RENPY_OWNER_INITIALIZATION", e.what());
    } catch (...) {
      initialization_status =
          fail(&initialization_error, ShinkiEmuPANICKED, "RENPY_OWNER_EXCEPTION",
               "Unknown interpreter initialization exception");
    }
    PyThreadState *python = nullptr;
    if (owns_python)
      python = PyEval_SaveThread();
    {
      std::lock_guard<std::mutex> lock(mutex);
      ready = true;
      changed.notify_all();
    }
    if (initialization_status != ShinkiEmuOK)
      return;
    for (;;) {
      std::shared_ptr<Session> task;
      {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&] { return static_cast<bool>(queued); });
        task = std::move(queued);
      }
      Session *s = task.get();
      PyEval_RestoreThread(python);
      current = s;
      ShinkiEmuError error{};
      bool failed = false;
      try {
        if (!s->cancelled()) {
          PyObject *module = PyImport_ImportModule("shinki_renpy_bridge");
          PyObject *entry =
              module ? PyObject_GetAttrString(module, "run_session") : nullptr;
          PyObject *game =
              entry ? PyUnicode_DecodeUTF8(s->game_root.data(),
                                           s->game_root.size(), "strict")
                    : nullptr;
          PyObject *result =
              entry && game ? PyObject_CallFunctionObjArgs(entry, game, nullptr)
                            : nullptr;
          Py_XDECREF(game);
          Py_XDECREF(entry);
          Py_XDECREF(module);
          if (!result) {
            const auto message = python_error();
            fail(&error, ShinkiEmuCORE_ERROR, "RENPY_PYTHON_EXCEPTION",
                 message.c_str());
            log(ShinkiEmuLOG_ERROR, "session.python_exception", message);
            failed = true;
          }
          Py_XDECREF(result);
        }
      } catch (const std::exception &e) {
        fail(&error, ShinkiEmuCORE_ERROR, "RENPY_SESSION_EXCEPTION", e.what());
        failed = true;
        PyErr_Clear();
      } catch (...) {
        fail(&error, ShinkiEmuPANICKED, "RENPY_SESSION_EXCEPTION",
             "Unknown session exception");
        failed = true;
        PyErr_Clear();
      }
      current = nullptr;
      python = PyEval_SaveThread();
      // close/open failure keeps the session alive until this acknowledgement.
      task.reset();
      {
        std::lock_guard<std::mutex> lock(s->mutex);
        if (s->pending) {
          s->pending->writing = false;
          s->pending = nullptr;
        }
        s->inputs.clear();
        s->failed = failed;
        s->failure = error;
        s->done = true;
        s->signal();
      }
    }
  }
};
struct Admission {
  std::shared_ptr<Service> service;
  bool retained = false;
  ~Admission() {
    if (!retained) {
      std::lock_guard<std::mutex> lock(service->mutex);
      service->occupied = false;
    }
  }
};
std::mutex resident_mutex;
// Intentionally process-resident: the SDK's Cython modules cannot be
// reinitialized. This object and its thread own no session or Host callback
// while idle.
std::shared_ptr<Service> &resident_storage() {
  static auto *value = new std::shared_ptr<Service>;
  return *value;
}
std::shared_ptr<Service> interpreter_service() {
  std::lock_guard<std::mutex> lock(resident_mutex);
  auto &resident = resident_storage();
  if (!resident) {
#ifdef _WIN32
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&package_root), &pinned))
      throw Failure(ShinkiEmuCORE_ERROR, "RENPY_MODULE_PIN", "Cannot retain the process-resident interpreter module");
#endif
    auto service = std::make_shared<Service>();
    std::thread worker([service] { service->work(); });
    worker.detach();
    resident = std::move(service);
  }
  std::unique_lock<std::mutex> service_lock(resident->mutex);
  resident->changed.wait(service_lock, [&] { return resident->ready; });
  if (resident->initialization_status != ShinkiEmuOK) {
    const auto &e = resident->initialization_error;
    throw Failure(
        resident->initialization_status, "RENPY_OWNER_INITIALIZATION",
        std::string(reinterpret_cast<const char *>(e.message), e.message_len));
  }
  return resident;
}
void cancel_session(Session &s) noexcept {
  if (!s.cancel.exchange(true, std::memory_order_acq_rel)) {
    if (s.host.window.invalidate)
      s.host.window.invalidate(s.host.window.context, s.epoch, s.context);
    if (s.host.text.invalidate)
      s.host.text.invalidate(s.host.text.context, s.epoch, s.context);
  }
  s.changed.notify_all();
}
void finish_session(Session &s) {
  cancel_session(s);
  std::unique_lock<std::mutex> lock(s.mutex);
  s.changed.wait(lock, [&] { return s.done; });
}

ShinkiEmuStatus initialize(const ShinkiEmuDiagnostics *sink,
                          ShinkiEmuError *error) noexcept {
  return boundary(error, [&] {
    if (!sink || !header(sink->header, sizeof(*sink)) || !sink->emit)
      invalid("Invalid diagnostic service header or callback");
    std::lock_guard<std::mutex> lock(diagnostics_mutex);
    diagnostics = *sink;
    return ShinkiEmuOK;
  });
}
ShinkiEmuStatus describe(ShinkiEmuDescriptor *out,
                        ShinkiEmuError *error) noexcept {
  if (out)
    *out = {};
  return boundary(error, [&] {
    if (!out)
      invalid("Null descriptor output");
    static const ShinkiEmuBytes formats[] = {bytes("rpy"), bytes("rpyc"),
                                            bytes("rpa")};
    *out = {{sizeof(*out), ShinkiEmuABI_VERSION},
            bytes("renpy"),
            bytes("renpy"),
            bytes("0.1.0"),
            ShinkiEmuCAP_CPU_SURFACE,
            formats,
            3,
            nullptr,
            0,
            bytes("Ren'Py"),
            bytes("Ren'Py"),
            {}};
    return ShinkiEmuOK;
  });
}
ShinkiEmuStatus probe(ShinkiEmuBytes path, ShinkiEmuProbeResult *out,
                     ShinkiEmuError *error) noexcept {
  if (out)
    *out = {};
  return boundary(error, [&] {
    if (!out)
      invalid("Null probe output");
    const fs::path root = canonical_directory(text(path, 131072, false));
    const auto detection = detect(root);
    if (!detection.matched)
      return fail(error, ShinkiEmuNO_MATCH, "RENPY_NO_MATCH",
                  "No Ren'Py script or archive signature found");
    static std::string id;
    id = "renpy-" + game_hash(root);
    *out = {bytes(id), bytes(detection.format), 10000, 0};
    return ShinkiEmuOK;
  });
}
bool valid_window(const ShinkiEmuWindowState &w) noexcept {
  return w.focused <= 1 && w.suspended <= 1 && std::isfinite(w.scale_factor) &&
         w.scale_factor > 0 &&
         (w.suspended || (w.drawable_width && w.drawable_height));
}
ShinkiEmuStatus open(const ShinkiEmuOpenRequest *request, ShinkiEmuOpenResult *out,
                    ShinkiEmuError *error) noexcept {
  if (out)
    *out = {};
  return boundary(error, [&]() -> ShinkiEmuStatus {
    if (!request || !out || !header(request->header, sizeof(*request)) ||
        !header(request->host.header, sizeof(request->host)))
      invalid("Invalid open or host-service header");
    if (!request->session_epoch || !request->host.cancellation.is_cancelled ||
        !valid_window(request->window))
      invalid(
          "Invalid session epoch, cancellation service or Host window state");
    if (request->render_extent.width || request->render_extent.height)
      invalid("Ren'Py does not accept a render extent override");
    const auto owner = owner_handle(request->module_owner);
    const auto root =
        canonical_directory(text(request->game_path, 131072, false));
    if (!detect(root).matched)
      throw Failure(ShinkiEmuNO_MATCH, "RENPY_NO_MATCH",
                    "No Ren'Py script or archive signature found");
    auto s = std::make_shared<Session>();
    s->owner = owner.get();
    s->epoch = request->session_epoch;
    s->context = next_context.fetch_add(1, std::memory_order_relaxed);
    s->host = request->host;
    s->window = request->window;
    s->update_pause();
    s->game_root = root.u8string();
    if (s->cancelled())
      return fail(error, ShinkiEmuCANCELLED, "RENPY_OPEN_CANCELLED", "Open was cancelled");
    auto service = owner->service;
    {
      std::lock_guard<std::mutex> lock(service->mutex);
      if (service->occupied)
        throw Failure(ShinkiEmuINVALID_STATE, "RENPY_SESSION_BUSY",
                      "Only one Ren'Py session may be open in this process");
      service->occupied = true;
    }
    Admission admission{service};
    {
      std::lock_guard<std::mutex> lock(service->mutex);
      service->queued = s;
      service->changed.notify_all();
    }
    // Every exceptional path after queueing must await Python's cleanup.
    try {
      std::unique_lock<std::mutex> lock(s->mutex);
      while (!s->configured && !s->done && !s->cancelled())
        s->changed.wait_for(lock, std::chrono::milliseconds(25));
      if (s->cancelled()) {
        lock.unlock();
        finish_session(*s);
        return fail(error, ShinkiEmuCANCELLED, "RENPY_OPEN_CANCELLED",
                    "Open was cancelled");
      }
      if (s->done) {
        if (s->failed) {
          if (error)
            *error = s->failure;
          return ShinkiEmuCORE_ERROR;
        }
        return fail(error, ShinkiEmuCORE_ERROR, "RENPY_NO_SURFACE",
                    "Ren'Py exited before configuring a renderer surface");
      }
      const ShinkiEmuOpenResult result{s.get(), s->context, s->output,
                                      s->surface};
      lock.unlock();
      {
        std::lock_guard<std::mutex> handles(handles_mutex);
        sessions.emplace(s.get(), s);
      }
      if (s->cancelled()) {
        {
          std::lock_guard<std::mutex> handles(handles_mutex);
          sessions.erase(s.get());
        }
        finish_session(*s);
        return fail(error, ShinkiEmuCANCELLED, "RENPY_OPEN_CANCELLED",
                    "Open was cancelled");
      }
      admission.retained = true;
      *out = result;
      return ShinkiEmuOK;
    } catch (...) {
      finish_session(*s);
      throw;
    }
  });
}
ShinkiEmuStatus input(ShinkiEmuSession value, const ShinkiEmuInputEvent *events,
                     size_t count, ShinkiEmuError *error) noexcept {
  return boundary(error, [&]() -> ShinkiEmuStatus {
    auto s = session_handle(value);
    if (count > 65536 || (count && !events))
      invalid("Invalid input event span");
    std::lock_guard<std::mutex> lock(s->mutex);
    if (s->done)
      return fail(error, ShinkiEmuINVALID_STATE, "RENPY_SESSION_ENDED",
                  "Session has ended");
    if (s->cancelled())
      return fail(error, ShinkiEmuCANCELLED, "RENPY_SESSION_CANCELLED",
                  "Session is cancelled");
    if (s->inputs.size() + count > 65536)
      return fail(error, ShinkiEmuCAPACITY, "RENPY_INPUT_CAPACITY",
                  "Input queue is full");
    const size_t original = s->inputs.size();
    struct Rollback {
      std::vector<QueuedInput> &queue;
      size_t size;
      bool committed = false;
      ~Rollback() {
        if (!committed)
          queue.resize(size);
      }
    } rollback{s->inputs, original};
    if (s->inputs.capacity() < original + count)
      s->inputs.reserve(
          (std::min)(static_cast<size_t>(65536),
                     (std::max)(original + count, s->inputs.capacity() * 2)));
    for (size_t i = 0; i < count; ++i) {
      const auto &event = events[i];
      if (event.kind < ShinkiEmuINPUT_KEY || event.kind > ShinkiEmuINPUT_WINDOW ||
          event.pressed > 1 || event.repeat > 1 ||
          (event.repeat && !event.pressed) || !std::isfinite(event.x) ||
          !std::isfinite(event.y))
        invalid("Invalid input event");
      QueuedInput item;
      item.event = event;
      item.physical = text(event.physical_key, ShinkiEmuMAX_CONFIG_TEXT);
      item.logical = text(event.logical_key, ShinkiEmuMAX_CONFIG_TEXT);
      for (size_t j = 0; j < item.logical.size(); ++j) {
        const auto ch = static_cast<unsigned char>(item.logical[j]);
        if (ch < 32 || ch == 127 ||
            (ch == 0xc2 && j + 1 < item.logical.size() &&
             static_cast<unsigned char>(item.logical[j + 1]) >= 0x80 &&
             static_cast<unsigned char>(item.logical[j + 1]) <= 0x9f))
          invalid("Logical keys must not contain control characters");
      }
      item.event.physical_key = {};
      item.event.logical_key = {};
      if (event.kind == ShinkiEmuINPUT_WINDOW) {
        if (!valid_window(event.window))
          invalid("Invalid Host window state");
      } else {
        if (event.displayed.session_epoch != s->epoch ||
            event.displayed.output_epoch != s->output ||
            !event.displayed.generation ||
            event.displayed.generation > s->generation)
          return fail(error, ShinkiEmuSTALE_CONTEXT, "RENPY_STALE_INPUT",
                      "Input refers to a stale or unpublished frame");
        if (event.kind == ShinkiEmuINPUT_POINTER_BUTTON &&
            (event.button < ShinkiEmuBUTTON_PRIMARY ||
             event.button > ShinkiEmuBUTTON_MIDDLE))
          invalid("Unknown pointer button");
        if (event.kind == ShinkiEmuINPUT_WHEEL &&
            (std::abs(event.x) > 1000000 || std::abs(event.y) > 1000000))
          invalid("Wheel detents exceed SDL range");
        if (event.kind == ShinkiEmuINPUT_KEY && item.physical.empty() &&
            item.logical.empty())
          invalid("Key input contains no key or text");
      }
      s->inputs.push_back(std::move(item));
    }
    // Commit Host state only after validating the complete batch.
    for (size_t i = original; i < s->inputs.size(); ++i) {
      if (s->inputs[i].event.kind == ShinkiEmuINPUT_WINDOW) {
        s->window = s->inputs[i].event.window;
        s->update_pause();
      }
    }
    rollback.committed = true;
    s->signal();
    return ShinkiEmuOK;
  });
}
ShinkiEmuStatus control(ShinkiEmuSession value, uint32_t command,
                       ShinkiEmuError *error) noexcept {
  return boundary(error, [&]() -> ShinkiEmuStatus {
    auto s = session_handle(value);
    std::lock_guard<std::mutex> lock(s->mutex);
    if (s->done)
      return fail(error, ShinkiEmuINVALID_STATE, "RENPY_SESSION_ENDED",
                  "Session has ended");
    if (s->cancelled())
      return fail(error, ShinkiEmuCANCELLED, "RENPY_SESSION_CANCELLED",
                  "Session is cancelled");
    if (command != ShinkiEmuCONTROL_PAUSE && command != ShinkiEmuCONTROL_RESUME)
      return fail(error, ShinkiEmuUNSUPPORTED, "RENPY_CONTROL_UNSUPPORTED",
                  "Ren'Py supports pause and resume controls only");
    s->user_paused = command == ShinkiEmuCONTROL_PAUSE;
    s->update_pause();
    s->signal();
    return ShinkiEmuOK;
  });
}
ShinkiEmuStatus state(ShinkiEmuSession value, ShinkiEmuSessionState *out,
                     ShinkiEmuError *error) noexcept {
  if (out)
    *out = {};
  return boundary(error, [&] {
    if (!out)
      invalid("Null session-state output");
    auto s = session_handle(value);
    std::lock_guard<std::mutex> lock(s->mutex);
    out->context_epoch = s->context;
    out->output_epoch = s->output;
    out->state = s->failed            ? ShinkiEmuSESSION_FAILED
                 : s->done            ? ShinkiEmuSESSION_FINISHED
                 : s->effective_pause ? ShinkiEmuSESSION_PAUSED
                                      : ShinkiEmuSESSION_RUNNING;
    if (s->failed)
      out->error = s->failure;
    return ShinkiEmuOK;
  });
}
ShinkiEmuStatus frame(ShinkiEmuSession value, ShinkiEmuFrameIdentity since,
                     ShinkiEmuFrameSurface *out, ShinkiEmuError *error) noexcept {
  if (out)
    *out = {};
  return boundary(error, [&]() -> ShinkiEmuStatus {
    if (!out)
      invalid("Null frame output");
    auto s = session_handle(value);
    std::lock_guard<std::mutex> lock(s->mutex);
    if ((since.session_epoch && since.session_epoch != s->epoch) ||
        since.output_epoch > s->output || since.generation > s->generation)
      return fail(error, ShinkiEmuSTALE_CONTEXT, "RENPY_STALE_FRAME",
                  "Frame identity does not belong to this session");
    if (s->failed) {
      if (error)
        *error = s->failure;
      return ShinkiEmuCORE_ERROR;
    }
    ShinkiEmuFrameSurface result{};
    result.info = s->surface;
    result.identity = {s->epoch, s->output, s->generation};
    Slot *slot = s->latest;
    if (!slot)
      result.state = ShinkiEmuFRAME_PENDING;
    else {
      result.identity = slot->identity;
      result.info = slot->info;
      result.timestamp_ns = slot->timestamp;
      if (since.session_epoch == slot->identity.session_epoch &&
          since.output_epoch == slot->identity.output_epoch &&
          since.generation == slot->identity.generation)
        result.state = ShinkiEmuFRAME_UNCHANGED;
      else {
        slot->references.fetch_add(1, std::memory_order_relaxed);
        result.state = ShinkiEmuFRAME_READY;
        result.cpu = {{slot->pixels.get(), slot->size}, slot, release_pixels};
      }
    }
    *out = result;
    return ShinkiEmuOK;
  });
}
void cancel(ShinkiEmuSession value) noexcept {
  // Host retains the handle until all concurrent cancellation calls return.
  if (value)
    cancel_session(*static_cast<Session *>(value));
}
ShinkiEmuStatus close(ShinkiEmuSession value, ShinkiEmuError *error) noexcept {
  return boundary(error, [&]() -> ShinkiEmuStatus {
    auto s = session_handle(value);
    {
      std::lock_guard<std::mutex> lock(handles_mutex);
      sessions.erase(value);
    }
    finish_session(*s);
    {
      std::lock_guard<std::mutex> lock(s->owner->service->mutex);
      s->owner->service->occupied = false;
    }
    // No Python/SDL/Host work is reachable from outstanding CPU leases.
    if (s->failed) {
      if (error)
        *error = s->failure;
      return ShinkiEmuCORE_ERROR;
    }
    return ShinkiEmuOK;
  });
}
ShinkiEmuStatus create_owner(ShinkiEmuModuleOwner *out,
                            ShinkiEmuError *error) noexcept {
  if (out)
    *out = nullptr;
  return boundary(error, [&] {
    if (!out)
      invalid("Null module-owner output");
    auto owner = std::make_shared<Owner>();
    owner->service = interpreter_service();
    std::lock_guard<std::mutex> lock(handles_mutex);
    owners.emplace(owner.get(), owner);
    *out = owner.get();
    return ShinkiEmuOK;
  });
}
ShinkiEmuStatus destroy_owner(ShinkiEmuModuleOwner value,
                             ShinkiEmuError *error) noexcept {
  return boundary(error, [&]() -> ShinkiEmuStatus {
    auto owner = owner_handle(value);
    std::shared_ptr<Session> retained;
    {
      std::lock_guard<std::mutex> lock(handles_mutex);
      for (const auto &entry : sessions) {
        if (entry.second->owner == owner.get()) {
          retained = entry.second;
          break;
        }
      }
      owners.erase(value);
      if (retained)
        sessions.erase(retained.get());
    }
    // ABI callers close first; consume defensively even if they violate that
    // order.
    if (retained) {
      finish_session(*retained);
      std::lock_guard<std::mutex> lock(owner->service->mutex);
      owner->service->occupied = false;
    }
    return ShinkiEmuOK;
  });
}
const ShinkiEmuModuleOwnerApi owner_api{
    {sizeof(ShinkiEmuModuleOwnerApi), ShinkiEmuMODULE_OWNER_VERSION},
    create_owner,
    destroy_owner};
ShinkiEmuStatus extension(ShinkiEmuBytes name, uint32_t version,
                         const void **out) noexcept {
  if (!out)
    return ShinkiEmuINVALID_ARGUMENT;
  *out = nullptr;
  constexpr char id[] = "shinki.module_owner";
  if (!name.data || name.len != sizeof(id) - 1 ||
      std::memcmp(name.data, id, sizeof(id) - 1) != 0 ||
      version != ShinkiEmuMODULE_OWNER_VERSION)
    return ShinkiEmuUNSUPPORTED;
  *out = &owner_api;
  return ShinkiEmuOK;
}
const ShinkiEmuCoreApi api{{sizeof(ShinkiEmuCoreApi), ShinkiEmuABI_VERSION},
                          initialize,
                          describe,
                          probe,
                          open,
                          input,
                          control,
                          state,
                          frame,
                          cancel,
                          close,
                          extension};
} // namespace

extern "C" __declspec(dllexport) ShinkiEmuStatus
shinki_emu_query(uint32_t version, const ShinkiEmuCoreApi **out) noexcept {
  if (!out)
    return ShinkiEmuINVALID_ARGUMENT;
  *out = nullptr;
  if (version != ShinkiEmuABI_VERSION)
    return ShinkiEmuUNSUPPORTED;
  *out = &api;
  return ShinkiEmuOK;
}
