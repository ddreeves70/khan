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
| `FeatureList` | `std::vector<FeatureId>`, one or more steps | `steps=Box1,Hole` | `model::argFeatures` |
| `FolderRef` | `FolderId` of a folder in the feature tree | `folder=Holes` | `model::argFolder` |
| `Vec2` | `sketch::Point2`, a point on a sketch's plane, in mm | `at=10,5` | `model::argVec2` |

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

**All the selected steps.** For a command that works on any number of steps,
give a `FeatureList` argument's name in `fromSelectionList`. Khan fills it with
every selected step, in the order they were picked, and enables the item when one
or more steps are selected. This is how **Edit > Turn off** works:

```cpp
ctx.addAction({.id = "kh.modeling.turn_off", .text = "Turn off", .menu = "Edit",
               .command = "feature.suppress",
               .args = {{"off", true}},
               .fromSelectionList = "steps",
               .tip = "Turn the picked steps off, so they aren't built. Nothing is deleted."});
```

Use `fromSelection` or `fromSelectionList`, not both.

**Toolbar buttons.** `.toolbar = true` also puts the item on the toolbar. If its
words are too long for a button, give shorter ones in `buttonText`: the
sketcher's **New sketch on XY (the ground)** is just **New sketch** on the
toolbar.

**Toolbar modes.** Some work needs its own buttons, and the usual ones only get in
the way. While a sketch is being edited, the toolbar shows only Line, Circle,
Arc, Point and Finish sketch. To do this, give your buttons a `toolbarMode`, and
switch to that mode with `ctx.setToolbarMode()` while the work goes on:

```cpp
ctx.addAction({.id = "sam.paint.brush", .text = "Brush", .menu = "Paint",
               .toolbar = true,
               .toolbarMode = "sam.paint.painting",  // shown only in this mode
               .run = [] { /* pick the brush */ }});
// When painting starts:
ctx.setToolbarMode("sam.paint.painting");
// When it stops:
ctx.setToolbarMode("");  // back to the usual toolbar
```

- Buttons without a `toolbarMode` show only when no mode is on. Khan's own New,
  Open, Save, Undo and Redo always show.
- Only one mode is on at a time. Khan sends `ToolbarModeChanged` when it switches.
- Mode names start with your plugin id and a dot, like event names; otherwise
  Khan warns.
- The menus don't change: every item stays in its menu, in every mode.

**Submenus.** A `/` in `menu` makes a submenu: `.menu = "View/Grid style"` puts
the item in the **Grid style** submenu of **View**.

Khan refuses a plugin whose action has a badly formed id, an id another action
already has, or a command no loaded plugin has. Like commands, actions can only
be added while the register function runs.

## Check-mark items, and items that run your own code

Some menu items don't change a design: they turn something on or off, or open a
dialog. Give them a `run` function instead of a command. Add `checked` and the
item gets a check mark whenever it returns `true`. (For things the viewer shows,
use view options instead, described below: they make these items for you.)

```cpp
auto& settings = ctx.settings();
ctx.addAction({.id = "sam.notes.show_dates",
               .text = "Show dates on notes",
               .menu = "View",
               .tip = "Write the date next to every note.",
               .run = [&settings] { settings.set("dates", !settings.flag("dates")); },
               .checked = [&settings] { return settings.flag("dates"); }});
```

- Khan asks `checked` when the menu opens, after every change to a design, and
  whenever a setting changes, so the check mark is always right.
- `currentText` (optional) gives the item's words the same way, for items like
  **Other (12.5 mm)...** whose words change.
- `enabled` (optional) greys the item out whenever it returns `false`, so a
  student can't pick something that can't work right now. Its toolbar button
  greys out too. Khan asks it at the
  same times as `checked`. If it depends on something only your plugin knows
  (like "a sketch is being edited"), send `plugin::ActionsChanged{}` with
  `ctx.publish` when that changes, and Khan asks again:

```cpp
static bool recording = false;  // something only this plugin knows
ctx.addAction({.id = "sam.notes.stop",
               .text = "Stop recording",
               .menu = "View",
               .run = [&ctx] {
                   recording = false;
                   ctx.publish(plugin::ActionsChanged{});
               },
               .enabled = [] { return recording; }});
```

- An item with `enabled` that throws an exception is greyed out.
- An item has a command *or* a `run` function, never both and never neither;
  Khan refuses the plugin otherwise.
- Changes to designs still go through commands. A `run` function that wants to
  change a design runs a command with `ctx.documents()`, so undo works.
- If `run` throws an exception, Khan shows "*item* stopped with an error" in the
  status bar and keeps going.
- In `kh_cli plugins` these items show `(check mark)` or `(runs plugin code)`
  instead of a command name.

## Remember settings

`ctx.settings()` is your plugin's own settings. The desktop app keeps them between
runs, with Khan's own settings; `kh_cli` keeps them only while it runs.

```cpp
auto& settings = ctx.settings();
double spacing = settings.number("spacing", 10.0);  // mm; 10 if never set
bool dates = settings.flag("dates");                // false if never set
std::string name = settings.text("name", "Sam");

settings.set("spacing", 12.5);  // numbers, text or true/false
settings.remove("name");        // back to the fallback
```

- Names are yours alone: Khan stores `spacing` as `plugins/sam.notes/spacing`, so
  two plugins can both have a `spacing`.
- Numbers are saved with a `.` as the decimal point on every computer, and
  read back exactly.
- Changing a setting sends a `kh.SettingChanged` event with the plugin id and the
  name, only if the value really changed. Listen to it to update what you show:

  ```cpp
  ctx.listen<kh::plugin::SettingChanged>([](const kh::plugin::SettingChanged& e) {
      if (e.plugin == "sam.notes" && e.key == "dates") { /* redraw the notes */ }
  });
  ```

- Store your own settings only. Read other plugins' settings through a
  [service](sharing.md) they offer, so they can change how they keep them.

The [Ruler example](sharing.md#step-2-offer-it) uses a setting and a check-mark
item together.

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

## Add view options (desktop app only)

If your plugin draws something in the viewer that the student may want to turn on
or off or adjust, make it a **view option**. The `kh.viewoptions` plugin then
remembers it, checks it, adds its View menu items, and shows it in the **View
options** panel next to the grid and the axes. You don't write any menu or settings
code.

List `kh.viewoptions` in `requires` in your `plugin.json` (`"requires":
["kh.viewoptions"]`). Because it's required, Khan loads it first, so you can look
up its service while registering:

```cpp
#include "kh/viewoptions/ViewOptions.h"

void registerNotes(kh::plugin::PluginContext& ctx) {
    auto* options = ctx.service<kh::viewoptions::IViewOptions>();
    if (!options) return;  // not in the desktop app
    using kh::viewoptions::OptionKind;
    kh::viewoptions::addOption(ctx, *options,
        {.id = "sam.notes.show", .text = "Show notes", .group = "Notes",
         .kind = OptionKind::Flag, .initial = true});
    kh::viewoptions::addOption(ctx, *options,
        {.id = "sam.notes.size", .text = "Note size", .group = "Notes",
         .kind = OptionKind::Length, .initial = 5.0, .min = 1, .max = 50,
         .presets = {2, 5, 10}});

    // Redraw when one of your options changes.
    ctx.listen<kh::viewoptions::ViewOptionChanged>([&ctx, options](const auto& e) {
        if (e.option.rfind("sam.notes.", 0) == 0) { /* options->flag("sam.notes.show") ... */ }
    });
}
```

| Kind | Holds | In the View menu |
| --- | --- | --- |
| `Flag` | On or off (`bool`) | A check-mark item (with `shortcut`, if you give one) |
| `Choice` | One of `choices` (each a `value` that's stored and a `text` that's shown) | A submenu named after the option, one item each |
| `Length` | A length in mm between `min` and `max` | A submenu of `presets` ("5 mm") and **Other...**, which asks for any value |
| `Number` | A number between `min` and `max` | Like Length, without "mm" |

- Read values with `options->flag(id)`, `number(id)` or `choice(id)`; change them
  with `options->set(id, value)`. A value outside the limits, or not one of the
  choices, is refused with a plain message, and nothing changes.
- Option ids start with your plugin's id and a dot. The menu items `addOption`
  makes are yours too, with ids built from the option's: `sam.notes.show`,
  `sam.notes.size.5`, `sam.notes.size.other`.
- Every change sends `kh.viewoptions.changed` (`ViewOptionChanged`) with the
  option's id.
- Khan keeps the values with the app's settings, under
  `plugins/kh.viewoptions/<option id>`.

**Panes.** Every option has an all-panes value, and the student can give one pane
its own value (right-click in the pane, or the panel's "Show options for"). If what
you draw depends on an option, draw it for each pane that has its own value:

```cpp
// The pane's own value if it has one, otherwise the all-panes value.
bool show = options->flag("sam.notes.show", pane);
// Which panes exist, and whether one has its own value.
for (const auto& p : options->panes()) {
    if (options->hasOwn("sam.notes.show", p.key)) {
        ctx.overlays().set("notes", notesOverlay(options->flag("sam.notes.show", p.key)), p.key);
    }
}
```

- An overlay set with a pane's key is drawn in that pane **instead of** the
  every-pane overlay with the same name; remove it with `remove("notes", pane)`
  when the pane follows all panes again.
- `ViewOptionChanged` has a `pane`: empty when the all-panes value changed, a
  pane's key when only that pane's value did. `PanesChanged` is sent when panes come
  and go, are renamed, or the student clicks into another pane
  (`options->activePane()`).
- `options->set(id, value)` changes every pane and clears the panes' own values;
  `options->setInPane(id, value, pane)` changes one pane.

**Worked examples:** the built-in [axes](../../plugins/axes/AxesPlugin.cpp)
(two Flags) and [grid](../../plugins/grid/GridPlugin.cpp) (Flags, a Choice and a
Length) plugins.

## Use the grid (desktop app only)

The grid is a plugin too: `kh.grid`. It offers the grid to other plugins as the
`kh.grid/2` service, described by its contract header `kh/grid/Grid.h`. A tool
that lets the student place points with the mouse snaps them through it, so it
obeys **View > Snap to grid** like everything else.

List `kh.grid` in `uses` in your `plugin.json` (`"uses": ["kh.grid"]`), then look
the grid up when your plugin starts:

```cpp
#include "kh/grid/Grid.h"

ctx.onStart([&ctx] {
    if (auto* grid = ctx.service<kh::grid::IGrid>()) {
        // The pane the point was picked in: a pane can have its own grid.
        kernel::Vec3 p = grid->snap(pointUnderMouse, pane);  // unchanged when snapping is off there
        const auto g = grid->settings(pane);                 // visible, spacing (mm), snap, style
    }
});
ctx.listen<kh::grid::GridChanged>([](const kh::grid::GridChanged& e) { /* e.grid.spacing */ });
```

`kh_cli` doesn't load `kh.grid`, so the service is `nullptr` there. The student
changes the grid with the View menu and the View options panel; other plugins only
read it. A pane's key comes from the view options (`options->activePane()` is the
pane the student is working in). `GridChanged` has a `pane` too: empty when the
all-panes grid changed.

## Draw with the mouse (desktop app only)

A **view tool** lets the student draw with the mouse on a plane, the way the
sketcher's Line and Circle tools do. The viewer (`kh.viewer`) offers it as the
`kh.viewer/1` service, described by its contract header `kh/viewer/Viewer.h`.
While your tool runs, a left click in any pane tells you the point on the plane
(in mm) instead of turning the view or picking; moving the mouse tells you too, so
you can draw a preview with an overlay. Panning and zooming still work, and Escape
calls `cancelled()`.

List `kh.viewer` in `uses` (or `requires`), then start your tool when the student
asks for it, for example from a menu item:

```cpp
#include "kh/viewer/Viewer.h"

struct DotTool final : kh::viewer::IViewTool {
    kh::plugin::PluginContext& ctx;
    explicit DotTool(kh::plugin::PluginContext& c) : ctx(c) {}
    void moved(const kh::viewer::ToolPoint&) override {}
    void clicked(const kh::viewer::ToolPoint& p) override {
        // p.at is the point in mm; p.pixel is how many mm one screen pixel covers there,
        // for "close enough"; p.pane is the pane, for the grid: snap with grid->snap(p.at, p.pane).
        kh::plugin::Overlay dot{.shape = kh::plugin::OverlayShape::Points, .points = {p.at}, .size = 8};
        ctx.overlays().set("dot", dot);
    }
    void cancelled() override {  // Escape
        if (auto* viewer = ctx.service<kh::viewer::IViewer>()) viewer->stopTool(*this);
    }
};

auto tool = std::make_shared<DotTool>(ctx);
ctx.addAction({.id = "sam.dots.place", .text = "Place a dot", .menu = "Tools",
               .run = [&ctx, tool] {
                   auto* viewer = ctx.service<kh::viewer::IViewer>();
                   if (!viewer) return;
                   const kh::viewer::ToolPlane ground{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};  // XY
                   viewer->face(ground);              // look straight down at it
                   viewer->startTool(*tool, ground);  // clicks now come to the tool
               }});
```

One tool runs at a time: starting yours stops the one that was running. Stop it
with `stopTool()` when you're done, so clicks pick and turn the view again.
`kh_cli` has no viewers, so nothing calls your tool there.

## Draw in the viewer

An **overlay** is something every viewer draws over the design: lines, dots, or
a lattice like graph paper. It's plain data in mm, so your plugin needs no Qt or
3D code, and the viewer doesn't need to know what it means. This is how the grid
is drawn.

```cpp
kh::plugin::Overlay guide;
guide.points = {{0, 0, 0}, {50, 0, 0}, {0, 0, 0}, {0, 50, 0}};  // two 50 mm lines
guide.color = {230, 120, 20};                                 // orange
guide.size = 2;                                               // 2 pixels wide
ctx.overlays().set("guides", guide);  // its id is "sam.notes.guides"
```

| Shape | What it draws | Fill in |
| --- | --- | --- |
| `OverlayShape::Lines` | A line between each pair of points | `points` (in mm) |
| `OverlayShape::Points` | A dot at each point | `points` (in mm) |
| `OverlayShape::Lattice` | Lines every `spacing` mm on a plane, or dots where they cross | `lattice`: `origin`, `xAxis`, `yAxis`, `spacing` and `halfSize` in mm, `dots` |
| `OverlayShape::Labels` | Words that always face the screen, like the axes' X, Y and Z | `points` (in mm) and `texts`, one word for each point; `size` is the letters' height in pixels |

- Calling `set` again with the same name replaces the overlay; `remove("guides")`
  takes it away; `visible = false` hides it.
- `color` has an `a` part for see-through overlays (0 to 255); `size` is the line
  width or dot size in screen pixels.
- Overlays are never picked with the mouse and don't count for **Fit all**.
- Every change sends `kh.OverlaysChanged`, which the viewer listens to.
- If your plugin fails to start, Khan removes its overlays.

**Worked example:** the built-in [grid plugin](../../plugins/grid/GridPlugin.cpp)
draws the grid with one Lattice overlay, and also uses settings, check-mark menu
items, submenus, a status-bar item and a service. It uses only the public plugin
API, so an outside plugin could replace it. Khan's app tests click through it.
The built-in [axes plugin](../../plugins/axes/AxesPlugin.cpp) is a smaller
example: three Lines overlays and three Labels overlays, two check-mark View
items and two settings, with no Qt at all.

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
| `kh::viewoptions::ViewOptionChanged` | A view option changes (sent by `kh.viewoptions`, desktop app) | `option` (its id) |
| `kh::grid::GridChanged` | The grid settings change (sent by `kh.grid`, desktop app) | `grid`: visible, spacing in mm, snap, style |
| `OverlaysChanged` | An overlay is added, changed or removed | `id` |
| `SettingChanged` | A plugin's setting changes | `plugin` (its id), `key` (the setting's name) |
| `ToolbarModeChanged` | A plugin switches the toolbar mode (see Toolbar modes, above) | `mode` (empty for the usual toolbar) |
| `ActionsChanged` | A plugin says its menu items' `enabled`, `checked` or words may have changed | nothing |

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
- **Status-bar items.** `ui->addStatusItem` puts a small widget at the bottom
  right of the window, like Khan's grid size. Khan calls your function once when
  the window opens and owns the widget:

  ```cpp
  ui->addStatusItem({"sam.notes.count",  // starts with your plugin id
                     [&ctx] { return new NoteCountLabel(ctx); }});
  ```
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
| Ribbon tabs | Grouping actions into tabs | Later |
| Viewer tools | A way to use the mouse in the 3D view, with a live preview | With the sketcher, Phase 1 |
| Feature types | A new kind of step that saves, loads and rebuilds | Phase 2 |
| File formats | Importers and exporters | Phase 2 |
| Assistant tools, lesson checks | Things the AI assistant and lessons can use | Phase 3 |
| Python plugins | All of the above, without a compiler | Later |
