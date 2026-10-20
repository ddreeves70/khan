#include "kh/model/Document.h"

#include <utility>

namespace kh::model {

Document::Document(DocKind kind, std::string name)
    : id_(DocId::generate()), kind_(kind), name_(std::move(name)) {}

}  // namespace kh::model
