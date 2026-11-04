# Installing SketchBillet

SketchBillet installs like any other program, on Windows, macOS or Linux. It's
free, needs no account, and works without the internet once it's installed.
Download it from [sketchbillet.org/download](https://sketchbillet.org/download/).

There are two kinds of download:

- **A release**, like SketchBillet 0.1.0. Tested and finished: use it for
  lessons and real work.
- **The preview build.** The newest SketchBillet, rebuilt every time a change is
  added. It has the newest features, and maybe new bugs.

## What your computer needs

| System | Needs | Download |
| --- | --- | --- |
| Windows | Windows 10 or 11, 64-bit | `SketchBillet-<version>-windows-x64.msi` |
| macOS | macOS 15 (Sequoia) or newer, on a Mac with Apple silicon (M1 or newer) | `SketchBillet-<version>-macos-arm64.dmg` |
| Linux | A 64-bit Linux from 2024 or newer, like Ubuntu 24.04, Debian 13 or Fedora 40 | `SketchBillet-<version>-linux-x64.AppImage` |

Every computer also needs a graphics card or chip that can do OpenGL 2.1 or
newer, which almost every computer made since 2010 can.

## Windows

1. Download the `.msi` file and open it.
2. Windows may say **"Windows protected your PC"**. That's because SketchBillet
   isn't signed with a paid certificate yet, not because something's wrong.
   Click **More info**, then **Run anyway**.
3. Click through the installer. It installs SketchBillet for everyone who uses
   the computer, in `C:\Program Files\SketchBillet`, so it asks for an
   administrator's OK.
4. Start SketchBillet from the Start menu or the shortcut on your desktop.

To remove it, go to **Settings > Apps > Installed apps**, find SketchBillet, and
choose **Uninstall**. Your designs stay where you saved them.

### For schools: installing on many computers

The `.msi` installs without any questions when you add `/qn`, so it works with
the tools IT departments use to set up many computers (Intune, Configuration
Manager, Group Policy, PDQ):

```
msiexec /i SketchBillet-0.1.0-windows-x64.msi /qn
```

A newer `.msi` replaces the older SketchBillet the same way, and
`msiexec /x SketchBillet-0.1.0-windows-x64.msi /qn` removes it. SketchBillet
never needs the internet, an account or a license server.

## macOS

1. Download the `.dmg` file and open it.
2. Drag **SketchBillet** onto the **Applications** folder.
3. Open SketchBillet from Applications. The first time, macOS says it can't
   check SketchBillet for malware, because SketchBillet isn't registered with
   Apple yet. Click **Done** (not Move to Trash).
4. Open **System Settings > Privacy & Security**, scroll down to the message
   about SketchBillet, and click **Open Anyway**. Type your password if asked.
   You only do this once.

To remove it, drag SketchBillet from Applications to the Trash.

## Linux

The AppImage is the whole program in one file: nothing to install.

1. Download the `.AppImage` file.
2. Let it run as a program: right-click it, choose **Properties > Permissions**,
   and tick **Allow executing file as program**. Or, in a terminal:
   `chmod +x SketchBillet-0.1.0-linux-x64.AppImage`
3. Double-click it, or run `./SketchBillet-0.1.0-linux-x64.AppImage`.

To remove it, delete the file.

## The command-line tool

Every download includes [`sketchbillet_cli`](../cli/README.md), SketchBillet's
command-line tool, as well as the app:

| System | Where it is |
| --- | --- |
| Windows | `C:\Program Files\SketchBillet\bin\sketchbillet_cli.exe` |
| macOS | `/Applications/SketchBillet.app/Contents/MacOS/sketchbillet_cli` |
| Linux | Inside the AppImage: run `./SketchBillet-0.1.0-linux-x64.AppImage cli`, then the command, like `./SketchBillet-0.1.0-linux-x64.AppImage cli --version` |

## Tips

- **Which version do I have?** In a terminal, run `sketchbillet_cli --version`.
- **"The app opens, but the 3D view is blank or the app closes."** Update your
  graphics driver (from the computer maker, or Intel, AMD or NVIDIA). On a
  virtual machine, turn on 3D acceleration.
- **Want to build SketchBillet yourself,** to change it or try a pull request?
  The [README](../../README.md) has the steps.
- The installers are made by SketchBillet's CI from the same code everyone can
  read. `packaging/README.md` explains how.
