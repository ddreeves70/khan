#include <catch2/catch_test_macros.hpp>

#include <unordered_set>

#include "kh/core/Id.h"

TEST_CASE("Uuid round-trips through text", "[core][id]") {
    const auto u = kh::Uuid::generate();
    const auto text = u.toString();
    REQUIRE(text.size() == 36);
    REQUIRE(text[14] == '4');  // version 4

    const auto parsed = kh::Uuid::parse(text);
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == u);
}

TEST_CASE("Uuid parsing rejects malformed text", "[core][id]") {
    REQUIRE_FALSE(kh::Uuid::parse("").has_value());
    REQUIRE_FALSE(kh::Uuid::parse("not-a-uuid").has_value());
    REQUIRE_FALSE(kh::Uuid::parse("123e4567e89b-12d3-a456-426614174000-").has_value());
    REQUIRE_FALSE(kh::Uuid::parse("zzze4567-e89b-12d3-a456-426614174000").has_value());
}

TEST_CASE("Generated ids are unique and not nil", "[core][id]") {
    std::unordered_set<kh::FeatureId> seen;
    for (int i = 0; i < 10000; ++i) {
        const auto id = kh::FeatureId::generate();
        REQUIRE_FALSE(id.isNil());
        REQUIRE(seen.insert(id).second);
    }
}

TEST_CASE("Default id is nil", "[core][id]") {
    REQUIRE(kh::DocId{}.isNil());
}
