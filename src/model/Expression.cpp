#include "kh/model/Expression.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>

namespace kh::model {

namespace {

Diagnostic error(std::string code, std::string message) {
    return {std::move(code), Severity::Error, std::move(message), {}};
}

constexpr double kDegree = std::numbers::pi / 180;

// The units a number can have, in what they measure and how big one is (mm or radians).
struct UnitWord {
    std::string_view word;
    Dimension dim;
    double size;
};
constexpr std::array<UnitWord, 8> kUnits{{
    {"mm", kLength, 1},
    {"cm", kLength, 10},
    {"m", kLength, 1000},
    {"in", kLength, 25.4},
    {"ft", kLength, 304.8},
    {"deg", kAngle, kDegree},
    {"rad", kAngle, 1},
    {"\xC2\xB0", kAngle, kDegree},  // °
}};

const UnitWord* findUnit(std::string_view word) {
    for (const auto& u : kUnits) {
        if (u.word == word) return &u;
    }
    return nullptr;
}

// How many values each function takes: exactly `fewest`, or at least `fewest` if `more`.
struct FunctionWord {
    std::string_view word;
    int fewest;
    bool more;
    std::string_view example;  // for messages
};
constexpr std::array<FunctionWord, 10> kFunctions{{
    {"sqrt", 1, false, "sqrt(16)"},
    {"abs", 1, false, "abs(-3 mm)"},
    {"min", 2, true, "min(3 mm, 5 mm)"},
    {"max", 2, true, "max(3 mm, 5 mm)"},
    {"sin", 1, false, "sin(30 deg)"},
    {"cos", 1, false, "cos(30 deg)"},
    {"tan", 1, false, "tan(30 deg)"},
    {"asin", 1, false, "asin(0.5)"},
    {"acos", 1, false, "acos(0.5)"},
    {"atan", 1, false, "atan(0.5)"},
}};

const FunctionWord* findFunction(std::string_view word) {
    for (const auto& f : kFunctions) {
        if (f.word == word) return &f;
    }
    return nullptr;
}

std::string functionList() {
    std::string out;
    for (std::size_t i = 0; i < kFunctions.size(); ++i) {
        if (i > 0) out += i + 1 == kFunctions.size() ? " and " : ", ";
        out += kFunctions[i].word;
    }
    return out;
}

// A number with a fixed number of decimals and the zeros at the end dropped. Never "-0".
std::string trimmed(double value, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, value);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

bool isNameStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool isNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

}  // namespace

std::string dimensionName(const Dimension& dim) {
    if (dim == kPlainNumber) return "plain number";
    if (dim == kAngle) return "angle";
    if (dim.angle == 0) {
        switch (dim.length) {
            case 1:
                return "length";
            case 2:
                return "area";
            case 3:
                return "volume";
            default:
                break;
        }
    }
    std::string out;
    if (dim.length != 0) out += "length^" + std::to_string(dim.length);
    if (dim.angle != 0) out += (out.empty() ? "" : " × ") + std::string("angle^") + std::to_string(dim.angle);
    return out;
}

std::string quantityText(const Quantity& q, LengthUnit unit) {
    const Dimension d = q.dim;
    if (d == kPlainNumber) return trimmed(q.value, 6);
    if (d == kLength) return lengthText(q.value, unit);
    if (d == kAngle) return trimmed(q.value / kDegree, 4) + " deg";
    // Anything else: the number in the unit's powers, like "12 mm^2" or "2 mm/deg".
    const double value = q.value / std::pow(mmPerUnit(unit), d.length) / std::pow(kDegree, d.angle);
    auto power = [](std::string_view word, int n) {
        std::string s(word);
        if (n != 1) s += "^" + std::to_string(n);
        return s;
    };
    std::string top, bottom;
    auto add = [](std::string& to, const std::string& part) { to += (to.empty() ? "" : "·") + part; };
    if (d.length > 0) add(top, power(unitSymbol(unit), d.length));
    if (d.angle > 0) add(top, power("deg", d.angle));
    if (d.length < 0) add(bottom, power(unitSymbol(unit), -d.length));
    if (d.angle < 0) add(bottom, power("deg", -d.angle));
    std::string units = top.empty() ? "1" : top;
    if (!bottom.empty()) units += "/" + bottom;
    return trimmed(value, 4) + " " + units;
}

// ---------------------------------------------------------------------------
// The parts of a formula
// ---------------------------------------------------------------------------

struct Expression::Node {
    enum class Kind { Number, Name, Negate, Add, Subtract, Multiply, Divide, Power, Call };
    Kind kind = Kind::Number;
    Quantity number;                                 // Number
    std::string name;                                // Name, or the function for Call
    std::vector<std::shared_ptr<const Node>> parts;  // operands or the function's values
    std::size_t begin = 0, end = 0;                  // where it is in the text
};

namespace {

using Node = Expression::Node;
using NodePtr = std::shared_ptr<const Node>;

struct Token {
    enum class Kind { Number, Word, Symbol, End };
    Kind kind = Kind::End;
    std::string text;
    double number = 0;
    std::size_t begin = 0, end = 0;
};

// Reads a formula into nodes. On the first problem it stops and says what it was.
class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    NodePtr parse(Diagnostics& problems) {
        if (!tokenize()) {
            problems.push_back(problem_);
            return nullptr;
        }
        if (tokens_.front().kind == Token::Kind::End) {
            problems.push_back(error("expression.empty", "There's no value. Type a number, like 3 mm."));
            return nullptr;
        }
        auto root = sum();
        if (root && peek().kind != Token::Kind::End) {
            const auto& t = peek();
            if (t.text == ")") {
                fail("expression.bracket",
                     "There's a ) with no ( before it in " + quoted() + ". Take it out, or add a (.");
            } else {
                fail("expression.operator", "Khan can't read " + quoted() + ": something is missing before " +
                                                t.text + ". Put +, -, * or / between values.");
            }
            root = nullptr;
        }
        if (!root) problems.push_back(problem_);
        return root;
    }

    std::vector<std::pair<std::string, std::size_t>> names;  // every name used, where it starts

private:
    std::string quoted() const { return "\"" + std::string(text_) + "\""; }

    void fail(std::string code, std::string message) {
        if (problem_.code.empty()) problem_ = error(std::move(code), std::move(message));
    }

    bool tokenize() {
        std::size_t i = 0;
        while (i < text_.size()) {
            const char c = text_[i];
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++i;
                continue;
            }
            Token t;
            t.begin = i;
            if (std::isdigit(static_cast<unsigned char>(c)) ||
                (c == '.' && i + 1 < text_.size() &&
                 std::isdigit(static_cast<unsigned char>(text_[i + 1])))) {
                std::size_t j = i;
                while (j < text_.size() && std::isdigit(static_cast<unsigned char>(text_[j]))) ++j;
                if (j < text_.size() && text_[j] == '.') {
                    ++j;
                    while (j < text_.size() && std::isdigit(static_cast<unsigned char>(text_[j]))) ++j;
                }
                // 1e3, 2.5E-2: only if digits follow, so "2 e" stays a number then a word.
                if (j < text_.size() && (text_[j] == 'e' || text_[j] == 'E')) {
                    std::size_t k = j + 1;
                    if (k < text_.size() && (text_[k] == '+' || text_[k] == '-')) ++k;
                    if (k < text_.size() && std::isdigit(static_cast<unsigned char>(text_[k]))) {
                        while (k < text_.size() && std::isdigit(static_cast<unsigned char>(text_[k]))) ++k;
                        j = k;
                    }
                }
                t.kind = Token::Kind::Number;
                t.text = std::string(text_.substr(i, j - i));
                t.number = std::strtod(t.text.c_str(), nullptr);
                i = j;
            } else if (isNameStart(c)) {
                // A name, which may have dots inside, like Sketch1.width.
                std::size_t j = i + 1;
                while (j < text_.size() &&
                       (isNameChar(text_[j]) ||
                        (text_[j] == '.' && j + 1 < text_.size() && isNameStart(text_[j + 1])))) {
                    ++j;
                }
                t.kind = Token::Kind::Word;
                t.text = std::string(text_.substr(i, j - i));
                i = j;
            } else if (text_.substr(i, 2) == "\xC2\xB0") {
                t.kind = Token::Kind::Word;  // °, the degree sign
                t.text = "\xC2\xB0";
                i += 2;
            } else if (std::string_view("+-*/^(),").find(c) != std::string_view::npos) {
                t.kind = Token::Kind::Symbol;
                t.text = std::string(1, c);
                ++i;
            } else {
                // One whole character, even if it takes several bytes (like ×).
                std::size_t j = i + 1;
                while (j < text_.size() && (static_cast<unsigned char>(text_[j]) & 0xC0) == 0x80) ++j;
                fail("expression.character",
                     "Khan can't read the " + std::string(text_.substr(i, j - i)) + " in " + quoted() +
                         ". Use numbers, names, + - * / ^ and brackets, like (wall + 2 mm) * 2.");
                return false;
            }
            t.end = i;
            tokens_.push_back(std::move(t));
        }
        Token end;
        end.begin = end.end = text_.size();
        tokens_.push_back(end);
        return true;
    }

    const Token& peek() const { return tokens_[at_]; }
    const Token& next() { return tokens_[at_ < tokens_.size() - 1 ? at_++ : at_]; }
    bool isSymbol(std::string_view s) const { return peek().kind == Token::Kind::Symbol && peek().text == s; }

    static NodePtr make(Node::Kind kind, std::vector<NodePtr> parts, std::size_t begin, std::size_t end) {
        auto n = std::make_shared<Node>();
        n->kind = kind;
        n->parts = std::move(parts);
        n->begin = begin;
        n->end = end;
        return n;
    }

    // sum := product (('+' | '-') product)*
    NodePtr sum() {
        auto left = product();
        while (left && (isSymbol("+") || isSymbol("-"))) {
            const auto kind = next().text == "+" ? Node::Kind::Add : Node::Kind::Subtract;
            auto right = product();
            if (!right) return nullptr;
            const auto begin = left->begin, end = right->end;
            left = make(kind, {left, right}, begin, end);
        }
        return left;
    }

    // product := sign (('*' | '/') sign)*
    NodePtr product() {
        auto left = sign();
        while (left && (isSymbol("*") || isSymbol("/"))) {
            const auto kind = next().text == "*" ? Node::Kind::Multiply : Node::Kind::Divide;
            auto right = sign();
            if (!right) return nullptr;
            const auto begin = left->begin, end = right->end;
            left = make(kind, {left, right}, begin, end);
        }
        return left;
    }

    // sign := ('-' | '+') sign | power
    NodePtr sign() {
        if (isSymbol("-") || isSymbol("+")) {
            const Token& t = next();
            const bool negate = t.text == "-";
            const auto begin = t.begin;
            auto inner = sign();
            if (!inner || !negate) return inner;
            const auto end = inner->end;
            return make(Node::Kind::Negate, {inner}, begin, end);
        }
        return power();
    }

    // power := value ('^' sign)?     (2 ^ -1 works; 2 ^ 3 ^ 2 is 2 ^ (3 ^ 2))
    NodePtr power() {
        auto base = value();
        if (base && isSymbol("^")) {
            next();
            auto exponent = sign();
            if (!exponent) return nullptr;
            const auto begin = base->begin, end = exponent->end;
            return make(Node::Kind::Power, {base, exponent}, begin, end);
        }
        return base;
    }

    // value := number [unit] | name | function '(' sum (',' sum)* ')' | '(' sum ')'
    NodePtr value() {
        const Token t = next();
        if (t.kind == Token::Kind::Number) {
            auto n = std::make_shared<Node>();
            n->kind = Node::Kind::Number;
            n->number = {t.number, kPlainNumber};
            n->begin = t.begin;
            n->end = t.end;
            if (peek().kind == Token::Kind::Word) {
                const Token& w = peek();
                if (const auto* unit = findUnit(w.text)) {
                    n->number = {t.number * unit->size, unit->dim};
                    n->end = next().end;
                } else {
                    fail("expression.unit", "Khan doesn't know the unit " + w.text + " in " + quoted() +
                                                ". Use mm, cm, m, in, ft, deg or rad, or put * between " +
                                                t.text + " and " + w.text + " to multiply.");
                    return nullptr;
                }
            }
            return n;
        }
        if (t.kind == Token::Kind::Word) {
            if (findUnit(t.text)) {
                fail("expression.unit", "The unit " + t.text + " in " + quoted() +
                                            " needs a number in front of it, like 3 " + t.text + ".");
                return nullptr;
            }
            if (isSymbol("(")) return call(t);
            if (const auto* fn = findFunction(t.text)) {
                fail("expression.function",
                     t.text + " needs its value in brackets, like " + std::string(fn->example) + ".");
                return nullptr;
            }
            auto n = std::make_shared<Node>();
            n->begin = t.begin;
            n->end = t.end;
            if (t.text == "pi") {
                n->kind = Node::Kind::Number;
                n->number = {std::numbers::pi, kPlainNumber};
            } else {
                n->kind = Node::Kind::Name;
                n->name = t.text;
                names.emplace_back(t.text, t.begin);
            }
            return n;
        }
        if (t.kind == Token::Kind::Symbol && t.text == "(") {
            auto inner = sum();
            if (!inner) return nullptr;
            if (!isSymbol(")")) {
                fail("expression.bracket",
                     "A ( in " + quoted() + " has no ) to close it. Add a ) where the bracket should end.");
                return nullptr;
            }
            next();
            return inner;
        }
        if (t.kind == Token::Kind::End) {
            fail("expression.incomplete", quoted() + " stops too soon: a value is missing at the end.");
        } else {
            fail("expression.operator",
                 "Khan can't read " + quoted() + ": a value is missing before " + t.text + ".");
        }
        return nullptr;
    }

    NodePtr call(const Token& word) {
        const auto* fn = findFunction(word.text);
        if (!fn) {
            fail("expression.function",
                 "Khan doesn't know a function called " + word.text + ". It knows " + functionList() + ".");
            return nullptr;
        }
        next();  // (
        std::vector<NodePtr> args;
        if (!isSymbol(")")) {
            while (true) {
                auto arg = sum();
                if (!arg) return nullptr;
                args.push_back(arg);
                if (!isSymbol(",")) break;
                next();
            }
        }
        if (!isSymbol(")")) {
            fail("expression.bracket", "The ( after " + word.text + " in " + quoted() +
                                           " has no ) to close it. Add a ) after its value.");
            return nullptr;
        }
        const auto end = next().end;
        const int count = static_cast<int>(args.size());
        if (count < fn->fewest || (!fn->more && count != fn->fewest)) {
            fail("expression.function", word.text +
                                            (fn->more ? " takes two or more values" : " takes one value") +
                                            ", like " + std::string(fn->example) + ".");
            return nullptr;
        }
        auto n = std::make_shared<Node>();
        n->kind = Node::Kind::Call;
        n->name = word.text;
        n->parts = std::move(args);
        n->begin = word.begin;
        n->end = end;
        return n;
    }

    std::string_view text_;
    std::vector<Token> tokens_;
    std::size_t at_ = 0;
    Diagnostic problem_;
};

// Works out nodes. Stops at the first problem.
class Evaluator {
public:
    Evaluator(std::string_view text, const NameLookup& lookup) : text_(text), lookup_(lookup) {}

    std::optional<Quantity> eval(const Node& n) {
        switch (n.kind) {
            case Node::Kind::Number:
                return n.number;
            case Node::Kind::Name: {
                auto v = lookup_ ? lookup_(n.name) : std::nullopt;
                if (!v) {
                    fail("expression.unknown_name",
                         "There's no parameter called " + n.name + ". Check the spelling, or add it first.");
                }
                return v;
            }
            case Node::Kind::Negate: {
                auto v = eval(*n.parts[0]);
                if (v) v->value = -v->value;
                return v;
            }
            case Node::Kind::Add:
            case Node::Kind::Subtract:
                return addOrSubtract(n);
            case Node::Kind::Multiply:
            case Node::Kind::Divide:
                return multiplyOrDivide(n);
            case Node::Kind::Power:
                return power(n);
            case Node::Kind::Call:
                return call(n);
        }
        return std::nullopt;
    }

    Diagnostics problems;

private:
    std::string part(const Node& n) const { return std::string(text_.substr(n.begin, n.end - n.begin)); }

    void fail(std::string code, std::string message) {
        if (problems.empty()) problems.push_back(error(std::move(code), std::move(message)));
    }

    std::optional<Quantity> finite(Quantity q, const Node& n) {
        if (!std::isfinite(q.value)) {
            fail("expression.too_big", part(n) + " is too big for Khan to work out. Use smaller numbers.");
            return std::nullopt;
        }
        return q;
    }

    // "an angle", "a length", "a plain number"
    static std::string withArticle(const Dimension& d) {
        const auto name = dimensionName(d);
        return (name.front() == 'a' ? "an " : "a ") + name;
    }

    std::optional<Quantity> addOrSubtract(const Node& n) {
        auto a = eval(*n.parts[0]);
        if (!a) return a;
        auto b = eval(*n.parts[1]);
        if (!b) return b;
        const bool adding = n.kind == Node::Kind::Add;
        if (a->dim != b->dim) {
            // Suggest the fix: give the plain number the other side's unit.
            std::string fix = "Give both the same kind of unit";
            const Node* plain = a->dim == kPlainNumber   ? n.parts[0].get()
                                : b->dim == kPlainNumber ? n.parts[1].get()
                                                         : nullptr;
            const Dimension other = a->dim == kPlainNumber ? b->dim : a->dim;
            if (plain && plain->kind == Node::Kind::Number && (other == kLength || other == kAngle)) {
                fix += ", like " + part(*plain) + (other == kLength ? " mm" : " deg");
            }
            fail("expression.units", "Khan can't " + std::string(adding ? "add " : "take ") +
                                         part(*n.parts[1]) + (adding ? " to " : " from ") +
                                         part(*n.parts[0]) + ": " + part(*n.parts[0]) + " is " +
                                         withArticle(a->dim) + " and " + part(*n.parts[1]) + " is " +
                                         withArticle(b->dim) + ". " + fix + ".");
            return std::nullopt;
        }
        return finite({adding ? a->value + b->value : a->value - b->value, a->dim}, n);
    }

    std::optional<Quantity> multiplyOrDivide(const Node& n) {
        auto a = eval(*n.parts[0]);
        if (!a) return a;
        auto b = eval(*n.parts[1]);
        if (!b) return b;
        if (n.kind == Node::Kind::Multiply) {
            return finite({a->value * b->value, {a->dim.length + b->dim.length, a->dim.angle + b->dim.angle}},
                          n);
        }
        if (b->value == 0) {
            fail("expression.divide_by_zero",
                 part(*n.parts[1]) + " is 0 in " + part(n) +
                     ", and Khan can't divide by 0. Change it to another number.");
            return std::nullopt;
        }
        return finite({a->value / b->value, {a->dim.length - b->dim.length, a->dim.angle - b->dim.angle}}, n);
    }

    std::optional<Quantity> power(const Node& n) {
        auto a = eval(*n.parts[0]);
        if (!a) return a;
        auto b = eval(*n.parts[1]);
        if (!b) return b;
        if (b->dim != kPlainNumber) {
            fail("expression.units",
                 "The power in " + part(n) + " must be a plain number, like " + part(*n.parts[0]) + " ^ 2.");
            return std::nullopt;
        }
        Dimension dim = kPlainNumber;
        if (a->dim != kPlainNumber) {
            if (b->value != std::round(b->value) || std::abs(b->value) > 12) {
                fail("expression.units", "A " + dimensionName(a->dim) +
                                             " can only be raised to a whole number, like " +
                                             part(*n.parts[0]) + " ^ 2. " + part(n) + " isn't one.");
                return std::nullopt;
            }
            const int k = static_cast<int>(b->value);
            dim = {a->dim.length * k, a->dim.angle * k};
        }
        if (a->value < 0 && b->value != std::round(b->value)) {
            fail("expression.negative",
                 "Khan can't raise a number below 0 to a power with decimals, as in " + part(n) + ".");
            return std::nullopt;
        }
        if (a->value == 0 && b->value < 0) {
            fail("expression.divide_by_zero", part(n) + " divides by 0. Change " + part(*n.parts[0]) + ".");
            return std::nullopt;
        }
        return finite({std::pow(a->value, b->value), dim}, n);
    }

    std::optional<Quantity> call(const Node& n) {
        std::vector<Quantity> args;
        for (const auto& p : n.parts) {
            auto v = eval(*p);
            if (!v) return v;
            args.push_back(*v);
        }
        const std::string& fn = n.name;
        const Quantity x = args.front();
        if (fn == "sqrt") {
            if (x.dim.length % 2 != 0 || x.dim.angle % 2 != 0) {
                fail("expression.units", "Khan can't take the square root of " + withArticle(x.dim) + " (" +
                                             part(*n.parts[0]) +
                                             "). Square roots work on plain numbers and areas.");
                return std::nullopt;
            }
            if (x.value < 0) {
                fail("expression.negative",
                     part(*n.parts[0]) + " is below 0, and a number below 0 has no square root.");
                return std::nullopt;
            }
            return Quantity{std::sqrt(x.value), {x.dim.length / 2, x.dim.angle / 2}};
        }
        if (fn == "abs") return Quantity{std::abs(x.value), x.dim};
        if (fn == "min" || fn == "max") {
            Quantity best = x;
            for (std::size_t i = 1; i < args.size(); ++i) {
                if (args[i].dim != x.dim) {
                    fail("expression.units", fn + " compares values of one kind, but " + part(*n.parts[0]) +
                                                 " is " + withArticle(x.dim) + " and " + part(*n.parts[i]) +
                                                 " is " + withArticle(args[i].dim) +
                                                 ". Give them the same kind of unit.");
                    return std::nullopt;
                }
                if (fn == "min" ? args[i].value < best.value : args[i].value > best.value) best = args[i];
            }
            return best;
        }
        if (fn == "sin" || fn == "cos" || fn == "tan") {
            if (x.dim != kAngle) {
                fail("expression.units", fn + " needs an angle, like " + fn + "(30 deg), but " +
                                             part(*n.parts[0]) + " is " + withArticle(x.dim) + ".");
                return std::nullopt;
            }
            const double v = fn == "sin"   ? std::sin(x.value)
                             : fn == "cos" ? std::cos(x.value)
                                           : std::tan(x.value);
            return finite({v, kPlainNumber}, n);
        }
        // asin, acos, atan: a plain number in, an angle out.
        if (x.dim != kPlainNumber) {
            fail("expression.units", fn + " needs a plain number, like " + fn + "(0.5), but " +
                                         part(*n.parts[0]) + " is " + withArticle(x.dim) + ".");
            return std::nullopt;
        }
        if (fn != "atan" && (x.value < -1 || x.value > 1)) {
            fail("expression.range", fn + " needs a number from -1 to 1, but " + part(*n.parts[0]) + " is " +
                                         trimmed(x.value, 6) + ".");
            return std::nullopt;
        }
        const double v = fn == "asin"   ? std::asin(x.value)
                         : fn == "acos" ? std::acos(x.value)
                                        : std::atan(x.value);
        return Quantity{v, kAngle};
    }

    std::string_view text_;
    const NameLookup& lookup_;
};

}  // namespace

// ---------------------------------------------------------------------------
// Expression
// ---------------------------------------------------------------------------

Expression::Expression() {
    problems_.push_back(error("expression.empty", "There's no value. Type a number, like 3 mm."));
}

Expression Expression::parse(std::string_view text) {
    Expression e;
    e.problems_.clear();
    e.text_ = std::string(text);
    Parser parser(e.text_);
    e.root_ = parser.parse(e.problems_);
    if (e.root_) {
        for (auto& [name, at] : parser.names) e.names_.push_back({std::move(name), at});
    }
    return e;
}

std::vector<std::string> Expression::references() const {
    std::vector<std::string> out;
    for (const auto& n : names_) {
        if (std::find(out.begin(), out.end(), n.name) == out.end()) out.push_back(n.name);
    }
    return out;
}

EvalResult Expression::eval(const NameLookup& lookup) const {
    EvalResult result;
    if (!ok()) {
        result.issues = problems_;
        return result;
    }
    Evaluator evaluator(text_, lookup);
    result.value = evaluator.eval(*root_);
    if (!result.value) result.issues = std::move(evaluator.problems);
    return result;
}

std::string Expression::renamed(std::string_view from, std::string_view to) const {
    std::string out = text_;
    // From the end, so earlier places don't move.
    for (auto it = names_.rbegin(); it != names_.rend(); ++it) {
        if (it->name == from) out.replace(it->at, it->name.size(), to);
    }
    return out;
}

bool Expression::isReservedWord(std::string_view word) {
    return word == "pi" || findUnit(word) || findFunction(word);
}

}  // namespace kh::model
