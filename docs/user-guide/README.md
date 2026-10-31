# Khan user's guide

Khan is a free, open-source CAD program for designing parts you can 3D print or
machine. This guide explains how Khan works and everything it can do, with
pictures. It grows with Khan: every new feature is added here when it's built.

![A desk organizer designed in Khan](../tutorials/images/organizer-done.png)

## Where Khan is today

Khan is early. You can design parts in the [desktop app](desktop-app.md), with
menus, a 3D viewer, a feature tree and a Properties panel, or with `kh_cli`,
Khan's command-line tool. Both run the same commands, so everything in this guide
works in either.

| You can do now | Coming next |
| --- | --- |
| Boxes, cylinders, union, cut, intersect | Revolve; pulling a face of a solid |
| Sketches with constraints and dimensions, drawn in the app or with `kh_cli` | |
| Extrude a sketch into a solid | |
| Name, rename and delete steps; undo and redo | |
| Move and turn bodies; copy, paste and duplicate steps; turn steps off; folders | |
| The desktop app: 3D viewer with 1, 2 or 4 panes, feature tree, Properties, layouts | |
| Materials: mass, weight, material cost and color | |
| Parameters: named values like `wall = 3 mm`, with formulas and units | Sketch dimensions and step sizes that use parameters |
| Save and open `.khan` designs | |
| Export STL (3D printing) and STEP (other CAD programs), from the app's File menu or `kh_cli` | Fillets, chamfers, holes, patterns |
| Add plugins | Import STEP, STL and 3MF |

## Contents

1. [Getting started](getting-started.md): get Khan and make your first part.
2. [The desktop app](desktop-app.md): the window, the feature tree, Properties, layouts.
3. [Designs and steps](designs-and-steps.md): how a Khan design works.
4. [Shapes](shapes.md): boxes and cylinders, and where they go.
5. [Combining bodies](combining.md): union, cut and intersect.
6. [Moving bodies](moving.md): slide and turn a body.
7. [Sketches](sketches.md): flat drawings with constraints and dimensions.
8. [Extrude](extrude.md): turn a sketch into a solid.
9. [Materials](materials.md): what a body is made of, its mass, weight and cost.
10. [Parameters](parameters.md): named values like `wall = 3 mm`, and formulas that use them.
11. [Files](files.md): saving, opening, and exporting for 3D printing.
12. [Plugins](plugins.md): adding features to Khan.
13. [When something goes wrong](troubleshooting.md): what Khan's messages mean.

## Other guides

- [Tutorials](../tutorials/README.md): step-by-step projects.
- [kh_cli reference](../cli/README.md): every command, with examples.
- [Plugin developer guide](../plugin-guide/README.md): write your own plugins.

## Units

Khan works in millimeters (mm). Volumes are in cubic millimeters (mm^3);
1000 mm^3 is 1 cm^3. A design can show its lengths in centimeters, meters,
inches or feet instead: see
[Document settings and units](desktop-app.md#document-settings-and-units).
Commands typed into `kh_cli` always take mm.
