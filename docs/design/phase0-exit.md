# Phase 0 exit check

Phase 0 (Foundations, see the [roadmap](roadmap.md#phase-0-foundations)) is done
when this works:

> Create a box, undo, save, reopen, export STL on all three OSes, with the box
> tool and the viewer loaded as plugins, and the guide's Hello plugin adding a
> working command.

The check has two halves. The **automated** half runs in CI on every pull request,
on Windows, macOS and Linux. The **by hand** half is a person using the real app
on a real screen, because CI draws without one.

## Automated (every pull request, all three OSes)

| Part of the exit check | Test | Where |
| --- | --- | --- |
| Box, undo, redo, save, new, reopen, export STL, through the app's menus | "Phase 0 exit check: box, undo, save, reopen, export STL, all through plugins" | `tests/app/test_shell.cpp` |
| The box tool is a plugin (`kh.modeling`) and the viewer is a plugin (`kh.viewer`) | Same test, plus "the shell is built from plugins" | `tests/app/test_shell.cpp` |
| The exported STL is a real box: 12 triangles | Same test, plus "File > Export writes the part as STL or STEP" | `tests/app/test_shell.cpp` |
| The guide's Hello plugin is built as an outside plugin, loads, and its command makes a cube | "the guide's Hello plugin loads and adds a working command" | `tests/plugin/test_hello_plugin.cpp` |
| Khan's own plugins load with no warnings | "Khan's own plugins follow the rules", and `cli.plugins_strict` | `tests/app/test_shell.cpp`, `tests/CMakeLists.txt` |
| The 3D view draws, picks, orbits and pans with real OpenGL | `view.gl` (Linux only, with a virtual screen) | `tests/view/test_view_gl.cpp` |

CI's three jobs (`ubuntu-24.04`, `windows-2022`, `macos-14`) build everything and
run all of these with `ctest`; the app tests run without a screen, so there the
viewer draws its flat fallback.

## By hand (once per OS, before calling Phase 0 done)

On each computer: build with the usual preset (`win` on Windows, `dev` on macOS
and Linux), run `kh_app`, and tick each step.

| Step | What you should see | Windows | macOS | Linux |
| --- | --- | --- | --- | --- |
| 1. Start `kh_app` | The window opens with the feature tree, a 3D viewer in the middle (not the flat stand-in) and Properties | ☐ | ☐ | ☐ |
| 2. Create > Box | A box in the viewer, `Box1` in the feature tree | ☐ | ☐ | ☐ |
| 3. Drag in the viewer, then Fit all | The view orbits, then the box fills the view | ☐ | ☐ | ☐ |
| 4. Edit > Undo, then Edit > Redo | The box goes away, then comes back | ☐ | ☐ | ☐ |
| 5. File > Save, as `box.khan` | The `*` leaves the title bar | ☐ | ☐ | ☐ |
| 6. File > New, then File > Open `box.khan` | The box is back, named `Box1` | ☐ | ☐ | ☐ |
| 7. File > Export..., as `box.stl` | The status bar says "Exported Box1 to box.stl" | ☐ | ☐ | ☐ |
| 8. Open `box.stl` in a slicer (Cura, PrusaSlicer, Bambu Studio) | A 20 mm cube (the default box) | ☐ | ☐ | ☐ |
| 9. `kh_cli plugins --plugins <build folder>/khan-plugins`, then `kh_cli run --plugins <build folder>/khan-plugins example.hello.add_cube size=25` | Hello is `loaded`; the run makes `Cube1` | ☐ | ☐ | ☐ |

Windows is the main machine; macOS is checked occasionally. Linux is covered by
CI's virtual screen (`view.gl`) and can be ticked by anyone who has it.

## Status

- Automated half: in place from the pull request that added File > Export
  (Oct 25, 2026), and running on all three OSes.
- By hand: not yet ticked. When a column is done, note the date and the commit
  here.
