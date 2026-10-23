# Plugins

A **plugin** adds features to Khan: new commands today, and later panels, tools
and file formats. Khan's own modeling commands come from a built-in plugin,
`kh.modeling`.

## See what's loaded

<!-- cli-example -->
```console
$ kh_cli plugins
loaded   kh.modeling 0.1.0  Modeling (built in)
           command feature.add_boolean
           command feature.add_box
           command feature.add_cylinder
           command feature.delete
           command feature.rename
```

## Use plugins from a folder

A plugin is a folder with a `plugin.json` file and a program file. Put plugin
folders inside one folder, and give that folder to `kh_cli` with `--plugins`.
The plugins built with Khan (like the example Hello plugin) are in
`build/dev/khan-plugins`:

<!-- cli-example -->
```console
$ kh_cli run --plugins build/dev/khan-plugins --save cube.khan example.hello.add_cube size=25
cube: 1 step
  Cube1        ok  volume 15625.00 mm^3
[example.hello] Hello saw a change: Add Cube1
wrote cube.khan
```

Lines in `[brackets]` are messages from plugins.

## Safety

A plugin runs inside Khan with the same access to your computer as Khan itself.
Only use plugins from people you trust, the same as any program you install.

## A plugin that doesn't load

Khan still starts, and [`kh_cli plugins`](../cli/plugins.md) shows the plugin as
`failed` with a message saying why and how to fix it. The most common reason is a
plugin made for a different version of Khan.

## Make your own

The [plugin developer guide](../plugin-guide/README.md) walks you through
writing one, starting with the Hello plugin.
