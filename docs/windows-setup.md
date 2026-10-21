# Windows development setup

Step-by-step setup for building Khan on Windows 10 or 11 with Visual Studio 2022.
Every step here fixes a problem that came up during the first Windows build, so
following them in order saves a lot of time.

## 1. Install the tools

1. **Visual Studio 2022** (Community is fine) with the **Desktop development with C++**
   workload. In the Visual Studio Installer, make sure **C++ CMake tools for Windows**
   is checked. Then click **Update** so you have the latest version (17.14 or newer,
   compiler 19.44 or newer). An old Visual Studio causes link errors against libraries
   built by vcpkg.
2. **Git for Windows**.
3. **Qt 6** with the Qt online installer. Under your Qt version, choose
   **MSVC 2022 64-bit**. The MinGW build of Qt does **not** work with Visual Studio.

## 2. Keep everything out of Documents

Put vcpkg and the repository in a short path such as `C:\dev`. Folders under
`Documents` can be locked by Windows Defender, Controlled folder access or OneDrive
sync, which makes vcpkg fail with "Permission denied" after a long build.

```powershell
mkdir C:\dev -Force
git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat -disableMetrics
setx VCPKG_ROOT C:\dev\vcpkg
git clone https://github.com/ddreeves70/khan.git C:\dev\khan
```

Optional but recommended, in PowerShell **run as Administrator**:

```powershell
# exclude the build folders from virus scanning (fewer lockups, faster builds)
Add-MpPreference -ExclusionPath "C:\dev"
# allow long paths, which vcpkg needs
New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name LongPathsEnabled -Value 1 -PropertyType DWORD -Force
git config --global core.longpaths true
```

## 3. Add a `vsdev` command to PowerShell

Do **not** use the Start-menu "Developer PowerShell for VS 2022": it loads the 32-bit
compiler and points `VCPKG_ROOT` at Visual Studio's own copy of vcpkg. Instead add this
function to your PowerShell profile:

```powershell
if (!(Test-Path $PROFILE)) { New-Item -ItemType File -Path $PROFILE -Force }
notepad $PROFILE
```

Paste, save and close:

```powershell
function vsdev {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -property installationPath
    $env:VCPKG_VISUAL_STUDIO_PATH = $vs    # vcpkg uses the same Visual Studio as the shell
    & "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $env:VCPKG_ROOT = [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User')    # our vcpkg, not VS's
}
```

If PowerShell says running scripts is disabled, run once:
`Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.

Check it in a new window:

```powershell
vsdev
cl                 # first line should say "for x64" and version 19.44 or newer
$env:VCPKG_ROOT    # C:\dev\vcpkg
```

## 4. Create your personal presets file

Copy `CMakeUserPresets.example.json` to `CMakeUserPresets.json` in the repository root
and edit the two paths if yours differ. This file is ignored by git, so your paths stay
on your machine.

## 5. Build, test and run

```powershell
vsdev
cd C:\dev\khan
cmake --preset win
cmake --build --preset win
ctest --preset win
.\build\win\apps\kh_cli\kh_cli.exe demo tag.step
.\build\win\apps\kh_app\kh_app.exe
```

The first `cmake --preset win` builds OCCT through vcpkg and takes 30-60 minutes. Leave
the window alone and close Visual Studio while it runs. Later configures take seconds.

The build copies Qt's runtime files next to `kh_app.exe` automatically, so the app runs
without changing `PATH`.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| `cmake` is not recognized | Visual Studio environment not loaded | Run `vsdev` first; install **C++ CMake tools for Windows** |
| Toolchain path shows `Program Files\Microsoft Visual Studio\...\vcpkg` | `VCPKG_ROOT` overridden by the VS shell | Use the `vsdev` function above; delete `build` and reconfigure |
| `Permission denied` while vcpkg copies files | Folder locked by Defender, OneDrive or another build | Move to `C:\dev`, close Visual Studio, add the Defender exclusion |
| `Could not find a package configuration file provided by "Qt6"` | Wrong Qt folder, or MinGW Qt | Install **MSVC 2022 64-bit** Qt; fix the path in `CMakeUserPresets.json`; `cmake --preset win --fresh` |
| Link errors like `unresolved external symbol __std_search_1` | vcpkg libraries built by a newer compiler than the one linking | Update Visual Studio, use the `vsdev` function, delete `build` and reconfigure |
| `kh_app.exe` does nothing | Qt runtime files missing | Rebuild (they are copied automatically) or run `windeployqt` on the exe |

When in doubt: open a new window, run `vsdev`, delete the `build` folder and configure again.
CMake remembers the compiler and toolchain from the first configure.
