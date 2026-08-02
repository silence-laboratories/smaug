#include "ap3/Manifest.h"

#include "ap3/Sha256.h"

#include <set>

namespace ap3 {
namespace {

using json::Value;

bool inSet(const std::string &v, std::initializer_list<const char *> allowed) {
  for (const char *a : allowed)
    if (v == a)
      return true;
  return false;
}

Value shapeToJson(const std::optional<std::vector<int64_t>> &shape) {
  if (!shape)
    return Value(nullptr);
  Value arr = Value::array();
  for (int64_t d : *shape)
    arr.push_back(Value(d));
  return arr;
}

} // namespace

// Mirrors canonical.py semantic_view(): the structured subset, with input.shape
// always present (null when absent). Object key ordering is handled by the
// canonical serializer, so insertion order here is irrelevant.
json::Value Manifest::semanticView() const {
  Value root = Value::object();
  root["manifest_version"] = Value(manifest_version);

  Value partiesArr = Value::array();
  for (const auto &p : parties)
    partiesArr.push_back(Value(p));
  root["parties"] = partiesArr;

  Value inputsArr = Value::array();
  for (const auto &in : inputs) {
    Value o = Value::object();
    o["name"] = Value(in.name);
    o["dtype"] = Value(in.dtype);
    o["party"] = Value(in.party);
    o["private"] = Value(in.priv);
    o["shape"] = shapeToJson(in.shape);
    inputsArr.push_back(std::move(o));
  }
  root["inputs"] = inputsArr;

  Value outputsArr = Value::array();
  for (const auto &out : outputs) {
    Value o = Value::object();
    o["name"] = Value(out.name);
    o["dtype"] = Value(out.dtype);
    o["reveal_to"] = Value(out.reveal_to);
    outputsArr.push_back(std::move(o));
  }
  root["outputs"] = outputsArr;

  Value rv = Value::object();
  Value rvOutputs = Value::array();
  for (const auto &ro : reveal_view.outputs) {
    Value o = Value::object();
    o["name"] = Value(ro.name);
    o["reveal_to"] = Value(ro.reveal_to);
    o["dtype"] = Value(ro.dtype);
    rvOutputs.push_back(std::move(o));
  }
  rv["outputs"] = rvOutputs;
  Value rvLeaks = Value::array();
  for (const auto &lk : reveal_view.leaks) {
    Value o = Value::object();
    o["kind"] = Value(lk.kind);
    o["to"] = Value(lk.to);
    rvLeaks.push_back(std::move(o));
  }
  rv["leaks"] = rvLeaks;
  root["reveal_view"] = rv;

  return root;
}

std::string Manifest::computeHash() const {
  return sha256_hex(semanticView().dump());
}

json::Value Manifest::toJson() const {
  Value root = Value::object();
  root["manifest_version"] = Value(manifest_version);

  Value prog = Value::object();
  prog["source_hash"] = Value(program.source_hash);
  prog["toolchain_ver"] = Value(program.toolchain_ver);
  if (program.name)
    prog["name"] = Value(*program.name);
  root["program"] = prog;

  Value partiesArr = Value::array();
  for (const auto &p : parties)
    partiesArr.push_back(Value(p));
  root["parties"] = partiesArr;

  Value inputsArr = Value::array();
  for (const auto &in : inputs) {
    Value o = Value::object();
    o["name"] = Value(in.name);
    o["dtype"] = Value(in.dtype);
    o["party"] = Value(in.party);
    o["private"] = Value(in.priv);
    if (in.shape)
      o["shape"] = shapeToJson(in.shape);
    inputsArr.push_back(std::move(o));
  }
  root["inputs"] = inputsArr;

  Value outputsArr = Value::array();
  for (const auto &out : outputs) {
    Value o = Value::object();
    o["name"] = Value(out.name);
    o["dtype"] = Value(out.dtype);
    o["reveal_to"] = Value(out.reveal_to);
    outputsArr.push_back(std::move(o));
  }
  root["outputs"] = outputsArr;

  Value rv = Value::object();
  Value rvOutputs = Value::array();
  for (const auto &ro : reveal_view.outputs) {
    Value o = Value::object();
    o["name"] = Value(ro.name);
    o["reveal_to"] = Value(ro.reveal_to);
    o["dtype"] = Value(ro.dtype);
    o["derivation"] = Value(ro.derivation);
    rvOutputs.push_back(std::move(o));
  }
  rv["outputs"] = rvOutputs;
  Value rvLeaks = Value::array();
  for (const auto &lk : reveal_view.leaks) {
    Value o = Value::object();
    o["kind"] = Value(lk.kind);
    o["to"] = Value(lk.to);
    if (lk.description)
      o["description"] = Value(*lk.description);
    rvLeaks.push_back(std::move(o));
  }
  rv["leaks"] = rvLeaks;
  if (reveal_view.summary)
    rv["summary"] = Value(*reveal_view.summary);
  root["reveal_view"] = rv;

  if (cost) {
    Value c = Value::object();
    if (cost->gate_count)
      c["gate_count"] = Value(*cost->gate_count);
    if (cost->depth)
      c["depth"] = Value(*cost->depth);
    if (cost->est_rounds)
      c["est_rounds"] = Value(*cost->est_rounds);
    root["cost"] = c;
  }

  root["manifest_hash"] = Value(manifest_hash ? *manifest_hash : computeHash());
  return root;
}

std::string Manifest::validate() const {
  if (manifest_version.empty())
    return "manifest_version is empty";
  if (parties.size() != 2 || parties[0] != "A" || parties[1] != "B")
    return "parties must be exactly [\"A\",\"B\"]";
  if (program.source_hash.empty())
    return "program.source_hash is empty";
  if (program.toolchain_ver.empty())
    return "program.toolchain_ver is empty";

  std::set<std::string> outputNames;
  for (const auto &in : inputs) {
    if (in.name.empty())
      return "input has empty name";
    if (in.dtype.empty())
      return "input '" + in.name + "' has empty dtype";
    if (!inSet(in.party, {"A", "B", "public"}))
      return "input '" + in.name + "' has invalid party '" + in.party + "'";
  }
  for (const auto &out : outputs) {
    if (out.name.empty())
      return "output has empty name";
    if (out.dtype.empty())
      return "output '" + out.name + "' has empty dtype";
    if (!inSet(out.reveal_to, {"A", "B", "both", "none"}))
      return "output '" + out.name + "' has invalid reveal_to '" + out.reveal_to + "'";
    outputNames.insert(out.name);
  }
  for (const auto &ro : reveal_view.outputs) {
    if (!inSet(ro.reveal_to, {"A", "B", "both"}))
      return "reveal_view output '" + ro.name + "' has invalid reveal_to '" + ro.reveal_to + "'";
    // Soundness cross-check: a revealed output must be a declared output.
    if (outputNames.find(ro.name) == outputNames.end())
      return "reveal_view output '" + ro.name + "' is not a declared output";
  }
  for (const auto &lk : reveal_view.leaks) {
    if (lk.kind.empty())
      return "leak has empty kind";
    if (!inSet(lk.to, {"A", "B", "both", "none"}))
      return "leak '" + lk.kind + "' has invalid 'to' '" + lk.to + "'";
  }
  return "";
}

Manifest Manifest::fromJson(const json::Value &v) {
  using json::Type;
  Manifest m;
  m.manifest_version = v.at("manifest_version").asStr();

  const auto &prog = v.at("program");
  m.program.source_hash = prog.at("source_hash").asStr();
  m.program.toolchain_ver = prog.at("toolchain_ver").asStr();
  if (prog.has("name"))
    m.program.name = prog.at("name").asStr();

  m.parties.clear();
  for (const auto &p : v.at("parties").items())
    m.parties.push_back(p.asStr());

  for (const auto &in : v.at("inputs").items()) {
    Input i;
    i.name = in.at("name").asStr();
    i.dtype = in.at("dtype").asStr();
    i.party = in.at("party").asStr();
    i.priv = in.at("private").asBool();
    if (in.has("shape") && in.at("shape").type() == Type::Array) {
      std::vector<int64_t> dims;
      for (const auto &d : in.at("shape").items())
        dims.push_back(d.asInt());
      i.shape = dims;
    }
    m.inputs.push_back(std::move(i));
  }

  for (const auto &out : v.at("outputs").items()) {
    Output o;
    o.name = out.at("name").asStr();
    o.dtype = out.at("dtype").asStr();
    o.reveal_to = out.at("reveal_to").asStr();
    m.outputs.push_back(std::move(o));
  }

  const auto &rv = v.at("reveal_view");
  for (const auto &ro : rv.at("outputs").items()) {
    RevealOutput r;
    r.name = ro.at("name").asStr();
    r.reveal_to = ro.at("reveal_to").asStr();
    r.dtype = ro.at("dtype").asStr();
    if (ro.has("derivation"))
      r.derivation = ro.at("derivation").asStr();
    m.reveal_view.outputs.push_back(std::move(r));
  }
  for (const auto &lk : rv.at("leaks").items()) {
    Leak l;
    l.kind = lk.at("kind").asStr();
    l.to = lk.at("to").asStr();
    if (lk.has("description"))
      l.description = lk.at("description").asStr();
    m.reveal_view.leaks.push_back(std::move(l));
  }
  if (rv.has("summary"))
    m.reveal_view.summary = rv.at("summary").asStr();

  if (v.has("cost")) {
    const auto &c = v.at("cost");
    Cost cost;
    if (c.has("gate_count")) cost.gate_count = c.at("gate_count").asInt();
    if (c.has("depth")) cost.depth = c.at("depth").asInt();
    if (c.has("est_rounds")) cost.est_rounds = c.at("est_rounds").asInt();
    m.cost = cost;
  }
  if (v.has("manifest_hash"))
    m.manifest_hash = v.at("manifest_hash").asStr();

  return m;
}

} // namespace ap3
