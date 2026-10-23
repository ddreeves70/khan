# Khan API reference {#mainpage}

Khan is a free, open-source parametric CAD application. This reference covers
the public headers of the core libraries, from the bottom layer up:

| Library | Namespace | What it holds |
| --- | --- | --- |
| `kh_core` | `kh` | Ids (`kh::Id`), diagnostics (`kh::Diagnostic`), version |
| `kh_kernel` | `kh::kernel` | The geometry kernel interface (`kh::kernel::IKernel`) and value types |
| `kh_kernel_occt` | `kh::kernel` | The Open CASCADE implementation of `IKernel` |
| `kh_model` | `kh::model` | Documents, features, commands, undo and the command registry |
| `kh_io` | `kh::io` | Saving and opening `.khan` files (`kh::io::saveFile`, `kh::io::openFile`) |

A good place to start is `kh::model::Document`, then `kh::model::Command`.

Units inside the code: lengths are millimeters and angles are radians.
