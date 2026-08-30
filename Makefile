# SPDX-License-Identifier: MIT
# Convenience targets for the Shadow SE site.

.PHONY: start stop run test build clean

# One command to start the whole website (web UI + admin + JSON API).
start run:
	./start.sh

# Also bring up the stealth onion service.
start-onion:
	./start.sh onion

stop:
	./stop.sh

test:
	ctest --test-dir build --output-on-failure

build:
	cmake -S . -B build -DSHADOWSE_BUILD_TESTS=ON
	cmake --build build -j"$$(nproc)"

clean:
	rm -rf build
