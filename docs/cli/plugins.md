# kh_cli plugins

Lists the plugins Khan finds: which loaded, the commands, menu items
(**actions**) and **services** each one adds, why any didn't load, and warnings
about things that work but are probably a mistake.

An action line reads `menu > item  (command)`: in the desktop app, that menu item
runs that command.

```text
kh_cli plugins [--plugins <folder>]... [--slow-start-ms <n>] [--strict]
```

| Option | Meaning |
| --- | --- |
| `--plugins <folder>` | A folder of plugins to load: each plugin is a subfolder with a `plugin.json`. Can be given more than once |
| `--slow-start-ms <n>` | Warn about a plugin that takes longer than `n` milliseconds to start (default 500; 0 turns the warning off). `kh_cli run` and `kh_cli help` take it too |
| `--strict` | Treat warnings as failures: exit with 1 if any plugin has a warning. For plugin authors' tests and CI |

Under each plugin, Khan lists its `tier` (see
[Tiers](../plugin-guide/manifest.md#tiers)), any tiers it `adds`, and the
commands, services and menu items (`action`) it adds.

Each problem line starts with how serious it is:

| Word | Meaning |
| --- | --- |
| `error` | The plugin didn't load. The message says why and what to do |
| `warning` | The plugin loaded, but something is probably a mistake, like an event named after another plugin. See [Warnings](../plugin-guide/sharing.md#warnings) |
| `note` | Worth knowing, nothing wrong: for example, a plugin it `uses` isn't installed |

Khan's built-in plugins, like `kh.modeling`, are always loaded.

## Examples

Only the built-in plugins:

<!-- cli-example -->
```console
$ kh_cli plugins
loaded   kh.modeling 0.1.0  Modeling (built in)
           tier    foundation
           command document.set_units
           command feature.add_boolean
           command feature.add_box
           command feature.add_cylinder
           command feature.add_extrude
           command feature.add_move
           command feature.add_sketch
           command feature.delete
           command feature.duplicate
           command feature.rename
           command feature.set_material
           command feature.suppress
           command folder.add
           command folder.delete
           command folder.rename
           command material.define
           command material.delete
           command sketch.add_arc
           command sketch.add_circle
           command sketch.add_line
           command sketch.add_point
           command sketch.constrain
           command sketch.delete
           command sketch.set_value
           action  Create > Box  (feature.add_box)
           action  Create > Cylinder  (feature.add_cylinder)
           action  Combine > Union  (feature.add_boolean)
           action  Combine > Cut  (feature.add_boolean)
           action  Combine > Intersect  (feature.add_boolean)
           action  Modify > Move  (feature.add_move)
           action  Create > Extrude  (feature.add_extrude)
           action  Edit > Duplicate step  (feature.duplicate)
           action  Edit > Turn off  (feature.suppress)
           action  Edit > Turn on  (feature.suppress)
           action  Edit > New folder  (folder.add)
           action  Edit > Delete step  (feature.delete)
```

With the plugins in your build folder (the plugin guide's Hello plugin is one):

<!-- cli-example -->
```console
$ kh_cli plugins --plugins build/dev/khan-plugins
loaded   example.ruler 1.0.0  Ruler
           tier    foundation
           service example.ruler/1
           action  View > Ruler shows inches  (check mark)
loaded   example.tape 1.0.0  Tape
           tier    tools
           log: The ruler shows lengths in in.
           log: A 25.4 mm box is 1.00 in long.
loaded   kh.modeling 0.1.0  Modeling (built in)
           tier    foundation
           command document.set_units
           command feature.add_boolean
           command feature.add_box
           command feature.add_cylinder
           command feature.add_extrude
           command feature.add_move
           command feature.add_sketch
           command feature.delete
           command feature.duplicate
           command feature.rename
           command feature.set_material
           command feature.suppress
           command folder.add
           command folder.delete
           command folder.rename
           command material.define
           command material.delete
           command sketch.add_arc
           command sketch.add_circle
           command sketch.add_line
           command sketch.add_point
           command sketch.constrain
           command sketch.delete
           command sketch.set_value
           action  Create > Box  (feature.add_box)
           action  Create > Cylinder  (feature.add_cylinder)
           action  Combine > Union  (feature.add_boolean)
           action  Combine > Cut  (feature.add_boolean)
           action  Combine > Intersect  (feature.add_boolean)
           action  Modify > Move  (feature.add_move)
           action  Create > Extrude  (feature.add_extrude)
           action  Edit > Duplicate step  (feature.duplicate)
           action  Edit > Turn off  (feature.suppress)
           action  Edit > Turn on  (feature.suppress)
           action  Edit > New folder  (folder.add)
           action  Edit > Delete step  (feature.delete)
loaded   example.hello 1.0.0  Hello
           tier    tools
           command example.hello.add_cube
           action  Create > Cube (Hello)  (example.hello.add_cube)
```

## When a plugin doesn't load

A plugin that can't load shows `failed` or `skipped`, with a line saying why and
how to fix it, and `kh_cli plugins` exits with code 1. Khan's own tests keep some
broken plugins to check this:

<!-- cli-example exit=1 -->
```console
$ kh_cli plugins --plugins build/dev/test-plugins
loaded   kh.modeling 0.1.0  Modeling (built in)
...
failed   test.badname 1.0.0  Test badname
           error [plugin.register_failed]: The Test badname plugin couldn't start. Its command feature.add_box must start with "test.badname.". Ask its author for a fixed version. (test.badname)
failed   test.noentry 1.0.0  Test noentry
           error [plugin.library_bad]: The Test noentry plugin's program file isn't a Khan plugin (it has no KH_PLUGIN_ENTRY). Ask its author for a fixed version. (...)
failed   test.oldapi 1.0.0  Test oldapi
           error [plugin.api_version]: The Test oldapi plugin was built for a different version of Khan (plugin API 0; this Khan uses 19). Get a version of the plugin made for this Khan. (...)
```

Every reason and its fix is listed in the plugin guide's
[plugin.json page](../plugin-guide/manifest.md#when-khan-refuses-a-plugin).

The `test.warns` plugin above sends an event named after another plugin, so it
loads with a warning. With `--strict`, that warning makes `kh_cli plugins` fail,
which is what a plugin author's CI wants:

<!-- cli-example exit=1 -->
```console
$ kh_cli plugins --plugins build/dev/test-plugins --strict
loaded   kh.modeling 0.1.0  Modeling (built in)
...
loaded   test.warns 1.0.0  Test warns
           warning [plugin.event_name]: Test warns sent the event test.other.ping. Event names must start with the plugin id, like test.warns.something, so they don't clash.
...
```

The slow-start limit must be a whole number of milliseconds:

<!-- cli-example exit=2 -->
```console
$ kh_cli plugins --slow-start-ms soon
error: --slow-start-ms needs a whole number of milliseconds, like 500 (0 turns the warning off).
```

To write your own plugin, start with the
[plugin developer guide](../plugin-guide/README.md).
