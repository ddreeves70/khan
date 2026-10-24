# Khan user's guide

Khan is a free, open-source CAD program for designing parts you can 3D print or
machine. This guide explains how Khan works and everything it can do, with
pictures. It grows with Khan: every new feature is added here when it's built.

![A desk organizer designed in Khan](../tutorials/images/organizer-done.png)

## Where Khan is today

Khan is early. You can design parts in the [desktop app](desktop-app.md), with
menus, a feature tree and a Properties panel, or with `kh_cli`, Khan's
command-line tool. The 3D view is being built now. Both run the same commands,
so everything in this guide works in either.

| You can do now | Coming next |
| --- | --- |
| Boxes, cylinders, union, cut, intersect | The 3D view, with 1, 2 or 4 panes |
| Name, rename and delete steps; undo and redo | Export from the app's File menu |
| The desktop app: feature tree, Properties, layouts | |
| Save and open `.khan` designs | Sketches, extrude, revolve |
| Export STL (3D printing) and STEP (other CAD programs) | Fillets, chamfers, holes, patterns |
| Add plugins | Import STEP, STL and 3MF |

## Contents

1. [Getting started](getting-started.md): get Khan and make your first part.
2. [The desktop app](desktop-app.md): the window, the feature tree, Properties, layouts.
3. [Designs and steps](designs-and-steps.md): how a Khan design works.
4. [Shapes](shapes.md): boxes and cylinders, and where they go.
5. [Combining bodies](combining.md): union, cut and intersect.
6. [Files](files.md): saving, opening, and exporting for 3D printing.
7. [Plugins](plugins.md): adding features to Khan.
8. [When something goes wrong](troubleshooting.md): what Khan's messages mean.

## Other guides

- [Tutorials](../tutorials/README.md): step-by-step projects.
- [kh_cli reference](../cli/README.md): every command, with examples.
- [Plugin developer guide](../plugin-guide/README.md): write your own plugins.

## Units

Khan works in millimeters (mm). Volumes are in cubic millimeters (mm^3);
1000 mm^3 is 1 cm^3.
