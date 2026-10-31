#include "kh/model/Formulas.h"

#include <algorithm>

namespace kh::model {

Dimension dimensionOf(sketch::ConstraintKind kind) {
    return kind == sketch::ConstraintKind::Angle ? kAngle : kLength;
}

EvalResult evalDimension(sketch::ConstraintKind kind, std::string_view formula, const ParameterValues& values,
                         std::string_view what) {
    const bool mustBePositive =
        kind == sketch::ConstraintKind::Length || kind == sketch::ConstraintKind::Distance ||
        kind == sketch::ConstraintKind::Radius || kind == sketch::ConstraintKind::Diameter;
    return evalNumber(dimensionOf(kind), mustBePositive, formula, values, what);
}

EvalResult evalNumber(const Dimension& wanted, bool mustBePositive, std::string_view formula,
                      const ParameterValues& values, std::string_view what) {
    const std::string name(what);
    EvalResult r = Expression::parse(formula).eval(values.lookup());
    if (!r.ok()) {
        // Say which dimension the problem is in.
        for (auto& d : r.issues) d.message = name + " uses " + std::string(formula) + ". " + d.message;
        return r;
    }
    if (r.value->dim != wanted) {
        const bool angle = wanted == kAngle;
        const std::string got = dimensionName(r.value->dim);
        r.issues = {{"dimension.units",
                     Severity::Error,
                     name + " needs " +
                         (angle ? "an angle, like 30 deg" : "a length, like 20 mm or wall * 2") + ", but " +
                         std::string(formula) + " is " + (got.front() == 'a' ? "an " : "a ") + got + "." +
                         (r.value->dim == kPlainNumber && Expression::parse(formula).isConstant()
                              ? std::string(" Give it a unit, like ") + std::string(formula) +
                                    (angle ? " deg." : " mm.")
                              : ""),
                     {}}};
        r.value.reset();
        return r;
    }
    if (mustBePositive && r.value->value <= 0) {
        r.issues = {{"dimension.size",
                     Severity::Error,
                     name + " would be " + lengthText(r.value->value, LengthUnit::Millimeter) + " (" +
                         std::string(formula) + "), but it must be more than 0 mm.",
                     {}}};
        r.value.reset();
    }
    return r;
}

std::vector<std::string> numbersUsing(const Feature& step, std::string_view name) {
    std::vector<std::string> out;
    for (const auto& [key, text] : step.formulas()) {
        const auto refs = Expression::parse(text).references();
        if (std::find(refs.begin(), refs.end(), name) != refs.end()) out.push_back(key);
    }
    return out;
}

std::vector<std::string> dimensionsUsing(const sketch::Sketch& sketch, std::string_view name) {
    std::vector<std::string> out;
    for (const auto& c : sketch.constraints) {
        if (c.expression.empty()) continue;
        const auto refs = Expression::parse(c.expression).references();
        if (std::find(refs.begin(), refs.end(), name) != refs.end()) out.push_back(c.name);
    }
    return out;
}

}  // namespace kh::model
