# Share something with other plugins

Sometimes several plugins need the same thing: a ruler that knows which units the
student likes, a grid to snap to, a list of snap points. One plugin can **offer**
such a thing, and any other plugin can **use** it, without either one changing
Khan itself.

This page builds two small plugins that do exactly that:

- **Ruler** (`example.ruler`) offers a ruler that shows lengths in inches.
- **Tape** (`example.tape`) uses the ruler to measure, if Ruler is installed.

Both are in [`sharing/`](sharing/), and Khan's tests build and load them, so
this page always matches the current Khan. Do [Hello](hello-plugin.md) first.

## The idea: a contract

Ruler doesn't let other plugins into its code. It publishes a **contract**: a
small header that says what the ruler can do, with no code inside. Tape includes
the header, asks Khan for the ruler, and calls it. Tape never links to Ruler.

```text
  Ruler plugin                          Tape plugin
  (has the real ruler)                  (wants to measure)
        |  ctx.provide(ruler)                 |  ctx.service<IRuler>()
        +-----------> Khan's services <-------+
                both include Ruler.h, the contract
```

## Step 1: Write the contract

<!-- guide-file: sharing/ruler/include/example/ruler/Ruler.h -->

```cpp
// The Ruler contract: what other plugins may use from example.ruler.
// It has no code of its own, so a plugin can use the ruler without linking to it.
#pragma once

namespace example::ruler {

// The ruler, as other plugins see it. They find it with
// ctx.service<example::ruler::IRuler>().
class IRuler {
public:
    static constexpr const char* kServiceName = "example.ruler/1";  // version 1
    virtual ~IRuler() = default;
    virtual const char* unit() const = 0;        // "mm" or "in"
    virtual double fromMm(double mm) const = 0;  // a length in mm, in the ruler's unit
};

// Sent when the ruler's unit changes, and once when Khan starts.
struct UnitChanged {
    static constexpr const char* kType = "example.ruler.unit_changed";
    const char* unit = "mm";
};

}  // namespace example::ruler
```

- **`IRuler`** is an *interface*: a class with only `virtual ... = 0` functions.
  Other plugins can call them, but the code lives in Ruler.
- **`kServiceName`** is the name other plugins ask for. It starts with the
  plugin's id and ends with a **version**, `/1`. If you ever change `IRuler` in a
  way that would break other plugins (a new function, a changed one), don't edit
  it: make `IRuler2` named `example.ruler/2`, and offer both for a while.
- **`UnitChanged`** is an **event** of Ruler's own. Its `kType` name also starts
  with the plugin id, so it can never clash with another plugin's events.
- Use plain types in a contract: numbers, `bool`, `const char*` for fixed text.

## Step 2: Offer it

<!-- guide-file: sharing/ruler/ruler.cpp -->

```cpp
// The Ruler plugin: shares a ruler that shows lengths in inches or mm. The
// student picks with View > Ruler shows inches, and Khan remembers it.
#include <string>

#include "example/ruler/Ruler.h"
#include "kh/plugin/Api.h"
#include "kh/plugin/PluginContext.h"

namespace {

using example::ruler::IRuler;
using example::ruler::UnitChanged;

class Ruler final : public IRuler {
public:
    bool inches = true;
    const char* unit() const override { return inches ? "in" : "mm"; }
    double fromMm(double mm) const override { return inches ? mm / 25.4 : mm; }  // 25.4 mm in an inch
};

Ruler ruler;  // lives until Khan closes

void registerRuler(kh::plugin::PluginContext& ctx) {
    // The unit the student picked last time ("in" the first time).
    auto& settings = ctx.settings();
    ruler.inches = settings.text("unit", "in") == "in";

    // Step 1, registering: offer the ruler to every other plugin.
    ctx.provide<IRuler>(ruler);

    // A View menu item with a check mark. Clicking it changes the setting.
    ctx.addAction({.id = "example.ruler.inches",
                   .text = "Ruler shows inches",
                   .menu = "View",
                   .tip = "Show lengths in inches instead of mm.",
                   .run = [&settings] { settings.set("unit", ruler.inches ? "mm" : "in"); },
                   .checked = [] { return ruler.inches; }});

    // When the setting changes, change the ruler and tell the other plugins.
    ctx.listen<kh::plugin::SettingChanged>([&ctx, &settings](const kh::plugin::SettingChanged& e) {
        if (e.plugin != "example.ruler" || e.key != "unit") return;
        ruler.inches = settings.text("unit", "in") == "in";
        ctx.publish(UnitChanged{ruler.unit()});
    });

    // Step 2, starting: every plugin has registered. Tell them the unit.
    ctx.onStart([&ctx] { ctx.publish(UnitChanged{ruler.unit()}); });
}

}  // namespace

KH_PLUGIN_ENTRY(registerRuler)
```

A plugin starts in **two steps**:

1. **Registering:** Khan calls every plugin's register function, one after the
   other. Here each plugin *offers* what it has: commands, menu items, services.
   `ctx.provide<IRuler>(ruler)` offers the ruler under its `kServiceName`. The
   ruler must live until Khan closes, so it's a global here.
2. **Starting:** once *every* plugin has registered, Khan runs the functions given
   to `ctx.onStart`, in the same order. Every service exists by now. Ruler uses
   this moment to tell everyone its unit with `ctx.publish`.

`ctx.publish` is like `ctx.events().publish`, but it checks that the event's name
starts with your plugin's id.

Ruler also lets the student pick its unit, and remembers the choice:

- `ctx.settings()` is the plugin's own settings. `settings.text("unit", "in")` reads
  the saved unit, or `"in"` if there isn't one yet. Khan keeps settings between runs.
- The menu item has a `run` function instead of a command, because picking a unit
  doesn't change a design. Its `checked` function puts a check mark next to it.
- `settings.set(...)` sends a `kh.SettingChanged` event. Ruler listens for its own
  `unit` setting, changes the ruler, and tells everyone the new unit.

See [What a plugin can do](extension-points.md#remember-settings) for more about
settings and check-mark items.

Ruler's `plugin.json` is ordinary: an `id`, a `name` and a `library`.

## Step 3: Use it

<!-- guide-file: sharing/tape/tape.cpp -->

```cpp
// The Tape plugin: measures with the Ruler plugin's ruler, if it's there.
#include <cstdio>
#include <string>

#include "example/ruler/Ruler.h"
#include "kh/plugin/Api.h"
#include "kh/plugin/PluginContext.h"

namespace {

using example::ruler::IRuler;
using example::ruler::UnitChanged;

std::string twoDecimals(double v) {
    char text[32];
    std::snprintf(text, sizeof text, "%.2f", v);
    return text;
}

void registerTape(kh::plugin::PluginContext& ctx) {
    // Listening needs no ruler yet: the event arrives when the ruler starts.
    ctx.listen<UnitChanged>(
        [&ctx](const UnitChanged& e) { ctx.log(std::string("The ruler shows lengths in ") + e.unit + "."); });

    // Look up other plugins' services when starting, not while registering.
    ctx.onStart([&ctx] {
        const auto* ruler = ctx.service<IRuler>();
        if (!ruler) {
            ctx.log("There's no ruler, so lengths stay in mm.");
            return;
        }
        ctx.log("A 25.4 mm box is " + twoDecimals(ruler->fromMm(25.4)) + " " + ruler->unit() + " long.");
    });
}

}  // namespace

KH_PLUGIN_ENTRY(registerTape)
```

- **Listen while registering, look up while starting.** `ctx.listen` can be called
  any time: the event arrives when Ruler sends it. But `ctx.service<IRuler>()`
  goes in `onStart`, because only then is every plugin's service there. (Ask
  while registering and it works only if Ruler happened to load first; Khan warns
  you.)
- **Cope with "not there".** `ctx.service` gives `nullptr` if no plugin offers
  the ruler. Tape then keeps using mm.

<!-- guide-file: sharing/tape/plugin.json -->

```json
{
  "id": "example.tape",
  "name": "Tape",
  "version": "1.0.0",
  "description": "Measures with the Ruler plugin's ruler, if it's there.",
  "library": "tape",
  "uses": ["example.ruler"]
}
```

**`uses`** says "Tape uses Ruler if it's there". Khan then loads Ruler first, and
Tape still loads when Ruler isn't installed. If Tape couldn't work at all without
the ruler, it would put Ruler in **`requires`** instead: then Tape doesn't load
when Ruler is missing or broken, and Khan says why.

## Step 4: Build and try it

The contract needs no code, so CMake describes it as headers only, and both
plugins use it ([`sharing/CMakeLists.txt`](sharing/CMakeLists.txt)):

```cmake
add_library(example_ruler_api INTERFACE)
target_include_directories(example_ruler_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/ruler/include")
kh_add_plugin(kh_example_tape_plugin MANIFEST tape/plugin.json SOURCES tape/tape.cpp DEPS example_ruler_api)
```

Build Khan, then list the plugins. Ruler offers its service and its View menu
item; Tape's log shows it heard the event and measured with the ruler:

<!-- cli-example -->
```console
$ kh_cli plugins --plugins build/dev/khan-plugins
...
loaded   example.ruler 1.0.0  Ruler
           service example.ruler/1
           action  View > Ruler shows inches  (check mark)
loaded   example.tape 1.0.0  Tape
           log: The ruler shows lengths in in.
           log: A 25.4 mm box is 1.00 in long.
...
```

**Your turn:** delete Ruler's folder from `build/dev/khan-plugins` and list the
plugins again. Tape still loads, says "There's no ruler, so lengths stay in mm",
and Khan adds a note that Ruler isn't installed.

## Warnings

While plugins load, Khan watches for things that work but are probably a
mistake, and adds a **warning** to the plugin. Warnings never stop a plugin.
`kh_cli plugins` shows them (`warning [code]: ...`), and so will the desktop
app's Plugin manager; the desktop app also writes them to its log.

| Code | When | How to fix it |
| --- | --- | --- |
| `plugin.undeclared_dependency` | You use a service or listen to an event from a plugin that isn't in your `requires` or `uses` | Add it to `uses` |
| `plugin.early_lookup` | You asked for another plugin's service while registering | Ask in `onStart` |
| `plugin.service_taken` | Another plugin already offers that service name. Khan keeps theirs | Pick a name starting with your plugin id |
| `plugin.event_name` | You sent an event whose name doesn't start with your plugin id | Rename the event's `kType` |
| `plugin.old_contract` | You use a service version that has a newer one, or that its plugin marked as going away (`ctx.provide(service, true)`) | Move to the newer version |
| `plugin.slow_start` | Registering and starting took longer than the limit (500 ms unless changed) | Do slow work later, when it's first needed |

There's also one **note**: `plugin.uses_missing`, when a plugin in `uses` isn't
installed. That's fine, just worth knowing.

To make warnings fail your plugin's own tests or CI, use
`kh_cli plugins --strict` (the `kh_cli plugins` page of the kh_cli reference,
`docs/cli/plugins.md`, has every option).

## Plugins that use each other

Khan loads plugins in order: a plugin after the plugins it `requires` or `uses`.
If two plugins name each other (A uses B, B uses A), there's no right order, and
that's fine: Khan loads them in id order. Because of the two steps (everyone
registers, then everyone starts), each one's services are there by the time the
other starts. There's no warning.

## Tips

- **Keep contracts small.** Every function in one is a promise you keep until you
  make a new version.
- **Never edit a released contract.** Make `/2` instead, and offer both until
  other plugins have moved.
- **Don't keep pointers from `ctx.service` across a plugin failing.** If the
  plugin that offers a service fails while starting, Khan takes the service back;
  your `onStart` then gets `nullptr`.
