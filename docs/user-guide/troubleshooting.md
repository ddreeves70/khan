# When something goes wrong

Khan explains every problem in plain words and says how to fix it. Each message
also has a **code** in brackets, like `[command.arg_missing]`. The code never
changes, so search for it here or quote it when you ask for help.

## A step failed

The step shows `failed`, with the reason under it:

<!-- cli-example exit=1 -->
```console
$ kh_cli run feature.add_box size=10,0,10
Untitled: 1 step
  Box1         failed
    error [kernel.box.size]: A box needs a width, depth and height greater than 0 mm.
```

Steps that use a failed step show `blocked`: fix the failed step first, and they
build again. A step is also `blocked` when it uses a step that's turned off
(`off`): turn that step back on (Edit > Turn on), or change the blocked step.

## A command was typed wrong

`kh_cli run` checks the settings first and changes nothing if one is wrong.

| What you see | Fix |
| --- | --- |
| `error [command.arg_missing]: Add box needs size: ...` | Add the setting it names |
| `error: size must be three numbers in mm, like 10,20,30` | Write x,y,z with commas and no spaces |
| `error: Add box has no setting called colour. Its settings are: ...` | Use one of the settings listed |
| `error [command.arg_feature]: ... points to a step that isn't in the design.` | Check the step's name with `kh_cli open` |
| `error: there is no command called ...` | Run `kh_cli help` to see the commands |

<!-- cli-example exit=1 -->
```console
$ kh_cli run feature.add_box
error [command.arg_missing]: Add box needs size: Width, depth and height in mm.
```

## A file won't open

| Code | Meaning | Fix |
| --- | --- | --- |
| `file.cant_read` | The file isn't there, or you aren't allowed to open it | Check the name and folder |
| `file.not_khan` | It isn't a Khan design | Open `.khan` files only; export files (STL, STEP) can't be opened as designs yet |
| `file.damaged` | Part of the file is missing or broken | Open an older copy |
| `file.too_new` | A newer Khan saved it | Update Khan |
| `file.unknown_step` | It uses a kind of step this Khan doesn't know | Update Khan, or add the plugin that makes that step |

<!-- cli-example exit=1 -->
```console
$ kh_cli open missing.khan
error [file.cant_read]: Khan couldn't open missing.khan. Check that the file is still there and that you're allowed to open it. missing.khan
```

## Export didn't work

The desktop app shows these in the status bar after **File > Export...**:

| Code | Meaning | Fix |
| --- | --- | --- |
| `export.nothing` | There's no shape to export yet, or the picked step didn't build | Make a shape (Create > Box), or fix the step that failed |
| `export.which` | The design has two or more finished bodies, and Export writes one | Pick one step in the feature tree, or join the bodies with Combine > Union |
| `export.format` | The file name doesn't end with `.stl`, `.step` or `.stp` | End the name with `.stl` (3D printing) or `.step` (other CAD programs) |
| `export.stl.failed`, `export.step.failed` | The file couldn't be written | Check the folder is still there and you're allowed to save in it |

## Paste didn't work

The desktop app shows these in the status bar after **Edit > Paste**:

| Code | Meaning | Fix |
| --- | --- | --- |
| `paste.not_steps` | What's on the clipboard isn't steps copied from Khan | Pick steps in the feature tree, Edit > Copy, then paste |
| `paste.too_new` | The steps were copied from a newer Khan | Update Khan, then paste again |
| `paste.damaged` | The copied text was changed or cut short | Copy the steps again |
| `paste.unknown_step` | A copied step is a kind this Khan doesn't know | Update Khan, or add the plugin that makes that kind of step |

## A parameter was refused

Each message says what's wrong and how to fix it. The codes:

| Code | Meaning | Fix |
| --- | --- | --- |
| `parameter.name` | The name can't be a parameter's | Use letters, digits and `_`, starting with a letter; not a unit like `mm` or a function like `sin` |
| `parameter.name_taken` | Another parameter has that name, maybe in other upper and lower case | Pick another name |
| `expression.unknown_name` | The formula uses a name that isn't a parameter | Check the spelling (case counts), or add that parameter first |
| `expression.units` | The units don't fit, like a length plus a plain number | Give both the same kind of unit: `wall + 2 mm` |
| `expression.unit` | A unit Khan doesn't know, like `kg` | Use `mm`, `cm`, `m`, `in`, `ft`, `deg` or `rad` |
| `parameter.loop` | Parameters use each other in a loop | Change one of them to a number |
| `parameter.breaks` | The change would stop another parameter working | Fix that one first, as the message says |
| `parameter.in_use` | Another parameter uses the one you're deleting | Change or delete that one first |

The [Parameters](parameters.md) page has what a formula can have.

## A plugin didn't load

Run [`kh_cli plugins`](../cli/plugins.md): it lists each plugin with the reason.
The plugin guide lists [every reason and its fix](../plugin-guide/manifest.md#when-khan-refuses-a-plugin).

## Still stuck?

Ask in [GitHub Discussions](https://github.com/ddreeves70/khan/discussions).
Include the output of `kh_cli --version` and the message you got.
