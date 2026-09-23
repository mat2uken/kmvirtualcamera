#pragma once
#include <charconv>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace km::json {
// Small, bounded JSON reader for signaling/control messages. No substring matching.
// Duplicate keys, invalid UTF-8, unpaired surrogates and trailing input are rejected.
struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    std::string text;
    std::vector<Value> array;
    std::map<std::string, Value, std::less<>> object;
    const Value& at(std::string_view name) const {
        if (kind != Kind::Object) throw std::runtime_error("expected JSON object");
        const auto it = object.find(name);
        if (it == object.end()) throw std::runtime_error("missing JSON field");
        return it->second;
    }
    const Value* find(std::string_view name) const {
        if (kind != Kind::Object) return nullptr;
        const auto it = object.find(name); return it == object.end() ? nullptr : &it->second;
    }
    std::string string() const {
        if (kind != Kind::String) throw std::runtime_error("expected JSON string");
        return text;
    }
    uint32_t uint32() const {
        uint32_t n = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), n);
        if (kind != Kind::Number || result.ec != std::errc{} || result.ptr != text.data() + text.size())
            throw std::runtime_error("expected bounded unsigned integer");
        return n;
    }
};
inline void appendUtf8(std::string& out, uint32_t c) {
    if (c <= 127) out.push_back(char(c));
    else if (c <= 2047) { out.push_back(char(192 | (c >> 6))); out.push_back(char(128 | (c & 63))); }
    else if (c <= 65535) {
        out.push_back(char(224 | (c >> 12))); out.push_back(char(128 | ((c >> 6) & 63))); out.push_back(char(128 | (c & 63)));
    } else {
        out.push_back(char(240 | (c >> 18))); out.push_back(char(128 | ((c >> 12) & 63)));
        out.push_back(char(128 | ((c >> 6) & 63))); out.push_back(char(128 | (c & 63)));
    }
}
inline bool validUtf8(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        uint32_t c = uint8_t(s[i++]);
        if (c < 128) continue;
        unsigned remaining; uint32_t minimum;
        if (c >= 194 && c <= 223) { remaining = 1; minimum = 128; c &= 31; }
        else if (c >= 224 && c <= 239) { remaining = 2; minimum = 2048; c &= 15; }
        else if (c >= 240 && c <= 244) { remaining = 3; minimum = 65536; c &= 7; }
        else return false;
        if (remaining > s.size() - i) return false;
        while (remaining--) {
            const auto next = uint8_t(s[i++]); if ((next & 192) != 128) return false;
            c = (c << 6) | (next & 63);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    }
    return true;
}
class Reader {
public:
    explicit Reader(std::string_view input) : input_(input) {}
    Value read() {
        if (input_.size() > 1024 * 1024 || !validUtf8(input_)) fail();
        auto result = value(0); spaces(); if (position_ != input_.size()) fail(); return result;
    }
private:
    [[noreturn]] void fail() const { throw std::runtime_error("invalid or oversized JSON"); }
    void spaces() { while (position_ < input_.size() && (input_[position_] == ' ' || input_[position_] == '\r' || input_[position_] == '\n' || input_[position_] == '\t')) ++position_; }
    bool take(char c) { if (position_ < input_.size() && input_[position_] == c) { ++position_; return true; } return false; }
    void literal(std::string_view s) { if (input_.substr(position_, s.size()) != s) fail(); position_ += s.size(); }
    uint32_t hex() {
        if (input_.size() - position_ < 4) fail();
        uint32_t n = 0;
        for (int i = 0; i < 4; ++i) {
            char c = input_[position_++];
            int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0) fail();
            n = n * 16 + unsigned(digit);
        }
        return n;
    }
    std::string string() {
        if (!take('"')) fail();
        std::string out;
        while (position_ < input_.size()) {
            const char c = input_[position_++];
            if (c == '"') return out;
            if (uint8_t(c) < 32) fail();
            if (c != '\\') { out.push_back(c); continue; }
            if (position_ == input_.size()) fail();
            switch (input_[position_++]) {
            case '"': out.push_back('"'); break; case '\\': out.push_back('\\'); break; case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break; case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break; case 'r': out.push_back('\r'); break; case 't': out.push_back('\t'); break;
            case 'u': {
                uint32_t cp = hex();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (!take('\\') || !take('u')) fail();
                    const uint32_t low = hex();
                    if (low < 0xdc00 || low > 0xdfff) fail();
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff) fail();
                appendUtf8(out, cp); break;
            }
            default: fail();
            }
        }
        fail();
    }
    bool digit() const { return position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9'; }
    Value value(unsigned depth) {
        if (depth > 32 || ++nodes_ > 16384) fail();
        spaces(); if (position_ == input_.size()) fail();
        Value v;
        if (input_[position_] == '"') { v.kind = Value::Kind::String; v.text = string(); }
        else if (take('{')) {
            v.kind = Value::Kind::Object; spaces();
            if (take('}')) return v;
            do {
                spaces(); auto key = string(); spaces(); if (!take(':')) fail();
                auto child = value(depth + 1);
                if (!v.object.emplace(std::move(key), std::move(child)).second) fail();
                spaces(); if (take('}')) return v;
            } while (take(','));
            fail();
        } else if (take('[')) {
            v.kind = Value::Kind::Array; spaces(); if (take(']')) return v;
            do { v.array.push_back(value(depth + 1)); spaces(); if (take(']')) return v; } while (take(','));
            fail();
        } else if (input_[position_] == 'n') literal("null");
        else if (input_[position_] == 't') { literal("true"); v.kind = Value::Kind::Bool; v.text = "true"; }
        else if (input_[position_] == 'f') { literal("false"); v.kind = Value::Kind::Bool; v.text = "false"; }
        else {
            const size_t begin = position_; take('-');
            if (take('0')) { if (digit()) fail(); }
            else { if (!digit()) fail(); while (digit()) ++position_; }
            if (take('.')) { if (!digit()) fail(); while (digit()) ++position_; }
            if (take('e') || take('E')) { if (!take('+')) take('-'); if (!digit()) fail(); while (digit()) ++position_; }
            v.kind = Value::Kind::Number; v.text = std::string(input_.substr(begin, position_ - begin));
        }
        return v;
    }
    std::string_view input_; size_t position_ = 0, nodes_ = 0;
};
inline Value parse(std::string_view s) { return Reader(s).read(); }
inline std::string quote(std::string_view s) {
    if (!validUtf8(s)) throw std::runtime_error("invalid UTF-8");
    std::string out = "\""; constexpr char digits[] = "0123456789abcdef";
    for (uint8_t c : s) {
        if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(char(c)); }
        else if (c < 32) { out += "\\u00"; out.push_back(digits[c >> 4]); out.push_back(digits[c & 15]); }
        else out.push_back(char(c));
    }
    return out + '"';
}
} // namespace km::json
