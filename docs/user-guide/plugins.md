# Plugins

A **plugin** adds features to SketchBillet: new commands today, and later panels, tools
and file formats. SketchBillet's own modeling commands come from a built-in plugin,
`sb.modeling`.

## See what's loaded

<!-- cli-example -->
```console
$ sketchbillet_cli plugins
loaded   sb.modeling 0.1.0  Modeling (built in)
           tier    foundation
           command document.set_units
           command feature.add_boolean
           command feature.add_box
           command feature.add_chamfer
           command feature.add_cylinder
           command feature.add_extrude
           command feature.add_fillet
           command feature.add_move
           command feature.add_sketch
           command feature.delete
           command feature.duplicate
           command feature.rename
           command feature.set_edges
           command feature.set_material
           command feature.set_value
           command feature.slide
           command feature.suppress
           command folder.add
           command folder.delete
           command folder.rename
           command material.define
           command material.delete
           command parameter.delete
           command parameter.rename
           command parameter.set
           command sketch.add_arc
           command sketch.add_circle
           command sketch.add_line
           command sketch.add_point
           command sketch.add_pole
           command sketch.add_rectangle
           command sketch.add_spline
           command sketch.constrain
           command sketch.delete
           command sketch.import_spline
           command sketch.reload_spline
           command sketch.remove_pole
           command sketch.round_corner
           command sketch.set_spline
           command sketch.set_value
           command sketch.set_weight
           command sketch.trim
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

## Use plugins from a folder

A plugin is a folder with a `plugin.json` file and a program file. Put plugin
folders inside one folder, and give that folder to `sketchbillet_cli` with `--plugins`.
The plugins built with SketchBillet (like the example Hello plugin) are in
`build/dev/sketchbillet-plugins`:

<!-- cli-example -->
```console
$ sketchbillet_cli run --plugins build/dev/sketchbillet-plugins --save cube.sb example.hello.add_cube size=25
cube: 1 step
  Cube1        ok  volume 15625.00 mm^3  mass 19.37 g (PLA)
[example.tape] The ruler shows lengths in in.
[example.tape] A 25.4 mm box is 1.00 in long.
[example.hello] Hello saw a change: Add Cube1
wrote cube.sb
```

Lines in `[brackets]` are messages from plugins.

## Safety

A plugin runs inside SketchBillet with the same access to your computer as SketchBillet itself.
Only use plugins from people you trust, the same as any program you install.

## A plugin that doesn't load

SketchBillet still starts, and [`sketchbillet_cli plugins`](../cli/plugins.md) shows the plugin as
`failed` with a message saying why and how to fix it. The most common reason is a
plugin made for a different version of SketchBillet.

Lines starting with `warning` are for the plugin's author: the plugin works, but
something about how it's put together is probably a mistake. You can keep using
it, and tell its author.

## Make your own

The [plugin developer guide](../plugin-guide/README.md) walks you through
writing one, starting with the Hello plugin.
