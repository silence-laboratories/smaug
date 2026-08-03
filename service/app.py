#!/usr/bin/env python3
"""AP3 manifest-emit service.

A thin HTTP surface over the Smaug toolchain so a Coolify deployment is directly
testable from a URL, and so ap3-pod's real CompilerBackend has a network seam to
call (compile source -> AP3 manifest). Standard library only: the base image has
python3, so this adds no dependencies.

Endpoints:
  GET /             human-readable index
  GET /health       liveness for the Coolify health check
  GET /toolchain    the resolved toolchain.lock (content-addressed toolchain_ver)
  GET /conformance  run the golden-vector conformance suite (proves the C++
                    manifest hasher reproduces ap3-manifest byte-for-byte, live)
  GET /passes       LLVM/opt version + whether the Smaug pass plugin loaded
  GET /benchmarks   list bundled benchmark programs
  GET /emit?program=<name>   emit the AP3 manifest for a benchmark
                             (returns 501 until the RevealView pass is wired in)
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

try:  # POSIX-only; used to bound an untrusted compile (absent on non-Linux dev hosts)
    import resource
except ImportError:  # pragma: no cover
    resource = None

PORT = int(os.environ.get("PORT", "8080"))
SMAUG = os.environ.get("SMAUG_HOME", "/opt/smaug")
PLUGIN = f"{SMAUG}/passes/build/libpasses.so"
LOCK = f"{SMAUG}/toolchain.lock"
CONFORMANCE_BIN = f"{SMAUG}/passes/manifest/build/test_conformance"
GOLDEN = f"{SMAUG}/passes/manifest/tests/golden_vectors.json"
BENCH_DIR = f"{SMAUG}/benchmarks"
META_DIR = f"{SMAUG}/benchmarks/metadata"
EMIT_SCRIPT = f"{SMAUG}/scripts/emit_manifest.sh"

# --- custom-source compile limits (Tier 1: compile-as-a-service) --------------
# The POST /emit endpoint compiles UNTRUSTED source. These bound the blast radius
# at the process level (ephemeral dir + rlimits + wall timeout). Stronger isolation
# (network namespace, gVisor/seccomp, one container per compile) is the production
# hardening — see docs/manifest-emit.md.
MAX_SOURCE_BYTES = 256 * 1024
MAX_METADATA_BYTES = 64 * 1024
COMPILE_WALL_TIMEOUT = int(os.environ.get("AP3_COMPILE_TIMEOUT", "45"))  # seconds
COMPILE_CPU_SECONDS = int(os.environ.get("AP3_COMPILE_CPU", "30"))
COMPILE_MEM_BYTES = int(os.environ.get("AP3_COMPILE_MEM_MB", "2048")) * 1024 * 1024
COMPILE_FSIZE_BYTES = 16 * 1024 * 1024


def _limit_resources():
    """preexec_fn: cap CPU time, address space, and file size for the compile.

    Inherited by clang/opt grandchildren. Each limit is best-effort: some (notably
    RLIMIT_AS) aren't honored on macOS dev hosts, but all apply on the Linux image
    where the untrusted compile actually runs. A failed limit must never abort the
    compile via a preexec exception.
    """
    if resource is None:
        return
    for what, limit in (
        (resource.RLIMIT_CPU, (COMPILE_CPU_SECONDS, COMPILE_CPU_SECONDS)),
        (resource.RLIMIT_AS, (COMPILE_MEM_BYTES, COMPILE_MEM_BYTES)),
        (resource.RLIMIT_FSIZE, (COMPILE_FSIZE_BYTES, COMPILE_FSIZE_BYTES)),
        (resource.RLIMIT_CORE, (0, 0)),
    ):
        try:
            resource.setrlimit(what, limit)
        except (ValueError, OSError):
            pass


def benchmark_names():
    if not os.path.isdir(BENCH_DIR):
        return []
    return sorted(f[:-4] for f in os.listdir(BENCH_DIR) if f.endswith(".cpp"))


def run(cmd, timeout=120):
    """Run a subprocess, capturing output; never raise on non-zero exit."""
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return {"rc": p.returncode, "stdout": p.stdout, "stderr": p.stderr}
    except subprocess.TimeoutExpired:
        return {"rc": -1, "stdout": "", "stderr": f"timeout after {timeout}s"}
    except FileNotFoundError as e:
        return {"rc": -1, "stdout": "", "stderr": str(e)}


INDEX = """<!doctype html><meta charset=utf-8>
<title>AP3 manifest-emit service</title>
<style>body{font:15px/1.5 system-ui,sans-serif;max-width:44rem;margin:3rem auto;padding:0 1rem}
code{background:#f4f4f5;padding:.1em .3em;border-radius:4px}a{color:#4f46e5}</style>
<h1>AP3 manifest-emit service</h1>
<p>Smaug compiles C/C++ into an MPC protocol and <em>emits into</em> the AP3
circuit manifest. This service exposes that toolchain over HTTP.</p>
<ul>
<li><a href="/health">/health</a> &mdash; liveness</li>
<li><a href="/toolchain">/toolchain</a> &mdash; resolved toolchain_ver + component SHAs</li>
<li><a href="/conformance">/conformance</a> &mdash; golden-vector hash conformance (live)</li>
<li><a href="/passes">/passes</a> &mdash; LLVM version + pass-plugin load check</li>
<li><a href="/benchmarks">/benchmarks</a> &mdash; bundled programs</li>
<li><a href="/emit?program=psi">GET /emit?program=psi</a> &mdash; emit a bundled program's manifest</li>
<li><code>POST /emit</code> &mdash; compile your own <code>{source, metadata}</code> (sandboxed) &rarr; manifest</li>
</ul>"""


class Handler(BaseHTTPRequestHandler):
    server_version = "ap3-emit/0.1"

    def log_message(self, *_):  # keep the container logs quiet
        pass

    def _send(self, code, body, ctype="application/json"):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def _json(self, code, obj):
        self._send(code, json.dumps(obj, indent=2, sort_keys=True))

    def do_HEAD(self):
        self.do_GET()

    def do_POST(self):
        path = urlparse(self.path).path.rstrip("/") or "/"
        if path != "/emit":
            return self._json(404, {"error": "not found", "see": "/"})
        try:
            self.emit_custom()
        except Exception as e:  # never leak a stack trace
            self._json(500, {"error": type(e).__name__, "detail": str(e)})

    def do_GET(self):
        route = urlparse(self.path)
        path = route.path.rstrip("/") or "/"
        query = parse_qs(route.query)
        handler = {
            "/": self.index,
            "/health": self.health,
            "/toolchain": self.toolchain,
            "/conformance": self.conformance,
            "/passes": self.passes,
            "/benchmarks": self.benchmarks,
            "/emit": lambda: self.emit(query),
        }.get(path)
        if handler is None:
            return self._json(404, {"error": "not found", "see": "/"})
        try:
            handler()
        except Exception as e:  # never 500 with a stack trace to the client
            self._json(500, {"error": type(e).__name__, "detail": str(e)})

    # -- endpoints ------------------------------------------------------------
    def index(self):
        self._send(200, INDEX, "text/html; charset=utf-8")

    def health(self):
        self._json(200, {"status": "ok"})

    def toolchain(self):
        if not os.path.exists(LOCK):
            return self._json(503, {"error": "toolchain.lock not present"})
        with open(LOCK) as f:
            self._json(200, json.load(f))

    def conformance(self):
        if not os.path.exists(CONFORMANCE_BIN):
            return self._json(503, {"error": "conformance binary not built"})
        res = run([CONFORMANCE_BIN, GOLDEN])
        self._json(200 if res["rc"] == 0 else 500, {
            "passed": res["rc"] == 0,
            "detail": res["stdout"] or res["stderr"],
        })

    def passes(self):
        ver = run(["opt", "--version"])
        self._json(200, {
            "opt_version": (ver["stdout"] or ver["stderr"]).strip(),
            "plugin_path": PLUGIN,
            "plugin_built": os.path.exists(PLUGIN),
            "plugin_bytes": os.path.getsize(PLUGIN) if os.path.exists(PLUGIN) else 0,
        })

    def benchmarks(self):
        self._json(200, {"benchmarks": benchmark_names()})

    def emit(self, query):
        program = (query.get("program") or ["psi"])[0]
        # Whitelist against the bundled programs - never interpolate arbitrary
        # user input into a path or command.
        if program not in benchmark_names():
            return self._json(404, {
                "error": "unknown program", "program": program,
                "available": benchmark_names(),
            })
        cpp = os.path.join(BENCH_DIR, f"{program}.cpp")
        meta = os.path.join(META_DIR, f"{program}.ll.json")
        if not os.path.exists(meta):
            return self._json(404, {"error": "no metadata", "program": program})

        out = f"/tmp/{program}.manifest.json"
        if os.path.exists(out):
            os.remove(out)
        res = run([EMIT_SCRIPT, cpp, meta, out])
        if res["rc"] != 0:
            return self._json(500, {
                "error": "emit failed", "program": program,
                "detail": res["stderr"] or res["stdout"],
            })
        # The pass only writes a manifest when the program carries ap3 annotations.
        if not os.path.exists(out):
            return self._json(200, {
                "emitted": False, "program": program,
                "reason": "program has no \"ap3\" annotation block",
                "detail": res["stderr"].strip(),
            })
        with open(out) as f:
            manifest = json.load(f)
        self._json(200, {
            "emitted": True,
            "program": program,
            "manifest_hash": manifest.get("manifest_hash"),
            "manifest": manifest,
        })

    def emit_custom(self):
        """POST /emit — compile UNTRUSTED source + ap3 metadata and emit a manifest.

        Body: {"source": "<C/C++>", "metadata": {<ap3 metadata obj>},
               "program": "<name?>", "source_hash": "<hex?>"}. The compile runs in
        an ephemeral dir under CPU/mem/file-size rlimits + a wall-clock timeout.
        """
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0 or length > MAX_SOURCE_BYTES + MAX_METADATA_BYTES + 4096:
            return self._json(413, {"error": "missing or oversized body"})
        try:
            body = json.loads(self.rfile.read(length).decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            return self._json(400, {"error": "body must be JSON"})

        source = body.get("source")
        metadata = body.get("metadata")
        if not isinstance(source, str) or not source:
            return self._json(400, {"error": "'source' (string) is required"})
        if not isinstance(metadata, dict) or not metadata:
            return self._json(400, {"error": "'metadata' (object with an ap3 block) is required"})
        if len(source.encode("utf-8")) > MAX_SOURCE_BYTES:
            return self._json(413, {"error": f"source exceeds {MAX_SOURCE_BYTES} bytes"})
        metadata_text = json.dumps(metadata)
        if len(metadata_text.encode("utf-8")) > MAX_METADATA_BYTES:
            return self._json(413, {"error": f"metadata exceeds {MAX_METADATA_BYTES} bytes"})

        program = body.get("program") or "custom"
        # Deterministic input anchor so two pods that submit identical source+metadata
        # compute the same source_hash (excluded from manifest_hash; used for audit).
        source_hash = body.get("source_hash") or hashlib.sha256(
            source.encode("utf-8") + b"\n" + metadata_text.encode("utf-8")
        ).hexdigest()

        work = tempfile.mkdtemp(prefix="ap3-emit-")
        try:
            cpp = os.path.join(work, "prog.cpp")
            meta = os.path.join(work, "prog.ll.json")
            out = os.path.join(work, "prog.manifest.json")
            with open(cpp, "w") as f:
                f.write(source)
            with open(meta, "w") as f:
                f.write(metadata_text)

            env = dict(os.environ, AP3_SOURCE_HASH=source_hash)
            try:
                p = subprocess.run(
                    [EMIT_SCRIPT, cpp, meta, out],
                    capture_output=True, text=True,
                    timeout=COMPILE_WALL_TIMEOUT,
                    preexec_fn=_limit_resources if resource else None,
                    env=env,
                )
            except subprocess.TimeoutExpired:
                return self._json(408, {"error": "compile timed out", "program": program})

            if p.returncode != 0 or not os.path.exists(out):
                return self._json(422, {
                    "error": "compile/emit failed",
                    "program": program,
                    "detail": (p.stderr or p.stdout)[-4000:],
                })
            with open(out) as f:
                manifest = json.load(f)
            self._json(200, {
                "emitted": True,
                "program": program,
                "source_hash": source_hash,
                "manifest_hash": manifest.get("manifest_hash"),
                "manifest": manifest,
            })
        finally:
            shutil.rmtree(work, ignore_errors=True)


def main():
    srv = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"ap3-emit listening on :{PORT}", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
