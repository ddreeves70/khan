# Khan plugin developer guide

A plugin adds something to Khan: a new command, a panel, a file format, a tool in
the 3D view. Khan's own features are plugins too. The box and cylinder commands
come from a plugin called `kh.modeling`, and it uses the same API you will.

This guide is for anyone who knows a little C++. It is version 0 and grows as
Khan grows. Every change to the plugin API updates it in the same pull request.

## Start here

1. **[Your first plugin: Hello](hello-plugin.md).** Build a plugin that adds a
   "make a cube" command and notices every change. About 30 minutes.
2. **[plugin.json](manifest.md).** Every key in a plugin's manifest.
3. **[What a plugin can do](extension-points.md).** Commands, events and open
   designs today, and what comes next.
4. **[Share something with other plugins](sharing.md).** Offer a service other
   plugins use, with two small plugins, Ruler and Tape. Also: the warnings Khan
   gives while loading, and plugins that use each other.

## How plugins fit in

```text
 your plugin          kh.modeling          (more plugins)
      \                    |                    /
       +------- PluginContext (the plugin API) -+
                           |
        PluginHost: finds, checks and loads plugins
        EventBus:   tells everyone what changed
        DocumentList: the open designs, changed only by commands
                           |
        Khan's core: documents, commands, geometry kernel
```

- A plugin is a **folder** with a `plugin.json` file and a **shared library** (a
  `.dll` on Windows, `.dylib` on macOS, `.so` on Linux).
- When Khan starts, the **plugin host** reads every `plugin.json`, checks it, and
  loads the plugins in order. A plugin loads after the plugins it needs.
- Each plugin has one **register function**. Khan calls it once and hands it a
  `PluginContext`, the plugin's only door into Khan. When every plugin has
  registered, Khan runs the functions they gave to `onStart`.
- A plugin that can't load is listed with a plain message, and Khan still starts.

## Three rules

1. **Change designs only by running commands.** That way undo always works, and
   every panel and view hears about the change.
2. **Every name you add has a stable id** that starts with your plugin's id, such as
   `example.hello.add_cube`. Don't rename it later: saved toolbars, shortcuts and
   scripts use it.
3. **Plugins that work on designs don't use Qt.** They then work in the desktop
   app, in `kh_cli` and (later) in Python scripts.

## What works today

| Part | Status |
| --- | --- |
| C++ plugins built with Khan | Works: [Hello](hello-plugin.md) |
| Commands | Works |
| Events: design opened, closed, changed, rebuilt, active design, setting changed | Works |
| Menu items and toolbar buttons (actions) | Works |
| Check-mark menu items, submenus, status-bar items | Works |
| Plugin settings Khan remembers | Works |
| The shared selection | Works |
| The grid (settings and snapping), from the `kh.grid` plugin | Works in the desktop app |
| Drawing in the viewer (overlays) | Works |
| Tiers, and tiers a plugin adds | Works: [plugin.json](manifest.md#tiers) |
| Panels in the desktop app | Works: the feature tree, Properties and the viewer are plugins |
| Sharing services and events between plugins | Works: [Share something](sharing.md) |
| 3D view tools | Coming with the sketcher (Phase 1) |
| New kinds of steps (feature types), file formats | Coming later |
| Building a plugin without the Khan source (an SDK) | Coming later |
| Python plugins | Coming later |

## Getting help

Ask in [GitHub Discussions](https://github.com/ddreeves70/khan/discussions). The
API reference, built from the code, covers every class in detail
(`cmake --build --preset dev --target docs`, then open
`build/dev/docs/html/index.html`).
