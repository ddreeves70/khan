#include "kh/plugin/Overlays.h"

#include <algorithm>
#include <utility>

namespace kh::plugin {

namespace {

bool same(const kernel::Vec3& a, const kernel::Vec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool same(const Overlay& a, const Overlay& b) {
    const auto& p = a.lattice;
    const auto& q = b.lattice;
    return a.id == b.id && a.shape == b.shape && a.color == b.color && a.size == b.size &&
           a.visible == b.visible && a.points.size() == b.points.size() &&
           std::equal(a.points.begin(), a.points.end(), b.points.begin(),
                      [](const auto& x, const auto& y) { return same(x, y); }) &&
           same(p.origin, q.origin) && same(p.xAxis, q.xAxis) && same(p.yAxis, q.yAxis) &&
           p.spacing == q.spacing && p.halfSize == q.halfSize && p.dots == q.dots;
}

}  // namespace

void OverlayList::set(Overlay overlay) {
    auto it = std::find_if(overlays_.begin(), overlays_.end(),
                           [&](const Overlay& o) { return o.id == overlay.id; });
    if (it == overlays_.end()) {
        overlays_.push_back(std::move(overlay));
        bus_.publish(OverlaysChanged{overlays_.back().id});
        return;
    }
    if (same(*it, overlay)) return;
    *it = std::move(overlay);
    bus_.publish(OverlaysChanged{it->id});
}

void OverlayList::remove(std::string_view id) {
    auto it = std::find_if(overlays_.begin(), overlays_.end(), [&](const Overlay& o) { return o.id == id; });
    if (it == overlays_.end()) return;
    std::string gone = it->id;
    overlays_.erase(it);
    bus_.publish(OverlaysChanged{std::move(gone)});
}

void OverlayList::removeOwner(std::string_view plugin) {
    const std::string prefix = std::string(plugin) + ".";
    std::vector<std::string> gone;
    for (const auto& o : overlays_) {
        if (o.id.rfind(prefix, 0) == 0) gone.push_back(o.id);
    }
    for (const auto& id : gone) remove(id);
}

const Overlay* OverlayList::find(std::string_view id) const {
    for (const auto& o : overlays_) {
        if (o.id == id) return &o;
    }
    return nullptr;
}

void PluginOverlays::set(std::string_view name, Overlay overlay) {
    overlay.id = fullId(name);
    list_.set(std::move(overlay));
}

void PluginOverlays::remove(std::string_view name) {
    list_.remove(fullId(name));
}

}  // namespace kh::plugin
