# Smaug hermetic build + AP3 manifest-emit developer workflow.
#
# All targets run inside the pinned toolchain image (docker/Dockerfile) so local
# host state (macOS, wrong LLVM, missing EMP) never affects results. See
# docs/reproducibility.md.
#
#   make image        Build the fast pass-dev image (LLVM-18, no EMP).
#   make image-full   Build the full image (LLVM-18 + EMP + runtime).
#   make shell        Interactive shell in the dev image, repo bind-mounted.
#   make build-passes Build libpasses.so inside the dev image.
#   make emit-psi     Compile the psi benchmark and emit its AP3 manifest.
#   make test-emit    Run the manifest-emit conformance tests.
#   make determinism  Determinism gate: N compiles x 2 instances, diff manifests.
#   make lock         Print the resolved toolchain.lock from the image.

include docker/toolchain.env
export

IMAGE      ?= smaug-dev
IMAGE_FULL ?= smaug-full
DOCKER     ?= docker
# Repo root bind-mounted read-write so in-container builds land back on the host.
MOUNT      := -v "$(CURDIR)":/opt/smaug -w /opt/smaug
PLATFORM   ?= linux/amd64   # Smaug's determinism target is x86-64.

BUILD_ARGS := \
  --build-arg BASE_REF=$(BASE_REF) \
  --build-arg LLVM_VERSION=$(LLVM_VERSION) \
  --build-arg EMP_TOOL_REF=$(EMP_TOOL_REF) \
  --build-arg EMP_OT_REF=$(EMP_OT_REF) \
  --build-arg EMP_SH2PC_REF=$(EMP_SH2PC_REF) \
  --build-arg EMP_ABY_REPO=$(EMP_ABY_REPO) \
  --build-arg EMP_ABY_BRANCH=$(EMP_ABY_BRANCH) \
  --build-arg SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH)

.PHONY: image image-full shell build-passes emit-psi test-emit determinism lock clean

image:
	$(DOCKER) build --platform $(PLATFORM) --target llvm-dev $(BUILD_ARGS) -f docker/Dockerfile -t $(IMAGE) .

image-full:
	$(DOCKER) build --platform $(PLATFORM) --target full $(BUILD_ARGS) -f docker/Dockerfile -t $(IMAGE_FULL) .

shell: image
	$(DOCKER) run --rm -it --platform $(PLATFORM) $(MOUNT) $(IMAGE) bash

# Rebuild the plugin against the mounted (possibly edited) sources.
build-passes: image
	$(DOCKER) run --rm --platform $(PLATFORM) $(MOUNT) $(IMAGE) \
	  bash -lc 'cmake -S passes -B passes/build -DLLVM_18_PREFIX=/usr/lib/llvm-$(LLVM_VERSION) -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build passes/build -j"$$(nproc)"'

emit-psi: build-passes
	$(DOCKER) run --rm --platform $(PLATFORM) $(MOUNT) $(IMAGE) \
	  bash -lc 'scripts/emit_manifest.sh benchmarks/psi.cpp benchmarks/metadata/psi.json'

test-emit: build-passes
	$(DOCKER) run --rm --platform $(PLATFORM) $(MOUNT) $(IMAGE) \
	  bash -lc 'scripts/run_emit_tests.sh'

determinism: build-passes
	$(DOCKER) run --rm --platform $(PLATFORM) $(MOUNT) $(IMAGE) \
	  bash -lc 'scripts/determinism_check.sh'

lock: image
	$(DOCKER) run --rm --platform $(PLATFORM) $(IMAGE) cat /opt/smaug/toolchain.lock

clean:
	rm -rf passes/build runtime/build benchmarks/build benchmarks/*.ll
