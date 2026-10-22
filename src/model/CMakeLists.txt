# kh_model: document, features, commands and undo. Parameters, the feature graph
# and naming follow in Phases 1-2.
kh_add_library(kh_model
  SOURCES
    Feature.cpp
    Document.cpp
    Commands.cpp
    CommandRegistry.cpp
  DEPS
    kh::core
    kh::kernel
)
