#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace kh {

// A random (version 4) UUID. Every persistent object in a design
// (documents, features, sketch entities, parameters, configurations)
// is identified by one, so references survive across files and versions.
class Uuid {
public:
    Uuid() = default;  // the nil UUID

    static Uuid generate();
    static std::optional<Uuid> parse(std::string_view text);

    std::string toString() const;  // "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"
    bool isNil() const;

    const std::array<std::uint8_t, 16>& bytes() const { return bytes_; }

    auto operator<=>(const Uuid&) const = default;

private:
    std::array<std::uint8_t, 16> bytes_{};
};

// A UUID tagged with the kind of object it names, so a FeatureId
// can never be passed where a DocId is expected.
//
//   struct FeatureTag {};
//   using FeatureId = Id<FeatureTag>;
template <typename Tag>
class Id {
public:
    Id() = default;
    explicit Id(Uuid value) : value_(value) {}

    static Id generate() { return Id(Uuid::generate()); }
    static std::optional<Id> parse(std::string_view text) {
        if (auto u = Uuid::parse(text)) return Id(*u);
        return std::nullopt;
    }

    const Uuid& uuid() const { return value_; }
    std::string toString() const { return value_.toString(); }
    bool isNil() const { return value_.isNil(); }

    auto operator<=>(const Id&) const = default;

private:
    Uuid value_;
};

// Core id kinds used across the code base.
struct DocTag {};
struct FeatureTag {};
struct EntityTag {};
struct ParamTag {};
struct ConfigTag {};
struct InstanceTag {};

using DocId = Id<DocTag>;
using FeatureId = Id<FeatureTag>;
using EntityId = Id<EntityTag>;
using ParamId = Id<ParamTag>;
using ConfigId = Id<ConfigTag>;
using InstanceId = Id<InstanceTag>;

}  // namespace kh

template <>
struct std::hash<kh::Uuid> {
    std::size_t operator()(const kh::Uuid& u) const noexcept;
};

template <typename Tag>
struct std::hash<kh::Id<Tag>> {
    std::size_t operator()(const kh::Id<Tag>& id) const noexcept {
        return std::hash<kh::Uuid>{}(id.uuid());
    }
};
