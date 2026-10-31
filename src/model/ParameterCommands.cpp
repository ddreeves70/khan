// The commands that change a design's parameters.

#include <algorithm>

#include "kh/model/Commands.h"
#include "kh/model/Document.h"
#include "kh/model/Formulas.h"

namespace kh::model {

namespace {

Diagnostic error(std::string code, std::string message) {
    return {std::move(code), Severity::Error, std::move(message), {}};
}

// "a", "a and b", "a, b and c"
std::string listed(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out += i + 1 == names.size() ? " and " : ", ";
        out += names[i];
    }
    return out;
}

// Why there's no parameter by a name, with a hint if only upper and lower case are wrong.
Diagnostic noSuchParameter(const ParameterTable& table, const std::string& name) {
    const auto* close = table.findIgnoringCase(name);
    return error("parameter.unknown",
                 "There's no parameter called " + name +
                     (close ? ". Did you mean " + close->name + "? Upper and lower case must match."
                            : ". Check the spelling."));
}

// Finds the sketch steps whose dimensions change when the parameters go from
// `before` to `after`, and makes their new versions, ready to swap in. With
// `renaming`, formulas that use renaming->first use renaming->second instead.
// A dimension that worked before and wouldn't now is a problem, named in words
// that start with `change`; one that didn't work before is left as it is.
Diagnostics updateDimensions(const Document& doc, const ParameterValues& before, const ParameterValues& after,
                             const std::string& change, const std::pair<std::string, std::string>* renaming,
                             std::vector<std::unique_ptr<Feature>>& out) {
    out.clear();
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        const Feature& f = doc.featureAt(i);
        if (f.type() != FeatureType::Sketch) continue;
        const auto& step = static_cast<const SketchFeature&>(f);
        sketch::Sketch s = step.sketch();
        bool changed = false;
        for (auto& c : s.constraints) {
            if (c.expression.empty() || !sketch::isDimension(c.kind)) continue;
            std::string formula = c.expression;
            if (renaming) {
                const auto e = Expression::parse(formula);
                const auto refs = e.references();
                if (std::find(refs.begin(), refs.end(), renaming->first) != refs.end()) {
                    formula = e.renamed(renaming->first, renaming->second);
                }
            }
            const std::string what = c.name + " in " + step.name();
            const auto now = evalDimension(c.kind, formula, after, what);
            if (now.ok()) {
                if (now.value->value != c.value) {
                    c.value = now.value->value;
                    changed = true;
                }
            } else if (evalDimension(c.kind, c.expression, before, what).ok()) {
                out.clear();
                return {error("parameter.breaks",
                              change + " would stop " + what + " working: " + now.issues.front().message)};
            }
            if (formula != c.expression) {
                c.expression = std::move(formula);
                changed = true;
            }
        }
        if (changed) {
            auto copy = step.clone();
            static_cast<SketchFeature&>(*copy).setSketch(std::move(s));
            out.push_back(std::move(copy));
        }
    }
    return {};
}

// Swaps steps into the design for the ones with the same ids; afterwards
// `steps` holds the versions they replaced.
void swapIn(DocumentEditor& editor, std::vector<std::unique_ptr<Feature>>& steps) {
    for (auto& f : steps) f = editor.replace(std::move(f));
}

}  // namespace

// ---------------------------------------------------------------------------
// SetParameterCommand
// ---------------------------------------------------------------------------

SetParameterCommand::SetParameterCommand(std::string name, std::string expression,
                                         std::optional<std::string> note)
    : name_(std::move(name)), expression_(std::move(expression)), note_(std::move(note)) {}

std::string SetParameterCommand::label() const {
    return old_ ? "Change " + name_ + " to " + expression_ : "Add parameter " + name_;
}

Diagnostics SetParameterCommand::apply(DocumentEditor& editor) {
    auto& table = editor.parameters();
    if (auto issues = ParameterTable::checkName(name_); !issues.empty()) return issues;
    const auto formula = Expression::parse(expression_);
    if (!formula.ok()) return formula.problems();

    const Parameter* existing = table.find(name_);
    if (!existing) {
        if (const auto* close = table.findIgnoringCase(name_)) {
            return {
                error("parameter.name_taken", "There's already a parameter called " + close->name +
                                                  ". Two names can't differ only in upper and lower case: "
                                                  "change " +
                                                  close->name + ", or pick another name.")};
        }
    }
    Parameter next = existing ? *existing : Parameter{id_.isNil() ? ParamId::generate() : id_, name_, {}, {}};
    next.expression = expression_;
    if (note_) next.note = *note_;

    // Work everything out with the change, before making it.
    ParameterTable trial = table;
    trial.define(next);
    const auto before = table.evaluate();
    const auto after = trial.evaluate();
    if (const auto* mine = after.find(name_); mine && !mine->ok()) return mine->issues;
    for (const auto& p : table.parameters()) {
        if (p.name == name_) continue;
        const auto* was = before.find(p.name);
        const auto* now = after.find(p.name);
        if (was && was->ok() && now && !now->ok()) {
            return {error("parameter.breaks", "Changing " + name_ + " to " + expression_ + " would stop " +
                                                  p.name + " working: " + now->issues.front().message)};
        }
    }
    // Sketch dimensions that use it follow.
    if (auto issues = updateDimensions(editor.document(), before, after,
                                       "Changing " + name_ + " to " + expression_, nullptr, steps_);
        !issues.empty()) {
        return issues;
    }

    old_ = existing ? std::optional<Parameter>(*existing) : std::nullopt;
    id_ = next.id;
    table.define(std::move(next));
    swapIn(editor, steps_);
    return {};
}

void SetParameterCommand::revert(DocumentEditor& editor) {
    swapIn(editor, steps_);
    if (old_) {
        editor.parameters().define(*old_);
    } else {
        editor.parameters().remove(id_);
    }
}

// ---------------------------------------------------------------------------
// RenameParameterCommand
// ---------------------------------------------------------------------------

RenameParameterCommand::RenameParameterCommand(std::string name, std::string to)
    : name_(std::move(name)), to_(std::move(to)) {}

std::string RenameParameterCommand::label() const {
    return "Rename " + name_ + " to " + to_;
}

Diagnostics RenameParameterCommand::apply(DocumentEditor& editor) {
    auto& table = editor.parameters();
    const Parameter* renaming = table.find(name_);
    if (!renaming) return {noSuchParameter(table, name_)};
    if (to_ == name_) return {error("parameter.same_name", name_ + " is already called " + to_ + ".")};
    if (auto issues = ParameterTable::checkName(to_); !issues.empty()) return issues;
    if (const auto* other = table.findIgnoringCase(to_); other && other->id != renaming->id) {
        return {error("parameter.name_taken",
                      "There's already a parameter called " + other->name + ". Pick another name.")};
    }

    // The parameter itself, and every formula that uses it.
    old_.clear();
    std::vector<Parameter> changed;
    for (const auto& p : table.parameters()) {
        const auto formula = Expression::parse(p.expression);
        const auto refs = formula.references();
        const bool uses = std::find(refs.begin(), refs.end(), name_) != refs.end();
        if (p.id != renaming->id && !uses) continue;
        old_.push_back(p);
        Parameter next = p;
        if (p.id == renaming->id) next.name = to_;
        if (uses) next.expression = formula.renamed(name_, to_);
        changed.push_back(std::move(next));
    }
    ParameterTable trial = table;
    for (const auto& p : changed) trial.define(p);
    const std::pair<std::string, std::string> names{name_, to_};
    if (auto issues = updateDimensions(editor.document(), table.evaluate(), trial.evaluate(),
                                       "Renaming " + name_ + " to " + to_, &names, steps_);
        !issues.empty()) {
        return issues;
    }
    for (auto& p : changed) table.define(std::move(p));
    swapIn(editor, steps_);
    return {};
}

void RenameParameterCommand::revert(DocumentEditor& editor) {
    swapIn(editor, steps_);
    for (const auto& p : old_) editor.parameters().define(p);
}

// ---------------------------------------------------------------------------
// DeleteParameterCommand
// ---------------------------------------------------------------------------

DeleteParameterCommand::DeleteParameterCommand(std::string name) : name_(std::move(name)) {}

std::string DeleteParameterCommand::label() const {
    return "Delete parameter " + name_;
}

Diagnostics DeleteParameterCommand::apply(DocumentEditor& editor) {
    auto& table = editor.parameters();
    const Parameter* p = table.find(name_);
    if (!p) return {noSuchParameter(table, name_)};
    auto users = table.dependents(name_);
    users.erase(std::remove(users.begin(), users.end(), name_), users.end());
    const auto& doc = editor.document();
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        const Feature& f = doc.featureAt(i);
        if (f.type() != FeatureType::Sketch) continue;
        for (const auto& d : dimensionsUsing(static_cast<const SketchFeature&>(f).sketch(), name_)) {
            users.push_back(d + " in " + f.name());
        }
    }
    if (!users.empty()) {
        const bool one = users.size() == 1;
        const std::string them = one ? users.front() : "them";
        return {error("parameter.in_use", listed(users) + (one ? " uses " : " use ") + name_ + ", so " +
                                              name_ + " can't be deleted. Change " + them +
                                              (one ? " so it doesn't" : " so they don't") + " use " + name_ +
                                              " first, or delete " + them + " too.")};
    }
    removed_ = *p;
    index_ = table.remove(p->id).value_or(0);
    return {};
}

void DeleteParameterCommand::revert(DocumentEditor& editor) {
    if (removed_) editor.parameters().insert(index_, *removed_);
}

// ---------------------------------------------------------------------------
// ReplaceParametersCommand
// ---------------------------------------------------------------------------

ReplaceParametersCommand::ReplaceParametersCommand(std::vector<Parameter> parameters)
    : parameters_(std::move(parameters)) {}

Diagnostics ReplaceParametersCommand::apply(DocumentEditor& editor) {
    ParameterTable next;
    for (const auto& p : parameters_) {
        if (auto issues = ParameterTable::checkName(p.name); !issues.empty()) return issues;
        if (const auto* other = next.findIgnoringCase(p.name)) {
            return {error("parameter.name_taken", "There are two parameters called " + other->name + ".")};
        }
        if (p.id.isNil() || next.find(p.id)) {
            return {error("parameter.id", "The parameter " + p.name + " has no id of its own.")};
        }
        next.define(p);
    }
    old_ = editor.parameters();
    editor.parameters() = std::move(next);
    return {};
}

void ReplaceParametersCommand::revert(DocumentEditor& editor) {
    editor.parameters() = old_;
}

}  // namespace kh::model
