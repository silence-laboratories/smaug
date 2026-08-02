// A tiny ordered JSON value with a *canonical* serializer that is byte-identical
// to Python's json.dumps(sort_keys=True, separators=(",",":"), ensure_ascii=False,
// allow_nan=False) over the manifest's ASCII, float-free semantic subset. That
// byte-parity is what lets Smaug (C++) and ap3-manifest (Python) agree on
// manifest_hash. See ap3-manifest/src/ap3_manifest/canonical.py.
//
// A minimal parser is included for tests/tooling (production emit only serializes).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ap3 {
namespace json {

enum class Type { Null, Bool, Int, Str, Array, Object };

class Value {
public:
  Value() : type_(Type::Null) {}
  Value(std::nullptr_t) : type_(Type::Null) {}
  Value(bool b) : type_(Type::Bool), bool_(b) {}
  Value(int64_t i) : type_(Type::Int), int_(i) {}
  Value(int i) : type_(Type::Int), int_(i) {}
  Value(const char *s) : type_(Type::Str), str_(s) {}
  Value(std::string s) : type_(Type::Str), str_(std::move(s)) {}

  static Value array() { Value v; v.type_ = Type::Array; return v; }
  static Value object() { Value v; v.type_ = Type::Object; return v; }

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool asBool() const { return bool_; }
  int64_t asInt() const { return int_; }
  const std::string &asStr() const { return str_; }
  const std::vector<Value> &items() const { return array_; }

  // Array building.
  void push_back(Value v) { array_.push_back(std::move(v)); }

  // Object building. Insertion order is irrelevant: the serializer sorts keys.
  Value &operator[](const std::string &key) {
    auto it = obj_index_.find(key);
    if (it != obj_index_.end())
      return obj_[it->second].second;
    obj_index_[key] = obj_.size();
    obj_.emplace_back(key, Value());
    return obj_.back().second;
  }
  bool has(const std::string &key) const { return obj_index_.count(key) != 0; }
  const Value &at(const std::string &key) const {
    auto it = obj_index_.find(key);
    if (it == obj_index_.end())
      throw std::out_of_range("json key not found: " + key);
    return obj_[it->second].second;
  }
  const std::vector<std::pair<std::string, Value>> &members() const { return obj_; }

  // Canonical (RFC 8785 subset) serialization used for hashing.
  std::string dump() const {
    std::string out;
    write(out);
    return out;
  }

private:
  void write(std::string &out) const;
  static void writeString(std::string &out, const std::string &s);

  Type type_;
  bool bool_ = false;
  int64_t int_ = 0;
  std::string str_;
  std::vector<Value> array_;
  std::vector<std::pair<std::string, Value>> obj_;
  std::map<std::string, std::size_t> obj_index_;
};

// Parse a JSON document. Throws std::runtime_error on malformed input. Intended
// for tests and tooling, not the hot path.
Value parse(const std::string &text);

} // namespace json
} // namespace ap3
