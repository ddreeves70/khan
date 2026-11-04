# SketchBillet API reference {#mainpage}

SketchBillet is a free, open-source parametric CAD application. This reference covers
the public headers of the core libraries, from the bottom layer up:

| Library | Namespace | What it holds |
| --- | --- | --- |
| `sb_core` | `sb` | Ids (`sb::Id`), diagnostics (`sb::Diagnostic`), version |
| `sb_kernel` | `sb::kernel` | The geometry kernel interface (`sb::kernel::IKernel`) and value types |
| `sb_kernel_occt` | `sb::kernel` | The Open CASCADE implementation of `IKernel` |
| `sb_model` | `sb::model` | Documents, features, commands, undo and the command registry |
| `sb_io` | `sb::io` | Saving and opening `.sb` files (`sb::io::saveFile`, `sb::io::openFile`) |
| `sb_plugin` | `sb::plugin` | The plugin host, manifests, the event bus and the open designs (`sb::plugin::PluginHost`, `sb::plugin::PluginContext`) |
| `sb_ui` | `sb::ui` | The Qt side of the plugin API: panels in the desktop app (`sb::ui::UiRegistry`) |
| `sb_view` | `sb::view` | The 3D view (`sb::view::ViewWidget`), pane layouts and standard views, and the flat view used without OpenGL |
| `sb_plugin_modeling` | `sb::plugins` | The built-in `sb.modeling` plugin |
| `sb_plugin_viewer` | `sb::plugins` | The built-in `sb.viewer` plugin (`sb::plugins::viewerPlugin`) |

A good place to start is `sb::model::Document`, then `sb::model::Command`. Plugin
authors start with the plugin developer guide (`docs/plugin-guide/`), then
`sb::plugin::PluginContext`.

Units inside the code: lengths are millimeters and angles are radians.
