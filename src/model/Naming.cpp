#include "sb/model/Naming.h"

#include "sb/model/Document.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace sb::model {

namespace {

using kernel::ElementInfo;
using kernel::TopoKind;

// Middles closer than this (mm) count as the same place when ordering "#k".
constexpr double kSamePlace = 1e-6;

bool before(const ElementInfo& a, const ElementInfo& b) {
    const double da[3] = {a.center.x, a.center.y, a.center.z};
    const double db[3] = {b.center.x, b.center.y, b.center.z};
    for (int k = 0; k < 3; ++k) {
        if (std::abs(da[k] - db[k]) > kSamePlace) return da[k] < db[k];
    }
    return false;
}

// Gives elements that share a name "#1", "#2", ... in order of where their middles are.
void numberRepeats(std::vector<std::string>& names, const std::vector<ElementInfo>& infos) {
    std::map<std::string, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < names.size(); ++i) groups[names[i]].push_back(i);
    for (auto& [name, members] : groups) {
        if (members.size() < 2) continue;
        // Stable, so elements at the same place keep the kernel's order (only as a last resort).
        std::stable_sort(members.begin(), members.end(), [&](std::size_t a, std::size_t b) {
            return a < infos.size() && b < infos.size() && before(infos[a], infos[b]);
        });
        for (std::size_t k = 0; k < members.size(); ++k)
            names[members[k]] = name + "#" + std::to_string(k + 1);
    }
}

std::string joined(std::vector<std::string> parts, char sep) {
    std::sort(parts.begin(), parts.end());
    parts.erase(std::unique(parts.begin(), parts.end()), parts.end());
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

// The names inside an edge's "E[a|b]" or a vertex's "V[a|b|c]", or a merged
// face's "a+b" parts; split only at the top level (face names can hold edge
// names, with their own brackets).
std::vector<std::string> topLevelParts(std::string_view name, TopoKind kind) {
    std::string_view inside = name;
    char sep = '+';
    if (kind != TopoKind::Face) {
        inside = nameStem(name);
        if (inside.size() < 3 || inside[1] != '[' || inside.back() != ']') return {};
        inside = inside.substr(2, inside.size() - 3);
        sep = '|';
    }
    std::vector<std::string> out;
    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < inside.size(); ++i) {
        const char c = inside[i];
        if (c == '[') ++depth;
        if (c == ']') --depth;
        if (c == sep && depth == 0) {
            out.emplace_back(nameStem(inside.substr(start, i - start)));
            start = i + 1;
        }
    }
    if (!inside.empty()) out.emplace_back(nameStem(inside.substr(start)));
    return out;
}

// How many of `a`'s parts are among `b`'s.
int shared(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::multiset<std::string> rest(b.begin(), b.end());
    int n = 0;
    for (const auto& x : a) {
        auto it = rest.find(x);
        if (it != rest.end()) {
            ++n;
            rest.erase(it);
        }
    }
    return n;
}

// How closely an element is related to a reference by name: 2 for the same
// origin (same stem, or an edge or vertex between all the same faces), 1 for
// part of it (some of the same faces, or one of a merged face's parts), 0 for none.
int relation(std::string_view ref, std::string_view candidate, TopoKind kind) {
    if (kind == TopoKind::Face) {
        if (nameStem(ref) == nameStem(candidate)) return 2;
        return shared(topLevelParts(nameStem(ref), kind), topLevelParts(nameStem(candidate), kind)) > 0 ? 1
                                                                                                        : 0;
    }
    const auto a = topLevelParts(ref, kind), b = topLevelParts(candidate, kind);
    if (a.empty() || b.empty()) return 0;
    const int n = shared(a, b);
    if (n == static_cast<int>(a.size()) && a.size() == b.size()) return 2;
    // An edge that kept one of its faces; a vertex that kept at least two.
    const int needed = kind == TopoKind::Edge ? 1 : std::min(2, static_cast<int>(a.size()));
    return n >= needed ? 1 : 0;
}

double length(const kernel::Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// How unlike the signature an element is: 0 for the same; larger is less like it.
double unlikeness(const ElementInfo& want, const ElementInfo& have, TopoKind kind) {
    const kernel::Vec3 d{have.center.x - want.center.x, have.center.y - want.center.y,
                         have.center.z - want.center.z};
    // Distances count relative to the element's own size, so a big face moving
    // 1 mm is still "the same place", but a tiny edge isn't.
    double scale = 1;
    if (kind == TopoKind::Face) scale = std::max(1.0, std::sqrt(std::max(want.size, 0.0)));
    if (kind == TopoKind::Edge) scale = std::max(1.0, want.size);
    double score = length(d) / scale;
    const double lw = length(want.direction), lh = length(have.direction);
    if (lw > 0.5 && lh > 0.5) {
        double dot = (want.direction.x * have.direction.x + want.direction.y * have.direction.y +
                      want.direction.z * have.direction.z) /
                     (lw * lh);
        // A flat face's normal points out, so its sign counts; an axis's or a line's doesn't.
        if (!(kind == TopoKind::Face && want.geom == kernel::GeomKind::Plane)) dot = std::abs(dot);
        score += 2 * (1 - dot);
    }
    if (want.size > 0) score += std::abs(have.size - want.size) / want.size;
    if (want.radius > 0) score += std::abs(have.radius - want.radius) / want.radius;
    return score;
}

}  // namespace

int ShapeNames::find(TopoKind kind, std::string_view name) const {
    const auto& list = of(kind);
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i] == name) return static_cast<int>(i);
    }
    return -1;
}

std::string_view nameStem(std::string_view name) {
    while (true) {
        const auto hash = name.rfind('#');
        if (hash == std::string_view::npos || hash + 1 == name.size()) return name;
        const auto tail = name.substr(hash + 1);
        if (!std::all_of(tail.begin(), tail.end(), [](char c) { return c >= '0' && c <= '9'; })) return name;
        name = name.substr(0, hash);
    }
}

std::vector<int> listingOrder(const kernel::Elements& elements, TopoKind kind) {
    const auto& infos = elements.of(kind);
    std::vector<int> order(infos.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    auto key = [&](int i) {
        const auto& e = infos[static_cast<std::size_t>(i)];
        // Rounded to 1e-6 mm, so tiny differences between kernels don't reorder.
        auto r = [](double v) { return std::llround(v * 1e6); };
        return std::tuple{r(e.center.z), r(e.center.y), r(e.center.x), r(e.size)};
    };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return key(a) < key(b); });
    return order;
}

std::string shortId(const std::string& id) {
    return id.substr(0, std::min<std::size_t>(8, id.size()));
}

GeometryRef referTo(const ShapeNames& names, TopoKind kind, int index) {
    GeometryRef ref;
    ref.kind = kind;
    const auto& list = names.of(kind);
    const auto& infos = names.elements.of(kind);
    if (index < 0 || static_cast<std::size_t>(index) >= list.size()) return ref;
    ref.name = list[static_cast<std::size_t>(index)];
    if (static_cast<std::size_t>(index) < infos.size())
        ref.signature = infos[static_cast<std::size_t>(index)];
    return ref;
}

Resolution resolve(const GeometryRef& ref, const ShapeNames& names) {
    Resolution out;
    if (ref.name.empty()) return out;
    const auto& list = names.of(ref.kind);
    const auto& infos = names.elements.of(ref.kind);
    if (const int exact = names.find(ref.kind, ref.name); exact >= 0) {
        out.status = ResolveStatus::Exact;
        out.index = exact;
        return out;
    }
    // Candidates of the same kind of geometry, in the closest relation by name.
    std::vector<int> best;
    int bestRelation = 0;
    for (std::size_t i = 0; i < list.size() && i < infos.size(); ++i) {
        if (infos[i].geom != ref.signature.geom) continue;
        const int r = relation(ref.name, list[i], ref.kind);
        if (r == 0 || r < bestRelation) continue;
        if (r > bestRelation) {
            best.clear();
            bestRelation = r;
        }
        best.push_back(static_cast<int>(i));
    }
    if (best.empty()) return out;  // Missing
    // From the same place (a split, a renumbering), one candidate is it. Only
    // partly related (an edge that kept one of its faces), it must also look
    // like it.
    const bool same = bestRelation == 2;
    std::vector<std::pair<double, int>> scored;
    for (const int i : best) {
        scored.emplace_back(unlikeness(ref.signature, infos[static_cast<std::size_t>(i)], ref.kind), i);
    }
    std::sort(scored.begin(), scored.end());
    const double first = scored[0].first;
    // One clearly wins: it's close, and the next is at least twice as unlike.
    const bool clear = scored.size() == 1 || scored[1].first >= 2 * first + 0.05;
    if (clear && (same ? scored.size() == 1 || first < 0.5 : first < 0.25)) {
        out.status = ResolveStatus::Matched;
        out.index = scored[0].second;
        return out;
    }
    // Several from the same place, too alike to choose: say so. Partly related
    // ones that don't clearly fit mean it's gone.
    if (!same) return out;  // Missing
    out.status = ResolveStatus::Ambiguous;
    for (const auto& s : scored) out.candidates.push_back(s.second);
    return out;
}

ShapeNames nameFromFaces(kernel::Elements elements, std::vector<std::string> faceNames) {
    ShapeNames out;
    faceNames.resize(elements.faces.size());
    numberRepeats(faceNames, elements.faces);
    out.faces = std::move(faceNames);

    for (std::size_t e = 0; e < elements.edges.size(); ++e) {
        std::vector<std::string> around;
        if (e < elements.edgeFaces.size()) {
            for (const int f : elements.edgeFaces[e])
                around.push_back(out.faces.at(static_cast<std::size_t>(f)));
        }
        out.edges.push_back("E[" + joined(std::move(around), '|') + "]");
    }
    numberRepeats(out.edges, elements.edges);

    std::vector<std::set<int>> facesAt(elements.vertices.size());
    for (std::size_t e = 0; e < elements.edgeVertices.size() && e < elements.edgeFaces.size(); ++e) {
        for (const int v : elements.edgeVertices[e]) {
            for (const int f : elements.edgeFaces[e]) facesAt.at(static_cast<std::size_t>(v)).insert(f);
        }
    }
    for (const auto& around : facesAt) {
        std::vector<std::string> parts;
        for (const int f : around) parts.push_back(out.faces.at(static_cast<std::size_t>(f)));
        out.vertices.push_back("V[" + joined(std::move(parts), '|') + "]");
    }
    numberRepeats(out.vertices, elements.vertices);
    out.elements = std::move(elements);
    return out;
}

ShapeNames carryNames(kernel::Elements elements, const kernel::History& history,
                      const std::vector<const ShapeNames*>& inputs, const std::string& creator,
                      const OriginNamer& other) {
    std::vector<std::string> faceNames;
    const bool traced = history.faces.size() == elements.faces.size();
    for (std::size_t i = 0; i < elements.faces.size(); ++i) {
        std::vector<std::string> from;
        if (traced) {
            for (const auto& o : history.faces[i]) {
                std::string name;
                const auto in = static_cast<std::size_t>(o.input);
                if (o.kind == kernel::OriginKind::Face && in < inputs.size() && inputs[in] && o.index >= 0 &&
                    static_cast<std::size_t>(o.index) < inputs[in]->faces.size()) {
                    name = inputs[in]->faces[static_cast<std::size_t>(o.index)];
                } else if (other) {
                    name = other(o);
                }
                if (!name.empty()) from.push_back(std::move(name));
            }
        }
        faceNames.push_back(from.empty() ? creator + "/new" : joined(std::move(from), '+'));
    }
    return nameFromFaces(std::move(elements), std::move(faceNames));
}

namespace {

// The step whose id starts with `id8`, or nullptr.
const Feature* stepFor(const Document& doc, std::string_view id8) {
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        const auto& f = doc.featureAt(i);
        if (shortId(f.id().toString()) == id8) return &f;
    }
    return nullptr;
}

std::string describeFace(const Document& doc, std::string_view name) {
    const std::string_view stem = nameStem(name);
    std::string piece;
    if (stem.size() < name.size()) piece = " (piece " + std::string(name.substr(name.rfind('#') + 1)) + ")";
    // Faces merged into one: say the first.
    if (const auto parts = topLevelParts(stem, TopoKind::Face); parts.size() > 1) {
        return describeFace(doc, parts.front()) + " and others joined" + piece;
    }
    const auto slash = stem.find('/');
    if (slash == std::string_view::npos) return std::string(stem) + piece;
    const Feature* step = stepFor(doc, stem.substr(0, slash));
    const std::string by = step ? step->name() : std::string("a deleted step");
    const std::string_view rest = stem.substr(slash + 1);
    const auto colon = rest.find(':');
    const std::string_view role = rest.substr(0, colon);
    const std::string_view from =
        colon == std::string_view::npos ? std::string_view() : rest.substr(colon + 1);
    std::string what;
    if (role == "top" || role == "bottom") {
        what = std::string(role) + " of " + by;
    } else if (role == "left" || role == "right" || role == "front" || role == "back") {
        what = std::string(role) + " side of " + by;
    } else if (role == "side" && from.empty()) {
        what = "side of " + by;
    } else if (role == "side") {
        // The sketch curve it was swept from, by name.
        std::string curve = "a deleted line";
        if (const auto* e = dynamic_cast<const ExtrudeFeature*>(step)) {
            if (const auto* sk = dynamic_cast<const SketchFeature*>(doc.find(e->sketch()))) {
                for (const auto& x : sk->sketch().entities) {
                    if (shortId(x.id.toString()) == from) curve = x.name + " of " + sk->name();
                }
            }
        }
        what = "side from " + curve + " (" + by + ")";
    } else if (role == "start" || role == "end") {
        what = std::string(role) + " of " + by;
    } else if (role == "round" || role == "bevel" || role == "corner") {
        what = std::string(role) + " of " + by;
    } else {
        what = "new face of " + by;
    }
    return what + piece;
}

}  // namespace

std::string describeElement(const Document& doc, TopoKind kind, std::string_view name) {
    if (kind == TopoKind::Face) return describeFace(doc, name);
    // In alphabetical order, so the words don't depend on the steps' ids.
    std::vector<std::string> faces;
    for (const auto& f : topLevelParts(name, kind)) faces.push_back(describeFace(doc, f));
    std::sort(faces.begin(), faces.end());
    std::string out;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        out += i == 0 ? "" : i + 1 == faces.size() ? " and " : ", ";
        out += faces[i];
    }
    if (kind == TopoKind::Edge) return faces.size() == 1 ? "seam of " + out : "between " + out;
    return "corner of " + out;
}

}  // namespace sb::model
