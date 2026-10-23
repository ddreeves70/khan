# Getting started

This page gets you from nothing to a part ready to 3D print.

## 1. Get Khan

Khan has no installer yet, so you build it from its source code. The main
[README](../../README.md) has the steps for Windows, macOS and Linux. It takes
about 30 minutes the first time, mostly downloading libraries.

When it's done, check that it works:

<!-- cli-example -->
```console
$ kh_cli --version
kh_cli 0.0.1
```

`kh_cli` is in your build folder; see [Where to find it](../cli/README.md#where-to-find-it).

## 2. Make your first part

Make an empty folder, open a terminal there, and make a box 40 mm wide, 30 mm deep
and 10 mm tall:

<!-- cli-example -->
```console
$ kh_cli run --save first.khan feature.add_box size=40,30,10
first: 1 step
  Box1         ok  volume 12000.00 mm^3
wrote first.khan
```

Khan made a design with one step, `Box1`, checked that it builds (`ok`), and saved
it as `first.khan`.

## 3. Add to it

Cut a round hole through the middle. First add a cylinder where the hole goes,
then cut it out of the box:

<!-- cli-example -->
```console
$ kh_cli run --open first.khan --save first.khan feature.add_cylinder base=20,15,-1 radius=6 height=12
first: 2 steps
  Box1         ok  volume 12000.00 mm^3
  Cylinder1    ok  volume 1357.17 mm^3
wrote first.khan
$ kh_cli run --open first.khan --save first.khan feature.add_boolean operation=cut a=Box1 b=Cylinder1
first: 3 steps
  Box1         ok  volume 12000.00 mm^3
  Cylinder1    ok  volume 1357.17 mm^3
  Cut1         ok  volume 10869.03 mm^3
wrote first.khan
```

![The box with a hole through it](../cli/images/run-block.png)

## 4. Export it

<!-- cli-example -->
```console
$ kh_cli open first.khan --export first.stl
first: 3 steps
  Box1         ok  volume 12000.00 mm^3
  Cylinder1    ok  volume 1357.17 mm^3
  Cut1         ok  volume 10869.03 mm^3
wrote first.stl
```

Open `first.stl` in a slicer to print it, or in any 3D viewer to look at it.

## Next

- Do [Tutorial 1: Keychain tag](../tutorials/01-keychain-tag.md).
- Read [Designs and steps](designs-and-steps.md) to see how it all fits together.
