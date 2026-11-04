# Your first plugin: Hello

In this tutorial you build **Hello**, a plugin that:

- adds a command, `example.hello.add_cube`, that makes a cube, and
- notices every change to a design and writes a line in its log.

The finished plugin is in [`hello/`](hello/). SketchBillet's tests build and load that
exact code, so it always works with the current SketchBillet.

**You need:** SketchBillet building on your computer (see the main README) and a little
C++: functions, `std::string`, and what a lambda looks like.

## Step 1: Make a folder

Make a folder for your plugin outside the SketchBillet folder, for example
`C:/code/my-cube` (Windows) or `~/code/my-cube`. You will put three files in it:

```text
my-cube/
  plugin.json      tells SketchBillet about the plugin
  hello.cpp        the plugin's code
  CMakeLists.txt   tells CMake how to build it
```

Copy the three files from [`hello/`](hello/) to start.

## Step 2: Describe the plugin in plugin.json

<!-- guide-file: hello/plugin.json -->

```json
{
  "id": "example.hello",
  "name": "Hello",
  "version": "1.0.0",
  "description": "Adds a command that makes a cube, and notices every change.",
  "library": "hello",
  "requires": ["sb.modeling"],
  "tier": "tools"
}
```

- **id** is the plugin's permanent name: lower-case words joined by dots. Start it
  with your own name, like `sam.cube`. Ids starting with `sb.` are kept for SketchBillet.
- There's no **apiVersion** here: `sb_add_plugin` (step 4) adds it for you when it
  builds the plugin, using the version of the SketchBillet you build against. SketchBillet checks
  it and refuses a plugin made for a different version, with a message saying so.
- **library** is the name of the program file CMake builds: `hello.dll`,
  `hello.dylib` or `hello.so`, depending on your computer.
- **requires** lists plugins that must load first. Hello makes boxes, so it needs
  `sb.modeling`.
- **tier** says which layer of SketchBillet Hello belongs to. It's something the student
  does, so `tools`. It may use plugins in `tools` or `foundation` (like
  `sb.modeling`), and SketchBillet warns if it uses one in a higher tier.

Every key is explained in [plugin.json](manifest.md).

**Your turn:** change `id` to something like `yourname.cube` and `name` to
`Cube`. Change `library` to `cube`.

## Step 3: Write the code

Here is all of `hello.cpp`. The parts are explained below.

<!-- guide-file: hello/hello.cpp -->

```cpp
// The Hello plugin: adds one command that makes a cube, and notices every change.
// Walked through step by step in docs/plugin-guide/hello-plugin.md.

#include <memory>

#include "sb/model/Commands.h"
#include "sb/model/Document.h"
#include "sb/plugin/Api.h"
#include "sb/plugin/Events.h"
#include "sb/plugin/PluginContext.h"

using namespace sb;

namespace {

// Builds the command when someone runs "example.hello.add_cube".
std::unique_ptr<model::Command> makeAddCube(const model::Document& doc, const model::Args& args) {
    const double size = model::argNumber(args, "size", 20.0);  // mm
    auto cube = std::make_unique<model::BoxFeature>(doc.uniqueName("Cube"), kernel::Vec3{0, 0, 0},
                                                    kernel::Vec3{size, size, size});
    return std::make_unique<model::AddFeatureCommand>(std::move(cube));
}

void registerHello(plugin::PluginContext& ctx) {
    // 1. A command: name, label, description, and its arguments.
    ctx.addCommand(
        {"example.hello.add_cube",
         "Add cube",
         "Adds a cube at the origin.",
         {{"size", model::ArgType::Number, "Length of each side, in mm. Default 20 mm.", false, {}}}},
        &makeAddCube);

    // 2. A menu item in the desktop app that runs the command.
    ctx.addAction({.id = "example.hello.cube",
                   .text = "Cube (Hello)",
                   .menu = "Create",
                   .command = "example.hello.add_cube",
                   .tip = "Add a 20 mm cube from the Hello plugin."});

    // 3. An event listener: runs after every change to any design.
    ctx.listen<plugin::DocumentChanged>(
        [&ctx](const plugin::DocumentChanged& e) { ctx.log("Hello saw a change: " + e.label); });
}

}  // namespace

SB_PLUGIN_ENTRY(registerHello)
```

### The command

A command has two parts:

1. **A description**, given to `ctx.addCommand`: its name, the label people see,
   one plain sentence, and its **arguments**. Here there is one argument, `size`.
   It's a number, it isn't required (`false`), and its help text gives the unit
   and the default.
2. **A factory**, `makeAddCube`. SketchBillet calls it when someone runs the command, after
   checking the arguments. It reads `size` (20 mm if it was left out), makes a
   box step called `Cube1`, `Cube2`, ... (`uniqueName` picks a free name), and
   returns an `AddFeatureCommand` that adds it.

The factory doesn't change the design itself. It returns a command, and SketchBillet
runs it. That's what makes undo work for your plugin's steps, with no extra
code.

The command's name must start with your plugin's id and a dot. SketchBillet refuses a
plugin that breaks this rule, so two plugins can never pick the same name.

### The menu item

`ctx.addAction(...)` adds a menu item to the desktop app: **Create > Cube (Hello)**.
Clicking it runs the command `example.hello.add_cube` on the open design, with
no settings, so the cube is 20 mm. An action is plain data: an id, the words to
show, which menu, the command to run and a tip. It has no Qt code, so the same
plugin still works in `sketchbillet_cli`, which lists the action instead of showing it.

Like command names, action ids must start with your plugin's id and a dot.

### The listener

`ctx.listen<plugin::DocumentChanged>(...)` runs the lambda after every change to
any open design: a command, an undo or a redo. The event `e` says which design
changed (`e.doc`) and what happened (`e.label`, like `Add Cube1`). Hello just
writes a line in its log with `ctx.log`. The Plugin manager and
`sketchbillet_cli plugins` show the log.

Listeners only report. A listener that wants to change a design runs a command,
like everyone else.

### The entry point

`SB_PLUGIN_ENTRY(registerHello)` exports `registerHello` from the shared library
under the name SketchBillet looks for. It also exports the API version the plugin was
compiled with, so SketchBillet can check it matches.

**Your turn:** rename the command to `yourname.cube.add_cube` (it must start with
your new id), and give it a second argument: a `Vec3` called `at` for where the
cube goes. Read it with `model::argVec3(args, "at")`.

## Step 4: Tell CMake how to build it

<!-- guide-file: hello/CMakeLists.txt -->

```cmake
# The Hello plugin from docs/plugin-guide/hello-plugin.md.
sb_add_plugin(sb_hello_plugin
  MANIFEST plugin.json
  SOURCES hello.cpp
  DEPS sb::model
)
```

`sb_add_plugin` builds the shared library and puts it, with a copy of
`plugin.json`, in `<build folder>/sketchbillet-plugins/<your plugin id>/`.

**Your turn:** rename the target `sb_hello_plugin` to something of your own, like
`my_cube_plugin`. Two targets can't have the same name.

## Step 5: Build it

Go to the SketchBillet folder and build with your plugin folder added:

```bash
cmake --preset dev -DSB_EXTRA_PLUGINS=~/code/my-cube
cmake --build --preset dev
```

On Windows, after running `vsdev`:

```powershell
cmake --preset win -DSB_EXTRA_PLUGINS=C:/code/my-cube
cmake --build --preset win
```

Use forward slashes `/` in the path, even on Windows. To build more than one
plugin, separate the folders with `;`.

## Step 6: Try it

List the plugins SketchBillet finds:

```bash
./build/dev/apps/sketchbillet_cli/sketchbillet_cli plugins --plugins build/dev/sketchbillet-plugins
```

```text
loaded   sb.modeling 0.1.0  Modeling (built in)
           command feature.add_boolean
           ...
loaded   example.hello 1.0.0  Hello
           command example.hello.add_cube
           action  Create > Cube (Hello)  (example.hello.add_cube)
```

Run the command, and save the result as a design:

```bash
./build/dev/apps/sketchbillet_cli/sketchbillet_cli run --plugins build/dev/sketchbillet-plugins --save cube.sb example.hello.add_cube size=25
```

```text
cube: 1 step
  Cube1        ok  volume 15625.00 mm^3
[example.hello] Hello saw a change: Add Cube1
wrote cube.sb
```

25 mm × 25 mm × 25 mm = 15 625 mm³, so the cube is the right size.

Use your own plugin's id and command name in place of `example.hello`. If your
plugin shows `failed`, the line under it says why and how to fix it.

## What's next

- [What a plugin can do](extension-points.md): every kind of thing a plugin can
  add today, and the events it can listen to.
- Look at `plugins/modeling/` in the SketchBillet source. It's the plugin that adds boxes
  and cylinders, and it's only a few lines long.
