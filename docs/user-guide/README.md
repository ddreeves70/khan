# Khan user's guide

Khan is a free, open-source CAD program for designing parts you can 3D print or
machine. This guide explains how Khan works and everything it can do, with
pictures. It grows with Khan: every new feature is added here when it's built.

![A desk organizer designed in Khan](../tutorials/images/organizer-done.png)

## Where Khan is today

Khan is early. Today you design parts with `kh_cli`, Khan's command-line tool,
one command at a time. The desktop app, with a 3D view and buttons, is being
built now; this guide will show it as soon as it exists. The ideas stay the
same, because the app's buttons run the same commands.

| You can do now | Coming next |
| --- | --- |
| Boxes, cylinders, union, cut, intersect | The desktop app with a 3D view |
| Name, rename and delete steps | Undo and redo in the app |
| Save and open `.khan` designs | Sketches, extrude, revolve |
| Export STL (3D printing) and STEP (other CAD programs) | Fillets, chamfers, holes, patterns |
| Add plugins | Import STEP, STL and 3MF |

## Contents

1. [Getting started](getting-started.md): get Khan and make your first part.
2. [Designs and steps](designs-and-steps.md): how a Khan design works.
3. [Shapes](shapes.md): boxes and cylinders, and where they go.
4. [Combining bodies](combining.md): union, cut and intersect.
5. [Files](files.md): saving, opening, and exporting for 3D printing.
6. [Plugins](plugins.md): adding features to Khan.
7. [When something goes wrong](troubleshooting.md): what Khan's messages mean.

## Other guides

- [Tutorials](../tutorials/README.md): step-by-step projects.
- [kh_cli reference](../cli/README.md): every command, with examples.
- [Plugin developer guide](../plugin-guide/README.md): write your own plugins.

## Units

Khan works in millimeters (mm). Volumes are in cubic millimeters (mm^3);
1000 mm^3 is 1 cm^3.
