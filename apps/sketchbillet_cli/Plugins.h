#pragma once

#include <string>

#include "sb/model/Document.h"
#include "sb/sketch/Sketch.h"

namespace sb::cli {

// sketchbillet_cli plugins [--plugins <folder>]...
// Loads sb.modeling and every plugin in the folders, and lists each one with
// its state, commands and problems. Returns 1 if any plugin didn't load.
int listPlugins(int argc, char** argv);

// sketchbillet_cli run [--plugins <folder>]... [--open <in.sb>] [--save <out.sb>] <command> [name=value]...
// Runs one command by name (from any loaded plugin) on a new or opened design,
// rebuilds it with OCCT, prints each step, and saves it.
int runCommand(int argc, char** argv);

// sketchbillet_cli help [--plugins <folder>]... [<command>]
// Lists every command that `run` can run, or describes one command and its settings.
int help(int argc, char** argv);

// One line about a solved sketch, like "4 lines, 8 constraints, fully constrained".
std::string sketchSummary(const sketch::Sketch& sketch, int freedom);

// Lists a step's numbers that come from formulas, one per line under the step:
// "    distance = wall * 4  (12 mm)".
void printFormulas(const model::Feature& step, LengthUnit unit, const char* indent);

// Lists the design's parameters, if it has any, after its steps: each with its
// formula and, if that isn't just the value, what it works out to.
void printParameters(const model::Document& doc);

}  // namespace sb::cli
