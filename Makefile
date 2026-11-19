# The genuine CrossPoint port is built separately from the old CrossPod prototype.
# Do not ship a legacy reader ZIP when this target is requested.
.PHONY: all test upstream clean
all: test

test:
	cmake -S crosspoint-core -B build/crosspoint-core -DCMAKE_BUILD_TYPE=Release
	cmake --build build/crosspoint-core --parallel 2
	ctest --test-dir build/crosspoint-core --output-on-failure

upstream:
	bash crosspoint-core/tools/fetch-upstream.sh

clean:
	rm -rf build/crosspoint-core
