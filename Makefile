# Harness Studio: build Blender with the Qt shell into ./build-blender.
BUILD := build-blender
JOBS ?= 10

.PHONY: all configure build build-launcher install run clean

all: build install build-launcher

configure:
	cmake -S blender -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=Release \
	  -DWITH_GHOST_QT=ON -DWITH_VULKAN_BACKEND=OFF -DWITH_LIBS_PRECOMPILED=OFF \
	  -DWITH_STRICT_BUILD_OPTIONS=OFF -DPYTHON_VERSION=3.14 \
	  -DEigen3_DIR=$(CURDIR)/deps/eigen/share/eigen3/cmake \
	  -DWITH_CYCLES_DEVICE_CUDA=OFF -DWITH_CYCLES_DEVICE_OPTIX=OFF -DWITH_CYCLES_DEVICE_HIP=OFF \
	  -DWITH_CYCLES_DEVICE_HIPRT=OFF -DWITH_CYCLES_DEVICE_ONEAPI=OFF -DWITH_CYCLES_CUDA_BINARIES=OFF \
	  -DWITH_CYCLES_HIP_BINARIES=OFF -DWITH_INSTALL_PORTABLE=ON -DCMAKE_INSTALL_PREFIX=$(CURDIR)/$(BUILD)/bin -DWITH_PYTHON_INSTALL=OFF \
	  -DWITH_DRACO=OFF -DWITH_TESTS=OFF -DWITH_OPENTIMELINEIO=OFF -DWITH_SYSTEM_GLOG=ON -DWITH_SYSTEM_GFLAGS=ON

build:
	@test -f $(BUILD)/build.ninja || $(MAKE) configure
	ninja -C $(BUILD) -j$(JOBS)

build-launcher:
	cd app && GOCACHE=/tmp/craftblender-gocache GOFLAGS=-buildvcs=false go build -buildvcs=false -o app .
	cp app/app harnessstudio
	cp app/app app/harnessstudio
	chmod +x run.sh app/run.sh app/main.py

install:
	ninja -C $(BUILD) install > /dev/null

run:
	go run ./app

clean:
	ninja -C $(BUILD) clean
