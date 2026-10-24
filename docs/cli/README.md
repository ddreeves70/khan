# kh_cli reference

`kh_cli` is Khan's command-line tool. It builds, opens, checks and exports
designs without the desktop app, so you can script Khan, run it in CI, or try new
features before they have buttons.

Every example on these pages is run by Khan's tests, and the test fails if
`kh_cli` prints anything different. So what you see here is what you get.

## Where to find it

After you build Khan, `kh_cli` is in your build folder:

| System | Path |
| --- | --- |
| Linux, macOS | `build/dev/apps/kh_cli/kh_cli` |
| Windows | `build\win\apps\kh_cli\kh_cli.exe` |

The examples write just `kh_cli`. Either type the whole path, or add that folder
to your `PATH`.

## Commands

| Command | What it does |
| --- | --- |
| [`kh_cli run`](run.md) | Runs one modeling command on a new or saved design, and saves it |
| [`kh_cli open`](open.md) | Opens a design, rebuilds it, reports each step, and exports STL or STEP |
| [`kh_cli materials`](materials.md) | Lists the materials a body can be made of |
| [`kh_cli help`](help.md) | Lists the modeling commands, or explains one |
| [`kh_cli plugins`](plugins.md) | Lists the plugins Khan finds, and what's wrong with any that don't load |
| [`kh_cli demo`](demo.md) | Makes a sample part, to check Khan works |
| [`kh_cli regress`](regress.md) | Runs geometry regression cases (for Khan developers) |
| [`kh_cli --version`](version.md) | Prints the version |

The **modeling commands** that `kh_cli run` runs (add a box, cut a hole, ...)
have their own page: [Modeling commands](commands.md).

## Reading the examples

Lines starting with `$` are what you type; the lines under them are what
`kh_cli` prints. `...` stands for text that changes from run to run, such as a
time.

Numbers are in millimeters (mm), volumes in cubic millimeters (mm^3), masses in
grams (g). A finished body's line ends with its mass and material.

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | It worked |
| 1 | Khan ran, but something failed: a step didn't build, a file couldn't be read, a plugin didn't load |
| 2 | The command line was wrong; `kh_cli` prints how to use it |
