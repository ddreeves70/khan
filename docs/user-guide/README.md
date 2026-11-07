# SketchBillet user's guide

SketchBillet is a free, open-source CAD program for designing parts you can 3D print or
machine. This guide explains how SketchBillet works and everything it can do, with
pictures. It grows with SketchBillet: every new feature is added here when it's built.

![A desk organizer designed in SketchBillet](../tutorials/images/organizer-done.png)

## Where SketchBillet is today

SketchBillet is early. [Install it](install.md) on Windows, macOS or Linux, then
design parts in the [desktop app](desktop-app.md), with
menus, a 3D viewer, a feature tree and a Properties panel, or with `sketchbillet_cli`,
SketchBillet's command-line tool. Both run the same commands, so everything in this guide
works in either.

| You can do now | Coming next |
| --- | --- |
| Boxes, cylinders, union, cut, intersect | Revolve; pulling a face of a solid |
| Sketches with constraints and dimensions, drawn in the app or with `sketchbillet_cli` | |
| Extrude a sketch into a solid | |
| Fillet and chamfer edges, picked in the 3D view; they stay on the same edges when earlier steps change | Sketching on a face of a body |
| Name, rename and delete steps; undo and redo | |
| Move and turn bodies; copy, paste and duplicate steps; turn steps off; folders | |
| The desktop app: 3D viewer with 1, 2 or 4 panes, feature tree, Properties, layouts | |
| Materials: mass, weight, material cost and color | |
| Parameters: named values like `wall = 3 mm`, with formulas and units, used by sketch dimensions and steps' numbers, with a Parameters panel in the app | Formulas typed straight into Properties and the Sketch panel |
| Save and open `.sb` designs | |
| Export STL (3D printing) and STEP (other CAD programs), from the app's File menu or `sketchbillet_cli` | Holes, shells, patterns |
| Add plugins | Import STEP, STL and 3MF |

## Contents

1. [Installing SketchBillet](install.md): download it for Windows, macOS or Linux.
2. [Getting started](getting-started.md): get SketchBillet and make your first part.
3. [The desktop app](desktop-app.md): the window, the feature tree, Properties, layouts.
4. [Designs and steps](designs-and-steps.md): how a SketchBillet design works.
5. [Shapes](shapes.md): boxes and cylinders, and where they go.
6. [Combining bodies](combining.md): union, cut and intersect.
7. [Moving bodies](moving.md): slide and turn a body.
8. [Sketches](sketches.md): flat drawings with constraints and dimensions.
9. [Extrude](extrude.md): turn a sketch into a solid.
10. [Fillet and chamfer](fillet-chamfer.md): round or bevel edges.
11. [Materials](materials.md): what a body is made of, its mass, weight and cost.
12. [Parameters](parameters.md): named values like `wall = 3 mm`, and formulas that use them.
13. [Files](files.md): saving, opening, and exporting for 3D printing.
14. [Plugins](plugins.md): adding features to SketchBillet.
15. [When something goes wrong](troubleshooting.md): what SketchBillet's messages mean.

## Other guides

- [Tutorials](../tutorials/README.md): step-by-step projects.
- [sketchbillet_cli reference](../cli/README.md): every command, with examples.
- [Plugin developer guide](../plugin-guide/README.md): write your own plugins.

## Units

SketchBillet works in millimeters (mm). Volumes are in cubic millimeters (mm^3);
1000 mm^3 is 1 cm^3. A design can show its lengths in centimeters, meters,
inches or feet instead: see
[Document settings and units](desktop-app.md#document-settings-and-units).
Commands typed into `sketchbillet_cli` always take mm.
