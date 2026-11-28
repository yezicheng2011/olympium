DOTNET ?= $(shell if [ -x "$(HOME)/.dotnet/dotnet" ]; then echo "$(HOME)/.dotnet/dotnet"; else command -v dotnet; fi)
CONFIG ?= Release
OUT_DIR := out

.PHONY: all build test clean

all: build

build:
	$(DOTNET) build Olympium/Olympium.vbproj -c $(CONFIG) -o $(OUT_DIR) --nologo

clean:
	rm -rf $(OUT_DIR) Olympium/bin Olympium/obj
