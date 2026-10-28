#pragma once

#include <string>

#include "kh/sketch/Sketch.h"

namespace kh::cli {

// kh_cli plugins [--plugins <folder>]...
// Loads kh.modeling and every plugin in the folders, and lists each one with
// its state, commands and problems. Returns 1 if any plugin didn't load.
int listPlugins(int argc, char** argv);

// kh_cli run [--plugins <folder>]... [--open <in.khan>] [--save <out.khan>] <command> [name=value]...
// Runs one command by name (from any loaded plugin) on a new or opened design,
// rebuilds it with OCCT, prints each step, and saves it.
int runCommand(int argc, char** argv);

// kh_cli help [--plugins <folder>]... [<command>]
// Lists every command that `run` can run, or describes one command and its settings.
int help(int argc, char** argv);

// One line about a solved sketch, like "4 lines, 8 constraints, fully constrained".
std::string sketchSummary(const sketch::Sketch& sketch, int freedom);

}  // namespace kh::cli
