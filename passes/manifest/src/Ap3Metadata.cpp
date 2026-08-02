#include "ap3/Ap3Metadata.h"

#include <stdexcept>

namespace ap3 {
namespace {

using json::Value;

std::string reqStr(const Value &o, const std::string &key, const std::string &ctx) {
  if (!o.has(key) || o.at(key).type() != json::Type::Str)
    throw std::runtime_error("ap3 metadata: missing/invalid string '" + key + "' in " + ctx);
  return o.at(key).asStr();
}

std::string optStr(const Value &o, const std::string &key) {
  return (o.has(key) && o.at(key).type() == json::Type::Str) ? o.at(key).asStr() : std::string();
}

Leak parseLeak(const Value &o) {
  Leak l;
  l.kind = reqStr(o, "kind", "leak");
  l.to = reqStr(o, "to", "leak");
  if (o.has("description"))
    l.description = o.at("description").asStr();
  return l;
}

Ap3Input parseInput(const Value &o) {
  Ap3Input in;
  if (o.has("arg"))
    in.arg = static_cast<int>(o.at("arg").asInt());
  in.name = reqStr(o, "name", "ap3.inputs[]");
  in.party = reqStr(o, "party", "ap3.inputs[]");
  in.dtype = optStr(o, "dtype");
  if (o.has("shape") && o.at("shape").type() == json::Type::Array) {
    std::vector<int64_t> dims;
    for (const auto &d : o.at("shape").items())
      dims.push_back(d.asInt());
    in.shape = dims;
  }
  return in;
}

Ap3Output parseOutput(const Value &o) {
  Ap3Output out;
  if (o.has("arg"))
    out.arg = static_cast<int>(o.at("arg").asInt());
  out.name = reqStr(o, "name", "ap3.outputs[]");
  out.dtype = optStr(o, "dtype");
  out.reveal_to = reqStr(o, "reveal_to", "ap3.outputs[]");
  out.derivation = optStr(o, "derivation");
  if (o.has("leaks"))
    for (const auto &lk : o.at("leaks").items())
      out.leaks.push_back(parseLeak(lk));
  return out;
}

Ap3FunctionSpec parseSpec(const std::string &func, const Value &ap3) {
  Ap3FunctionSpec spec;
  spec.func_name = func;
  spec.program = optStr(ap3, "program");
  spec.entry = ap3.has("entry") && ap3.at("entry").type() == json::Type::Bool &&
               ap3.at("entry").asBool();
  if (ap3.has("inputs"))
    for (const auto &in : ap3.at("inputs").items())
      spec.inputs.push_back(parseInput(in));
  if (ap3.has("outputs"))
    for (const auto &out : ap3.at("outputs").items())
      spec.outputs.push_back(parseOutput(out));
  if (ap3.has("summary"))
    spec.summary = ap3.at("summary").asStr();
  return spec;
}

} // namespace

std::map<std::string, Ap3FunctionSpec> parseAp3Metadata(const std::string &jsonText) {
  Value doc = json::parse(jsonText);
  std::map<std::string, Ap3FunctionSpec> out;
  if (doc.type() != json::Type::Object)
    throw std::runtime_error("ap3 metadata: top level is not an object");
  for (const auto &kv : doc.members()) {
    const Value &fn = kv.second;
    if (fn.type() == json::Type::Object && fn.has("ap3"))
      out.emplace(kv.first, parseSpec(kv.first, fn.at("ap3")));
  }
  return out;
}

std::optional<Ap3FunctionSpec>
selectEntry(const std::map<std::string, Ap3FunctionSpec> &specs, std::string &err) {
  if (specs.empty()) {
    err = "no function carries an \"ap3\" annotation block";
    return std::nullopt;
  }
  const Ap3FunctionSpec *chosen = nullptr;
  for (const auto &kv : specs) {
    if (kv.second.entry) {
      if (chosen) {
        err = "multiple functions declare ap3.entry=true";
        return std::nullopt;
      }
      chosen = &kv.second;
    }
  }
  if (!chosen) {
    if (specs.size() != 1) {
      err = "no ap3.entry=true and multiple annotated functions; cannot pick entry";
      return std::nullopt;
    }
    chosen = &specs.begin()->second;
  }
  return *chosen;
}

Manifest buildManifest(const Ap3FunctionSpec &spec, const std::string &source_hash,
                       const std::string &toolchain_ver) {
  Manifest m;
  m.manifest_version = "1.0.0";
  m.program.source_hash = source_hash;
  m.program.toolchain_ver = toolchain_ver;
  if (!spec.program.empty())
    m.program.name = spec.program;
  m.parties = {"A", "B"};

  for (const auto &in : spec.inputs) {
    Input i;
    i.name = in.name;
    i.dtype = in.dtype;
    i.party = in.party;
    i.priv = (in.party != "public"); // party-owned inputs are secret-shared
    i.shape = in.shape;
    m.inputs.push_back(std::move(i));
  }

  for (const auto &out : spec.outputs) {
    Output o;
    o.name = out.name;
    o.dtype = out.dtype;
    o.reveal_to = out.reveal_to;
    m.outputs.push_back(std::move(o));

    // Only revealed outputs appear in the reveal-view (schema allows A/B/both).
    if (out.reveal_to != "none") {
      RevealOutput ro;
      ro.name = out.name;
      ro.reveal_to = out.reveal_to;
      ro.dtype = out.dtype;
      ro.derivation = out.derivation;
      m.reveal_view.outputs.push_back(std::move(ro));
    }
    for (const auto &lk : out.leaks)
      m.reveal_view.leaks.push_back(lk);
  }
  m.reveal_view.summary = spec.summary;

  m.setHash();
  return m;
}

} // namespace ap3
