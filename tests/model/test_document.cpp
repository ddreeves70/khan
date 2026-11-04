#include <catch2/catch_test_macros.hpp>

#include "sb/model/Document.h"

TEST_CASE("New documents get distinct ids", "[model][document]") {
    sb::model::Document a;
    sb::model::Document b(sb::model::DocKind::Assembly, "Lid");
    REQUIRE_FALSE(a.id().isNil());
    REQUIRE(a.id() != b.id());
    REQUIRE(b.kind() == sb::model::DocKind::Assembly);
    REQUIRE(b.name() == "Lid");
}
