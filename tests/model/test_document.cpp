#include <catch2/catch_test_macros.hpp>

#include "kh/model/Document.h"

TEST_CASE("New documents get distinct ids", "[model][document]") {
    kh::model::Document a;
    kh::model::Document b(kh::model::DocKind::Assembly, "Lid");
    REQUIRE_FALSE(a.id().isNil());
    REQUIRE(a.id() != b.id());
    REQUIRE(b.kind() == kh::model::DocKind::Assembly);
    REQUIRE(b.name() == "Lid");
}
