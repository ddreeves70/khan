# kh_cli plugins

Lists the plugins Khan finds: which loaded, the commands and menu items
(**actions**) each one adds, and why any didn't load.

An action line reads `menu > item  (command)`: in the desktop app, that menu item
runs that command.

```text
kh_cli plugins [--plugins <folder>]...
```

| Option | Meaning |
| --- | --- |
| `--plugins <folder>` | A folder of plugins to load: each plugin is a subfolder with a `plugin.json`. Can be given more than once |

Khan's built-in plugins, like `kh.modeling`, are always loaded.

## Examples

Only the built-in plugins:

<!-- cli-example -->
```console
$ kh_cli plugins
loaded   kh.modeling 0.1.0  Modeling (built in)
           command feature.add_boolean
           command feature.add_box
           command feature.add_cylinder
           command feature.delete
           command feature.rename
           action  Create > Box  (feature.add_box)
           action  Create > Cylinder  (feature.add_cylinder)
           action  Combine > Union  (feature.add_boolean)
           action  Combine > Cut  (feature.add_boolean)
           action  Combine > Intersect  (feature.add_boolean)
           action  Edit > Delete step  (feature.delete)
```

With the plugins in your build folder (the plugin guide's Hello plugin is one):

<!-- cli-example -->
```console
$ kh_cli plugins --plugins build/dev/khan-plugins
loaded   kh.modeling 0.1.0  Modeling (built in)
           command feature.add_boolean
           command feature.add_box
           command feature.add_cylinder
           command feature.delete
           command feature.rename
           action  Create > Box  (feature.add_box)
           action  Create > Cylinder  (feature.add_cylinder)
           action  Combine > Union  (feature.add_boolean)
           action  Combine > Cut  (feature.add_boolean)
           action  Combine > Intersect  (feature.add_boolean)
           action  Edit > Delete step  (feature.delete)
loaded   example.hello 1.0.0  Hello
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
           error [plugin.api_version]: The Test oldapi plugin was built for a different version of Khan (plugin API 0; this Khan uses 3). Get a version of the plugin made for this Khan. (...)
```

Every reason and its fix is listed in the plugin guide's
[plugin.json page](../plugin-guide/manifest.md#when-khan-refuses-a-plugin).

To write your own plugin, start with the
[plugin developer guide](../plugin-guide/README.md).
