#pragma once

/// @file
/// Steps as text, for copying and pasting between designs (the clipboard).

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/core/Id.h"
#include "kh/model/Document.h"
#include "kh/model/Feature.h"
#include "kh/model/Material.h"

namespace kh::io {

/// Steps read back from text.
struct StepsResult {
    std::vector<std::unique_ptr<model::Feature>>
        steps;                               ///< In order, with new ids; empty if the text couldn't be read.
    std::vector<model::Material> materials;  ///< The design's own materials the steps use.
    Diagnostics issues;                      ///< Why the text couldn't be read, in plain words.

    /// Checks whether any steps were read.
    /// @return True if @ref steps isn't empty.
    bool ok() const { return !steps.empty(); }
};

/// Writes steps as text for the clipboard: the steps, every step they use, and
/// the design's own materials they use, in the same form as in a .khan file.
/// @code
/// const std::string text = kh::io::stepsToText(doc, {cutId});  // Cut1, Box1 and Cylinder1
/// auto pasted = kh::io::stepsFromText(text);
/// other.execute(std::make_unique<kh::model::AddFeaturesCommand>(std::move(pasted.steps),
///                                                              std::move(pasted.materials), "Paste"));
/// @endcode
/// @param doc The design the steps are in.
/// @param ids The steps to copy.
/// @return The text (JSON), or an empty string if none of @p ids is in the design.
std::string stepsToText(const model::Document& doc, const std::vector<FeatureId>& ids);

/// Checks whether text looks like steps copied from Khan, without reading it all.
/// @param text Text from the clipboard.
/// @return True if it starts like stepsToText()'s text.
bool looksLikeSteps(std::string_view text);

/// Reads steps written by stepsToText(), with new ids, ready for AddFeaturesCommand.
/// @param text The text.
/// @return The steps and materials, or why they couldn't be read.
StepsResult stepsFromText(std::string_view text);

}  // namespace kh::io
