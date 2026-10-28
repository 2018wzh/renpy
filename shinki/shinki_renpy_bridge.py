"""ShinkiEMU platform boundary for the unmodified Ren'Py game/renderer ABI."""

import functools
import importlib.abc
import importlib.util
import os
from shinki_save_migration import migrate_legacy_saves
import sys
import threading
import types
import warnings

import _shinki

_RUNTIME = os.path.dirname(os.path.abspath(__file__))
_active = False
_closing = False
_owner = None
_game_root = None
_audio_paused = False
_surface_ready = False
_pause_seconds = 0.0
_patches = []
_module_baseline = None
_warned = set()
_sdk_callables = None
_sdk_classes = []


class _DisplayFinder(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname == "renpy.pygame.display":
            filename = os.path.join(_RUNTIME, "overrides", "display.pyd")
            return importlib.util.spec_from_file_location(fullname, filename)
        return None


_finder = _DisplayFinder()
# Install before importing even renpy.pygame: event cimports this display's C API.
if "renpy.pygame.display" in sys.modules:
    raise RuntimeError("The embedded display override must precede pygame imports.")
sys.meta_path.insert(0, _finder)


def session_active():
    return _active


def unsupported(operation):
    if operation not in _warned:
        _warned.add(operation)
        warnings.warn("ShinkiEMU Host does not support " + operation, RuntimeWarning, stacklevel=2)
    return False


def window(kind, flags=0, text="", width=0, height=0, x=0, y=0):
    result = _shinki.window(kind, flags, text, width, height, x, y)
    if result[0] == 5:  # Native Host cancellation is not an unsupported request.
        _check_cancelled()
    elif result[0] != 0:
        unsupported("window request {} (status {})".format(kind, result[0]))
    return result


def initial_size():
    import renpy
    # A fullscreen request must not create a zero-sized desktop SDL window.
    return renpy.config.screen_width, renpy.config.screen_height


def _check_cancelled():
    if _active and not _closing and _shinki.cancelled():
        import renpy.game
        raise renpy.game.QuitException()


def _gate():
    global _audio_paused, _pause_seconds
    if not _active or _closing:
        return
    import renpy.audio.audio
    _check_cancelled()
    # Interface polls events before constructing the renderer. A suspended open
    # must reach configure_surface before waiting for the Host to resume it.
    if not _surface_ready:
        if _shinki.paused():
            renpy.audio.audio.pause_all()
            _audio_paused = True
        return
    if _shinki.paused():
        renpy.audio.audio.pause_all()
        _audio_paused = True
        while _shinki.paused():
            _check_cancelled()
            _pause_seconds = _shinki.paused_seconds()
            _shinki.wait(10)
    if _audio_paused:
        renpy.audio.audio.unpause_all()
        _audio_paused = False
    _pause_seconds = _shinki.paused_seconds()
    _check_cancelled()


def _pump():
    _gate()
    if not _active or _closing:
        return
    import renpy.display
    draw = renpy.display.draw
    if draw is None:
        return
    _shinki.pump_events(draw.draw_per_phys, draw.draw_per_phys)
    _check_cancelled()


def _geometry():
    import renpy.display
    draw = renpy.display.draw
    return (*draw.drawable_size, *draw.virtual_size)


def publish_frame():
    """Read the final GL2 back buffer directly into the native leased pool."""
    _gate()
    from renpy.uguu import uguu as gl
    geometry = _geometry()
    target = _shinki.begin_frame(*geometry)
    if target is None:
        return
    try:
        gl.glReadPixels(0, 0, geometry[0], geometry[1], gl.GL_RGBA, gl.GL_UNSIGNED_BYTE, target)
        _shinki.end_frame()
    except BaseException:
        _shinki.abort_frame()
        raise
    finally:
        target.release()


def _replace(module, name, replacement):
    original = getattr(module, name)
    if getattr(original, "_shinki_hook", False):
        return
    replacement._shinki_hook = True
    _patches.append((module, name, original, replacement))
    setattr(module, name, replacement)


def _event_hook(original):
    @functools.wraps(original)
    def call(*args, **kwargs):
        _pump()
        return original(*args, **kwargs)
    return call


def _wait_event():
    import renpy.pygame as pygame
    # Never enter SDL_WaitEvent: native close/control wakes the bounded CV wait.
    while True:
        _pump()
        event = _event_poll()
        if event.type != pygame.NOEVENT:
            return event
        _shinki.wait(5)


_event_poll = None


def _set_mouse_pos(position):
    import renpy.display
    draw = renpy.display.draw
    if draw is None:
        return False
    # SDK GL2Draw.set_mouse_pos has already converted virtual -> physical.
    x, y = draw.translate_point(*position)
    return window(4, 1, x=int(x), y=int(y))[0] == 0


def _set_mouse_visible(visible):
    return window(4, 2 | (4 if visible else 0))[0] == 0


def _backing_size():
    import renpy.pygame as pygame
    surface = pygame.display.get_surface()
    if surface is None:
        return None
    width, height = surface.get_size()
    return max(width, 256), max(height, 256)


def _set_grab(enabled):
    return unsupported("pointer capture") if enabled else False


def _blocked_editor(*args, **kwargs):
    return unsupported("external editor")


def _reset_logs():
    module = sys.modules.get("renpy.log")
    if module is None:
        return
    for logfile in module.log_cache.values():
        handle = logfile.file
        if handle and handle not in (sys.stdout, sys.stderr, module.real_stdout):
            handle.close()
        logfile.file = None
    module.log_file = None


def restore_sdk_callables():
    """Restore binding identities before the SDK unpickles module snapshots."""
    _restore_hooks()
    for module, name, original in _sdk_callables or ():
        setattr(module, name, original)
    for cls, names, methods in _sdk_classes:
        for name in set(vars(cls)) - names:
            delattr(cls, name)
        for name, original in methods.items():
            if vars(cls).get(name) is not original:
                setattr(cls, name, original)


def install_hooks():
    """Called after import_all's backup and after every official reload_all."""
    global _event_poll, _module_baseline, _surface_ready, _sdk_callables
    if not _active or _closing:
        return
    import renpy
    import renpy.pygame as pygame
    from renpy.display import core
    if _sdk_callables is None:
        modules = {module for module in sys.modules.values()
                   if isinstance(module, types.ModuleType)
                   and module.__name__.startswith("renpy")}
        _sdk_callables = [(module, name, value) for module in modules
                          for name, value in vars(module).items() if callable(value)]
        classes = {value for _, _, value in _sdk_callables
                   if isinstance(value, type) and value.__module__.startswith("renpy")}
        for cls in classes:
            namespace = vars(cls)
            methods = {name: value for name, value in namespace.items()
                       if callable(value) or isinstance(value, (classmethod, staticmethod, property))}
            _sdk_classes.append((cls, set(namespace), methods))

    # Public window queries describe Host; GL2 geometry describes its own SDL
    # backing surface. Keeping those separate avoids inventing Host window state.
    _replace(core, "get_size", _backing_size)

    renpy.exports.__main__ = renpy.__main__
    renpy.loader.loadable_cache.clear()
    pygame.display.hint("renpy.pygame_shinki_EMBEDDED", "1")
    if renpy.display.draw is None:
        _surface_ready = False
    if not renpy.display.im.cache.keep_preloading:
        renpy.display.im.reset_module()

    for name in ("pump", "poll", "get", "peek", "clear"):
        original = getattr(pygame.event, name)
        if name == "poll" and not getattr(original, "_shinki_hook", False):
            _event_poll = original
        _replace(pygame.event, name, _event_hook(original))
    _replace(pygame.event, "wait", _wait_event)
    _replace(pygame.event, "set_grab", _set_grab)
    _replace(pygame.event, "get_grab", lambda: False)
    _replace(pygame.key, "get_focused", lambda: _shinki.focused())
    _replace(pygame.mouse, "get_focused", lambda: _shinki.focused())
    _replace(pygame.mouse, "set_pos", _set_mouse_pos)
    _replace(pygame.mouse, "set_visible", _set_mouse_visible)
    _replace(renpy.editor, "launch_editor", _blocked_editor)
    _replace(renpy.exports, "launch_editor", _blocked_editor)

    original_time = core.get_time
    if not getattr(original_time, "_shinki_hook", False):
        def get_time():
            global _pause_seconds
            if threading.get_ident() == _owner and not _closing:
                _pause_seconds = _shinki.paused_seconds()
            return original_time() - _pause_seconds * core.time_mult
        _replace(core, "get_time", get_time)

    original_mode = core.Interface.set_mode
    if not getattr(original_mode, "_shinki_hook", False):
        def set_mode(interface):
            global _surface_ready
            result = original_mode(interface)
            if renpy.session.get("renderer") not in ("gl2", "angle2", "gles2"):
                raise RuntimeError("ShinkiEMU requires the SDK GL2 renderer.")
            _shinki.configure_surface(*_geometry())
            _surface_ready = True
            return result
        _replace(core.Interface, "set_mode", set_mode)

    # Gate before SDK render/update, as well as at event and publication boundaries.
    original_draw = core.Interface.draw_screen
    if not getattr(original_draw, "_shinki_hook", False):
        def draw_screen(interface, *args, **kwargs):
            _pump()
            return original_draw(interface, *args, **kwargs)
        _replace(core.Interface, "draw_screen", draw_screen)

    if _module_baseline is None:
        _module_baseline = set(sys.modules)


def _restore_hooks():
    for module, name, original, replacement in reversed(_patches):
        if getattr(module, name, None) is replacement:
            setattr(module, name, original)
    _patches.clear()


def _cleanup():
    """Finish SDK workers before restoring their backed-up module state."""
    import renpy
    errors = []

    def finish(action):
        try:
            action()
        except BaseException as error:
            errors.append(error)

    interface = getattr(getattr(renpy, "display", None), "interface", None)
    if interface is not None:
        finish(interface.finish_pending)
    loadsave = sys.modules.get("renpy.loadsave")
    if loadsave is not None and loadsave.autosave_thread is not None:
        finish(loadsave.autosave_thread.join)
    for name, action in (("renpy.loader", "auto_quit"), ("renpy.savelocation", "quit")):
        module = sys.modules.get(name)
        if module is not None:
            finish(getattr(module, action))
    renpy.session.clear()
    if renpy.backup.names:
        finish(renpy.reload_all)
        # reload_all starts a fresh preload worker for an UtterRestart. A closed
        # session must not retain even that otherwise-idle worker.
        finish(renpy.display.im.cache.quit)
        finish(renpy.importer.quit_importer)
        renpy.display.info = None
        renpy.loader.loadable_cache.clear()
        renpy.python.old_py_compile_cache.clear()
    else:
        for name, action in (("renpy.audio.audio", "quit"), ("renpy.display.im", None)):
            module = sys.modules.get(name)
            if module is not None:
                finish(module.cache.quit if action is None else getattr(module, action))
    pygame = sys.modules.get("renpy.pygame")
    if pygame is not None:
        finish(pygame.mouse.quit)
        for action in reversed(pygame.quit_functions):
            finish(action)
        finish(pygame.event.clear)
        pygame.display.default_icon = None
        pygame.display.default_title = "pygame window"
    finish(_reset_logs)
    if errors:
        raise BaseExceptionGroup("Ren'Py session cleanup failed", errors)


def migrate_saves():
    import renpy

    destinations = [renpy.config.savedir]
    if not renpy.mobile and not renpy.macapp:
        destinations.append(os.path.join(renpy.config.gamedir, "saves"))
    try:
        migrate_legacy_saves(
            _game_root,
            destinations,
            renpy.__main__.path_to_saves(renpy.config.gamedir, "tokens"),
            renpy.__main__.path_to_saves(renpy.config.gamedir, "backups"),
        )
    except OSError as error:
        warnings.warn("Could not migrate beta Ren'Py saves: " + str(error), RuntimeWarning)


def run_session(game_root):
    global _active, _closing, _game_root, _owner, _audio_paused, _pause_seconds, _module_baseline, _surface_ready
    if _active:
        raise RuntimeError("A Ren'Py session is already active.")
    game_root = os.path.abspath(game_root)

    old_path = list(sys.path)
    old_meta_path = list(sys.meta_path)
    old_path_hooks = list(sys.path_hooks)
    old_importer_cache = dict(sys.path_importer_cache)
    old_argv = sys.argv
    old_cwd = os.getcwd()
    old_excepthook = sys.excepthook
    old_stdout, old_stderr = sys.stdout, sys.stderr
    old_env = dict(os.environ)
    initial_modules = set(sys.modules)
    _game_root = game_root
    _owner = threading.get_ident()
    _active = True
    _closing = False
    _audio_paused = False
    _surface_ready = False
    _pause_seconds = 0.0
    _module_baseline = None
    _warned.clear()
    os.environ["renpy.pygame_shinki_EMBEDDED"] = "1"
    os.environ["RENPY_HIGHDPI"] = "1"
    os.environ["RENPY_NO_REDIRECT_STDIO"] = "1"
    os.environ["RENPY_RENDERER"] = "gl2"
    os.environ["RENPY_DISABLE_JOYSTICK"] = "1"
    sys.argv = ["renpy", game_root, "run"]
    os.chdir(game_root)
    try:
        import renpy
        renpy.session.clear()
        _reset_logs()
        # Load the SDK distributor entry, not any game's renamed launcher script.
        spec = importlib.util.spec_from_file_location("_shinki_renpy_entry", os.path.join(_RUNTIME, "renpy.py"))
        entry = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(entry)
        renpy.__main__ = entry
        import renpy.bootstrap
        try:
            renpy.bootstrap.bootstrap(_RUNTIME)
        except SystemExit as error:
            if error.code not in (None, 0):
                raise RuntimeError("Ren'Py exited with status {}.".format(error.code)) from error
    finally:
        _closing = True
        try:
            _restore_hooks()
            if "renpy" in sys.modules:
                _cleanup()
        finally:
            renpy_module = sys.modules.get("renpy")
            if renpy_module is not None:
                renpy_module.__main__ = None
            exports = sys.modules.get("renpy.exports")
            if exports is not None:
                exports.__main__ = None
            baseline = _module_baseline or initial_modules
            for name in list(sys.modules):
                if name in baseline:
                    continue
                module = sys.modules[name]
                filename = getattr(module, "__file__", None)
                spec = getattr(module, "__spec__", None)
                if spec is not None and spec.origin in ("built-in", "frozen"):
                    continue
                if filename:
                    path = os.path.normcase(os.path.abspath(filename))
                    runtime = os.path.normcase(_RUNTIME) + os.sep
                    if path.startswith(runtime):
                        continue
                del sys.modules[name]
                parent_name, _, child_name = name.rpartition(".")
                parent = sys.modules.get(parent_name)
                if parent is not None and getattr(parent, child_name, None) is module:
                    delattr(parent, child_name)
            sys.path[:] = old_path
            sys.meta_path[:] = old_meta_path
            sys.path_hooks[:] = old_path_hooks
            sys.path_importer_cache.clear()
            sys.path_importer_cache.update(old_importer_cache)
            sys.argv = old_argv
            sys.excepthook = old_excepthook
            sys.stdout, sys.stderr = old_stdout, old_stderr
            os.chdir(old_cwd)
            os.environ.clear()
            os.environ.update(old_env)
            _active = False
            _closing = False
            _owner = None
            _game_root = None
            _module_baseline = None
