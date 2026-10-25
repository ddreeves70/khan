// The Ruler contract: what other plugins may use from example.ruler.
// It has no code of its own, so a plugin can use the ruler without linking to it.
#pragma once

namespace example::ruler {

// The ruler, as other plugins see it. They find it with
// ctx.service<example::ruler::IRuler>().
class IRuler {
public:
    static constexpr const char* kServiceName = "example.ruler/1";  // version 1
    virtual ~IRuler() = default;
    virtual const char* unit() const = 0;        // "mm" or "in"
    virtual double fromMm(double mm) const = 0;  // a length in mm, in the ruler's unit
};

// Sent when the ruler's unit changes, and once when Khan starts.
struct UnitChanged {
    static constexpr const char* kType = "example.ruler.unit_changed";
    const char* unit = "mm";
};

}  // namespace example::ruler
