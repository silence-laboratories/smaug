# syntax=docker/dockerfile:1.7
#
# Hermetic Smaug build toolchain.
#
# Two build targets share one pinned base so hashes are comparable:
#
#   --target llvm-dev   Ubuntu + LLVM-18 only. Enough to build the pass plugin
#                       (libpasses.so) and emit AP3 manifests via `opt` on .ll.
#                       Fast inner loop for compiler work; NO EMP, so it cannot
#                       link or run the 2PC executables.
#
#   --target full       llvm-dev + EMP toolkit + the built runtime. Needed for
#                       end-to-end 2PC runs and the determinism gate. (default)
#
# Every externally-fetched dependency is pinned by an ARG (see docker/toolchain.env).
# Tags are used to FETCH; the exact resolved commit SHAs are captured into
# /opt/smaug/toolchain.lock at build time (see scripts/gen_toolchain_lock.sh), so
# a build is auditable even when an upstream tag moves. Pin the ARGs to digests /
# SHAs for a fully reproducible build (see docs/reproducibility.md).

# ---- pinned inputs -----------------------------------------------------------
# Base image. Override BASE_DIGEST with a sha256 for a byte-reproducible base:
#   docker build --build-arg BASE_REF=ubuntu@sha256:... .
ARG BASE_REF=ubuntu:24.04

# LLVM major version. Smaug pins 18 (its vendored vectorizer targets LLVM-18).
ARG LLVM_VERSION=18

# EMP toolkit refs. v0.3.x are moving tags; the lock file records resolved SHAs.
ARG EMP_TOOL_REF=0.3.x
ARG EMP_OT_REF=0.3.x
ARG EMP_SH2PC_REF=0.3.x
# emp-aby (Yao + GMW backends) - Smaug's fork/branch.
ARG EMP_ABY_REPO=https://github.com/radhika1601/ScalableMixedModeMPC.git
ARG EMP_ABY_BRANCH=no-open-fhe

# Reproducibility knob: fix embedded timestamps. Default is the Unix epoch.
ARG SOURCE_DATE_EPOCH=0

# Build parallelism. Empty = all cores. Set low (e.g. 2) on memory-constrained
# builders: the vendored vectorizer TUs are memory-heavy and can OOM a small box.
ARG BUILD_JOBS=

# =============================================================================
# base - common OS packages, deterministic apt
# =============================================================================
FROM ${BASE_REF} AS base
ARG SOURCE_DATE_EPOCH
ENV DEBIAN_FRONTEND=noninteractive \
    SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH} \
    LC_ALL=C.UTF-8 \
    LANG=C.UTF-8
# --no-install-recommends keeps the layer minimal and stable across rebuilds.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        cmake \
        git \
        build-essential \
        libssl-dev \
        python3 \
        python3-pip \
        wget \
        curl \
        sudo \
        lsb-release \
        software-properties-common \
        gnupg \
        jq \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /opt/smaug

# =============================================================================
# llvm - LLVM-18 dev toolchain (clang, opt, dev libs, cmake config)
# =============================================================================
FROM base AS llvm
ARG LLVM_VERSION
# apt.llvm.org's official installer, pinned to the requested major version.
RUN wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh \
    && chmod +x /tmp/llvm.sh \
    && /tmp/llvm.sh ${LLVM_VERSION} \
    && apt-get update && apt-get install -y --no-install-recommends \
        llvm-${LLVM_VERSION}-dev \
        clang-${LLVM_VERSION} \
        libclang-${LLVM_VERSION}-dev \
    && rm -rf /var/lib/apt/lists/* /tmp/llvm.sh
# Canonical, version-agnostic install prefix the passes CMake expects.
ENV LLVM_18_PREFIX=/usr/lib/llvm-${LLVM_VERSION}
ENV PATH=/usr/lib/llvm-${LLVM_VERSION}/bin:${PATH}
RUN clang-${LLVM_VERSION} --version && opt --version

# =============================================================================
# llvm-dev - TARGET: build the pass plugin only (no EMP). Fast compiler loop.
# =============================================================================
FROM llvm AS llvm-dev
ARG LLVM_VERSION
ARG BUILD_JOBS
# Source is bind-mounted at build/run time (see Makefile: `make shell`). We copy
# just enough to pre-build the plugin so the image is self-contained for CI.
COPY passes/ /opt/smaug/passes/
COPY runtime/mpc/ /opt/smaug/runtime/mpc/
COPY scripts/ /opt/smaug/scripts/
COPY benchmarks/ /opt/smaug/benchmarks/
RUN cmake -S passes -B passes/build \
        -DLLVM_18_PREFIX=/usr/lib/llvm-${LLVM_VERSION} \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    && cmake --build passes/build -j"${BUILD_JOBS:-$(nproc)}"
# Manifest core (LLVM-free) - builds the conformance test binaries the service
# exposes at /conformance. Cheap; no EMP, no LLVM linkage.
RUN cmake -S passes/manifest -B passes/manifest/build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    && cmake --build passes/manifest/build -j"${BUILD_JOBS:-$(nproc)}" \
    && ctest --test-dir passes/manifest/build --output-on-failure
# Standalone AP3 manifest-emit plugin (libap3pass.so) - light LLVM-18 module that
# powers `opt -passes=ap3-reveal-view`; used by the emit service and CI.
RUN cmake -S passes/ap3 -B passes/ap3/build \
        -DLLVM_18_PREFIX=/usr/lib/llvm-${LLVM_VERSION} \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    && cmake --build passes/ap3/build -j"${BUILD_JOBS:-$(nproc)}"
RUN /opt/smaug/scripts/gen_toolchain_lock.sh > /opt/smaug/toolchain.lock \
    && cat /opt/smaug/toolchain.lock

# =============================================================================
# service - TARGET: manifest-emit HTTP service on the pass-dev image (Coolify).
# Long-lived, health-checked; exposes the toolchain over HTTP. No EMP: manifest
# emission needs only LLVM + the plugin. See docs/coolify.md.
# =============================================================================
FROM llvm-dev AS service
COPY service/ /opt/smaug/service/
ENV PORT=8080 SMAUG_HOME=/opt/smaug
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=5 \
    CMD curl -fsS "http://localhost:${PORT}/health" || exit 1
CMD ["python3", "/opt/smaug/service/app.py"]

# =============================================================================
# emp - EMP toolkit (emp-tool, emp-ot, emp-sh2pc, emp-aby), pinned + installed
# =============================================================================
FROM llvm AS emp
ARG EMP_TOOL_REF
ARG EMP_OT_REF
ARG EMP_SH2PC_REF
ARG EMP_ABY_REPO
ARG EMP_ABY_BRANCH
WORKDIR /opt/emp
# Core EMP libs via the upstream installer (clones each repo at the given tag).
RUN wget -q https://raw.githubusercontent.com/emp-toolkit/emp-readme/master/scripts/install.py \
    && python3 install.py --deps --tool ${EMP_TOOL_REF} --ot ${EMP_OT_REF} --sh2pc ${EMP_SH2PC_REF}
# emp-aby (Yao + GMW). Depth-1 clone of the pinned branch.
RUN git clone --depth 1 -b ${EMP_ABY_BRANCH} ${EMP_ABY_REPO} emp-aby \
    && cmake -S emp-aby -B emp-aby/build \
    && cmake --build emp-aby/build -j"$(nproc)" \
    && cmake --install emp-aby/build
WORKDIR /opt/smaug

# =============================================================================
# full - TARGET (default): EMP + built runtime + built pass plugin. E2E + gate.
# =============================================================================
FROM emp AS full
ARG LLVM_VERSION
ARG BUILD_JOBS
COPY runtime/ /opt/smaug/runtime/
COPY passes/ /opt/smaug/passes/
COPY scripts/ /opt/smaug/scripts/
# Runtime (libmpc, libgc, libgc-opt) - links against EMP.
RUN cmake -S runtime -B runtime/build \
    && cmake --build runtime/build -j"${BUILD_JOBS:-$(nproc)}" \
    && cmake --install runtime/build
# Pass plugin.
RUN cmake -S passes -B passes/build \
        -DLLVM_18_PREFIX=/usr/lib/llvm-${LLVM_VERSION} \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    && cmake --build passes/build -j"${BUILD_JOBS:-$(nproc)}"
RUN /opt/smaug/scripts/gen_toolchain_lock.sh > /opt/smaug/toolchain.lock \
    && cat /opt/smaug/toolchain.lock
