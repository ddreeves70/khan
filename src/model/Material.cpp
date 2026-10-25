#include "kh/model/Material.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace kh::model {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

Material builtin(const char* key, const char* name, double density, Color color, double youngs,
                 double poisson, double yield, double cost) {
    return {key, name, density, color, youngs, poisson, yield, cost, true};
}

}  // namespace

// Typical values from material data sheets (MatWeb, makers' sheets), rounded.
// Densities in g/cm^3, stiffness in GPa, yield strength in MPa, prices in
// rough US dollars per kg. Wood is stronger along the grain; these are along it.
const std::vector<Material>& builtinMaterials() {
    static const std::vector<Material> materials{
        // Plastics for 3D printing
        builtin("pla", "PLA", 1.24, {120, 170, 220}, 3.5, 0.36, 50, 20),
        builtin("petg", "PETG", 1.27, {90, 180, 170}, 2.1, 0.38, 50, 22),
        builtin("abs", "ABS", 1.05, {230, 185, 80}, 2.2, 0.35, 40, 20),
        builtin("tpu", "TPU (flexible)", 1.21, {80, 82, 92}, 0.03, 0.48, 9, 35),
        builtin("nylon", "Nylon (PA12)", 1.01, {230, 226, 212}, 1.7, 0.39, 48, 60),
        builtin("resin", "Resin (standard)", 1.18, {165, 150, 200}, 2.5, 0.35, 45, 50),
        builtin("acrylic", "Acrylic (PMMA)", 1.18, {190, 220, 232}, 3.2, 0.37, 70, 8),
        builtin("polycarbonate", "Polycarbonate", 1.20, {205, 210, 220}, 2.4, 0.37, 62, 12),
        // Metals
        builtin("aluminum_6061", "Aluminum 6061", 2.70, {196, 200, 206}, 69, 0.33, 276, 6),
        builtin("steel_1018", "Steel (mild, 1018)", 7.87, {128, 134, 142}, 205, 0.29, 370, 2),
        builtin("stainless_304", "Stainless steel 304", 8.00, {176, 181, 188}, 193, 0.29, 215, 5),
        builtin("brass_360", "Brass (C360)", 8.50, {205, 170, 90}, 97, 0.31, 140, 9),
        builtin("copper_110", "Copper (C110)", 8.94, {200, 120, 80}, 117, 0.34, 69, 11),
        // Wood and sheets
        builtin("oak", "Oak (red)", 0.70, {180, 135, 90}, 12.5, 0.35, 45, 3),
        builtin("plywood", "Plywood (birch)", 0.68, {214, 184, 134}, 9, 0.30, 30, 2.5),
        builtin("mdf", "MDF", 0.75, {165, 130, 95}, 4, 0.25, 18, 1),
    };
    return materials;
}

const Material& defaultMaterial() {
    return builtinMaterials().front();
}

const Material* findBuiltinMaterial(std::string_view keyOrName) {
    const std::string wanted = lower(keyOrName);
    for (const auto& m : builtinMaterials()) {
        if (m.key == wanted || lower(m.name) == wanted) return &m;
    }
    return nullptr;
}

double massGrams(double volume, const Material& material) {
    return volume / 1000.0 * material.density;  // 1 cm^3 = 1000 mm^3
}

double weightNewtons(double grams) {
    return grams / 1000.0 * 9.81;
}

double materialCost(double grams, const Material& material) {
    return grams / 1000.0 * material.costPerKg;
}

BodyMass bodyMass(const kernel::IKernel& kernel, const kernel::ShapeHandle& shape, const Material& material) {
    BodyMass out;
    if (!shape.valid()) return out;
    const auto props = kernel.massProps(shape);
    out.volume = props.volume;
    out.area = props.area;
    out.center = props.centerOfMass;
    out.grams = massGrams(props.volume, material);
    out.newtons = weightNewtons(out.grams);
    out.cost = materialCost(out.grams, material);
    return out;
}

std::optional<Color> parseColor(std::string_view text) {
    if (text.size() != 7 || text[0] != '#') return std::nullopt;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int v[6];
    for (int i = 0; i < 6; ++i) {
        v[i] = hex(text[static_cast<std::size_t>(i) + 1]);
        if (v[i] < 0) return std::nullopt;
    }
    return Color{static_cast<std::uint8_t>(v[0] * 16 + v[1]), static_cast<std::uint8_t>(v[2] * 16 + v[3]),
                 static_cast<std::uint8_t>(v[4] * 16 + v[5])};
}

std::string colorText(Color c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
    return buf;
}

Diagnostics checkMaterial(const Material& m) {
    Diagnostics out;
    auto bad = [&](std::string code, std::string message) {
        out.push_back({std::move(code), Severity::Error, std::move(message), {}});
    };
    if (m.name.empty()) bad("material.name_empty", "Give the material a name.");
    if (!(m.density >= 0.01 && m.density <= 25)) {
        bad("material.density",
            "A material's density must be between 0.01 and 25 g/cm³. Water is 1 g/cm³, PLA 1.24, steel "
            "7.87.");
    }
    if (!(m.youngsModulus >= 0) || !(m.yieldStrength >= 0) || !(m.costPerKg >= 0)) {
        bad("material.negative",
            "Stiffness, strength and price can't be less than 0. Use 0 if you don't know.");
    }
    if (!(m.poissonsRatio >= 0 && m.poissonsRatio <= 0.5)) {
        bad("material.poisson", "Poisson's ratio must be between 0 and 0.5 (most solids are 0.25 to 0.4).");
    }
    return out;
}

}  // namespace kh::model
