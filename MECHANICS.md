# Harness Studio — mechanics sheet

**Architecture (current):** the application *is* Blender (open-source source tree in `blender/`,
branch `harness-studio`), so every Blender editor, tool, operator, render engine and add-on is
the original code. What changed:

* `intern/ghost/intern/GHOST_SystemQt|WindowQt|ContextQt|QtSurface` — a new GHOST back-end on
  Qt 6. Blender draws each window into an OpenGL frame-buffer (stable FBO name, the GPU module
  caches it); on swap the frame is blitted to a front texture that a Qt Quick item shows 1:1 in
  device pixels. Input from Qt becomes GHOST events. Qt is pumped from Blender's own loop
  (basic render loop, shared GL context group).
* `editors/screen/screen_edit.cc` — with the Qt back-end, Blender's top bar is not created (the
  shell draws title bar, menus and workspace tabs); Blender's status bar stays in the main window.
* `intern/harness` — the Qt Quick shell, the `_harness` Python module and Harness Studio (agent,
  Create, library, profiles/settings). `scripts/startup/harness_bridge.py` runs shell requests on
  Blender's main loop with a real window context and pushes Blender's state to the shell.
* One Blender window fills everything left of Harness Studio. Each workspace tab is laid out as a
  split tree (outliner over properties on the left, the work area right of it, one 3D viewport at
  most: Layout = viewport + timeline, UV Editing = UV editor | viewport, Shading = viewport over
  shader nodes, Geometry Nodes = spreadsheet | viewport over geometry nodes, Scripting = viewport
  + console/info | text editor...). Blender tiles the areas (no overlap); borders drag to resize,
  "Swap two panels" trades any two areas (`screen.area_swap` gained a `target` point). The tree
  of each tab is read back after every change and kept in `~/.config/HarnessStudio/layouts.json`
  with saved templates; built-in templates: Default, Panels on the right, With timeline, Editor
  only; reset per tab or for all.
* Brand palette and type (attachments/picture-2.png): #0A0A0A base, #FFFFFF text, #D5D9E2
  in-between tone (secondary text, lines), #4B93FF action (selection, focus, toggles, send),
  #9ECBFF the agent (its caret, tool cards), #FB940B making (Create button, chosen template,
  Blender's active object). Instrument Sans for the interface (shell and Blender's own UI font),
  Cabin for secondary text (section labels, menu headers, template names), Notable for hero
  text (wordmark, panel title, empty-state title).
* Blender theme: rebuilt from Blender's default dark theme at each start — neutral backgrounds
  mapped to 0.039 + (L − 0.094) × 0.62, widget faces 0.05 + (L − 0.09) × 0.78, interface text
  #FFFFFF (quieter text #D5D9E2), selection blues take the brand hue at Blender's brightness,
  status bar opaque, text hinting Full.
* Viewport smoothness: Qt Quick renders on Blender's main thread, so the shell window swaps
  without v-sync (Qt still paces frames by the compositor) and update requests are not delayed
  by Qt's 5 ms idle timer. Measured with HARNESS_PROFILE=1 and a continuously orbiting view
  (XWayland): before, 50 Blender frames/s of which 25 shown; after, 45/45. HARNESS_ORBIT=1 runs
  the orbit test.
* GPU: on hybrid laptops the app used the integrated GPU. Measured with HARNESS_PROFILE=1 and
  an orbiting viewport: Intel UHD (ADL-S GT0.5) 28–45 frames/s; RTX 4050 208 frames/s with every
  144 Hz refresh shown. GHOST_SystemQt now selects the discrete GPU before any GL vendor is
  picked (NVIDIA PRIME offload variables, NVIDIA EGL vendor file; DRI_PRIME=1 on other hybrid
  systems), unless the environment chose one or HARNESS_GPU=integrated. Blender's window frames
  are paced to the display refresh (HARNESS_UNCAPPED=1 turns it off): 145 frames/s at 144 Hz.
* Scene link: @ in the agent's or Create's prompt lists the scene's objects, cameras, lights,
  materials and collections (live); the chosen items go with the message as live data
  (transform, bounds, materials...) and become targets. Create sends the viewport or camera as
  beauty, depth (near = white, range fitted to the visible geometry), normal (camera space),
  edges (depth + normal discontinuities) or an object mask (EEVEE with a material override, one
  sample, Standard view transform); "Follow the 3D camera path" sends the scene camera's first
  frame as the start image, the last frame as the end image when the model has such an input,
  and the move in words (dolly/truck/pedestal metres, pan/tilt/roll degrees, zoom mm); "Edit an
  area" lets the user drag a box over the viewport and sends the view with a mask to a model
  with a mask input; image results can dress @ references (or the selection) in a box-projected
  PBR material (colour, roughness and bump from the image) and set the lighting (world colour
  from the mean, a key sun from the brightest 5 % and where the light falls); 3D results are
  placed at the 3D cursor, on an @ reference (resting on top, sized to 80 % of it) or at the
  origin, standing on the surface below. The agent has the same as tools.
* Carry on where you left off: projects open with their own UI (every tab's panels, views and
  editors as saved in the .blend), saved projects' screens are kept as they are, new files get
  the Harness layouts; at start the most recent project reopens (setting "Carry on where I
  left off", on by default; not with --factory-startup or a file given); each project keeps its
  own agent conversation (threads/<profile>/projects.map).
* Create follows Higgsfield's studio composer (read from higgsfield.ai/create/video source): a
  32 px upload button (radius 10, white 5 %, hover 10 %) and a two-line prompt, a sideways
  row of 32 px setting chips (model, duration "5s", aspect "16:9", resolution, audio "On"), and
  a large Create button (Higgsfield 80 × 120 px; here 120 × ≥84) beside them, on a 16 px-radius
  card. Harness adds scene chips (reference + pass, camera path, area, as material, placement,
  @), a preset picker popover, start/end frame uploads for models with an end-frame input, and
  the feed of this mode's results above the composer.
* Menus: the size is computed from the items (the laid-out height arrives a frame late, which
  made every menu measure as empty and open downwards); they open below the button when they
  fit, otherwise above, never past a window edge, and scroll in the room they have. Top-level
  menus and pickers are modal without dimming: a press outside closes them and is not passed
  on, so pressing the same button again closes the menu instead of reopening it; Escape and the
  window losing focus close them too. Chip rows scroll sideways (wheel, touchpad, drag, edge
  arrows). Model lists: grouped by maker, each row the maker's mark, the full name, and the API
  that serves it.
* Results persist in library.json beside the files; result files without a record are brought
  in; unfinished fal queue requests resume polling after a restart. The viewer shows images
  (zoom, pan), videos (Qt Multimedia) and glTF/GLB models with a built-in reader (Qt's Assimp
  importer needs a system library that isn't installed): orbit, lighting presets, procedural sky
  reflections, wireframe, material channels, ground, turntable, size and triangle counts.
* Presets carry real pictures: tools/render_presets.py renders each with Cycles (a lit set per
  preset: camera where the move starts, effects built for real) into resources/presets; with a
  fal.ai key, "New previews" replaces them with FLUX schnell pictures (~$0.003 each). Tiles
  play the preset's move over the picture.
* Generation loader: a 3 × 3 isometric field of cubes rising in a diagonal wave with a light
  band sweeping their tops (single tumbling cube below 28 px). The agent's status line follows
  its real steps (thinking, reading, tool with its own icon, approval, writing), with elapsed
  time and step n of max.
* Create templates (TemplateData.qml): 53 camera moves, 10 effects, 4 product videos, 12 image
  apps, 4 product images and 6 3D recipes — the names of the moves Higgsfield's catalogue lists,
  each with our own prompt wording and a drawn, animated storyboard tile (no site imagery).
* Menus in the title bar are recorded from Blender's own `TOPBAR_MT_*` menu classes, so every
  entry (add-ons included) is the real operator with Blender's icon.

The values below were read from the stripped 5.2.1 binary during the earlier reverse-engineering
round; they are kept as a reference (the real code now provides them).


Values read from the original program (`/usr/bin/blender` 5.2.1, Ghidra addresses are the
built-in engine's; Ghidra's own are +0x100000), from its shipped Python UI/keymap files, and
from the person's Openfield add-on (`~/İndirilenler/openfield_blender-2.0.0-linux_x64.zip`).
Code in this project was written from this sheet, not copied.

## Add Mesh operator defaults (mesh.primitive_*_add registration functions)
| value | number | source |
|---|---|---|
| Circle vertices | 32 (min 3, max 10,000,000) | sub_4f31760 |
| Circle fill | Nothing (enum index 0) | sub_4f31760 |
| Cylinder vertices | 32 | sub_4f31850 |
| Cylinder depth | 2.0 | sub_4f31850, float @0x17c3a2c |
| Cylinder cap fill | N-Gon (enum index 1) | sub_4f31850 |
| Cone vertices | 32 (soft max 500) | sub_4f31980 |
| UV sphere segments / rings | 32 / 16 (max 100,000) | sub_4f31ca0 |
| Ico sphere subdivisions | 2 (1..10) | sub_4f31da0 |
| Grid X/Y subdivisions | 10 / 10 | sub_4f31b20 |
| Radius (shared) | 1.0 (float @0x17c4008) | sub_4fcdc30 → sub_4fcdbe0 |
| Size (plane, cube, grid, monkey) | 2.0 (float @0x17c3a2c) | sub_4fcdb90 |
| Torus major/minor segments | 48 / 12 | scripts/startup/bl_operators/add_mesh_torus.py |
| Torus major/minor radius | 1.0 / 0.25 | add_mesh_torus.py |
| New objects are placed at | the 3D cursor | mesh_add behaviour (estimated) |
| Monkey | an original stand-in head (the original's model is not copied) | — |

## Default dark theme (compiled table at file offset 0xb5a3b0…0xb5abc8)
| item | colour |
|---|---|
| Widget outline / inner / selected | #3d3d3d / #545454 / #4772b3 |
| Widget text / selected text | #e6e6e6 / #ffffff |
| Widget roundness | 0.2 |
| Text field inner | #282828 |
| Menu background / text | #181818 / #d9d9d9 |
| 3D viewport background | #3d3d3d (0xb5ab68) |
| Header | #303030 |
| Grid | #545454, alpha 0x80 |
| Object selected / active | #ed5700 / #ffa028 (0xb5abb8, 0xb5abc8) |
| Vertex select / edge select | #ff7a00 / #ffa000 |
| Axis X / Y / Z | #ff3352 / #8bdc00 / #2890ff (0xb5a764) |
| Keyframe selected | #ffbe33 |

## Keymap (scripts/presets/keyconfig/keymap_data/blender_default.py)
G move, R rotate, S scale (X/Y/Z axis, Shift+axis plane, typed numbers, Enter/LMB confirm,
Esc/RMB cancel, Ctrl snap), Alt+G/R/S clear, Shift+D duplicate, X/Delete delete, A / Alt+A
select all / none, Ctrl+I invert, H / Alt+H / Shift+H hide, Tab edit mode, E extrude, F make
face, Shift+A add menu, I insert keyframe, Alt+I clear keys, Ctrl+Z / Ctrl+Shift+Z undo/redo,
F12 render, F3 search, Space play, ←/→ frame, ↑/↓ keyframe, Shift+←/→ endpoints, Numpad
1/3/7 (Ctrl opposite), 5 ortho, 0 camera, . frame selected, Home frame all, 2/4/6/8 orbit,
Z shading menu, Shift+Z wireframe, Alt+Z X-ray, N sidebar, T toolbar, Shift+C reset cursor,
Shift+S snap, Ctrl+A apply, Ctrl+J join. MMB orbit, Shift+MMB pan, Ctrl+MMB / wheel zoom,
Shift+RMB set 3D cursor, LMB click select (Shift extend), LMB drag box select.

## View (estimated where marked)
| value | number | source |
|---|---|---|
| Viewport lens | 50 mm | estimated (DNA default) |
| Viewport sensor × zoom | 36 mm × 2 (sensor fit auto) | estimated |
| Clip start / end | 0.01 / 1000 m | estimated |
| Turntable sensitivity | 0.4° per pixel | estimated |
| Wheel zoom factor | 1.2 per step | estimated |
| Numpad orbit step | 15° | estimated |
| Drag threshold | 3 px | estimated |
| Undo steps | 32 | estimated |
| Rotate snap / scale snap / move snap | 5° / 0.1 / 1 m | estimated |
| Auto perspective (axis views go ortho) | on | estimated |
| Camera sensor / clip | 36 mm / 0.1–1000 m | estimated |

## Scene defaults (startup file is compressed inside the program; estimated)
Cube at origin (size 2); Camera at (7.3589, −6.9258, 4.9583) rot (63.559°, 0, 46.692°);
Point light 1000 W at (4.0762, 1.0055, 5.9039); frames 1–250 at 24 fps; render 1920×1080 100 %;
world colour #0d0d0d. Keyframes interpolate smoothly (auto-clamped bezier look).

## Window layer
The original ships GHOST_SystemX11, GHOST_SystemWayland and GHOST_SystemHeadless (type names at
0xc9e230, 0xcaddc0, 0xc9d700). Craft3D has a GHOST-style system/window/event API with a Qt 6
backend (`GHOST_SystemQt`, `GHOST_WindowQt`); the editor consumes its events as the window manager.

## Openfield (the person's add-on) — features and look
* Tokens: background #050506, surface white 5 % (hover 9 %, pressed 13 %, selected 24 %), text
  #f4f7ff / #8b8f99, brand (Generate) #f4f7ff with bevel #b8c2d6, on-brand #000, danger #ff3b30,
  info #0a84ff, success #34c759, radii card 19 / surface 16 / button 10 / chip 6 / pill 26,
  fonts DM Sans 11/10/8 and Pixelify Sans for brand labels.
* Composer: pill 288 wide → card up to 720 wide; mode tabs Scene builder, 3D Model, Character
  animation, Image, Video, Assets; prompt up to 68 px; chip strip 28 px; Generate 112×80.
* Launcher capsule 26 high (166 wide while running, recents tiles 48); chat bubble 32, panel
  304 wide, max 320 high, title "Supercomputer", placeholder "Ask anything, or describe a shot…".
* Assets explorer tabs All / 3D / Animation / Images / Video, tiles 106.
* Providers: fal.ai queue (POST https://queue.fal.run/{endpoint}, status → response, cancel),
  CDN uploads via rest.fal.ai/storage/auth/token?storage_type=fal-cdn-v3; OpenRouter chat
  completions with tools for Scene Builder, default model anthropic/claude-sonnet-5.5.
  Keys: FAL_KEY / OPENROUTER_API_KEY environment variables, else a private keys.json (0600).
* 35 curated model families (image, video, 3D, tools) with their input fields, converted from
  the add-on's bundled schemas into `resources/openfield_catalog.json`.
