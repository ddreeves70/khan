# Khan design

> **Draft.** This design changes as Khan is built. Questions and ideas are welcome in [GitHub Discussions](https://github.com/ddreeves70/khan/discussions).

Khan is a free, open-source parametric CAD application for Windows, macOS and Linux. It starts with high school engineering classes, then grows to makers, CNC users and small shops. The long-term goal is a tool as capable as SolidWorks that stays free forever.

These pages describe how Khan is designed: what it will do, how the code is organized, and the order it gets built in. For how to build and test the code today, see the [main README](../../README.md).

## Pages

| Page | What it covers |
| --- | --- |
| [Architecture](architecture.md) | Goals, the Fusion capability map, system layers, the geometry kernel, every major area (sketcher, modeling, assemblies, drawings, CAM, simulation), the plugin architecture, file formats and risks |
| [UI screens](ui-screens.md) | Mockups of the main window, split views, two monitors and the Plugin manager, with a numbered breakdown of each |
| [Design model](design-model.md) | The module map, the core domain classes, key C++ interfaces, the `.khan` file format, regeneration and naming |
| [Class model](class-model.md) | UML class diagrams for the document, features and naming, sketches, the kernel layer, assemblies, and the shell and plugins |
| [AI assistant](ai-assistant.md) | The optional assistant: what it can do, how changes stay under the user's control, teaching it, and privacy for schools |
| [Roadmap](roadmap.md) | Decisions, what is reused versus built, and the phases from foundations to the first classroom release and beyond |
| [SolidWorks-class scope](solidworks-scope.md) | What professional, SolidWorks-class capability adds, and the small choices made now so it can land later |

## Principles

Every feature of the app stays free, up to and including SolidWorks-class capability.

- **No paid edition, no locked features, no trial limits.** Anything that runs on your computer is free and open source.
- **Paid things are optional services** that cost money or someone's time to provide, such as hosted cloud and AI, courses, expert help, or making parts. Each one has a free path: the hub can be self-hosted, the AI can run locally or with your own key, and courses add to free documentation.
- **No ads, no selling data, and no designs sent anywhere without consent.**
- **Sponsors never steer the product.**

## How the code follows the design

The rules that keep the code faithful to this design are in [CLAUDE.md](../../CLAUDE.md): which library may depend on which, why only commands change a document, and why errors are reported as plain-language messages instead of exceptions.
