#pragma once

/// @file
/// Materials: what a body is made of, for its mass, weight, cost and color.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/kernel/IKernel.h"

namespace kh::model {

/// A color, 0 to 255 per channel (sRGB).
struct Color {
    std::uint8_t r = 0;  ///< Red, 0 to 255.
    std::uint8_t g = 0;  ///< Green, 0 to 255.
    std::uint8_t b = 0;  ///< Blue, 0 to 255.

    /// Compares two colors.
    /// @param other The color to compare with.
    /// @return True if all three channels are the same.
    bool operator==(const Color& other) const = default;
};

/// What a body is made of.
///
/// Khan has built-in materials (builtinMaterials()), and a design can add its
/// own (Document::customMaterials()). Numbers that aren't known are 0.
/// Strength numbers are typical values for comparing materials, not for
/// safety-critical design: check the supplier's data sheet.
struct Material {
    /// How steps refer to it: a built-in material's key, like "pla", or a
    /// custom material's id (a UUID). Never changes.
    std::string key{};
    std::string name{};        ///< The name people see, like "PLA" or "Aluminum 6061".
    double density = 0;        ///< Density in g/cm^3 (the same as g/mL). Always more than 0.
    Color color{};             ///< How the viewer draws it.
    double youngsModulus = 0;  ///< Stiffness (Young's modulus) in GPa, or 0 if unknown.
    double poissonsRatio = 0;  ///< Poisson's ratio, 0 to 0.5, or 0 if unknown.
    double yieldStrength = 0;  ///< Stress at which it starts to bend for good, in MPa, or 0 if unknown.
    double costPerKg = 0;      ///< Price per kilogram, or 0 if unknown. Built-in prices are rough US dollars.
    bool builtin = false;      ///< True for Khan's own materials, which can't be changed.

    /// Compares two materials.
    /// @param other The material to compare with.
    /// @return True if every field is the same.
    bool operator==(const Material& other) const = default;
};

/// Gives Khan's built-in materials, in the order menus list them: plastics for
/// 3D printing first, then metals, then wood and sheet materials.
/// @return The materials. Their keys and names never change.
const std::vector<Material>& builtinMaterials();

/// Gives the material a body has when nobody picked one: PLA, the most common
/// 3D printing plastic.
/// @return The PLA entry of builtinMaterials().
const Material& defaultMaterial();

/// Finds a built-in material by key or name, ignoring upper and lower case.
/// @param keyOrName For example "pla", "PLA" or "aluminum 6061".
/// @return The material, or nullptr if there is none by that key or name.
const Material* findBuiltinMaterial(std::string_view keyOrName);

/// Works out a body's mass.
/// @param volume The body's volume, in mm^3.
/// @param material What it is made of.
/// @return The mass, in grams.
double massGrams(double volume, const Material& material);

/// Works out how hard gravity pulls on a mass on Earth (9.81 m/s^2).
/// @param grams The mass, in grams.
/// @return The weight, in newtons.
double weightNewtons(double grams);

/// Works out what the material for a body costs, from the price per kilogram.
/// @param grams The body's mass, in grams.
/// @param material What it is made of.
/// @return The cost, in the material's money per kilogram; 0 if its price is unknown.
double materialCost(double grams, const Material& material);

/// A body's size and mass, worked out from its shape and material.
struct BodyMass {
    double volume = 0;        ///< Volume, in mm^3.
    double area = 0;          ///< Surface area, in mm^2.
    double grams = 0;         ///< Mass, in grams.
    double newtons = 0;       ///< Weight on Earth, in newtons.
    double cost = 0;          ///< Material cost, in the material's money (0 if its price is unknown).
    kernel::Point3 center{};  ///< Center of mass, in mm.
};

/// Works out a body's volume, mass, weight, material cost and center of mass.
/// @param kernel The kernel that made the shape.
/// @param shape The body's shape.
/// @param material What it is made of.
/// @return The numbers; all 0 for an empty shape.
BodyMass bodyMass(const kernel::IKernel& kernel, const kernel::ShapeHandle& shape, const Material& material);

/// Reads a color written as "#rrggbb", like "#3366cc".
/// @param text The color.
/// @return The color, or nothing if @p text isn't in that form.
std::optional<Color> parseColor(std::string_view text);

/// Writes a color as "#rrggbb", lower case.
/// @param color The color.
/// @return The text, like "#3366cc".
std::string colorText(Color color);

/// Checks a material's numbers: a name, a density from 0.01 to 25 g/cm^3, and
/// no negative numbers; Poisson's ratio at most 0.5.
/// @param material The material to check.
/// @return Nothing if it's fine, otherwise what's wrong in plain words.
Diagnostics checkMaterial(const Material& material);

}  // namespace kh::model
