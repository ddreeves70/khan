# kh_model: document, features, parameters, commands and undo. The feature graph
# and naming follow in Phase 2.
kh_add_library(kh_model
  SOURCES
    Feature.cpp
    Document.cpp
    Commands.cpp
    CommandRegistry.cpp
    Material.cpp
    Expression.cpp
    Parameters.cpp
    ParameterCommands.cpp
  DEPS
    kh::core
    kh::kernel
    kh::sketch
)
