#pragma once

namespace kh::cli {

// kh_cli plugins [--plugins <folder>]...
// Loads kh.modeling and every plugin in the folders, and lists each one with
// its state, commands and problems. Returns 1 if any plugin didn't load.
int listPlugins(int argc, char** argv);

// kh_cli run [--plugins <folder>]... [--open <in.khan>] [--save <out.khan>] <command> [name=value]...
// Runs one command by name (from any loaded plugin) on a new or opened design,
// rebuilds it with OCCT, prints each step, and saves it.
int runCommand(int argc, char** argv);

}  // namespace kh::cli
