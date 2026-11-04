# sb_model: document, features, parameters, commands and undo. The feature graph
# and naming follow in Phase 2.
sb_add_library(sb_model
  SOURCES
    Feature.cpp
    Document.cpp
    Commands.cpp
    CommandRegistry.cpp
    Material.cpp
    Expression.cpp
    Parameters.cpp
    ParameterCommands.cpp
    Formulas.cpp
  DEPS
    sb::core
    sb::kernel
    sb::sketch
)
