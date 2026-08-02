# Deploying the manifest-emit service on Coolify

The `service` target in [`docker/Dockerfile`](../docker/Dockerfile) builds a small
HTTP service (`service/app.py`) on the LLVM-18 toolchain image. Deploying it gives
a live URL that proves the compiler toolchain built correctly and that the AP3
manifest hasher reproduces the golden vectors - testable straight from a browser.

It is also the network seam `ap3-pod`'s real `CompilerBackend` calls: `POST /emit`
→ AP3 manifest.

## What gets deployed

`docker-compose.yaml` (repo root) builds the `service` stage and runs it,
health-checked, on port **8080**.

| Endpoint | What it proves |
|----------|----------------|
| `GET /health` | container is live (Coolify health check) |
| `GET /toolchain` | resolved `toolchain_ver` + component SHAs (`toolchain.lock`) |
| `GET /conformance` | the C++ manifest hasher reproduces every ap3-manifest golden vector, **live in the deployed image** |
| `GET /passes` | LLVM/`opt` version + that `libpasses.so` loaded |
| `GET /benchmarks` | bundled programs |
| `GET /emit?program=psi` | emits the AP3 manifest (501 until the RevealView pass lands) |

## Coolify setup (one time)

1. **Connect the repo.** Coolify → your project → **+ New** → **Application** →
   Private Repository (via the GitHub App) → select `silence-laboratories/smaug`.
2. **Branch:** `feat/ap3-manifest-emit` (or `main` once merged).
3. **Build Pack:** **Docker Compose**.
   - **Compose file location:** `/docker-compose.yaml`
   - **Base directory:** `/`
4. **Domain:** assign one to the `smaug-emit` service; it routes to container
   port **8080**. (Coolify reads the `expose`d port.)
5. **Deploy.**

### Auto-deploy on push

Enable **Auto Deploy** on the application (Coolify installs a GitHub webhook).
Every push to the configured branch rebuilds and redeploys.

## Builder resources (important)

The image installs `llvm-18-dev` and compiles the pass plugin, including Smaug's
vendored vectorizer - this is **memory-heavy**. Building it on an undersized box
OOMs (exactly what killed the local emulated build: `exit 137`).

- **Recommended builder:** x86-64, **≥ 8 GB RAM**, a few GB free disk.
- Parallelism is controlled by the `BUILD_JOBS` build arg (empty = all cores).
  On a small builder set it low (e.g. `2`) to bound peak memory; on a large box
  leave it open. Set it in Coolify → application → Build args → `BUILD_JOBS`.
- x86-64 also matches the determinism target, so this builder doubles as the
  authoritative host for the determinism gate (see [reproducibility.md](reproducibility.md)).

## Verifying a deploy

```bash
curl https://<your-domain>/health
curl https://<your-domain>/toolchain      # the pinned toolchain_ver this image emits under
curl https://<your-domain>/conformance    # expect {"passed": true}
```
