#include "ap3/CanonicalJson.h"

#include <algorithm>

namespace ap3 {
namespace json {

void Value::writeString(std::string &out, const std::string &s) {
  static const char *hexd = "0123456789abcdef";
  out.push_back('"');
  for (unsigned char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\b': out += "\\b"; break;
    case '\t': out += "\\t"; break;
    case '\n': out += "\\n"; break;
    case '\f': out += "\\f"; break;
    case '\r': out += "\\r"; break;
    default:
      if (c < 0x20) {
        // Other control chars: \u00XX, lowercase hex — matches CPython json.
        out += "\\u00";
        out.push_back(hexd[c >> 4]);
        out.push_back(hexd[c & 0xf]);
      } else {
        // Printable ASCII and raw UTF-8 bytes pass through (ensure_ascii=False).
        out.push_back(static_cast<char>(c));
      }
    }
  }
  out.push_back('"');
}

void Value::write(std::string &out) const {
  switch (type_) {
  case Type::Null:
    out += "null";
    break;
  case Type::Bool:
    out += bool_ ? "true" : "false";
    break;
  case Type::Int:
    out += std::to_string(int_);
    break;
  case Type::Str:
    writeString(out, str_);
    break;
  case Type::Array: {
    out.push_back('[');
    for (std::size_t i = 0; i < array_.size(); ++i) {
      if (i)
        out.push_back(',');
      array_[i].write(out);
    }
    out.push_back(']');
    break;
  }
  case Type::Object: {
    // sort_keys=True: emit members in ascending key order. Keys are ASCII, so a
    // byte-wise compare matches Python's code-point ordering.
    std::vector<const std::pair<std::string, Value> *> sorted;
    sorted.reserve(obj_.size());
    for (const auto &kv : obj_)
      sorted.push_back(&kv);
    std::sort(sorted.begin(), sorted.end(),
              [](const auto *a, const auto *b) { return a->first < b->first; });
    out.push_back('{');
    for (std::size_t i = 0; i < sorted.size(); ++i) {
      if (i)
        out.push_back(',');
      writeString(out, sorted[i]->first);
      out.push_back(':');
      sorted[i]->second.write(out);
    }
    out.push_back('}');
    break;
  }
  }
}

// ----------------------------------------------------------------------------
// Minimal recursive-descent parser (tests/tooling). Float-free by design: a
// fractional number is an error, mirroring the manifest's float-free invariant.
// ----------------------------------------------------------------------------
namespace {

struct Parser {
  const std::string &s;
  std::size_t i = 0;

  explicit Parser(const std::string &text) : s(text) {}

  [[noreturn]] void fail(const std::string &msg) {
    throw std::runtime_error("json parse error at " + std::to_string(i) + ": " + msg);
  }

  void skipWs() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
      ++i;
  }

  char peek() {
    skipWs();
    if (i >= s.size())
      fail("unexpected end of input");
    return s[i];
  }

  Value parseValue() {
    char c = peek();
    switch (c) {
    case '{': return parseObject();
    case '[': return parseArray();
    case '"': return Value(parseString());
    case 't': case 'f': return parseBool();
    case 'n': expect("null"); return Value(nullptr);
    default:
      if (c == '-' || (c >= '0' && c <= '9'))
        return parseNumber();
      fail("unexpected character");
    }
  }

  void expect(const char *lit) {
    for (const char *p = lit; *p; ++p) {
      if (i >= s.size() || s[i] != *p)
        fail(std::string("expected literal ") + lit);
      ++i;
    }
  }

  Value parseBool() {
    if (s[i] == 't') { expect("true"); return Value(true); }
    expect("false");
    return Value(false);
  }

  Value parseNumber() {
    std::size_t start = i;
    if (i < s.size() && s[i] == '-')
      ++i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9')
      ++i;
    if (i < s.size() && (s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
      fail("floating-point numbers are not allowed in manifests");
    return Value(static_cast<int64_t>(std::stoll(s.substr(start, i - start))));
  }

  std::string parseString() {
    if (s[i] != '"')
      fail("expected string");
    ++i;
    std::string out;
    while (i < s.size()) {
      char c = s[i++];
      if (c == '"')
        return out;
      if (c == '\\') {
        if (i >= s.size())
          fail("unterminated escape");
        char e = s[i++];
        switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          if (i + 4 > s.size())
            fail("bad \\u escape");
          unsigned code = 0;
          for (int k = 0; k < 4; ++k) {
            char h = s[i++];
            code <<= 4;
            if (h >= '0' && h <= '9') code |= unsigned(h - '0');
            else if (h >= 'a' && h <= 'f') code |= unsigned(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') code |= unsigned(h - 'A' + 10);
            else fail("bad hex in \\u escape");
          }
          // Encode the code point as UTF-8 (BMP only; sufficient for manifests).
          if (code < 0x80) {
            out.push_back(char(code));
          } else if (code < 0x800) {
            out.push_back(char(0xC0 | (code >> 6)));
            out.push_back(char(0x80 | (code & 0x3F)));
          } else {
            out.push_back(char(0xE0 | (code >> 12)));
            out.push_back(char(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(char(0x80 | (code & 0x3F)));
          }
          break;
        }
        default: fail("invalid escape");
        }
      } else {
        out.push_back(c);
      }
    }
    fail("unterminated string");
  }

  Value parseArray() {
    ++i; // consume '['
    Value arr = Value::array();
    if (peek() == ']') { ++i; return arr; }
    while (true) {
      arr.push_back(parseValue());
      char c = peek();
      if (c == ',') { ++i; continue; }
      if (c == ']') { ++i; break; }
      fail("expected ',' or ']'");
    }
    return arr;
  }

  Value parseObject() {
    ++i; // consume '{'
    Value obj = Value::object();
    if (peek() == '}') { ++i; return obj; }
    while (true) {
      if (peek() != '"')
        fail("expected object key");
      std::string key = parseString();
      if (peek() != ':')
        fail("expected ':'");
      ++i;
      obj[key] = parseValue();
      char c = peek();
      if (c == ',') { ++i; continue; }
      if (c == '}') { ++i; break; }
      fail("expected ',' or '}'");
    }
    return obj;
  }
};

} // namespace

Value parse(const std::string &text) {
  Parser p(text);
  Value v = p.parseValue();
  p.skipWs();
  if (p.i != text.size())
    p.fail("trailing characters after document");
  return v;
}

} // namespace json
} // namespace ap3
