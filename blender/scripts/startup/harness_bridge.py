# SPDX-FileCopyrightText: 2026 Harness Studio contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Harness Studio: Blender side of the Qt shell bridge.

The shell (C++/QML) queues requests in the built-in `_harness` module. An application timer
takes them on Blender's main thread and runs them with a real window context, so every
operator, dialog and undo step behaves exactly as in Blender. Blender's state is pushed back
on every tick when it changed.

This module does nothing when Blender runs without the Qt shell (no `_harness` module).
"""

import json
import os
import sys
import io
import time
import traceback

import bpy

try:
    import _harness
except ImportError:  # Native GHOST back-end or background mode.
    _harness = None

SIDE_WORKSPACE = "Harness Side"
TICK = 0.03
_last_state = None
_theme_done = False
_DEBUG = bool(os.environ.get("HARNESS_DEBUG"))


def _log(*args):
    if _DEBUG:
        print("harness:", *args, file=sys.stderr, flush=True)


# --------------------------------------------------------------------------------------
# Windows. The shell shows one Blender window (everything left of Harness Studio). Older
# versions docked a second "Harness Side" window; such a workspace is hidden and its window
# closed.

def _main_window():
    wm = bpy.context.window_manager
    for win in wm.windows:
        if win.workspace and win.workspace.name != SIDE_WORKSPACE and win.parent is None:
            return win
    return wm.windows[0] if wm.windows else None


def _largest_area(screen, kind):
    best = None
    for area in screen.areas:
        if area.type == kind and (best is None or area.width * area.height > best.width * best.height):
            best = area
    return best


def _close_area(win, screen, area):
    try:
        with bpy.context.temp_override(window=win, screen=screen, area=area):
            return 'FINISHED' in bpy.ops.screen.area_close()
    except Exception:
        return False


def _close_legacy_side_windows():
    wm = bpy.context.window_manager
    for win in list(wm.windows):
        if win.workspace and win.workspace.name == SIDE_WORKSPACE and len(wm.windows) > 1:
            try:
                with bpy.context.temp_override(window=win):
                    bpy.ops.wm.window_close()
            except Exception:
                traceback.print_exc()


# --------------------------------------------------------------------------------------
# Panel layouts.
#
# Every workspace's screen is a tiling of areas, like a tiling window manager: areas never
# overlap, their borders are dragged to resize neighbours, and areas swap places (View > Area >
# Swap Areas, or Ctrl-drag from an area corner). A layout is kept as a split tree:
#   {"split": "h", "ratio": r, "a": left,  "b": right}   r = share of the left part
#   {"split": "v", "ratio": r, "a": top,   "b": bottom}  r = share of the top part
#   {"ui": "<Area.ui_type>"}                               a leaf
# The tree of each workspace is read back from the screen whenever the person changes it and
# stored in ~/.config/HarnessStudio/layouts.json, so it comes back after switching tabs and
# after restarting. Factory layouts show one 3D viewport at most.

def _leaf(ui):
    return {"ui": ui}


def _h(ratio, a, b):
    return {"split": "h", "ratio": ratio, "a": a, "b": b}


def _v(ratio, a, b):
    return {"split": "v", "ratio": ratio, "a": a, "b": b}


def _side(top=0.38):
    return _v(top, _leaf('OUTLINER'), _leaf('PROPERTIES'))


# The work area of each workspace (right of the outliner/properties column).
FACTORY_WORK = {
    "Layout": _v(0.85, _leaf('VIEW_3D'), _leaf('TIMELINE')),
    "Modeling": _leaf('VIEW_3D'),
    "Sculpting": _leaf('VIEW_3D'),
    "UV Editing": _h(0.5, _leaf('UV'), _leaf('VIEW_3D')),
    "Texture Paint": _h(0.42, _leaf('IMAGE_EDITOR'), _leaf('VIEW_3D')),
    "Shading": _v(0.6, _leaf('VIEW_3D'), _leaf('ShaderNodeTree')),
    "Animation": _v(0.66, _leaf('VIEW_3D'), _leaf('DOPESHEET')),
    "Rendering": _leaf('IMAGE_EDITOR'),
    "Compositing": _v(0.8, _leaf('CompositorNodeTree'), _leaf('TIMELINE')),
    "Geometry Nodes": _v(0.54, _h(0.34, _leaf('SPREADSHEET'), _leaf('VIEW_3D')), _leaf('GeometryNodeTree')),
    "Scripting": _h(0.46, _v(0.62, _leaf('VIEW_3D'), _h(0.5, _leaf('CONSOLE'), _leaf('INFO'))),
                    _leaf('TEXT_EDITOR')),
}
FACTORY_SIDE_SHARE = {"UV Editing": 0.22, "Texture Paint": 0.22, "Geometry Nodes": 0.22,
                      "Scripting": 0.22}

# Tab order of Blender's own startup file.
WORKSPACE_ORDER = ["Layout", "Modeling", "Sculpting", "UV Editing", "Texture Paint", "Shading",
                   "Animation", "Rendering", "Compositing", "Geometry Nodes", "Scripting"]

BUILTIN_TEMPLATES = [
    ("default", "Default"),
    ("panels_right", "Panels on the right"),
    ("with_timeline", "With timeline"),
    ("editor_only", "Editor only"),
]

_store = {"version": 1, "workspaces": {}, "templates": {}}
_applied = {}            # workspace name -> tree on screen (applied or last read back)
_settle_until = 0.0
_last_read = 0.0


def _store_path():
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return os.path.join(base, "HarnessStudio", "layouts.json")


def _load_store():
    global _store
    try:
        with open(_store_path(), encoding="utf-8") as fh:
            data = json.load(fh)
        if isinstance(data, dict) and data.get("version") == 1:
            data.setdefault("workspaces", {})
            data.setdefault("templates", {})
            _store = data
    except (OSError, ValueError):
        pass


def _save_store():
    path = _store_path()
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tmp = path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(_store, fh, indent=1, sort_keys=True)
        os.replace(tmp, path)
    except OSError:
        traceback.print_exc()


def _work(name):
    return FACTORY_WORK.get(name)


def _factory(name):
    work = _work(name)
    if work is None:
        return None
    return _h(FACTORY_SIDE_SHARE.get(name, 0.25), _side(), work)


def _template_tree(key, name):
    """A built-in template for workspace `name`, or a saved one by its name."""
    if key in _store["templates"]:
        return _store["templates"][key]
    work = _work(name)
    if work is None:
        # Unknown workspaces: their current work area is the largest editor on screen.
        cur = _applied.get(name)
        work = _leaf(_main_ui(cur)) if cur else _leaf('VIEW_3D')
    if key == "default":
        return _factory(name) or _h(0.25, _side(), work)
    if key == "panels_right":
        return _h(0.74, work, _side())
    if key == "with_timeline":
        if 'TIMELINE' in _leaf_uis(work) or 'DOPESHEET' in _leaf_uis(work):
            return _h(0.25, _side(), work)
        return _h(0.25, _side(), _v(0.84, work, _leaf('TIMELINE')))
    if key == "editor_only":
        return work
    return None


def _leaf_uis(node):
    if node is None:
        return []
    if "ui" in node:
        return [node["ui"]]
    return _leaf_uis(node["a"]) + _leaf_uis(node["b"])


def _main_ui(node):
    uis = _leaf_uis(node)
    for ui in ('VIEW_3D', 'UV', 'IMAGE_EDITOR', 'ShaderNodeTree', 'CompositorNodeTree',
               'GeometryNodeTree', 'TEXT_EDITOR', 'SEQUENCE_EDITOR'):
        if ui in uis:
            return ui
    return uis[0] if uis else 'VIEW_3D'


def _set_ui(area, ui):
    try:
        area.ui_type = ui
    except Exception:
        fallback = {'TIMELINE': 'DOPESHEET', 'UV': 'IMAGE_EDITOR'}.get(ui)
        if fallback:
            try:
                area.ui_type = fallback
            except Exception:
                pass


def _split(win, screen, area, node):
    """Split `area` for `node` and return (area for node["a"], area for node["b"]), or None."""
    ratio = min(0.92, max(0.08, float(node.get("ratio", 0.5))))
    if node["split"] == "h":
        direction, fac = 'VERTICAL', ratio
        new_is_b = fac > 0.5          # Blender puts the new area right when fac > 0.5
    else:
        direction, fac = 'HORIZONTAL', 1.0 - ratio
        new_is_b = not (fac > 0.5)    # ... and on top when fac > 0.5
    before = {a.as_pointer() for a in screen.areas}
    try:
        with bpy.context.temp_override(window=win, screen=screen, area=area):
            bpy.ops.screen.area_split(direction=direction, factor=fac)
    except Exception:
        traceback.print_exc()
    new = [a for a in screen.areas if a.as_pointer() not in before]
    if not new:
        return None
    return (area, new[0]) if new_is_b else (new[0], area)


# Areas made by a split get their size on Blender's next screen refresh, and Blender only
# splits areas that have one, so a tree is built one level per tick: (area pointer, node).
_pending = []
_pending_tries = 0
_pending_ws = ""


def _build_step(win):
    """Process the pending splits whose areas have been sized. True when the tree is done."""
    global _pending, _pending_tries
    screen = win.screen
    by_ptr = {a.as_pointer(): a for a in screen.areas}
    keep = []
    for ptr, node in _pending:
        area = by_ptr.get(ptr)
        if area is None:
            continue
        if area.width <= 1 or area.height <= 1:
            keep.append((ptr, node))
            continue
        if "ui" in node:
            _set_ui(area, node["ui"])
            continue
        parts = _split(win, screen, area, node)
        if parts is None:
            _set_ui(area, _main_ui(node))   # Too small to split: show the part that matters.
            continue
        keep.append((parts[0].as_pointer(), node["a"]))
        keep.append((parts[1].as_pointer(), node["b"]))
    _pending = keep
    _pending_tries += 1
    if _pending_tries > 200:   # Never wait forever on an area that doesn't get a size.
        _pending = []
    if not _pending:
        _log("layout built", win.workspace.name, [a.ui_type for a in screen.areas])
    return not _pending


def _apply_tree(win, tree):
    """Rebuild the window's screen as `tree`. The largest area of the tree's main editor is
    kept (so its view, shading and settings carry over) and every other area is closed; the
    splits follow on the next ticks."""
    global _pending, _pending_tries, _pending_ws
    screen = win.screen
    main = _main_ui(tree)
    keep = None
    for a in screen.areas:
        if a.ui_type == main and (keep is None or a.width * a.height > keep.width * keep.height):
            keep = a
    if keep is None:
        keep = max(screen.areas, key=lambda a: a.width * a.height)
    keep_ptr = keep.as_pointer()
    for _ in range(64):
        others = [a for a in screen.areas if a.as_pointer() != keep_ptr]
        if not others:
            break
        # Close the smallest first: they always have a neighbour to merge into.
        others.sort(key=lambda a: a.width * a.height)
        if not any(_close_area(win, screen, a) for a in others):
            break
    areas = list(screen.areas)
    if len(areas) != 1:
        _log("layout: could not reduce", win.workspace.name, len(areas))
        return False
    _pending = [(areas[0].as_pointer(), tree)]
    _pending_tries = 0
    _pending_ws = win.workspace.name
    _build_step(win)
    return True


def _read_tree(screen):
    rects = [(a.x, a.y, a.x + a.width, a.y + a.height, a.ui_type) for a in screen.areas]
    if not rects:
        return None
    return _decompose(rects)


def _decompose(rects, tol=4):
    if len(rects) == 1:
        return _leaf(rects[0][4])
    x0 = min(r[0] for r in rects)
    y0 = min(r[1] for r in rects)
    x1 = max(r[2] for r in rects)
    y1 = max(r[3] for r in rects)
    for cx in sorted({r[2] for r in rects}):
        if cx >= x1 - tol:
            continue
        left = [r for r in rects if r[2] <= cx + tol]
        right = [r for r in rects if r[2] > cx + tol]
        if left and right and all(r[0] >= cx - tol for r in right):
            a, b = _decompose(left, tol), _decompose(right, tol)
            if a is None or b is None:
                return None
            return _h(round((cx - x0) / max(1, x1 - x0), 4), a, b)
    for cy in sorted({r[3] for r in rects}):
        if cy >= y1 - tol:
            continue
        bottom = [r for r in rects if r[3] <= cy + tol]
        top = [r for r in rects if r[3] > cy + tol]
        if bottom and top and all(r[1] >= cy - tol for r in top):
            a, b = _decompose(top, tol), _decompose(bottom, tol)
            if a is None or b is None:
                return None
            return _v(round((y1 - cy) / max(1, y1 - y0), 4), a, b)
    return None   # Not a split tree (Blender allows a few such layouts); keep the last one.


def _same(t1, t2, tol=0.015):
    if t1 is None or t2 is None:
        return t1 is t2
    if ("ui" in t1) != ("ui" in t2):
        return False
    if "ui" in t1:
        return t1["ui"] == t2["ui"] or {t1["ui"], t2["ui"]} <= {'TIMELINE', 'DOPESHEET'}
    return (t1["split"] == t2["split"] and abs(t1["ratio"] - t2["ratio"]) < tol and
            _same(t1["a"], t2["a"], tol) and _same(t1["b"], t2["b"], tol))


def _busy(win):
    try:
        return len(win.modal_operators) > 0
    except AttributeError:
        return False


_cycle_at = 0.0


def _debug_cycle():
    """HARNESS_CYCLE=1: show every workspace in turn (checks the layouts)."""
    global _cycle_at
    now = time.monotonic()
    if _pending or now - _cycle_at < 1.5:
        return
    _cycle_at = now
    win = _main_window()
    names = _ordered_workspaces()
    if win is None or not names:
        return
    i = names.index(win.workspace.name) if win.workspace.name in names else -1
    if i + 1 < len(names):
        win.workspace = bpy.data.workspaces[names[i + 1]]


_adopt_file = False       # the open project brought its own screens: keep them as they are
_reopen_done = False
_config = {"reopen_last": True, "received": False}
_started_at = time.monotonic()
_prebuild = None          # workspaces still to lay out at start, or None before it began
_prebuild_home = None
_prebuild_modes = {}
_layouts_ready = False


def _prebuild_step(win):
    """At start, lay out every workspace once (while the shell still shows its loading cover),
    so switching tabs later only swaps screens that are already built: no closing and splitting
    of areas, no flicker. Workspaces are visited with their object mode held at the current
    one, so the scene isn't put into Sculpt or Edit mode on the way."""
    global _prebuild, _prebuild_home, _layouts_ready
    if _prebuild is None:
        _prebuild_home = win.workspace.name
        _prebuild = [n for n in _ordered_workspaces() if n != _prebuild_home and
                     (n in _store["workspaces"] or _factory(n) is not None)]
        mode = bpy.context.mode if bpy.context.mode in {'OBJECT'} else None
        for n in _prebuild:
            ws = bpy.data.workspaces.get(n)
            if ws is not None and mode is not None and hasattr(ws, "object_mode"):
                _prebuild_modes[n] = ws.object_mode
                try:
                    ws.object_mode = 'OBJECT'
                except Exception:
                    pass
    if _pending:
        return False
    name = win.workspace.name
    if name not in _applied:
        return False                    # _maintain_layout builds it on this tick
    if _prebuild:
        nxt = _prebuild.pop(0)
        ws = bpy.data.workspaces.get(nxt)
        if ws is not None:
            win.workspace = ws
        return False
    if name != _prebuild_home:
        ws = bpy.data.workspaces.get(_prebuild_home)
        if ws is not None:
            win.workspace = ws
            return False
    for n, mode in _prebuild_modes.items():
        ws = bpy.data.workspaces.get(n)
        if ws is not None:
            try:
                ws.object_mode = mode
            except Exception:
                pass
    _prebuild_modes.clear()
    _layouts_ready = True
    _log("layouts ready")
    return True


def _reopen_last():
    """Carry on where the person left off: at start, open the most recent project (with its
    tabs, views and editors as they were saved) unless a file was given or they turned it off."""
    global _reopen_done
    if not _config["received"] and time.monotonic() - _started_at < 4.0:
        return                      # wait briefly for the shell's settings
    _reopen_done = True
    if (os.environ.get("HARNESS_SCREENSHOT") or bpy.data.filepath or not _config.get("reopen_last", True)
            or "--factory-startup" in sys.argv or bpy.data.is_dirty):
        return
    recent = [p for p in _recent_files() if os.path.isfile(p)]
    if not recent:
        return
    _log("reopening", recent[0])
    try:
        with bpy.context.temp_override(**_window_context()):
            bpy.ops.wm.open_mainfile(filepath=recent[0], load_ui=True)
    except Exception:
        traceback.print_exc()


def _maintain_layout():
    global _settle_until, _last_read, _pending
    win = _main_window()
    if win is None or win.screen is None or win.workspace is None:
        return
    if win.screen.show_fullscreen or win.screen.name.endswith("-nonnormal"):
        return   # A maximized area (Ctrl+Space) is temporary; leave it alone.
    name = win.workspace.name
    now = time.monotonic()
    if _pending and _pending_ws != name:
        # Switched away mid-build: build that workspace again when it's shown next.
        _pending = []
        _applied.pop(_pending_ws, None)
    if _pending:
        if _build_step(win):
            _settle_until = now + 0.6
        return
    if name not in _applied and _adopt_file:
        # The project's own layout for this tab: remember it, change nothing.
        _applied[name] = _read_tree(win.screen)
        _settle_until = now + 0.3
        return
    if name not in _applied:
        tree = _store["workspaces"].get(name) or _factory(name)
        if tree is not None:
            _apply_tree(win, tree)
            _applied[name] = tree
        else:
            _applied[name] = _read_tree(win.screen)
        _settle_until = now + 0.6
        return
    if now < _settle_until or now - _last_read < 0.4 or _busy(win):
        return
    _last_read = now
    cur = _read_tree(win.screen)
    if cur is None or _same(cur, _applied[name]):
        return
    _applied[name] = cur
    _store["workspaces"][name] = cur
    _save_store()
    _log("layout saved", name)


def _layout_request(p):
    action = p.get("action", "")
    name_arg = (p.get("name") or "").strip()
    win = _main_window()
    if win is None:
        return {"error": "No window"}
    ws = win.workspace.name
    if action == "apply_template":
        tree = _template_tree(name_arg, ws)
        if tree is None:
            return {"error": "Unknown template %r" % name_arg}
        _apply_tree(win, tree)
        _applied[ws] = tree
        _store["workspaces"][ws] = tree
        _save_store()
    elif action == "save_template":
        tree = _read_tree(win.screen) or _applied.get(ws)
        if not name_arg or tree is None:
            return {"error": "Nothing to save"}
        _store["templates"][name_arg] = tree
        _save_store()
    elif action == "delete_template":
        _store["templates"].pop(name_arg, None)
        _save_store()
    elif action == "reset":
        _store["workspaces"].pop(ws, None)
        _save_store()
        tree = _factory(ws)
        if tree is not None:
            _apply_tree(win, tree)
            _applied[ws] = tree
    elif action == "reset_all":
        _store["workspaces"] = {}
        _save_store()
        _applied.clear()
    else:
        return {"error": "Unknown layout action %r" % action}
    global _last_state
    _last_state = None
    return {"ok": True}


def _layout_templates():
    out = [{"id": k, "name": t, "builtin": True} for k, t in BUILTIN_TEMPLATES]
    out += [{"id": k, "name": k, "builtin": False} for k in sorted(_store["templates"])]
    return out


# --------------------------------------------------------------------------------------
# Menus: Blender's own menu classes, recorded into a tree the shell draws natively.

class _Props:
    def __init__(self):
        object.__setattr__(self, "_values", {})

    def __setattr__(self, key, value):
        try:
            json.dumps(value)
        except TypeError:
            value = str(value)
        self._values[key] = value

    def __getattr__(self, key):
        return self._values.get(key)


class _Recorder:
    """Stands in for `UILayout` while a menu's draw() runs; records what it would show."""

    def __init__(self, items, enabled=True):
        self.items = items
        self.operator_context = 'INVOKE_REGION_WIN'
        self.enabled = enabled
        self.active = True
        self.alert = False
        self.scale_x = self.scale_y = 1.0
        self.use_property_split = False
        self.use_property_decorate = False
        self.emboss = 'NORMAL'
        self.alignment = 'EXPAND'
        self.active_default = False
        self.activate_init = False
        self.ui_units_x = 0
        self.ui_units_y = 0

    def _sub(self):
        return _Recorder(self.items, self.enabled)

    def row(self, *args, **kwargs): return self._sub()
    def column(self, *args, **kwargs): return self._sub()
    def column_flow(self, *args, **kwargs): return self._sub()
    def grid_flow(self, *args, **kwargs): return self._sub()
    def split(self, *args, **kwargs): return self._sub()
    def box(self, *args, **kwargs): return self._sub()
    def menu_pie(self, *args, **kwargs): return self._sub()

    def separator(self, *args, **kwargs):
        if self.items and self.items[-1].get("type") != "separator":
            self.items.append({"type": "separator"})

    def separator_spacer(self, *args, **kwargs):
        pass

    def label(self, text="", icon='NONE', **kwargs):
        if text:
            self.items.append({"type": "label", "text": text, "icon": icon})

    def operator(self, operator, text=None, icon='NONE', **kwargs):
        props = _Props()
        label = text
        if label is None:
            try:
                mod, name = operator.split(".", 1)
                label = getattr(getattr(bpy.ops, mod), name).get_rna_type().name
            except Exception:
                label = operator
        enabled = bool(self.enabled and _operator_poll(operator))
        self.items.append({
            "type": "operator", "op": operator, "text": label, "icon": icon,
            "context": self.operator_context, "enabled": enabled,
            "props": props._values,
        })
        return props

    def operator_menu_enum(self, operator, property, text=None, icon='NONE', **kwargs):
        self.items.append({"type": "operator", "op": operator, "text": text or property,
                           "icon": icon, "context": 'INVOKE_DEFAULT', "enabled": True, "props": {}})
        return _Props()

    def operator_enum(self, operator, property, **kwargs):
        pass

    def menu(self, menu, text=None, icon='NONE', **kwargs):
        cls = getattr(bpy.types, menu, None)
        label = text if text is not None else (getattr(cls, "bl_label", menu) if cls else menu)
        self.items.append({"type": "menu", "menu": menu, "text": label, "icon": icon,
                           "enabled": bool(self.enabled)})

    def menu_contents(self, menu):
        self.items.extend(record_menu(menu))

    def prop(self, data, property, text=None, icon='NONE', **kwargs):
        try:
            rna = data.bl_rna.properties[property]
        except Exception:
            return
        if rna.type == 'BOOLEAN':
            path = _data_path(data)
            if path:
                self.items.append({"type": "toggle", "text": text or rna.name, "icon": icon,
                                   "path": path, "prop": property,
                                   "value": bool(getattr(data, property))})

    def props_enum(self, *args, **kwargs): pass
    def prop_menu_enum(self, *args, **kwargs): pass
    def prop_enum(self, *args, **kwargs): pass
    def popover(self, *args, **kwargs): pass
    def context_pointer_set(self, *args, **kwargs): pass
    def context_string_set(self, *args, **kwargs): pass

    def __getattr__(self, name):
        # template_*() and anything else UILayout offers: draw nothing.
        def _noop(*args, **kwargs):
            return self._sub()
        return _noop


def _data_path(data):
    ctx = bpy.context
    for name in ("preferences", "scene", "space_data", "tool_settings", "window_manager"):
        try:
            if getattr(ctx, name) == data:
                return "bpy.context." + name
        except Exception:
            pass
    try:
        if data == ctx.preferences.view:
            return "bpy.context.preferences.view"
    except Exception:
        pass
    return None


def record_menu(menu_name):
    cls = getattr(bpy.types, menu_name, None)
    items = []
    if cls is None:
        return items
    layout = _Recorder(items)

    class _Self:
        pass

    me = _Self()
    me.layout = layout
    me.bl_idname = menu_name
    try:
        # Menu recording used to draw against whatever context happened to be current in
        # Blender's Python timer.  After opening a .blend this could still be the startup
        # context, which made File/Edit look like the empty initial scene.  Always record
        # against the live main window so blend_data, workspace and operator polls agree with
        # the state sent to QML.
        with bpy.context.temp_override(**_window_context()):
            cls.draw(me, bpy.context)
    except Exception:
        traceback.print_exc()
    for fn in getattr(cls, "_dyn_ui_initialize", lambda: [])() if hasattr(cls, "_dyn_ui_initialize") else []:
        try:
            fn(me, bpy.context)
        except Exception:
            pass
    while items and items[-1].get("type") == "separator":
        items.pop()
    if menu_name == "TOPBAR_MT_file":
        # Blender has no shell-level "close project" command.  Keep it in the same live menu
        # snapshot as Save/Revert so it cannot accidentally target the startup state.
        items.extend([
            {"type": "separator"},
            {"type": "operator", "op": "wm.read_homefile", "text": "Close Project",
             "icon": "CANCEL", "context": "INVOKE_DEFAULT", "enabled": True,
             "props": {"use_empty": True, "app_template": ""}},
        ])
    return items


def _operator_poll(op):
    """Return the live Blender poll result without letting a menu query fail the bridge."""
    try:
        mod, name = op.split(".", 1)
        fn = getattr(getattr(bpy.ops, mod), name)
        with bpy.context.temp_override(**_window_context()):
            return bool(fn.poll())
    except Exception:
        return False


def _menu_tree(menu_name, depth):
    items = record_menu(menu_name)
    if depth > 1:
        for item in items:
            if item.get("type") == "menu":
                item["children"] = _menu_tree(item["menu"], depth - 1)
    return items


# --------------------------------------------------------------------------------------
# Requests

def _window_context(prefer_area=None):
    win = _main_window()
    if win is None:
        return {}
    ctx = {"window": win, "screen": win.screen}
    area = None
    if prefer_area:
        area = _largest_area(win.screen, prefer_area)
    if area is None:
        area = _largest_area(win.screen, 'VIEW_3D')
    if area is not None:
        ctx["area"] = area
        region = next((r for r in area.regions if r.type == 'WINDOW'), None)
        if region is not None:
            ctx["region"] = region
    return ctx


def _call_operator(op, props, context):
    mod, name = op.split(".", 1)
    fn = getattr(getattr(bpy.ops, mod), name)
    with bpy.context.temp_override(**_window_context()):
        return list(fn(context, **(props or {})))


def _scene_summary():
    scene = bpy.context.scene
    objects = []
    for ob in scene.objects:
        item = {
            "name": ob.name, "type": ob.type,
            "location": [round(v, 4) for v in ob.location],
            "rotation_euler_deg": [round(v * 57.29577951308232, 3) for v in ob.rotation_euler],
            "scale": [round(v, 4) for v in ob.scale],
            "dimensions": [round(v, 4) for v in ob.dimensions],
            "visible": ob.visible_get(), "selected": ob.select_get(),
            "parent": ob.parent.name if ob.parent else None,
            "collection": ob.users_collection[0].name if ob.users_collection else None,
            "materials": [s.material.name for s in ob.material_slots if s.material],
            "modifiers": [m.type for m in ob.modifiers],
        }
        if ob.type == 'MESH':
            item["vertices"] = len(ob.data.vertices)
            item["faces"] = len(ob.data.polygons)
        objects.append(item)
    active = bpy.context.view_layer.objects.active
    return {
        "scene": scene.name, "frame": scene.frame_current,
        "frame_range": [scene.frame_start, scene.frame_end], "fps": scene.render.fps,
        "render_engine": scene.render.engine,
        "resolution": [scene.render.resolution_x, scene.render.resolution_y,
                       scene.render.resolution_percentage],
        "camera": scene.camera.name if scene.camera else None,
        "active_object": active.name if active else None,
        "mode": bpy.context.mode, "unit_system": scene.unit_settings.system,
        "objects": objects,
        "collections": [c.name for c in bpy.data.collections],
        "materials": [m.name for m in bpy.data.materials],
        "world": scene.world.name if scene.world else None,
    }


def _object_info(name):
    ob = bpy.data.objects.get(name)
    if ob is None:
        return {"error": "No object named %r" % name}
    info = {
        "name": ob.name, "type": ob.type, "data": ob.data.name if ob.data else None,
        "location": list(ob.location), "rotation_mode": ob.rotation_mode,
        "rotation_euler_deg": [v * 57.29577951308232 for v in ob.rotation_euler],
        "scale": list(ob.scale), "dimensions": list(ob.dimensions),
        "parent": ob.parent.name if ob.parent else None,
        "children": [c.name for c in ob.children],
        "modifiers": [{"name": m.name, "type": m.type} for m in ob.modifiers],
        "constraints": [{"name": c.name, "type": c.type} for c in ob.constraints],
        "materials": [s.material.name if s.material else None for s in ob.material_slots],
        "animation": bool(ob.animation_data and ob.animation_data.action),
        "custom_properties": {k: str(ob[k]) for k in ob.keys()},
    }
    if ob.type == 'MESH':
        me = ob.data
        info.update({"vertices": len(me.vertices), "edges": len(me.edges),
                     "faces": len(me.polygons), "uv_layers": [u.name for u in me.uv_layers]})
    elif ob.type == 'LIGHT':
        info.update({"light_type": ob.data.type, "energy": ob.data.energy,
                     "color": list(ob.data.color)})
    elif ob.type == 'CAMERA':
        info.update({"lens": ob.data.lens, "sensor_width": ob.data.sensor_width,
                     "clip": [ob.data.clip_start, ob.data.clip_end]})
    return info


# Helpers for the agent's code (Blender 5 moved F-curves into layered actions).

def _fcurves(target):
    """F-curves of an object's (or any ID's) assigned action, [] when it has none."""
    ad = getattr(target, "animation_data", None)
    if ad is None or ad.action is None:
        return []
    act = ad.action
    if hasattr(act, "fcurves"):           # Blender 4.x
        return list(act.fcurves)
    from bpy_extras import anim_utils
    bag = anim_utils.action_get_channelbag_for_slot(act, ad.action_slot)
    return list(bag.fcurves) if bag else []


def _set_interpolation(target, kind='LINEAR'):
    n = 0
    for fc in _fcurves(target):
        for kp in fc.keyframe_points:
            kp.interpolation = kind
            n += 1
        fc.update()
    return n


def _install_fcurves_compat():
    """Blender 5 keeps F-curves in layered actions (slots, channel bags), but nearly all bpy
    code in the wild, and what language models write, uses action.fcurves. Give Action (and
    keyframe strips) a read-only fcurves that lists them, so that code works unchanged."""
    def action_fcurves(self):
        from bpy_extras import anim_utils
        out = []
        for slot in self.slots:
            bag = anim_utils.action_get_channelbag_for_slot(self, slot)
            if bag:
                out.extend(bag.fcurves)
        return out

    def strip_fcurves(self):
        out = []
        for bag in getattr(self, "channelbags", []):
            out.extend(bag.fcurves)
        return out
    try:
        if not hasattr(bpy.types.Action, "fcurves"):
            bpy.types.Action.fcurves = property(action_fcurves)
        strip = getattr(bpy.types, "ActionKeyframeStrip", None)
        if strip is not None and not hasattr(strip, "fcurves"):
            strip.fcurves = property(strip_fcurves)
    except Exception:
        traceback.print_exc()


def _set_engine(name):
    items = {e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items}
    want = {"EEVEE": ("BLENDER_EEVEE_NEXT", "BLENDER_EEVEE"), "CYCLES": ("CYCLES",),
            "WORKBENCH": ("BLENDER_WORKBENCH",)}.get(name.upper(), (name,))
    for e in want:
        if e in items:
            bpy.context.scene.render.engine = e
            return e
    raise ValueError("Unknown render engine %r" % name)


def _exec_python(code, undo_name):
    """Run the agent's code as one undo step. A call that fails is rolled back, so a half-done
    change (two of three lights made, say) never stays behind for the next attempt."""
    out = io.StringIO()
    import math
    import mathutils
    namespace = {"bpy": bpy, "C": bpy.context, "D": bpy.data, "result": None, "math": math,
                 "mathutils": mathutils, "Vector": mathutils.Vector, "Matrix": mathutils.Matrix,
                 "Euler": mathutils.Euler, "Quaternion": mathutils.Quaternion,
                 "fcurves": _fcurves, "set_interpolation": _set_interpolation, "set_engine": _set_engine}
    old = sys.stdout
    sys.stdout = out
    error = None
    rolled_back = False
    with bpy.context.temp_override(**_window_context()):
        try:
            bpy.ops.ed.undo_push(message="Before: " + (undo_name or "Harness Agent"))
        except Exception:
            pass
        try:
            exec(compile(code, "<harness-agent>", "exec"), namespace)
            try:
                bpy.ops.ed.undo_push(message=undo_name or "Harness Agent")
            except Exception:
                pass
        except Exception as ex:
            # Only the frames of the agent's own code, then the error itself.
            tb = traceback.extract_tb(ex.__traceback__)
            frames = [f for f in tb if f.filename == "<harness-agent>"]
            lines = ["line %d: %s" % (f.lineno, (code.splitlines()[f.lineno - 1].strip()
                                                if 0 < f.lineno <= len(code.splitlines()) else ""))
                     for f in frames[-3:]]
            error = "\n".join(lines + ["".join(traceback.format_exception_only(type(ex), ex)).strip()])
            try:
                # Record the half-done state, then step back to the state before the call.
                bpy.ops.ed.undo_push(message="Failed: " + (undo_name or "Harness Agent"))
                bpy.ops.ed.undo()
                rolled_back = True
            except Exception:
                pass
        finally:
            sys.stdout = old
    result = namespace.get("result")
    try:
        json.dumps(result)
    except TypeError:
        result = repr(result)
    res = {"stdout": out.getvalue()[-12000:], "result": result, "error": error}
    if error:
        res["rolled_back"] = rolled_back
        res["note"] = ("Nothing from this call was kept; fix the error and run the whole step again."
                       if rolled_back else "Part of this call may have run before the error.")
    return res


def _capture_viewport(path, width):
    """Draw the 3D viewport (with overlays off) into an image file."""
    ctx = _window_context('VIEW_3D')
    area = ctx.get("area")
    if area is None or area.type != 'VIEW_3D':
        return {"error": "No 3D viewport is open"}
    space = area.spaces.active
    overlays = space.overlay.show_overlays
    try:
        space.overlay.show_overlays = False
        with bpy.context.temp_override(**ctx):
            bpy.ops.screen.screenshot_area(filepath=path, check_existing=False)
    except Exception:
        return {"error": traceback.format_exc(limit=3)}
    finally:
        space.overlay.show_overlays = overlays
    return {"path": path}


def _render_still(path):
    scene = bpy.context.scene
    old = scene.render.filepath
    try:
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
    finally:
        scene.render.filepath = old
    return {"path": path}


def _import_file(path, kind, place=None, target=None):
    res = _import_file_raw(path, kind)
    if place and place != "none" and res.get("imported"):
        res["placement"] = _place_objects(res["imported"], place, target)
    return res


def _import_file_raw(path, kind):
    ext = os.path.splitext(path)[1].lower()
    with bpy.context.temp_override(**_window_context()):
        before = set(bpy.data.objects)
        if ext in {".glb", ".gltf"}:
            bpy.ops.import_scene.gltf(filepath=path)
        elif ext == ".fbx":
            bpy.ops.import_scene.fbx(filepath=path)
        elif ext == ".obj":
            bpy.ops.wm.obj_import(filepath=path)
        elif ext in {".usd", ".usda", ".usdc", ".usdz"}:
            bpy.ops.wm.usd_import(filepath=path)
        elif ext == ".stl":
            bpy.ops.wm.stl_import(filepath=path)
        elif ext == ".ply":
            bpy.ops.wm.ply_import(filepath=path)
        elif ext == ".abc":
            bpy.ops.wm.alembic_import(filepath=path)
        elif ext == ".bvh":
            bpy.ops.import_anim.bvh(filepath=path)
        elif ext in {".png", ".jpg", ".jpeg", ".webp", ".exr", ".hdr", ".mp4", ".mov", ".webm"}:
            if kind == "world":
                world = bpy.context.scene.world or bpy.data.worlds.new("World")
                bpy.context.scene.world = world
                world.use_nodes = True
                nt = world.node_tree
                env = nt.nodes.new("ShaderNodeTexEnvironment")
                env.image = bpy.data.images.load(path, check_existing=True)
                bg = nt.nodes.get("Background") or nt.nodes.new("ShaderNodeBackground")
                nt.links.new(env.outputs["Color"], bg.inputs["Color"])
            elif kind == "texture":
                ob = bpy.context.view_layer.objects.active
                if ob is None or ob.type != 'MESH':
                    return {"error": "Select a mesh to texture"}
                mat = bpy.data.materials.new(os.path.basename(path))
                ob.data.materials.clear()
                ob.data.materials.append(mat)
                nt = mat.node_tree
                tex = nt.nodes.new("ShaderNodeTexImage")
                tex.image = bpy.data.images.load(path, check_existing=True)
                bsdf = nt.nodes.get("Principled BSDF")
                nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
            else:
                bpy.ops.image.import_as_mesh_planes(files=[{"name": os.path.basename(path)}],
                                                    directory=os.path.dirname(path))
        else:
            return {"error": "Unsupported file type %s" % ext}
        added = [o.name for o in bpy.data.objects if o not in before]
        bpy.ops.ed.undo_push(message="Import " + os.path.basename(path))
    return {"imported": added}


# --------------------------------------------------------------------------------------
# Scene link: what Harness Studio's @ references, reference passes, camera paths,
# materials, placement, lighting and region edits need from the scene.

_ICON_FOR_TYPE = {
    'MESH': 'MESH_DATA', 'CURVE': 'CURVE_DATA', 'SURFACE': 'SURFACE_DATA', 'META': 'META_DATA',
    'FONT': 'FONT_DATA', 'CURVES': 'CURVES_DATA', 'POINTCLOUD': 'POINTCLOUD_DATA',
    'VOLUME': 'VOLUME_DATA', 'GREASEPENCIL': 'OUTLINER_DATA_GREASEPENCIL', 'ARMATURE': 'ARMATURE_DATA',
    'LATTICE': 'LATTICE_DATA', 'EMPTY': 'EMPTY_DATA', 'LIGHT': 'LIGHT', 'CAMERA': 'CAMERA_DATA',
    'SPEAKER': 'SPEAKER', 'LIGHT_PROBE': 'LIGHTPROBE_SPHERE',
}


def _scene_items(limit=600):
    """Everything an @ reference can point at: objects, cameras, lights, materials, collections."""
    scene = bpy.context.scene
    items = []
    active = bpy.context.view_layer.objects.active
    for ob in scene.objects:
        kind = "camera" if ob.type == 'CAMERA' else "light" if ob.type == 'LIGHT' else "object"
        detail = ob.type.title()
        if ob.type == 'MESH':
            d = ob.dimensions
            detail = "Mesh · %.2g × %.2g × %.2g m" % (d.x, d.y, d.z)
        elif ob.type == 'LIGHT':
            detail = "%s light · %.0f W" % (ob.data.type.title(), ob.data.energy)
        elif ob.type == 'CAMERA':
            detail = "Camera · %.0f mm%s" % (ob.data.lens, " · active" if scene.camera == ob else "")
        items.append({"kind": kind, "name": ob.name, "detail": detail,
                      "icon": _ICON_FOR_TYPE.get(ob.type, 'OBJECT_DATA'),
                      "selected": ob.select_get(), "active": ob == active})
    for mat in bpy.data.materials:
        if mat.users == 0 or mat.is_grease_pencil:
            continue
        items.append({"kind": "material", "name": mat.name, "detail": "Material · %d users" % mat.users,
                      "icon": 'MATERIAL'})
    for col in bpy.data.collections:
        items.append({"kind": "collection", "name": col.name,
                      "detail": "Collection · %d objects" % len(col.all_objects), "icon": 'OUTLINER_COLLECTION'})
    # Selected and active first, then by kind and name.
    order = {"object": 0, "camera": 1, "light": 2, "material": 3, "collection": 4}
    items.sort(key=lambda i: (not i.get("active"), not i.get("selected"), order[i["kind"]], i["name"].lower()))
    return {"items": items[:limit], "total": len(items)}


def _material_info(mat):
    info = {"name": mat.name, "users": [o.name for o in bpy.data.objects
                                        if any(s.material == mat for s in o.material_slots)]}
    if mat.use_nodes and mat.node_tree:
        bsdf = next((n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)
        if bsdf:
            def val(name):
                s = bsdf.inputs.get(name)
                if s is None:
                    return None
                if s.is_linked:
                    return "linked:" + s.links[0].from_node.type
                v = s.default_value
                return [round(x, 3) for x in v] if hasattr(v, "__len__") else round(v, 3)
            info["principled"] = {k: val(k) for k in ("Base Color", "Metallic", "Roughness",
                                                      "IOR", "Alpha", "Emission Color")}
        info["textures"] = [n.image.filepath or n.image.name for n in mat.node_tree.nodes
                            if n.type == 'TEX_IMAGE' and n.image]
    return info


def _ref_context(refs):
    """Live detail of each @ reference, for the agent and for prompts."""
    out = []
    for r in refs or []:
        kind, name = r.get("kind", "object"), r.get("name", "")
        if kind in {"object", "camera", "light"}:
            info = _object_info(name)
            ob = bpy.data.objects.get(name)
            if ob is not None:
                info["world_bbox_min"], info["world_bbox_max"] = _world_bbox(ob)
                info["selected"] = ob.select_get()
        elif kind == "material":
            mat = bpy.data.materials.get(name)
            info = _material_info(mat) if mat else {"error": "No material named %r" % name}
        elif kind == "collection":
            col = bpy.data.collections.get(name)
            info = ({"name": col.name, "objects": [o.name for o in col.all_objects][:200]}
                    if col else {"error": "No collection named %r" % name})
        else:
            info = {"error": "Unknown reference kind %r" % kind}
        info["ref_kind"] = kind
        out.append(info)
    return {"references": out}


def _world_bbox(ob):
    from mathutils import Vector
    if ob.type in {'MESH', 'CURVE', 'SURFACE', 'FONT', 'META', 'CURVES', 'POINTCLOUD', 'VOLUME'}:
        pts = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
    else:
        pts = [ob.matrix_world.translation.copy()]
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    return [round(v, 4) for v in lo], [round(v, 4) for v in hi]


def _ref_objects(refs):
    """Objects named by references (collections expand to their objects)."""
    out = []
    for r in refs or []:
        kind, name = r.get("kind", "object"), r.get("name", "")
        if kind in {"object", "camera", "light"}:
            ob = bpy.data.objects.get(name)
            if ob:
                out.append(ob)
        elif kind == "collection":
            col = bpy.data.collections.get(name)
            if col:
                out.extend(col.all_objects)
        elif kind == "material":
            mat = bpy.data.materials.get(name)
            if mat:
                out.extend(o for o in bpy.context.scene.objects
                           if any(s.material == mat for s in o.material_slots))
    seen, uniq = set(), []
    for ob in out:
        if ob.name not in seen:
            seen.add(ob.name)
            uniq.append(ob)
    return uniq


# ---- Reference passes -----------------------------------------------------------------

def _eevee_engine():
    items = {e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items}
    for e in ('BLENDER_EEVEE_NEXT', 'BLENDER_EEVEE'):
        if e in items:
            return e
    return None


class _Saved:
    """Set attributes for a while and put them all back."""

    def __init__(self):
        self._items = []

    def set(self, owner, attr, value):
        try:
            self._items.append((owner, attr, getattr(owner, attr)))
            setattr(owner, attr, value)
        except Exception:
            pass

    def restore(self):
        for owner, attr, value in reversed(self._items):
            try:
                setattr(owner, attr, value)
            except Exception:
                pass


def _viewport_camera(width):
    """A temporary camera that sees what the main 3D viewport sees. Returns (camera, w, h) or
    (scene camera, w, h) when the viewport looks through it."""
    ctx = _window_context('VIEW_3D')
    area = ctx.get("area")
    scene = bpy.context.scene
    if area is None or area.type != 'VIEW_3D':
        return None, 0, 0
    region = next(r for r in area.regions if r.type == 'WINDOW')
    space = area.spaces.active
    r3d = space.region_3d
    aspect = region.width / max(1, region.height)
    w = int(width)
    h = max(16, int(round(w / aspect)))
    if r3d.view_perspective == 'CAMERA' and scene.camera:
        return scene.camera, w, h
    data = bpy.data.cameras.new("Harness View")
    cam = bpy.data.objects.new("Harness View", data)
    scene.collection.objects.link(cam)
    cam.matrix_world = r3d.view_matrix.inverted()
    data.sensor_fit = 'AUTO'
    data.sensor_width = 36.0
    data.clip_start = space.clip_start
    data.clip_end = space.clip_end
    if r3d.view_perspective == 'ORTHO':
        data.type = 'ORTHO'
        data.ortho_scale = r3d.view_distance * 36.0 / max(1e-6, space.lens)
    else:
        data.lens = space.lens / 2.0     # the viewport's lens at its default zoom
    cam["harness_temp"] = True
    return cam, w, h


def _pass_material(kind, near, far):
    mat = bpy.data.materials.new("Harness %s pass" % kind)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    emit = nt.nodes.new("ShaderNodeEmission")
    nt.links.new(emit.outputs["Emission"], out.inputs["Surface"])
    if kind == "depth":
        cd = nt.nodes.new("ShaderNodeCameraData")
        mr = nt.nodes.new("ShaderNodeMapRange")
        mr.inputs["From Min"].default_value = near
        mr.inputs["From Max"].default_value = far
        mr.inputs["To Min"].default_value = 1.0     # near is white, like depth-control models expect
        mr.inputs["To Max"].default_value = 0.0
        nt.links.new(cd.outputs["View Z Depth"], mr.inputs["Value"])
        nt.links.new(mr.outputs["Result"], emit.inputs["Color"])
    elif kind == "normal":
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        vt = nt.nodes.new("ShaderNodeVectorTransform")
        vt.vector_type = 'NORMAL'
        vt.convert_from = 'WORLD'
        vt.convert_to = 'CAMERA'
        ma = nt.nodes.new("ShaderNodeVectorMath")
        ma.operation = 'MULTIPLY_ADD'
        ma.inputs[1].default_value = (0.5, 0.5, -0.5)   # camera space looks down -Z
        ma.inputs[2].default_value = (0.5, 0.5, 0.5)
        nt.links.new(geo.outputs["Normal"], vt.inputs["Vector"])
        nt.links.new(vt.outputs["Vector"], ma.inputs[0])
        nt.links.new(ma.outputs["Vector"], emit.inputs["Color"])
    elif kind == "mask":
        oi = nt.nodes.new("ShaderNodeObjectInfo")
        gt = nt.nodes.new("ShaderNodeMath")
        gt.operation = 'GREATER_THAN'
        gt.inputs[1].default_value = 0.5
        nt.links.new(oi.outputs["Object Index"], gt.inputs[0])
        nt.links.new(gt.outputs["Value"], emit.inputs["Color"])
    return mat


def _depth_range(cam):
    """Nearest and farthest visible geometry from the camera, so the depth map uses its range."""
    from mathutils import Vector
    inv = cam.matrix_world.inverted()
    near, far = None, None
    for ob in bpy.context.scene.objects:
        if not ob.visible_get() or ob.type in {'CAMERA', 'LIGHT', 'EMPTY', 'SPEAKER'}:
            continue
        for c in ob.bound_box:
            z = -(inv @ (ob.matrix_world @ Vector(c))).z
            if z <= 0:
                continue
            near = z if near is None else min(near, z)
            far = z if far is None else max(far, z)
    if near is None:
        return 0.1, 100.0
    return max(0.01, near * 0.9), max(near * 1.2, far * 1.05)


def _render_with(cam, w, h, path, kind, refs=None):
    scene = bpy.context.scene
    vl = bpy.context.view_layer
    keep = _Saved()
    mat = None
    old_index = {}
    try:
        engine = _eevee_engine()
        if engine is None:
            return {"error": "EEVEE is not available"}
        near, far = _depth_range(cam)
        mat = _pass_material(kind, near, far)
        if kind == "mask":
            targets = {o.name for o in _ref_objects(refs)} or {o.name for o in bpy.context.selected_objects}
            if not targets:
                return {"error": "Reference an object with @ (or select one) to make a mask"}
            for ob in scene.objects:
                old_index[ob.name] = ob.pass_index
                ob.pass_index = 1 if ob.name in targets else 0
        r = scene.render
        keep.set(scene, "camera", cam)
        keep.set(r, "engine", engine)
        keep.set(r, "resolution_x", w)
        keep.set(r, "resolution_y", h)
        keep.set(r, "resolution_percentage", 100)
        keep.set(r, "film_transparent", True)
        keep.set(r, "use_compositing", False)
        keep.set(r, "use_sequencer", False)
        keep.set(r.image_settings, "file_format", 'PNG')
        keep.set(r.image_settings, "color_mode", 'RGB')
        keep.set(scene.view_settings, "view_transform", 'Standard')
        keep.set(scene.view_settings, "look", 'None')
        keep.set(scene.view_settings, "exposure", 0.0)
        keep.set(scene.view_settings, "gamma", 1.0)
        keep.set(scene.eevee, "taa_render_samples", 1)
        keep.set(vl, "material_override", mat)
        bpy.ops.render.render(write_still=False)
        img = bpy.data.images.get("Render Result")
        img.save_render(filepath=path, scene=scene)
    except Exception:
        return {"error": traceback.format_exc(limit=4)}
    finally:
        keep.restore()
        for name, idx in old_index.items():
            ob = bpy.data.objects.get(name)
            if ob:
                ob.pass_index = idx
        if mat is not None:
            bpy.data.materials.remove(mat)
    return {"path": path, "width": w, "height": h}


def _edges_from(depth_path, normal_path, path):
    """Edge lines from depth and normal discontinuities (a clean line drawing of the shapes)."""
    import numpy as np
    def load(p):
        im = bpy.data.images.load(p, check_existing=False)
        w, h = im.size
        a = np.empty(w * h * 4, dtype=np.float32)
        im.pixels.foreach_get(a)
        bpy.data.images.remove(im)
        return a.reshape(h, w, 4), w, h
    d, w, h = load(depth_path)
    n, _, _ = load(normal_path)

    def grad(x):
        gx = np.zeros_like(x)
        gy = np.zeros_like(x)
        gx[:, 1:-1] = x[:, 2:] - x[:, :-2]
        gy[1:-1, :] = x[2:, :] - x[:-2, :]
        return np.sqrt(gx * gx + gy * gy)
    e = grad(d[..., 0]) * 6.0
    for c in range(3):
        e = np.maximum(e, grad(n[..., c]) * 1.6)
    e = np.clip((e - 0.08) * 3.0, 0.0, 1.0)
    out = np.ones((h, w, 4), dtype=np.float32)
    out[..., 0] = out[..., 1] = out[..., 2] = e
    im = bpy.data.images.new("Harness edges", w, h, alpha=False)
    im.pixels.foreach_set(out.ravel())
    im.filepath_raw = path
    im.file_format = 'PNG'
    im.save()
    bpy.data.images.remove(im)
    return {"path": path, "width": w, "height": h}


def _capture_pass(path, kind, source, refs, width):
    """One reference image of the scene: beauty, depth, normal, edges or mask, from the
    viewport's angle or the scene camera."""
    width = int(width or 1024)
    if kind == "beauty":
        if source == "camera":
            return _render_still(path)
        return _capture_viewport(path, width)
    scene = bpy.context.scene
    if source == "camera":
        if scene.camera is None:
            return {"error": "The scene has no camera"}
        cam = scene.camera
        w = width
        h = max(16, int(round(w * scene.render.resolution_y / max(1, scene.render.resolution_x))))
    else:
        cam, w, h = _viewport_camera(width)
        if cam is None:
            return {"error": "No 3D viewport is open"}
    try:
        if kind in {"depth", "normal", "mask"}:
            return _render_with(cam, w, h, path, kind, refs)
        if kind == "edges":
            base = os.path.splitext(path)[0]
            dr = _render_with(cam, w, h, base + "-depth.png", "depth")
            if "error" in dr:
                return dr
            nr = _render_with(cam, w, h, base + "-normal.png", "normal")
            if "error" in nr:
                return nr
            return _edges_from(dr["path"], nr["path"], path)
        return {"error": "Unknown pass %r" % kind}
    finally:
        if cam is not None and cam.get("harness_temp"):
            data = cam.data
            bpy.data.objects.remove(cam)
            bpy.data.cameras.remove(data)


# ---- Camera path ----------------------------------------------------------------------

def _camera_path(first_path=None, last_path=None, width=1024):
    """The scene camera's move over the frame range, as samples and in words, with its first
    and last frames drawn from the camera (fast viewport render)."""
    import math
    scene = bpy.context.scene
    cam = scene.camera
    if cam is None:
        return {"error": "The scene has no camera"}
    f0, f1 = scene.frame_start, scene.frame_end
    keep_frame = scene.frame_current
    step = max(1, (f1 - f0) // 60)
    samples = []
    try:
        for f in list(range(f0, f1 + 1, step)) + ([f1] if (f1 - f0) % step else []):
            scene.frame_set(f)
            m = cam.matrix_world.copy()
            e = m.to_euler('XYZ')
            samples.append({"frame": f, "location": [round(v, 4) for v in m.translation],
                            "rotation_deg": [round(math.degrees(v), 3) for v in e],
                            "lens": round(cam.data.lens, 3),
                            "focus_distance": round(cam.data.dof.focus_distance, 3) if cam.data.dof.use_dof else None})
        scene.frame_set(f0)
        m0 = cam.matrix_world.copy()
        lens0 = cam.data.lens
        if first_path:
            _opengl_camera_frame(first_path, width)
        scene.frame_set(f1)
        m1 = cam.matrix_world.copy()
        lens1 = cam.data.lens
        if last_path:
            _opengl_camera_frame(last_path, width)
    finally:
        scene.frame_set(keep_frame)
    # The move in the camera's own frame at the start: forward (dolly), right (truck), up (pedestal).
    d = m0.to_3x3().inverted() @ (m1.translation - m0.translation)
    forward, right, up = -d.z, d.x, d.y
    q = m0.to_quaternion().rotation_difference(m1.to_quaternion())
    rel = (m0.to_3x3().inverted() @ m1.to_3x3()).to_euler('YXZ')
    pan, tilt, roll = math.degrees(rel.y), math.degrees(rel.x), math.degrees(rel.z)
    secs = (f1 - f0 + 1) / max(1.0, scene.render.fps / scene.render.fps_base)
    words = []
    if abs(forward) > 0.05:
        words.append("dolly %s %.1f m" % ("in" if forward > 0 else "out", abs(forward)))
    if abs(right) > 0.05:
        words.append("truck %s %.1f m" % ("right" if right > 0 else "left", abs(right)))
    if abs(up) > 0.05:
        words.append("pedestal %s %.1f m" % ("up" if up > 0 else "down", abs(up)))
    if abs(pan) > 1:
        words.append("pan %s %.0f°" % ("left" if pan > 0 else "right", abs(pan)))
    if abs(tilt) > 1:
        words.append("tilt %s %.0f°" % ("up" if tilt > 0 else "down", abs(tilt)))
    if abs(roll) > 1:
        words.append("roll %.0f°" % roll)
    if abs(lens1 - lens0) > 0.5:
        words.append("zoom %.0f→%.0f mm" % (lens0, lens1))
    moving = bool(words)
    text = ("Camera move over %.1f s: %s." % (secs, ", ".join(words))) if moving else \
        "Static camera, %.0f mm lens." % lens0
    return {"camera": cam.name, "frames": [f0, f1], "seconds": round(secs, 2), "moving": moving,
            "description": text, "samples": samples, "first_frame": first_path, "last_frame": last_path,
            "rotation_angle_deg": round(math.degrees(q.angle), 2)}


def _opengl_camera_frame(path, width):
    scene = bpy.context.scene
    keep = _Saved()
    try:
        r = scene.render
        keep.set(r, "resolution_percentage", max(1, min(100, int(100 * width / max(1, r.resolution_x)))))
        keep.set(r.image_settings, "file_format", 'PNG')
        with bpy.context.temp_override(**_window_context('VIEW_3D')):
            bpy.ops.render.opengl(write_still=False, view_context=False)
        bpy.data.images["Render Result"].save_render(filepath=path, scene=scene)
    finally:
        keep.restore()


# ---- Materials from images ------------------------------------------------------------

def _apply_material(image_path, refs, name=None):
    """Dress the referenced objects (or the selection) in a PBR material made from an image:
    base colour from the image, bump and roughness derived from its brightness, projected
    by box mapping so it works without UVs."""
    targets = [o for o in _ref_objects(refs) if o.type in {'MESH', 'CURVE', 'SURFACE', 'FONT', 'META'}]
    if not targets:
        targets = [o for o in bpy.context.selected_objects if o.type == 'MESH']
    if not targets:
        return {"error": "Reference a mesh with @ (or select one) to apply the material to"}
    img = bpy.data.images.load(image_path, check_existing=True)
    mat = bpy.data.materials.new(name or ("AI " + os.path.splitext(os.path.basename(image_path))[0][-24:]))
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    coord = nt.nodes.new("ShaderNodeTexCoord")
    mapping = nt.nodes.new("ShaderNodeMapping")
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    tex.projection = 'BOX'
    tex.projection_blend = 0.25
    nt.links.new(coord.outputs["Object"], mapping.inputs["Vector"])
    nt.links.new(mapping.outputs["Vector"], tex.inputs["Vector"])
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    bw = nt.nodes.new("ShaderNodeRGBToBW")
    nt.links.new(tex.outputs["Color"], bw.inputs["Color"])
    rough = nt.nodes.new("ShaderNodeMapRange")
    rough.inputs["To Min"].default_value = 0.85
    rough.inputs["To Max"].default_value = 0.35
    nt.links.new(bw.outputs["Val"], rough.inputs["Value"])
    nt.links.new(rough.outputs["Result"], bsdf.inputs["Roughness"])
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.35
    nt.links.new(bw.outputs["Val"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    for i, n in enumerate((coord, mapping, tex, bw, rough, bump)):
        n.location = (-900 + 180 * (i % 3), 300 - 260 * (i // 3))
    for ob in targets:
        if ob.data and hasattr(ob.data, "materials"):
            if len(ob.material_slots) == 0:
                ob.data.materials.append(mat)
            else:
                for slot in ob.material_slots:
                    slot.material = mat
    try:
        bpy.ops.ed.undo_push(message="Apply AI material")
    except Exception:
        pass
    return {"material": mat.name, "objects": [o.name for o in targets]}


# ---- Placement ------------------------------------------------------------------------

def _place_objects(names, at="cursor", target=None, fit=True):
    """Put new objects where they belong: at the 3D cursor or on a referenced object, standing
    on the surface below and sized to the target."""
    from mathutils import Vector
    scene = bpy.context.scene
    obs = [bpy.data.objects[n] for n in names if n in bpy.data.objects]
    roots = [o for o in obs if o.parent is None or o.parent not in obs]
    if not roots:
        return {"error": "Nothing to place"}
    bpy.context.view_layer.update()
    pts = []
    for o in obs:
        if o.type == 'MESH':
            pts += [o.matrix_world @ Vector(c) for c in o.bound_box]
    if not pts:
        pts = [o.matrix_world.translation for o in roots]
    lo = Vector([min(p[i] for p in pts) for i in range(3)])
    hi = Vector([max(p[i] for p in pts) for i in range(3)])
    size = hi - lo
    base = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))
    tgt = bpy.data.objects.get(target) if target else None
    scale = 1.0
    if at == "object" and tgt is not None:
        tlo, thi = _world_bbox(tgt)
        dest = Vector(((tlo[0] + thi[0]) / 2, (tlo[1] + thi[1]) / 2, thi[2]))
        if fit:
            tsize = max(thi[0] - tlo[0], thi[1] - tlo[1], 0.05)
            scale = (tsize * 0.8) / max(size.x, size.y, 1e-6)
    elif at == "origin":
        dest = Vector((0, 0, 0))
    else:
        dest = scene.cursor.location.copy()
        if fit and max(size) > 0:
            scale = min(1.0, 2.0 / max(size)) if max(size) > 2.0 else 1.0
    # Stand on whatever is below the destination.
    hit, loc, *_ = scene.ray_cast(bpy.context.evaluated_depsgraph_get(), dest + Vector((0, 0, 0.01)), Vector((0, 0, -1)))
    if hit and at != "object":
        dest.z = loc.z
    for o in roots:
        o.scale = o.scale * scale
        o.location = dest + (o.matrix_world.translation - base) * scale
    try:
        bpy.ops.ed.undo_push(message="Place generated object")
    except Exception:
        pass
    return {"placed": [o.name for o in roots], "at": list(dest), "scale": round(scale, 4)}


# ---- Lighting from an image ----------------------------------------------------------

def _match_lighting(image_path):
    """Match the scene's light to an image: world colour from its average, a key light whose
    colour is its brightest tones and whose direction follows where the light falls."""
    import numpy as np, math
    im = bpy.data.images.load(image_path, check_existing=False)
    w, h = im.size
    a = np.empty(w * h * 4, dtype=np.float32)
    im.pixels.foreach_get(a)
    bpy.data.images.remove(im)
    px = a.reshape(h, w, 4)[..., :3]
    lum = px @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    mean = px.reshape(-1, 3).mean(0)
    thresh = np.quantile(lum, 0.95)
    bright = px[lum >= thresh].mean(0) if (lum >= thresh).any() else mean
    ys, xs = np.mgrid[0:h, 0:w]
    wsum = max(1e-6, float(lum.sum()))
    cx = float((xs * lum).sum() / wsum) / max(1, w - 1) - 0.5     # -0.5 left .. 0.5 right
    cy = float((ys * lum).sum() / wsum) / max(1, h - 1) - 0.5     # bottom row first in Blender
    key_color = tuple(float(c) for c in bright / max(1e-6, bright.max()))
    scene = bpy.context.scene
    world = scene.world or bpy.data.worlds.new("World")
    scene.world = world
    world.use_nodes = True
    bg = next((n for n in world.node_tree.nodes if n.type == 'BACKGROUND'), None)
    if bg:
        if not bg.inputs["Color"].is_linked:
            bg.inputs["Color"].default_value = (float(mean[0]), float(mean[1]), float(mean[2]), 1.0)
        bg.inputs["Strength"].default_value = float(0.4 + 1.2 * float(lum.mean()))
    light = bpy.data.objects.get("Harness Key Light")
    if light is None:
        data = bpy.data.lights.new("Harness Key Light", 'SUN')
        light = bpy.data.objects.new("Harness Key Light", data)
        scene.collection.objects.link(light)
    light.data.color = key_color
    light.data.energy = float(2.0 + 4.0 * float(thresh))
    cam = scene.camera
    yaw0 = cam.matrix_world.to_euler().z if cam else 0.0
    light.rotation_euler = (math.radians(35 + 30 * cy), 0.0, yaw0 + math.radians(-70 * cx))
    try:
        bpy.ops.ed.undo_push(message="Match lighting to image")
    except Exception:
        pass
    return {"light": light.name, "key_color": [round(c, 3) for c in key_color],
            "world_color": [round(float(c), 3) for c in mean], "direction": [round(cx, 3), round(cy, 3)]}


# ---- Region of the viewport -----------------------------------------------------------

_region = {"rect": None}


class HARNESS_OT_pick_region(bpy.types.Operator):
    """Drag a box over the 3D viewport: that area is edited or regenerated in Harness Studio"""
    bl_idname = "harness.pick_region"
    bl_label = "Pick a Region"

    def invoke(self, context, event):
        area = context.area
        if area is None or area.type != 'VIEW_3D':
            return {'CANCELLED'}
        self._region = next(r for r in area.regions if r.type == 'WINDOW')
        self._start = None
        self._end = None
        import gpu
        from gpu_extras.batch import batch_for_shader
        self._shader = gpu.shader.from_builtin('UNIFORM_COLOR')
        self._handle = bpy.types.SpaceView3D.draw_handler_add(self._draw, (), 'WINDOW', 'POST_PIXEL')
        self._batch_for = batch_for_shader
        context.window_manager.modal_handler_add(self)
        try:
            context.window.cursor_modal_set('CROSSHAIR')
            context.workspace.status_text_set("Drag a box over the area to edit · Esc cancels")
        except Exception:
            pass
        return {'RUNNING_MODAL'}

    def _local(self, event):
        r = self._region
        return (min(max(event.mouse_x - r.x, 0), r.width), min(max(event.mouse_y - r.y, 0), r.height))

    def _draw(self):
        if self._start is None or self._end is None:
            return
        import gpu
        (x0, y0), (x1, y1) = self._start, self._end
        verts = ((x0, y0), (x1, y0), (x1, y1), (x0, y1))
        gpu.state.blend_set('ALPHA')
        fill = self._batch_for(self._shader, 'TRI_FAN', {"pos": verts})
        self._shader.uniform_float("color", (0.294, 0.576, 1.0, 0.14))
        fill.draw(self._shader)
        line = self._batch_for(self._shader, 'LINE_LOOP', {"pos": verts})
        self._shader.uniform_float("color", (0.294, 0.576, 1.0, 0.95))
        gpu.state.line_width_set(2.0)
        line.draw(self._shader)
        gpu.state.line_width_set(1.0)
        gpu.state.blend_set('NONE')

    def _finish(self, context):
        bpy.types.SpaceView3D.draw_handler_remove(self._handle, 'WINDOW')
        try:
            context.window.cursor_modal_restore()
            context.workspace.status_text_set(None)
        except Exception:
            pass
        context.area.tag_redraw()

    def modal(self, context, event):
        context.area.tag_redraw()
        if event.type in {'ESC', 'RIGHTMOUSE'} and event.value == 'PRESS':
            self._finish(context)
            return {'CANCELLED'}
        if event.type == 'LEFTMOUSE' and event.value == 'PRESS':
            self._start = self._end = self._local(event)
        elif event.type == 'MOUSEMOVE' and self._start is not None:
            self._end = self._local(event)
        elif event.type == 'LEFTMOUSE' and event.value == 'RELEASE' and self._start is not None:
            self._end = self._local(event)
            (x0, y0), (x1, y1) = self._start, self._end
            r = self._region
            if abs(x1 - x0) > 8 and abs(y1 - y0) > 8:
                # Normalised, top-left origin (image convention).
                _region["rect"] = [min(x0, x1) / r.width, 1 - max(y0, y1) / r.height,
                                   max(x0, x1) / r.width, 1 - min(y0, y1) / r.height]
            self._finish(context)
            return {'FINISHED'}
        return {'RUNNING_MODAL'}


def _region_capture(image_path, mask_path, width):
    """The viewport image and a mask of the picked region (white = regenerate)."""
    if not _region["rect"]:
        return {"error": "Pick an area of the viewport first"}
    res = _capture_viewport(image_path, width)
    if "error" in res:
        return res
    import numpy as np
    im = bpy.data.images.load(image_path, check_existing=False)
    w, h = im.size
    bpy.data.images.remove(im)
    x0, y0, x1, y1 = _region["rect"]
    m = np.zeros((h, w, 4), dtype=np.float32)
    m[..., 3] = 1.0
    # Blender images are stored bottom row first.
    r0, r1 = int((1 - y1) * h), int((1 - y0) * h)
    c0, c1 = int(x0 * w), int(x1 * w)
    m[r0:r1, c0:c1, :3] = 1.0
    mk = bpy.data.images.new("Harness mask", w, h, alpha=False)
    mk.pixels.foreach_set(m.ravel())
    mk.filepath_raw = mask_path
    mk.file_format = 'PNG'
    mk.save()
    bpy.data.images.remove(mk)
    return {"image": image_path, "mask": mask_path, "rect": _region["rect"]}


def _handle(req):
    kind = req.get("kind")
    p = req.get("payload") or {}
    if kind == "operator":
        return {"result": _call_operator(p["op"], p.get("props"), p.get("context") or 'INVOKE_DEFAULT')}
    if kind == "workspace":
        win = _main_window()
        ws = bpy.data.workspaces.get(p.get("name", ""))
        if win and ws and ws.name != SIDE_WORKSPACE:
            win.workspace = ws
        return {"ok": True}
    if kind == "open_file":
        path = os.path.abspath(os.path.expanduser(p["path"]))
        if not os.path.isfile(path):
            return {"error": "Project file does not exist: %s" % path}
        with bpy.context.temp_override(**_window_context()):
            result = bpy.ops.wm.open_mainfile('EXEC_DEFAULT', filepath=path, load_ui=True)
        return {"ok": 'FINISHED' in result, "result": list(result), "path": path}
    if kind == "call_menu":
        with bpy.context.temp_override(**_window_context()):
            bpy.ops.wm.call_menu(name=p["menu"])
        return {"ok": True}
    if kind == "config":
        _config.update(p)
        _config["received"] = True
        return {"ok": True}
    if kind == "layout":
        return _layout_request(p)
    if kind == "scene_items":
        return _scene_items()
    if kind == "ref_context":
        return _ref_context(p.get("refs"))
    if kind == "capture_pass":
        return _capture_pass(p["path"], p.get("pass", "beauty"), p.get("source", "viewport"),
                             p.get("refs"), p.get("width", 1024))
    if kind == "camera_path":
        return _camera_path(p.get("first"), p.get("last"), p.get("width", 1024))
    if kind == "apply_material":
        return _apply_material(p["image"], p.get("refs"), p.get("name"))
    if kind == "place":
        return _place_objects(p.get("names", []), p.get("at", "cursor"), p.get("target"), p.get("fit", True))
    if kind == "match_lighting":
        return _match_lighting(p["image"])
    if kind == "pick_region":
        _region["rect"] = None
        with bpy.context.temp_override(**_window_context('VIEW_3D')):
            bpy.ops.harness.pick_region('INVOKE_DEFAULT')
        return {"ok": True}
    if kind == "region_capture":
        return _region_capture(p["image"], p["mask"], p.get("width", 1280))
    if kind == "menu":
        return {"items": record_menu(p["menu"])}
    if kind == "menu_tree":
        return {"items": _menu_tree(p["menu"], int(p.get("depth", 3)))}
    if kind == "toggle":
        data = eval(p["path"], {"bpy": bpy})
        setattr(data, p["prop"], not getattr(data, p["prop"]))
        return {"ok": True}
    if kind == "scene_summary":
        return _scene_summary()
    if kind == "object_info":
        return _object_info(p.get("name", ""))
    if kind == "exec":
        return _exec_python(p.get("code", ""), p.get("undo", "Harness Agent"))
    if kind == "capture_viewport":
        return _capture_viewport(p["path"], p.get("width", 1280))
    if kind == "render_still":
        return _render_still(p["path"])
    if kind == "import_file":
        return _import_file(p["path"], p.get("as", "object"), p.get("place"), p.get("target"))
    if kind == "undo":
        with bpy.context.temp_override(**_window_context()):
            bpy.ops.ed.undo() if p.get("redo") is not True else bpy.ops.ed.redo()
        return {"ok": True}
    return {"error": "Unknown request %r" % kind}


# --------------------------------------------------------------------------------------
# Theme: Blender's interface in the brand palette (attachments/picture-2.png), rebuilt from
# Blender's default dark theme at every start so it never compounds:
#   #0A0A0A base, neutral backgrounds lowered towards it; #FFFFFF interface text, #D5D9E2 for
#   quieter text; #4B93FF for selection and active widgets; #FB940B for the active object.
# The interface font is Instrument Sans, like the shell.

BRAND_BLUE = (0x4b / 255, 0x93 / 255, 0xff / 255)
BRAND_ORANGE = (0xfb / 255, 0x94 / 255, 0x0b / 255)
BRAND_MIST = (0xd5 / 255, 0xd9 / 255, 0xe2 / 255)
BRAND_BASE = 0x0a / 255

# Region and panel backgrounds: lowered towards the base, keeping their order.
_BACKGROUNDS = {"back", "header", "button", "list", "navigation_bar", "execution_buts",
                "tab_back", "sub_back", "gradient", "high_gradient", "title", "space"}
# Widget faces (fields, buttons, menus): lowered less, so they stay readable on the panels.
_FACES = {"inner", "item"}


def _hsv(r, g, b):
    import colorsys
    return colorsys.rgb_to_hsv(r, g, b)


def _theme_color(pid, col):
    r, g, b = col[0], col[1], col[2]
    rest = list(col[3:])
    h, sat, val = _hsv(r, g, b)
    lum = (r + g + b) / 3.0
    if sat > 0.12:
        # Blender's selection blue becomes the brand blue (axes and gizmos keep theirs).
        if (0.53 <= h <= 0.64 and sat > 0.35 and val > 0.30 and
                not pid.startswith(("axis_", "gizmo", "view_overlay"))):
            # Brand hue and saturation at Blender's own brightness, so quiet highlights stay
            # quiet and text on them stays readable.
            import colorsys
            bh, bs, _bv = colorsys.rgb_to_hsv(*BRAND_BLUE)
            return list(colorsys.hsv_to_rgb(bh, bs, val)) + rest
        return col
    if "text" in pid or pid == "title":
        if lum <= 0.40:
            return col                      # Dark text on a light widget.
        if lum > 0.70 or pid.endswith(("_hi", "_sel")):
            return [1.0, 1.0, 1.0] + rest
        return list(BRAND_MIST) + rest
    if pid in _BACKGROUNDS and pid != "title":
        v = BRAND_BASE + max(0.0, lum - 0.094) * 0.62
        return [v, v, v] + rest
    if pid in _FACES:
        v = 0.05 + max(0.0, lum - 0.09) * 0.78
        return [v, v, v] + rest
    return col


def _apply_theme():
    prefs = bpy.context.preferences
    try:
        bpy.ops.preferences.reset_default_theme()
    except Exception:
        traceback.print_exc()
        return
    try:
        prefs.view.use_text_antialiasing = True
        prefs.view.text_hinting = 'FULL'
        prefs.view.use_text_render_subpixelaa = False
    except Exception:
        pass
    try:
        font = _harness.font_path("InstrumentSans-Variable.ttf")
        if font and prefs.view.font_path_ui != font:
            prefs.view.font_path_ui = font
    except Exception:
        traceback.print_exc()
    seen = set()

    def walk(struct, depth):
        if struct is None or depth > 5:
            return
        # A struct and its first member share an address: key by address and type.
        key = (struct.as_pointer(), struct.bl_rna.identifier)
        if key in seen:
            return
        seen.add(key)
        for prop in struct.bl_rna.properties:
            pid = prop.identifier
            if pid == "rna_type":
                continue
            if prop.type == 'POINTER':
                walk(getattr(struct, pid, None), depth + 1)
            elif (prop.type == 'FLOAT' and prop.subtype in {'COLOR', 'COLOR_GAMMA'} and
                  prop.array_length in {3, 4} and not prop.is_readonly):
                col = list(getattr(struct, pid))
                new = _theme_color(pid, col)
                if new != col:
                    try:
                        setattr(struct, pid, new)
                    except Exception:
                        pass

    theme = prefs.themes[0]
    walk(theme, 0)
    # Weights (Instrument Sans): labels and widgets Medium so small text isn't thin at
    # fractional scales, panel titles SemiBold.
    try:
        style = prefs.ui_styles[0]
        for name, weight in (("widget", 500), ("tooltip", 500), ("panel_title", 600)):
            fs = getattr(style, name, None)
            if fs is not None and hasattr(fs, "character_weight"):
                fs.character_weight = weight
    except Exception:
        traceback.print_exc()
    try:
        theme.view_3d.object_active = BRAND_ORANGE
        theme.view_3d.object_selected = (0.86, 0.45, 0.02)
        theme.user_interface.widget_text_cursor = BRAND_BLUE
    except Exception:
        traceback.print_exc()
    try:
        # The status bar is drawn over nothing: make it opaque.
        for name in ("header", "back"):
            col = list(getattr(theme.statusbar.space, name))
            if len(col) == 4:
                setattr(theme.statusbar.space, name, col[:3] + [1.0])
    except Exception:
        traceback.print_exc()


# --------------------------------------------------------------------------------------
# State

_recent_cache = ([], 0.0)


def _recent_files():
    global _recent_cache
    now = time.monotonic()
    if now - _recent_cache[1] < 2.0:
        return _recent_cache[0]
    recent = []
    try:
        recent_path = os.path.join(bpy.utils.user_resource('CONFIG'), "recent-files.txt")
        with open(recent_path, encoding="utf-8") as fh:
            recent = [l.strip() for l in fh if l.strip()][:12]
    except OSError:
        pass
    _recent_cache = (recent, now)
    return recent


def _can_undo_redo():
    """Use Blender's own operator polls so QML sees the actual history for this file."""
    return _operator_poll("ed.undo"), _operator_poll("ed.redo")


def _ordered_workspaces():
    names = [w.name for w in bpy.data.workspaces if w.name != SIDE_WORKSPACE]
    rank = {n: i for i, n in enumerate(WORKSPACE_ORDER)}
    return sorted(names, key=lambda n: (rank.get(n, len(rank)), names.index(n)))


def _state():
    data = bpy.data
    wm = bpy.context.window_manager
    main = _main_window()
    path = data.filepath
    scene = bpy.context.scene
    active = bpy.context.view_layer.objects.active if bpy.context.view_layer else None
    recent = _recent_files()
    job_text, job_progress = "", 0.0
    if wm.is_interface_locked:
        job_text = "Rendering"
    can_undo, can_redo = _can_undo_redo()
    return {
        "project_name": bpy.path.basename(path) if path else "",
        "project_path": path,
        "modified": data.is_dirty,
        "workspaces": _ordered_workspaces(),
        "layout_templates": _layout_templates(),
        "region_rect": _region["rect"],
        "layouts_ready": _layouts_ready,
        "layout_customized": bool(main and main.workspace and main.workspace.name in _store["workspaces"]),
        "active_workspace": main.workspace.name if main and main.workspace else "",
        "mode": bpy.context.mode,
        "active_object": active.name if active else "",
        "selected": len(bpy.context.selected_objects) if hasattr(bpy.context, "selected_objects") else 0,
        "objects": len(scene.objects) if scene else 0,
        "render_engine": scene.render.engine if scene else "",
        "recent_files": recent,
        "version": bpy.app.version_string,
        "job_text": job_text,
        "job_progress": job_progress,
        "can_undo": can_undo,
        "can_redo": can_redo,
        "has_project": bool(path),
        "project_saved": bool(path) and not data.is_dirty,
    }


def _tick():
    global _last_state, _theme_done
    try:
        if not _theme_done:
            _theme_done = True
            _apply_theme()
        _close_legacy_side_windows()
        _maintain_layout()
        if not _reopen_done:
            _reopen_last()          # before laying out: a reopened project brings its own
        elif not _layouts_ready and not _adopt_file:
            win = _main_window()
            if win is not None and win.workspace is not None:
                _prebuild_step(win)
        if _DEBUG and os.environ.get("HARNESS_CYCLE"):
            _debug_cycle()
        requests = json.loads(_harness.take())
        for req in requests:
            try:
                result = _handle(req)
            except Exception as ex:
                result = {"error": "".join(traceback.format_exception_only(type(ex), ex)).strip(),
                          "traceback": traceback.format_exc(limit=6)}
            try:
                _harness.reply(int(req["id"]), json.dumps(result, default=str))
            except Exception:
                _harness.reply(int(req["id"]), json.dumps({"error": "unserialisable result"}))
        state = _state()
        encoded = json.dumps(state, default=str, sort_keys=True)
        if encoded != _last_state:
            _last_state = encoded
            _harness.state(encoded)
    except Exception:
        traceback.print_exc()
    return TICK


@bpy.app.handlers.persistent
def _on_load_post(*_args):
    """Projects open with their own UI (every tab as it was left). A saved project's screens
    are taken as they are; a new file (startup file) gets the Harness layouts again."""
    global _adopt_file, _layouts_ready, _prebuild, _pending, _last_state, _recent_cache
    _applied.clear()
    _pending = []
    _adopt_file = bool(bpy.data.filepath)
    _layouts_ready = _adopt_file
    _prebuild = None if _adopt_file else _prebuild
    _recent_cache = ([], 0.0)
    # Force one complete state publication immediately after Blender replaces Main.  This is
    # the hand-off point between Blender, the file manager and Qt/QML; without it the shell can
    # keep the startup filename/dirty/history snapshot until some unrelated edit occurs.
    _last_state = None
    if not _adopt_file:
        _prebuild = None
    _log("loaded", bpy.data.filepath or "(new file)", "adopt" if _adopt_file else "lay out")


class HARNESS_OT_swap_panels(bpy.types.Operator):
    """Click a panel, then the panel it should trade places with"""
    bl_idname = "harness.swap_panels"
    bl_label = "Swap Panels"

    def invoke(self, context, event):
        self._first = None
        context.window_manager.modal_handler_add(self)
        for cursor in ('SWAP_AREA', 'HAND'):
            try:
                context.window.cursor_modal_set(cursor)
                break
            except Exception:
                continue
        self._status(context, "Click the panel to move")
        return {'RUNNING_MODAL'}

    @staticmethod
    def _status(context, text):
        try:
            context.workspace.status_text_set(text)
        except Exception:
            pass

    def _finish(self, context):
        try:
            context.window.cursor_modal_restore()
        except Exception:
            pass
        self._status(context, None)

    def modal(self, context, event):
        if event.type in {'ESC', 'RIGHTMOUSE'} and event.value == 'PRESS':
            self._finish(context)
            return {'CANCELLED'}
        if event.type == 'LEFTMOUSE' and event.value == 'PRESS':
            xy = (event.mouse_x, event.mouse_y)
            if self._first is None:
                self._first = xy
                self._status(context, "Click the panel to trade places with")
                return {'RUNNING_MODAL'}
            win = context.window
            try:
                with context.temp_override(window=win, screen=win.screen):
                    bpy.ops.screen.area_swap('EXEC_DEFAULT', cursor=self._first, target=xy)
            except Exception:
                traceback.print_exc()
            self._finish(context)
            return {'FINISHED'}
        return {'RUNNING_MODAL'}


_orbit_n, _orbit_t = 0, 0.0


def _debug_orbit():
    """HARNESS_ORBIT=1: turn the 3D view every main-loop pass (measures viewport frame rate)."""
    import mathutils
    global _orbit_n, _orbit_t
    _orbit_n += 1
    now = time.monotonic()
    if now - _orbit_t >= 1.0:
        print("harness-orbit: %d passes/s" % _orbit_n, file=sys.stderr, flush=True)
        _orbit_n, _orbit_t = 0, now
    win = _main_window()
    if win is not None:
        area = _largest_area(win.screen, 'VIEW_3D')
        if area is not None:
            r3d = area.spaces.active.region_3d
            r3d.view_rotation = mathutils.Quaternion((0, 0, 1), 0.02) @ r3d.view_rotation
            area.tag_redraw()
    return 0.0


def register():
    if _harness is None or bpy.app.background:
        return
    prefs = bpy.context.preferences
    try:
        # Projects open with their own UI: every tab, view and editor as it was saved.
        prefs.filepaths.use_load_ui = True
        prefs.view.show_splash = False
    except Exception:
        pass
    _load_store()
    _install_fcurves_compat()
    for cls in (HARNESS_OT_swap_panels, HARNESS_OT_pick_region):
        try:
            bpy.utils.register_class(cls)
        except ValueError:
            pass
    if _on_load_post not in bpy.app.handlers.load_post:
        bpy.app.handlers.load_post.append(_on_load_post)
    if not bpy.app.timers.is_registered(_tick):
        bpy.app.timers.register(_tick, first_interval=0.1, persistent=True)
    if os.environ.get("HARNESS_ORBIT"):
        bpy.app.timers.register(_debug_orbit, first_interval=2.0, persistent=True)
    _harness.ready()


def unregister():
    if _harness is None:
        return
    for cls in (HARNESS_OT_swap_panels, HARNESS_OT_pick_region):
        try:
            bpy.utils.unregister_class(cls)
        except RuntimeError:
            pass
    if bpy.app.timers.is_registered(_tick):
        bpy.app.timers.unregister(_tick)
    if _on_load_post in bpy.app.handlers.load_post:
        bpy.app.handlers.load_post.remove(_on_load_post)
