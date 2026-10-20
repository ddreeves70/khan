#include "kh/core/Id.h"

#include <random>

namespace kh {

namespace {

std::mt19937_64& rng() {
    thread_local std::mt19937_64 engine = [] {
        std::random_device rd;
        std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
        return std::mt19937_64(seq);
    }();
    return engine;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

Uuid Uuid::generate() {
    Uuid u;
    auto& gen = rng();
    for (std::size_t i = 0; i < 16; i += 8) {
        std::uint64_t r = gen();
        for (std::size_t j = 0; j < 8; ++j) {
            u.bytes_[i + j] = static_cast<std::uint8_t>(r >> (8 * j));
        }
    }
    u.bytes_[6] = static_cast<std::uint8_t>((u.bytes_[6] & 0x0F) | 0x40);  // version 4
    u.bytes_[8] = static_cast<std::uint8_t>((u.bytes_[8] & 0x3F) | 0x80);  // RFC 4122 variant
    return u;
}

std::optional<Uuid> Uuid::parse(std::string_view text) {
    if (text.size() != 36) return std::nullopt;
    Uuid u;
    std::size_t byte = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (text[i] != '-') return std::nullopt;
            ++i;
            continue;
        }
        int hi = hexValue(text[i]);
        int lo = hexValue(text[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        u.bytes_[byte++] = static_cast<std::uint8_t>((hi << 4) | lo);
        i += 2;
    }
    return u;
}

std::string Uuid::toString() const {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(digits[bytes_[i] >> 4]);
        out.push_back(digits[bytes_[i] & 0x0F]);
    }
    return out;
}

bool Uuid::isNil() const {
    for (auto b : bytes_) {
        if (b != 0) return false;
    }
    return true;
}

}  // namespace kh

std::size_t std::hash<kh::Uuid>::operator()(const kh::Uuid& u) const noexcept {
    // FNV-1a over the 16 bytes.
    std::uint64_t h = 1469598103934665603ULL;
    for (auto b : u.bytes()) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(h);
}
