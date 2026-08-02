#!/usr/bin/env bash
#
# gen_toolchain_lock.sh — emit a JSON record of the *resolved* toolchain.
#
# Runs INSIDE the build container. Captures the exact versions and commit SHAs of
# every component that can affect a compiled manifest, then derives a stable,
# content-addressed `toolchain_ver` string. Downstream (ap3-pod, consensus) treats
# `toolchain_ver` as an opaque equality anchor: two pods agree only if their
# toolchains produce the same string.
#
# Emitted to /opt/smaug/toolchain.lock at build time; also printed so it is
# visible in the build log for auditing.
set -euo pipefail

# --- component versions -------------------------------------------------------
llvm_full="$(clang --version | head -1 | sed 's/.*version //' | awk '{print $1}')"
llvm_major="${llvm_full%%.*}"
ubuntu_ver="$(. /etc/os-release && echo "${VERSION_ID}")"

# EMP repos are cloned by the upstream installer under /opt/emp when present.
emp_sha() {
  local dir="/opt/emp/$1"
  if [ -d "${dir}/.git" ]; then git -C "${dir}" rev-parse HEAD; else echo "absent"; fi
}
emp_tool_sha="$(emp_sha emp-tool)"
emp_ot_sha="$(emp_sha emp-ot)"
emp_sh2pc_sha="$(emp_sha emp-sh2pc)"
emp_aby_sha="$(emp_sha emp-aby)"

# Smaug source revision (the compiler passes themselves).
if git -C /opt/smaug rev-parse HEAD >/dev/null 2>&1; then
  smaug_sha="$(git -C /opt/smaug rev-parse --short HEAD)"
else
  # Source was COPYed without .git (normal for the image build). Fall back to a
  # content hash of the pass sources so the lock still moves when they change.
  smaug_sha="src-$(find /opt/smaug/passes/src /opt/smaug/passes/include -type f \
      -exec sha256sum {} + 2>/dev/null | sort | sha256sum | cut -c1-12)"
fi

# --- content-addressed toolchain_ver -----------------------------------------
# Human-readable family string (mirrors the golden-vector convention
# "smaug-<ver>+llvm<major>+emp-<x>") with a digest suffix binding the exact
# component SHAs, so equality is meaningful and tamper-evident.
digest_input="ubuntu=${ubuntu_ver};llvm=${llvm_full};smaug=${smaug_sha};emp-tool=${emp_tool_sha};emp-ot=${emp_ot_sha};emp-sh2pc=${emp_sh2pc_sha};emp-aby=${emp_aby_sha}"
toolchain_digest="$(printf '%s' "${digest_input}" | sha256sum | cut -c1-16)"
toolchain_ver="smaug-${smaug_sha}+llvm${llvm_major}+emp-0.3.x+${toolchain_digest}"

jq -n \
  --arg toolchain_ver "${toolchain_ver}" \
  --arg toolchain_digest "${toolchain_digest}" \
  --arg ubuntu "${ubuntu_ver}" \
  --arg llvm "${llvm_full}" \
  --arg smaug "${smaug_sha}" \
  --arg emp_tool "${emp_tool_sha}" \
  --arg emp_ot "${emp_ot_sha}" \
  --arg emp_sh2pc "${emp_sh2pc_sha}" \
  --arg emp_aby "${emp_aby_sha}" \
  '{
    toolchain_ver: $toolchain_ver,
    toolchain_digest: $toolchain_digest,
    components: {
      ubuntu: $ubuntu,
      llvm: $llvm,
      smaug: $smaug,
      "emp-tool": $emp_tool,
      "emp-ot": $emp_ot,
      "emp-sh2pc": $emp_sh2pc,
      "emp-aby": $emp_aby
    }
  }'
