#include "Regression.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace kh::cli {

namespace {

using json = nlohmann::ordered_json;
using kernel::IKernel;
using kernel::ShapeHandle;
using kernel::Vec3;

// Results are rounded before they are stored or compared for determinism, so
// expected values in case files stay readable.
double round6(double v) {
    return std::round(v * 1e6) / 1e6;
}

Vec3 vec3(const json& j) {
    return {j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>()};
}

// A flat outline in a case: edges like {"line": [[0, 0], [40, 0]]},
// {"arc": {"center": [x, y], "radius": r, "from": 0, "to": 90}} (degrees,
// counterclockwise) or {"circle": {"center": [x, y], "radius": r}}, each with an
// optional "reversed": true.
kernel::Loop2 loop2(const json& edges) {
    kernel::Loop2 out;
    const double deg = std::numbers::pi / 180.0;
    auto p2 = [](const json& j) { return kernel::Point2{j.at(0).get<double>(), j.at(1).get<double>()}; };
    for (const auto& e : edges) {
        kernel::Edge2 edge;
        edge.reversed = e.value("reversed", false);
        if (e.contains("line")) {
            edge.curve = kernel::Line2{p2(e.at("line").at(0)), p2(e.at("line").at(1))};
        } else if (e.contains("arc")) {
            const auto& a = e.at("arc");
            edge.curve = kernel::Arc2{p2(a.at("center")), a.at("radius").get<double>(),
                                      a.at("from").get<double>() * deg, a.at("to").get<double>() * deg};
        } else {
            const auto& c = e.at("circle");
            edge.curve = kernel::Circle2{p2(c.at("center")), c.at("radius").get<double>()};
        }
        out.push_back(edge);
    }
    return out;
}

json toJson(const Vec3& v) {
    return json::array({round6(v.x), round6(v.y), round6(v.z)});
}

// What one run of a case produced.
struct Outcome {
    std::string errorStep;     // id of the step that failed, if any
    std::string errorCode;     // its diagnostic code
    std::string errorMessage;  // its plain-language message
    json actual;               // measured results (empty if a step failed)
};

Outcome build(IKernel& k, const json& c) {
    Outcome out;
    std::map<std::string, ShapeHandle> shapes;
    std::string last;

    for (const auto& step : c.at("steps")) {
        const auto id = step.at("id").get<std::string>();
        const auto op = step.at("op").get<std::string>();
        kernel::OpResult r;
        if (op == "box") {
            const auto size = vec3(step.at("size"));
            r = k.makeBox(vec3(step.value("corner", json::array({0, 0, 0}))), size.x, size.y, size.z);
        } else if (op == "cylinder") {
            r = k.makeCylinder(vec3(step.value("base", json::array({0, 0, 0}))),
                               vec3(step.value("axis", json::array({0, 0, 1}))),
                               step.at("radius").get<double>(), step.at("height").get<double>());
        } else if (op == "union" || op == "cut" || op == "intersect") {
            const auto boolOp = op == "union" ? kernel::BoolOp::Union
                                : op == "cut" ? kernel::BoolOp::Cut
                                              : kernel::BoolOp::Intersect;
            r = k.boolean(boolOp, shapes.at(step.at("a").get<std::string>()),
                          shapes.at(step.at("b").get<std::string>()));
        } else if (op == "move") {
            kernel::Motion m;
            m.offset = vec3(step.value("by", json::array({0, 0, 0})));
            m.angle = step.value("turn", 0.0) * std::numbers::pi / 180.0;  // degrees in the file
            m.axis = vec3(step.value("axis", json::array({0, 0, 1})));
            m.center = vec3(step.value("about", json::array({0, 0, 0})));
            r = k.transform(shapes.at(step.at("body").get<std::string>()), m);
        } else if (op == "face") {
            kernel::Plane plane;
            if (step.contains("plane")) {
                const auto& pl = step.at("plane");
                plane = {vec3(pl.value("origin", json::array({0, 0, 0}))), vec3(pl.at("x")),
                         vec3(pl.at("y"))};
            }
            kernel::Profile2 profile{loop2(step.at("outer")), {}};
            for (const auto& h : step.value("holes", json::array())) profile.holes.push_back(loop2(h));
            r = k.makeFace(profile, plane);
        } else if (op == "extrude") {
            r = k.extrude(shapes.at(step.at("face").get<std::string>()), vec3(step.at("by")));
        } else {
            out.errorStep = id;
            out.errorCode = "regress.unknown_op";
            out.errorMessage = "Unknown operation '" + op + "'.";
            return out;
        }
        if (!r.ok()) {
            out.errorStep = id;
            out.errorCode = r.issues.empty() ? "unknown" : r.issues.front().code;
            out.errorMessage = r.issues.empty() ? "" : r.issues.front().message;
            return out;
        }
        shapes[id] = r.shape;
        last = id;
    }

    const auto& shape = shapes.at(c.value("result", last));
    const auto mp = k.massProps(shape);
    const auto b = k.bounds(shape);
    const auto t = k.topology(shape);
    out.actual = json{
        {"valid", k.validate(shape).empty()},
        {"volume", round6(mp.volume)},
        {"area", round6(mp.area)},
        {"bounds", json::array({toJson(b.min), toJson(b.max)})},
        {"solids", t.solids},
        {"faces", t.faces},
        {"edges", t.edges},
        {"vertices", t.vertices},
    };
    return out;
}

std::string str(const json& j) {
    return j.dump();
}

// Compares measured results to the case's "expect" block. Only keys present in
// "expect" are checked, so a case can pin down as much or as little as it needs.
std::vector<std::string> compare(const json& expect, const Outcome& o) {
    std::vector<std::string> fails;

    if (expect.contains("error")) {
        const auto want = expect.at("error").get<std::string>();
        if (o.errorCode != want) {
            fails.push_back("expected error " + want + ", got " +
                            (o.errorCode.empty() ? std::string("success") : o.errorCode));
        }
        return fails;
    }
    if (!o.errorCode.empty()) {
        fails.push_back("step '" + o.errorStep + "' failed [" + o.errorCode + "]: " + o.errorMessage);
        return fails;
    }

    const auto tol = expect.value("tolerance", json::object());
    const double relTol = tol.value("relative", 1e-6);
    const double lenTol = tol.value("length", 1e-5);
    const auto& a = o.actual;

    if (expect.contains("valid") && expect["valid"] != a["valid"]) {
        fails.push_back("valid expected " + str(expect["valid"]) + ", got " + str(a["valid"]));
    }
    for (const char* key : {"volume", "area"}) {
        if (!expect.contains(key)) continue;
        const double want = expect[key].get<double>();
        const double got = a[key].get<double>();
        if (std::abs(got - want) > relTol * std::max(std::abs(want), 1.0)) {
            fails.push_back(std::string(key) + " expected " + str(expect[key]) + ", got " + str(a[key]));
        }
    }
    if (expect.contains("bounds")) {
        for (int corner = 0; corner < 2; ++corner) {
            for (int axis = 0; axis < 3; ++axis) {
                const double want = expect["bounds"][corner][axis].get<double>();
                const double got = a["bounds"][corner][axis].get<double>();
                if (std::abs(got - want) > lenTol) {
                    fails.push_back("bounds expected " + str(expect["bounds"]) + ", got " + str(a["bounds"]));
                    corner = 2;
                    break;
                }
            }
        }
    }
    for (const char* key : {"solids", "faces", "edges", "vertices"}) {
        if (expect.contains(key) && expect[key] != a[key]) {
            fails.push_back(std::string(key) + " expected " + str(expect[key]) + ", got " + str(a[key]));
        }
    }
    return fails;
}

json readCase(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) throw std::runtime_error("cannot open file");
    return json::parse(in);
}

void writeCase(const std::filesystem::path& file, const json& c) {
    std::ofstream out(file, std::ios::binary);
    out << c.dump(2) << "\n";
}

std::vector<std::filesystem::path> collect(const std::vector<std::filesystem::path>& paths) {
    std::vector<std::filesystem::path> files;
    for (const auto& p : paths) {
        if (std::filesystem::is_directory(p)) {
            std::vector<std::filesystem::path> inDir;
            for (const auto& e : std::filesystem::directory_iterator(p)) {
                if (e.is_regular_file() && e.path().extension() == ".json") inDir.push_back(e.path());
            }
            std::sort(inDir.begin(), inDir.end());
            files.insert(files.end(), inDir.begin(), inDir.end());
        } else {
            files.push_back(p);
        }
    }
    return files;
}

}  // namespace

int runRegression(IKernel& k, const std::vector<std::filesystem::path>& paths,
                  const RegressionOptions& options) {
    int passed = 0;
    int failed = 0;

    for (const auto& file : collect(paths)) {
        const auto name = file.stem().string();
        std::vector<std::string> fails;
        double ms = 0;
        try {
            auto c = readCase(file);
            const auto start = std::chrono::steady_clock::now();
            const auto first = build(k, c);
            ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

            // Rule: kernel operations are deterministic. Build the case a second
            // time and require identical results.
            const auto second = build(k, c);
            if (first.actual != second.actual || first.errorCode != second.errorCode) {
                fails.push_back("results differ between two identical runs (not deterministic)");
            }

            if (options.update) {
                json expect = json::object();
                if (!first.errorCode.empty()) {
                    expect["error"] = first.errorCode;
                } else {
                    expect = first.actual;
                }
                if (c.contains("expect") && c["expect"].contains("tolerance")) {
                    expect["tolerance"] = c["expect"]["tolerance"];
                }
                c["expect"] = expect;
                writeCase(file, c);
            } else {
                auto diffs = compare(c.value("expect", json::object()), first);
                fails.insert(fails.end(), diffs.begin(), diffs.end());
            }
        } catch (const std::exception& e) {
            fails.push_back(std::string("could not run case: ") + e.what());
        }

        if (fails.empty()) {
            ++passed;
            std::printf("%s %-28s %8.1f ms\n", options.update ? "UPDATED" : "PASS", name.c_str(), ms);
        } else {
            ++failed;
            for (const auto& f : fails) std::printf("FAIL %s: %s\n", name.c_str(), f.c_str());
        }
    }

    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace kh::cli
