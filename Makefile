# Convenience wrapper around CMake. `make` builds, `make test` runs the unit tests.
BUILD ?= build
TYPE  ?= Release
LINK  ?= ON
SAN   ?= OFF

all: $(BUILD)/build.ninja
	cmake --build $(BUILD)

$(BUILD)/build.ninja:
	cmake -S . -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=$(TYPE) -DPROWAL_LINK_ROOT=$(LINK) -DPROWAL_SANITIZE=$(SAN)

test: all
	ctest --test-dir $(BUILD) --output-on-failure

debug:
	$(MAKE) BUILD=build-debug TYPE=Debug LINK=OFF

# ASan/UBSan build in build-asan/, with its tests.
sanitize:
	$(MAKE) BUILD=build-asan TYPE=Debug LINK=OFF SAN=ON test

# Release tarball (binary, man page, desktop file, docs) in build/dist/.
dist: all
	cmake --build $(BUILD) --target dist

clean:
	cmake --build $(BUILD) --target clean

.PHONY: all test debug sanitize dist clean
