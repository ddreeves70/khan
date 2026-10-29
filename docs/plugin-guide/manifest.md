# plugin.json

Every plugin folder has a `plugin.json` file. Khan reads it before loading
anything, so it can list the plugin and explain problems even when the plugin's
code can't load.

```json
{
  "id": "example.hello",
  "name": "Hello",
  "version": "1.0.0",
  "apiVersion": 16,
  "description": "Adds a command that makes a cube, and notices every change.",
  "library": "hello",
  "requires": ["kh.modeling"],
  "uses": [],
  "permissions": [],
  "tier": "tools"
}
```

| Key | Needed | What it is |
| --- | --- | --- |
| `id` | yes | The plugin's permanent name: lower-case letters, digits and `_`, in two or more parts joined by dots, like `sam.cube`. Never change it once people use your plugin. Ids starting with `kh.` are kept for Khan's own plugins. |
| `name` | yes | The name people see, like `Cube`. |
| `version` | yes | Your plugin's own version, like `1.0.0`. Change it with each release. |
| `apiVersion` | yes, in the folder Khan loads | The plugin API version the plugin was built for, a whole number. This Khan uses **16**. You don't type it: `kh_add_plugin` writes it into the `plugin.json` next to the built library, from the Khan you build against. |
| `description` | no | One plain sentence about what the plugin adds. |
| `library` | yes for C++ | The program file's name without its ending: `"hello"` means `hello.dll` on Windows, `hello.dylib` on macOS and `hello.so` on Linux. It must be in the same folder as `plugin.json`. |
| `requires` | no | Ids of plugins this one can't work without. They load first. If one is missing or fails, this plugin doesn't load, and Khan says which one. |
| `uses` | no | Ids of plugins this one uses if they're there, such as a plugin whose service it asks for (see [Share something with other plugins](sharing.md)). They load first; if one is missing, this plugin loads anyway, with a note. |
| `tier` | no | Which layer of Khan the plugin belongs to: `foundation`, `tools`, `ui`, or a tier a plugin adds. Khan uses it only to warn about a plugin that uses a plugin in a higher tier (see [Tiers](manifest.md#tiers)). Without it, the plugin isn't checked. |
| `tiers` | no | Tiers this plugin adds, like `[{"name": "sam.mill.machining", "after": "tools", "before": "ui"}]` (see [Tiers](manifest.md#tiers)). |
| `permissions` | no | What the plugin needs to do, such as reading files. Khan stores the list but doesn't check it yet. Python plugins will need it. |

Khan ignores keys it doesn't know, so a newer `plugin.json` still loads in an
older Khan as long as `apiVersion` matches.

## Tiers

Khan's plugins are arranged in **tiers**, like the floors of a building. A plugin
should only use plugins on its own floor or the floors below:

| Tier | For | Khan's own plugins |
| --- | --- | --- |
| `foundation` | Shared ideas other plugins build on | `kh.modeling`, `kh.grid` |
| `tools` | Things the student does | the sketcher and measuring tools, later |
| `ui` | Panels and views that show the design | `kh.viewer`, `kh.featuretree`, `kh.properties` |

If a plugin uses a plugin in a higher tier, it still loads, with a
`plugin.tier_order` warning (see [Warnings](sharing.md#warnings)). That usually
means something is in the wrong place: move what both plugins need down into a
lower plugin, or have the higher plugin send events the lower one listens to.
Tiers never change the order plugins load in.

**Adding a tier.** A plugin can add its own tier, placed between the others:

```json
{
  "id": "sam.mill",
  "tiers": [{ "name": "sam.mill.machining", "after": "tools", "before": "ui" }],
  "tier": "sam.mill.machining"
}
```

- The new tier's name starts with your plugin's id and a dot, so it can't clash
  with another plugin's.
- `after` and `before` name existing tiers (Khan's or another plugin's); give at
  least one. With only `after`, the tier is above that one and side by side with
  the others above it.
- Two tiers placed in the same gap are side by side: plugins in them may use
  each other with no warning.
- A tier that can't be added (a name that doesn't start with your id, a name
  already used, no `after` or `before`, a tier that isn't there, or `after` a
  tier that's above the `before` one) gets a `plugin.tier_conflict` warning, and
  Khan ignores it. A plugin whose `tier` nobody adds gets `plugin.tier_unknown`
  and isn't checked.

`kh_cli plugins` shows each plugin's tier and the tiers it adds.

## When Khan refuses a plugin

Khan never stops starting because of a plugin. It lists the plugin as `failed` or
`skipped` with one of these reasons:

| Code | What happened | What to do |
| --- | --- | --- |
| `plugin.manifest_bad` | `plugin.json` is damaged or is missing a key | Fix the key the message names |
| `plugin.api_version` | Made for a different plugin API version | Rebuild it against this Khan (`kh_add_plugin` updates `apiVersion` for you), or get a version made for it |
| `plugin.reserved_id` | The id starts with `kh.` | Pick another id |
| `plugin.duplicate` | Another plugin has the same id | Remove one of them |
| `plugin.no_library` | No `library` key | Add it (Python plugins come later) |
| `plugin.library_missing` | The program file isn't in the folder | Build it, or check `library` matches the file's name |
| `plugin.library_bad` | The program file can't be loaded, or has no `KH_PLUGIN_ENTRY` | Rebuild it for this computer and this Khan |
| `plugin.missing_dependency` | A plugin in `requires` isn't installed | Install it |
| `plugin.dependency_failed` | A plugin in `requires` didn't load (state `skipped`) | Fix that plugin first |
| `plugin.register_failed` | The register function threw an error, called `fail()`, or added a command or action with a bad or taken name, or an action whose command no loaded plugin has | Read the message: it says which |
| `plugin.start_failed` | A function given to `onStart` threw an error or called `fail()` | Read the message |

Plugins that `require` or `use` each other in a loop load normally (see
[Plugins that use each other](sharing.md#plugins-that-use-each-other)).

`kh_cli plugins --plugins <folder>` shows every plugin with its state and the
message.

## Why the API version must match exactly

A C++ plugin is compiled against Khan's own classes. If those change, an old
plugin could crash Khan instead of failing politely. So Khan only loads plugins
built for its plugin API version, and you rebuild your plugin for each Khan
release, with the same compiler Khan uses (MSVC on Windows, Clang on macOS, GCC
on Linux). Python plugins, coming later, won't need rebuilding.
