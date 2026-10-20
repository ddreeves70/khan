# kh_model: document, parameters, feature graph, commands and naming.
# Phase 0 week 3 adds Document, Command, CommandStack and CommandRegistry here.
kh_add_library(kh_model
  SOURCES
    Document.cpp
  DEPS
    kh::core
    kh::kernel
)
