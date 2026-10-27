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
    return a.id == b.id && a.pane == b.pane && a.shape == b.shape && a.color == b.color && a.size == b.size &&
           a.visible == b.visible && a.texts == b.texts && a.points.size() == b.points.size() &&
           std::equal(a.points.begin(), a.points.end(), b.points.begin(),
                      [](const auto& x, const auto& y) { return same(x, y); }) &&
           same(p.origin, q.origin) && same(p.xAxis, q.xAxis) && same(p.yAxis, q.yAxis) &&
           p.spacing == q.spacing && p.halfSize == q.halfSize && p.dots == q.dots;
}

}  // namespace

void OverlayList::set(Overlay overlay) {
    auto it = std::find_if(overlays_.begin(), overlays_.end(),
                           [&](const Overlay& o) { return o.id == overlay.id && o.pane == overlay.pane; });
    if (it == overlays_.end()) {
        overlays_.push_back(std::move(overlay));
        bus_.publish(OverlaysChanged{overlays_.back().id, overlays_.back().pane});
        return;
    }
    if (same(*it, overlay)) return;
    *it = std::move(overlay);
    bus_.publish(OverlaysChanged{it->id, it->pane});
}

void OverlayList::remove(std::string_view id, std::string_view pane) {
    auto it = std::find_if(overlays_.begin(), overlays_.end(),
                           [&](const Overlay& o) { return o.id == id && o.pane == pane; });
    if (it == overlays_.end()) return;
    OverlaysChanged gone{it->id, it->pane};
    overlays_.erase(it);
    bus_.publish(gone);
}

void OverlayList::removeOwner(std::string_view plugin) {
    const std::string prefix = std::string(plugin) + ".";
    std::vector<std::pair<std::string, std::string>> gone;
    for (const auto& o : overlays_) {
        if (o.id.rfind(prefix, 0) == 0) gone.emplace_back(o.id, o.pane);
    }
    for (const auto& [id, pane] : gone) remove(id, pane);
}

const Overlay* OverlayList::find(std::string_view id, std::string_view pane) const {
    for (const auto& o : overlays_) {
        if (o.id == id && o.pane == pane) return &o;
    }
    return nullptr;
}

std::vector<Overlay> OverlayList::forPane(std::string_view pane) const {
    std::vector<Overlay> out;
    for (const auto& o : overlays_) {
        if (!o.pane.empty()) continue;
        const Overlay* own = pane.empty() ? nullptr : find(o.id, pane);
        out.push_back(own ? *own : o);
    }
    // A pane's own overlay with no every-pane one of the same name.
    for (const auto& o : overlays_) {
        if (!pane.empty() && o.pane == pane && !find(o.id)) out.push_back(o);
    }
    return out;
}

void PluginOverlays::set(std::string_view name, Overlay overlay, std::string_view pane) {
    overlay.id = fullId(name);
    overlay.pane = std::string(pane);
    list_.set(std::move(overlay));
}

void PluginOverlays::remove(std::string_view name, std::string_view pane) {
    list_.remove(fullId(name), pane);
}

}  // namespace kh::plugin
