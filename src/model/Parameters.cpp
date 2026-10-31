#include "kh/model/Parameters.h"

#include <algorithm>
#include <cctype>

namespace kh::model {

namespace {

Diagnostic error(std::string code, std::string message) {
    return {std::move(code), Severity::Error, std::move(message), {}};
}

bool sameIgnoringCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
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

// Why the parameters in a loop can't be worked out. `loop` lists them in the
// order each uses the next, the last using the first.
std::string loopMessage(const std::vector<std::string>& loop) {
    if (loop.size() == 1) {
        return loop[0] + " uses itself, so Khan can't work it out. Give it a value that doesn't use " +
               loop[0] + ", like 3 mm.";
    }
    std::vector<std::string> uses;
    for (std::size_t i = 0; i < loop.size(); ++i)
        uses.push_back(loop[i] + " uses " + loop[(i + 1) % loop.size()]);
    return listed(uses) + ", so Khan can't work " + (loop.size() == 2 ? "either" : "any of them") +
           " out. Change one so it doesn't use the other" + (loop.size() == 2 ? "" : "s") + ".";
}

}  // namespace

// ---------------------------------------------------------------------------
// ParameterValues
// ---------------------------------------------------------------------------

const ParameterValue* ParameterValues::find(std::string_view name) const {
    const auto it = values_.find(name);
    return it == values_.end() ? nullptr : &it->second;
}

bool ParameterValues::ok() const {
    return std::all_of(values_.begin(), values_.end(), [](const auto& v) { return v.second.ok(); });
}

NameLookup ParameterValues::lookup() const {
    return [this](std::string_view name) -> std::optional<Quantity> {
        const auto* v = find(name);
        return v ? v->value : std::nullopt;
    };
}

// ---------------------------------------------------------------------------
// ParameterTable
// ---------------------------------------------------------------------------

const Parameter* ParameterTable::find(std::string_view name) const {
    const auto it =
        std::find_if(list_.begin(), list_.end(), [&](const Parameter& p) { return p.name == name; });
    return it == list_.end() ? nullptr : &*it;
}

const Parameter* ParameterTable::findIgnoringCase(std::string_view name) const {
    const auto it = std::find_if(list_.begin(), list_.end(),
                                 [&](const Parameter& p) { return sameIgnoringCase(p.name, name); });
    return it == list_.end() ? nullptr : &*it;
}

const Parameter* ParameterTable::find(const ParamId& id) const {
    const auto it = std::find_if(list_.begin(), list_.end(), [&](const Parameter& p) { return p.id == id; });
    return it == list_.end() ? nullptr : &*it;
}

void ParameterTable::define(Parameter parameter) {
    const auto it =
        std::find_if(list_.begin(), list_.end(), [&](const Parameter& p) { return p.id == parameter.id; });
    if (it != list_.end()) {
        *it = std::move(parameter);
    } else {
        list_.push_back(std::move(parameter));
    }
}

void ParameterTable::insert(std::size_t index, Parameter parameter) {
    index = std::min(index, list_.size());
    list_.insert(list_.begin() + static_cast<std::ptrdiff_t>(index), std::move(parameter));
}

std::optional<std::size_t> ParameterTable::remove(const ParamId& id) {
    const auto it = std::find_if(list_.begin(), list_.end(), [&](const Parameter& p) { return p.id == id; });
    if (it == list_.end()) return std::nullopt;
    const auto index = static_cast<std::size_t>(it - list_.begin());
    list_.erase(it);
    return index;
}

std::vector<std::string> ParameterTable::dependents(std::string_view name) const {
    std::vector<std::string> out;
    for (const auto& p : list_) {
        const auto refs = Expression::parse(p.expression).references();
        if (std::find(refs.begin(), refs.end(), name) != refs.end()) out.push_back(p.name);
    }
    return out;
}

ParameterValues ParameterTable::evaluate() const {
    ParameterValues out;
    const std::size_t n = list_.size();
    std::vector<Expression> formulas;
    formulas.reserve(n);
    for (const auto& p : list_) formulas.push_back(Expression::parse(p.expression));

    auto indexOf = [&](std::string_view name) -> std::optional<std::size_t> {
        for (std::size_t i = 0; i < n; ++i) {
            if (list_[i].name == name) return i;
        }
        return std::nullopt;
    };

    enum class State { New, Working, Done };
    std::vector<State> state(n, State::New);
    std::vector<std::size_t> path;  // the parameters being worked out, each using the next

    // Depth first, so each parameter is worked out after the ones it uses.
    auto visit = [&](auto&& self, std::size_t i) -> void {
        state[i] = State::Working;
        path.push_back(i);
        const auto& p = list_[i];
        ParameterValue result;
        if (!formulas[i].ok()) result.issues = formulas[i].problems();
        if (result.issues.empty()) {
            for (const auto& ref : formulas[i].references()) {
                const auto j = indexOf(ref);
                if (!j) {
                    // A name that's only wrong in upper and lower case gets a hint.
                    const auto* close = findIgnoringCase(ref);
                    result.issues.push_back(error(
                        "expression.unknown_name",
                        "There's no parameter called " + ref +
                            (close ? ". Did you mean " + close->name + "? Upper and lower case must match."
                                   : ". Check the spelling, or add it first.")));
                    break;
                }
                if (state[*j] == State::Working) {
                    // A loop: every parameter on it, from *j to here, gets the same problem.
                    const auto from = std::find(path.begin(), path.end(), *j);
                    std::vector<std::string> loop;
                    for (auto it = from; it != path.end(); ++it) loop.push_back(list_[*it].name);
                    const auto message = loopMessage(loop);
                    for (auto it = from; it != path.end(); ++it) {
                        auto& v = out.values_[list_[*it].name];
                        v.value.reset();
                        if (v.issues.empty()) v.issues.push_back(error("parameter.loop", message));
                    }
                    break;
                }
                if (state[*j] == State::New) self(self, *j);
                const auto* used = out.find(ref);
                if (!used || !used->ok()) {
                    result.issues.push_back(error(
                        "parameter.uses_problem",
                        p.name + " uses " + ref + ", which can't be worked out. Fix " + ref + " first."));
                    break;
                }
            }
        }
        // A loop found further down may already have given this one its problem.
        auto& slot = out.values_[p.name];
        if (slot.issues.empty()) {
            if (result.issues.empty()) {
                auto r = formulas[i].eval(out.lookup());
                result.value = r.value;
                result.issues = std::move(r.issues);
            }
            slot = std::move(result);
        }
        path.pop_back();
        state[i] = State::Done;
        out.order_.push_back(p.name);
    };
    for (std::size_t i = 0; i < n; ++i) {
        if (state[i] == State::New) visit(visit, i);
    }
    return out;
}

Diagnostics ParameterTable::checkName(std::string_view name) {
    if (name.empty()) return {error("parameter.name", "A parameter needs a name, like wall.")};
    const std::string shown(name);
    const bool startsWell = std::isalpha(static_cast<unsigned char>(name.front()));
    const bool allGood = std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    });
    if (!startsWell || !allGood) {
        return {error("parameter.name",
                      shown + " can't be a parameter's name. Use letters, digits and _, starting with "
                              "a letter, like wall_2.")};
    }
    if (name.size() > 40) {
        return {error("parameter.name",
                      "A parameter's name can be at most 40 characters long. Make " + shown + " shorter.")};
    }
    if (Expression::isReservedWord(name)) {
        return {error("parameter.name",
                      shown +
                          " already means something in formulas (it's a unit, a function or pi). "
                          "Pick another name, like " +
                          shown + "_size.")};
    }
    return {};
}

}  // namespace kh::model
