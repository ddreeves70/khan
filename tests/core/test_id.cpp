#include <catch2/catch_test_macros.hpp>

#include <unordered_set>

#include "sb/core/Id.h"

TEST_CASE("Uuid round-trips through text", "[core][id]") {
    const auto u = sb::Uuid::generate();
    const auto text = u.toString();
    REQUIRE(text.size() == 36);
    REQUIRE(text[14] == '4');  // version 4

    const auto parsed = sb::Uuid::parse(text);
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == u);
}

TEST_CASE("Uuid parsing rejects malformed text", "[core][id]") {
    REQUIRE_FALSE(sb::Uuid::parse("").has_value());
    REQUIRE_FALSE(sb::Uuid::parse("not-a-uuid").has_value());
    REQUIRE_FALSE(sb::Uuid::parse("123e4567e89b-12d3-a456-426614174000-").has_value());
    REQUIRE_FALSE(sb::Uuid::parse("zzze4567-e89b-12d3-a456-426614174000").has_value());
}

TEST_CASE("Generated ids are unique and not nil", "[core][id]") {
    std::unordered_set<sb::FeatureId> seen;
    for (int i = 0; i < 10000; ++i) {
        const auto id = sb::FeatureId::generate();
        REQUIRE_FALSE(id.isNil());
        REQUIRE(seen.insert(id).second);
    }
}

TEST_CASE("Default id is nil", "[core][id]") {
    REQUIRE(sb::DocId{}.isNil());
}
