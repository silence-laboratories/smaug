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

import json
import os
import subprocess
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

PORT = int(os.environ.get("PORT", "8080"))
SMAUG = os.environ.get("SMAUG_HOME", "/opt/smaug")
PLUGIN = f"{SMAUG}/passes/build/libpasses.so"
LOCK = f"{SMAUG}/toolchain.lock"
CONFORMANCE_BIN = f"{SMAUG}/passes/manifest/build/test_conformance"
GOLDEN = f"{SMAUG}/passes/manifest/tests/golden_vectors.json"
BENCH_DIR = f"{SMAUG}/benchmarks"
META_DIR = f"{SMAUG}/benchmarks/metadata"
EMIT_SCRIPT = f"{SMAUG}/scripts/emit_manifest.sh"


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
<li><a href="/emit?program=psi">/emit?program=psi</a> &mdash; emit a manifest</li>
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


def main():
    srv = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"ap3-emit listening on :{PORT}", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
