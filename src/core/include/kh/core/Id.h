#pragma once

/// @file
/// Stable identifiers for everything saved in a design.

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace kh {

/// A random (version 4) UUID.
///
/// Every persistent object in a design (documents, features, sketch entities,
/// parameters, configurations) is identified by one, so references survive
/// across files and versions. Code normally uses a typed kh::Id instead of a
/// bare Uuid.
class Uuid {
public:
    /// Makes the nil UUID (all zeros). See isNil().
    Uuid() = default;

    /// Makes a new random version 4 UUID.
    /// @return A UUID that is, for all practical purposes, unique.
    static Uuid generate();

    /// Reads a UUID from its text form.
    /// @param text Text like "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx" (either letter case).
    /// @return The UUID, or std::nullopt if @p text is not a valid UUID.
    static std::optional<Uuid> parse(std::string_view text);

    /// Writes the UUID as lowercase text.
    /// @return Text like "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx".
    std::string toString() const;

    /// Checks for the nil UUID.
    /// @return True if every byte is zero.
    bool isNil() const;

    /// Gives the raw bytes.
    /// @return The 16 bytes of the UUID, in the order they are written as text.
    const std::array<std::uint8_t, 16>& bytes() const { return bytes_; }

    /// Compares two UUIDs byte by byte. Provides ==, != and ordering (for std::map).
    /// @param other The UUID to compare with.
    /// @return The ordering of the two UUIDs.
    auto operator<=>(const Uuid& other) const = default;

private:
    std::array<std::uint8_t, 16> bytes_{};
};

/// A UUID tagged with the kind of object it names.
///
/// Each tag makes a separate type, so a FeatureId can never be passed where a
/// DocId is expected; the compiler rejects it. The tag is never stored, so an
/// Id costs exactly as much as a Uuid.
///
/// @code
/// struct FeatureTag {};
/// using FeatureId = Id<FeatureTag>;
/// FeatureId id = FeatureId::generate();
/// @endcode
///
/// @tparam Tag An empty struct that names the kind of object, e.g. FeatureTag.
template <typename Tag>
class Id {
public:
    /// Makes the nil id. See isNil().
    Id() = default;

    /// Wraps an existing UUID, for example one read from a file.
    /// @param value The UUID to wrap.
    explicit Id(Uuid value) : value_(value) {}

    /// Makes a new random id.
    /// @return A new id of this kind.
    static Id generate() { return Id(Uuid::generate()); }

    /// Reads an id from its text form.
    /// @param text Text in the form written by toString().
    /// @return The id, or std::nullopt if @p text is not a valid UUID.
    static std::optional<Id> parse(std::string_view text) {
        if (auto u = Uuid::parse(text)) return Id(*u);
        return std::nullopt;
    }

    /// Gives the UUID inside this id.
    /// @return The wrapped UUID.
    const Uuid& uuid() const { return value_; }

    /// Writes the id as text, for files and logs.
    /// @return Text like "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx".
    std::string toString() const { return value_.toString(); }

    /// Checks for the nil id, meaning "no object".
    /// @return True if this id was default-constructed or wraps the nil UUID.
    bool isNil() const { return value_.isNil(); }

    /// Compares two ids of the same kind. Provides ==, != and ordering (for std::map).
    /// @param other The id to compare with.
    /// @return The ordering of the two ids.
    auto operator<=>(const Id& other) const = default;

private:
    Uuid value_;
};

/// Tag for kh::DocId.
struct DocTag {};
/// Tag for kh::FeatureId.
struct FeatureTag {};
/// Tag for kh::EntityId.
struct EntityTag {};
/// Tag for kh::ParamId.
struct ParamTag {};
/// Tag for kh::ConfigId.
struct ConfigTag {};
/// Tag for kh::InstanceId.
struct InstanceTag {};
/// Tag for kh::MaterialId.
struct MaterialTag {};

/// Identifies a document (a part or an assembly).
using DocId = Id<DocTag>;
/// Identifies a feature (one step in a design's history).
using FeatureId = Id<FeatureTag>;
/// Identifies a sketch entity (point, line, arc, ...).
using EntityId = Id<EntityTag>;
/// Identifies a named parameter.
using ParamId = Id<ParamTag>;
/// Identifies a configuration of a design.
using ConfigId = Id<ConfigTag>;
/// Identifies a component instance in an assembly.
using InstanceId = Id<InstanceTag>;
/// Identifies a material a design added (a custom material).
using MaterialId = Id<MaterialTag>;

}  // namespace kh

/// Lets kh::Uuid be a key in std::unordered_map and std::unordered_set.
template <>
struct std::hash<kh::Uuid> {
    /// Hashes a UUID.
    /// @param u The UUID to hash.
    /// @return The hash value.
    std::size_t operator()(const kh::Uuid& u) const noexcept;
};

/// Lets every kh::Id kind be a key in std::unordered_map and std::unordered_set.
/// @tparam Tag The id's tag.
template <typename Tag>
struct std::hash<kh::Id<Tag>> {
    /// Hashes an id by its UUID.
    /// @param id The id to hash.
    /// @return The hash value.
    std::size_t operator()(const kh::Id<Tag>& id) const noexcept { return std::hash<kh::Uuid>{}(id.uuid()); }
};
