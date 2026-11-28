CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter
CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter
CRYPTO_LIBS := -lcrypto -lz
GTK_FLAGS := $(shell pkg-config --cflags --libs gtk+-3.0 2>/dev/null)
OH_WINDOW := $(shell pkg-config --exists gtk+-3.0 2>/dev/null && echo openharmony-window)

SYSTEM_BINS := \
	olympium-activate \
	olympium-launch \
	olympium-64demo \
	olympium-bash64 \
	olympium-hello32 \
	olympium-hello64 \
	Xwayland-software \
	driver-report \
	driver-install \
	firefox-launch \
	olympium-mac-run \
	olympium-run64 \
	install-to-disk \
	run-session \
	guest-fetch

RUNTIME_BINS := \
	olympium-openharmony \
	olympium-android \
	$(OH_WINDOW)

TEST_BINS := \
	test-driver-report \
	test-android-core \
	test-openharmony

.PHONY: all system runtime test clean

all: system runtime

system: $(SYSTEM_BINS)

runtime: $(RUNTIME_BINS)

olympium-activate: src/olympium-activate.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

olympium-launch: src/olympium-launch.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

olympium-64demo: src/olympium-64demo.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

olympium-bash64: src/olympium-bash64.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

olympium-hello32: src/olympium-hello32.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

olympium-hello64: src/olympium-hello64.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

Xwayland-software: src/xwayland-software.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

driver-report: src/driver-report.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

driver-install: src/driver-install.cpp src/common.hpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ src/driver-install.cpp $(CRYPTO_LIBS)

firefox-launch: src/firefox-launch.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

olympium-mac-run: src/olympium-mac-run.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

olympium-run64: src/olympium-run64.cpp src/common.hpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ src/olympium-run64.cpp $(CRYPTO_LIBS)

install-to-disk: src/install-to-disk.cpp src/common.hpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ src/install-to-disk.cpp $(CRYPTO_LIBS)

run-session: src/run-session.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

guest-fetch: src/guest-fetch.cpp src/common.hpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ src/guest-fetch.cpp $(CRYPTO_LIBS)

olympium-openharmony: src/olympium-openharmony.cpp src/common.hpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ src/olympium-openharmony.cpp $(CRYPTO_LIBS)

openharmony-window: src/openharmony-window.c
	$(CC) $(CFLAGS) -o $@ $< $(GTK_FLAGS)

olympium-android: src/olympium-android.cpp src/android_core.hpp src/common.hpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ src/olympium-android.cpp $(CRYPTO_LIBS)

test-driver-report: src/test_driver_report.cpp driver-report
	$(CXX) $(CXXFLAGS) -Isrc -o $@ src/test_driver_report.cpp $(CRYPTO_LIBS)

test-android-core: src/test_android_core.cpp src/android_core.hpp src/common.hpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ src/test_android_core.cpp $(CRYPTO_LIBS)

test-openharmony: src/test_openharmony.cpp src/common.hpp olympium-openharmony
	$(CXX) $(CXXFLAGS) -Isrc -o $@ src/test_openharmony.cpp $(CRYPTO_LIBS)

test: $(TEST_BINS)
	./test-driver-report
	./test-android-core
	./test-openharmony

clean:
	rm -f $(SYSTEM_BINS) olympium-openharmony olympium-android openharmony-window $(TEST_BINS)
