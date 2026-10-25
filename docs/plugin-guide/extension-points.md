# What a plugin can do

Everything a plugin does goes through the `PluginContext` that Khan hands to its
register function. This page lists what that context offers today, with a short
example of each. The API reference (`kh::plugin::PluginContext`) has every detail.

## Add commands

A command is anything a student can do to a design: add a step, change a number,
delete a step. Menus, scripts and the AI assistant all run commands by name, so a
command you add is usable everywhere.

```cpp
ctx.addCommand(
    {"sam.cube.add_cube",                // name: starts with your plugin id
     "Add cube",                         // label people see
     "Adds a cube at the origin.",       // one plain sentence
     {{"size", model::ArgType::Number,   // arguments
       "Length of each side, in mm. Default 20 mm.", false, {}}}},
    &makeAddCube);                       // builds the command
```

Argument types:

| `ArgType` | Value in C++ | On the `kh_cli run` command line | Read it with |
| --- | --- | --- | --- |
| `Number` | `double`, in mm or radians | `size=25` | `model::argNumber` |
| `Integer` | `std::int64_t` | `count=3` | `std::get<std::int64_t>` |
| `Bool` | `bool` | `hollow=true` | `std::get<bool>` |
| `Text` | `std::string` | `name=Lid` | `model::argText` |
| `Choice` | `std::string`, one of `choices` | `operation=cut` | `model::argText` |
| `Vec3` | `kernel::Vec3`, in mm | `at=10,0,5` | `model::argVec3` |
| `FeatureRef` | `FeatureId` of a step | `a=Box1` | `model::argFeature` |

Khan checks the arguments before calling your factory: unknown names, missing
required arguments, wrong types, choices and steps that don't exist all get a
plain message. Your factory still checks what only it knows (a size of 0 mm, say)
when its command runs.

Commands can only be added while the register function runs.

## Add menu items and toolbar buttons

An **action** is a menu item, and optionally a toolbar button, that runs a
command by name. Actions are plain data with no Qt code, so a plugin that adds
them still works in `kh_cli`.

```cpp
ctx.addAction({.id = "sam.cube.cube",           // starts with your plugin id
               .text = "Cube",                  // the words on the item
               .menu = "Create",                // made if it doesn't exist
               .command = "sam.cube.add_cube",  // the command it runs
               .args = {{"size", 30.0}},        // fixed settings for the command
               .shortcut = "Ctrl+Shift+U",
               .toolbar = true,
               .tip = "Add a 30 mm cube."});
```

**Settings from the selection.** List argument names in `fromSelection`, and
Khan fills them with the selected steps, in the order they were picked. The
item is only enabled when exactly that many steps are selected. This is how
`kh.modeling`'s **Combine > Cut** works:

```cpp
ctx.addAction({.id = "kh.modeling.cut", .text = "Cut", .menu = "Combine",
               .command = "feature.add_boolean",
               .args = {{"operation", std::string("cut")}},
               .fromSelection = {"a", "b"},
               .tip = "Cut the second selected body out of the first."});
```

Khan refuses a plugin whose action has a badly formed id, an id another action
already has, or a command no loaded plugin has. Like commands, actions can only
be added while the register function runs.

## Use the selection

`ctx.selection()` is what the student has picked, shared by every panel and view.

```cpp
auto& sel = ctx.selection();
if (sel.features().size() == 1) {
    const auto* step = ctx.documents().find(sel.doc())->find(sel.features()[0]);
}
sel.set(docId, {boxId});   // select the box everywhere
```

Every change sends `SelectionChanged`. The selection keeps itself valid: steps
that are deleted or undone drop out, and closing the design clears it.

## Use the grid (desktop app only)

The desktop app shares one grid, provided as the `kh.grid` service. A tool that
lets the student place points with the mouse snaps them through it, so it obeys
**View > Snap to grid** like everything else:

```cpp
#include "kh/plugin/Grid.h"

if (auto* grid = ctx.service<kh::plugin::Grid>()) {
    kernel::Vec3 p = grid->snap(pointUnderMouse);  // unchanged when snapping is off
    const auto& g = grid->settings();               // visible, spacing (mm), snap, style
}
```

`kh_cli` has no grid, so the service is `nullptr` there. Change the grid with
`setVisible`, `setSpacing` (0.01 mm to 1000 mm; outside that it returns a
problem and changes nothing), `setSnap` and `setStyle`. Each change sends
`GridChanged`.

## Listen to events

Events tell every part of Khan what just happened, so a change made in one place
updates every panel and view.

```cpp
ctx.listen<plugin::RebuildFinished>([&ctx](const plugin::RebuildFinished& e) {
    const auto* report = ctx.documents().lastRebuild(e.doc);
    // ... show which steps built ...
});
```

| Event | Sent when | Carries |
| --- | --- | --- |
| `DocumentOpened` | A design is made or opened | `doc` |
| `DocumentClosed` | A design is closed | `doc` |
| `ActiveDocumentChanged` | The student switches to another design | `doc` (nil if none is open) |
| `DocumentChanged` | A command runs, or a step is undone or redone | `doc`, `kind` (`Executed`, `Undone`, `Redone`), `label` |
| `RebuildFinished` | A design was rebuilt after a change | `doc`, `ok` (did every step build?) |
| `SelectionChanged` | The selection changes | `doc`, `features` (in picking order) |
| `GridChanged` | The grid settings change (desktop app) | `grid`: visible, spacing in mm, snap, style |

Things to know:

- **Events carry ids, never pointers.** Look things up with
  `ctx.documents().find(e.doc)`.
- **Events arrive in order, one at a time.** If a listener sends an event, it
  arrives after every listener has seen the current one.
- **Listeners only report.** To change a design, run a command.
- **A listener that throws doesn't break Khan.** The other listeners still get
  the event, and the problem is recorded.
- `ctx.listen` keeps the listener until Khan closes. For a listener you want to
  stop, keep the `Subscription` from `ctx.events().subscribe<E>(...)`; it
  disconnects when destroyed.

## Read and change open designs

`ctx.documents()` is the list of open designs.

```cpp
auto& docs = ctx.documents();
DocId id = docs.active();                        // the design the student is on
const model::Document* doc = docs.find(id);      // read it
docs.run(id, "feature.add_box", {{"size", kernel::Vec3{10, 10, 10}}});  // change it
docs.undo(id);
```

You can read a design directly, but you change it only with `run`, `execute`,
`undo` and `redo`. Each change sends `DocumentChanged`, rebuilds the design and
sends `RebuildFinished`.

To ask about a body's size and mass, use the kernel the designs are built with
and the body's material:

```cpp
#include "kh/model/Material.h"

const auto* report = docs.lastRebuild(id);
const auto* shape = report ? report->shapeOf(stepId) : nullptr;
if (shape && docs.kernel()) {
    const auto& material = doc->materialOf(stepId);  // PLA unless one was picked
    const auto mass = model::bodyMass(*docs.kernel(), *shape, material);
    ctx.log("It weighs " + std::to_string(mass.grams) + " g");  // also volume, newtons, cost, center
}
```

## Add a panel (desktop app only)

A panel is a dockable window, like the feature tree. Panels are Qt widgets, so
they use `kh_ui`, the Qt side of the plugin API. Only the desktop app provides
it: ask for the `UiRegistry` service, and add panels only when it's there. In
`kh_cli` the same plugin still adds its commands and actions.

```cpp
#include "kh/ui/UiRegistry.h"

void registerNotes(kh::plugin::PluginContext& ctx) {
    auto* ui = ctx.service<kh::ui::UiRegistry>();
    if (!ui) return;  // not in the desktop app
    ui->addPanel({"sam.notes.panel",               // starts with your plugin id
                  QObject::tr("Notes"),            // the tab's title
                  kh::ui::PanelArea::Right,        // where it docks at first
                  [&ctx] { return new NotesPanel(ctx); }});
}
```

- Khan calls the function when the main window opens and owns the widget it
  returns. The student can move the panel anywhere; Khan remembers where.
- Keep the panel up to date by listening to events (`ctx.events().subscribe`,
  keeping the `Subscription` as a member, so it disconnects when the panel
  closes).
- Change designs only through `ctx.documents()`, like everywhere else.
- `ui->showProblems(problems)` shows what went wrong in the status bar.
- **More than one?** Set `allowMultiple` and the **Window** menu gets
  **New** *your panel's title*. Each one is made by its own call to your function,
  so keep its state in the widget, not in the plugin. Copies aren't reopened when
  Khan restarts.

  ```cpp
  kh::ui::PanelSpec spec{"sam.notes.panel", QObject::tr("Notes"), kh::ui::PanelArea::Right,
                         [&ctx] { return new NotesPanel(ctx); }};
  spec.allowMultiple = true;  // Window > New notes
  ui->addPanel(std::move(spec));
  ```

- `PanelArea::Center` puts the panel in the middle of the window. The first
  Center panel (Khan's viewer) fills it; later ones become tabs next to it.
- In CMake, link `kh::ui` as well: `DEPS kh::model kh::ui`.

**Worked examples:** the built-in [feature tree](../../plugins/featuretree/FeatureTreePlugin.cpp),
[Properties](../../plugins/properties/PropertiesPlugin.cpp) and
[viewer](../../plugins/viewer/ViewerPlugin.cpp) panels are plugins written exactly
this way. Khan's app tests click through them.

## Share something with other plugins

A plugin can offer a service other plugins use, and send events of its own. The
whole story, with two working plugins, is on
[Share something with other plugins](sharing.md). In short:

```cpp
ctx.provide<example::ruler::IRuler>(ruler);           // offer it, while registering
ctx.onStart([&ctx] {                                  // after every plugin registered
    if (auto* r = ctx.service<example::ruler::IRuler>()) { /* use it */ }
    ctx.publish(MyEvent{});                           // MyEvent::kType starts with your id
});
```

List the plugins you use in `uses` (or `requires`) in `plugin.json`, or Khan
warns you.

## Log, or refuse to start

```cpp
ctx.log("Found 3 printers.");               // shown in the Plugin manager and kh_cli plugins
ctx.fail("This plugin needs a 3D printer connected. Connect one and restart Khan.");
```

Call `fail` from the register function (or an `onStart` function) when your plugin can't work. When the
register function returns, Khan removes the plugin's commands and listeners and
shows your message, so write it for a student and say how to fix it. Throwing an
exception has the same effect, but your message is better.

## Coming later

These are planned and will be added to this guide as they're built:

| Extension point | What you'll add | When |
| --- | --- | --- |
| Plugin settings, check-mark menu items, status-bar items | Settings Khan remembers for your plugin, and menu items with a check mark | Next |
| View overlays, tiers | Drawing in the 3D view; saying which layer your plugin belongs to | Next |
| Ribbon tabs | Grouping actions into tabs | Later |
| Viewer tools | A way to use the mouse in the 3D view, with a live preview | With the sketcher, Phase 1 |
| Feature types | A new kind of step that saves, loads and rebuilds | Phase 2 |
| File formats | Importers and exporters | Phase 2 |
| Assistant tools, lesson checks | Things the AI assistant and lessons can use | Phase 3 |
| Python plugins | All of the above, without a compiler | Later |
