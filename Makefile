# SPDX-License-Identifier: MIT
# Convenience targets for the Shadow SE site.

.PHONY: start stop run test build clean install

# One command to start the whole website (web UI + admin + JSON API).
start run:
	./start.sh

# Also bring up the stealth onion service.
start-onion:
	./start.sh onion

stop:
	./stop.sh

# Put the `shadow-se` command on your PATH so you can start the site from anywhere.
install:
	./install.sh

test:
	ctest --test-dir build --output-on-failure

build:
	cmake -S . -B build -DSHADOWSE_BUILD_TESTS=ON
	cmake --build build -j"$$(nproc)"

clean:
	rm -rf build
