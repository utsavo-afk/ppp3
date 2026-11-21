# PPP3 -- simple Makefile for one-file programs in src/chXX/*.cpp
#
#   make run  FILE=src/ch02/hello.cpp   build + run one program
#   make build FILE=src/ch02/hello.cpp  just build it
#   make                                build every program in src/
#   make new CH=03 NAME=drill           create src/ch03/drill.cpp
#   make MODE=release run FILE=...      optimized build (no sanitizers)
#   make clean                          delete build/
#
# Every program can use `import std;`, `#include "PPP.h"`, ordinary
# #includes (in any order) and dbg(...) -- include/ppp_prelude.h is
# force-included (precompiled). Debug builds come with AddressSanitizer,
# UBSan and libc++ range checks.

# ---- toolchain (LLVM) -------------------------------------------------------
LLVM_VERSION ?= 22
CXX          := clang++-$(LLVM_VERSION)
# libc++ ships the source of the `std` module; find it next to the compiler
# (<llvm>/lib/clang/NN is the resource dir, sources are in <llvm>/share).
LLVM_ROOT       := $(abspath $(shell $(CXX) -print-resource-dir)/../../..)
STD_CPPM        := $(LLVM_ROOT)/share/libc++/v1/std.cppm
STD_COMPAT_CPPM := $(LLVM_ROOT)/share/libc++/v1/std.compat.cppm

# ---- flags ------------------------------------------------------------------
MODE ?= debug
BUILD := build/$(MODE)
MOD   := $(BUILD)/modules

CXXFLAGS := -std=c++20 -stdlib=libc++ -Iinclude -Iinclude/PPP \
            -Wall -Wextra -Wpedantic -Wshadow -Wimplicit-fallthrough
ifeq ($(MODE),release)
  CXXFLAGS += -O2 -DNDEBUG -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST
  LDFLAGS  :=
else
  CXXFLAGS += -O0 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined \
              -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG
  LDFLAGS  := -fsanitize=address,undefined
endif
LDFLAGS += -stdlib=libc++ -fuse-ld=lld
MODFLAGS := -fprebuilt-module-path=$(MOD)

# ---- prelude: every std header + dbg.h, precompiled, in every program -------
PRELUDE := include/ppp_prelude.h
PCH     := $(BUILD)/ppp_prelude.pch

$(PCH): $(PRELUDE) include/dbg.h Makefile $(MOD)/std.pcm
	@mkdir -p $(@D)
	@echo "  PCH $(PRELUDE)"
	@$(CXX) $(CXXFLAGS) $(MODFLAGS) -x c++-header $< -o $@

# ---- modules: std, std.compat, PPP (built once per MODE) --------------------
MODULES := $(MOD)/std.pcm $(MOD)/std.compat.pcm $(MOD)/PPP.pcm
MODOBJS := $(MODULES:.pcm=.o)

$(MOD)/std.pcm $(MOD)/std.o &: $(STD_CPPM)
	@mkdir -p $(MOD)
	@echo "  MODULE std"
	@$(CXX) $(CXXFLAGS) -Wno-reserved-module-identifier -c $< \
	    -fmodule-output=$(MOD)/std.pcm -o $(MOD)/std.o

$(MOD)/std.compat.pcm $(MOD)/std.compat.o &: $(STD_COMPAT_CPPM) $(MOD)/std.pcm
	@echo "  MODULE std.compat"
	@$(CXX) $(CXXFLAGS) $(MODFLAGS) -Wno-reserved-module-identifier -c $< \
	    -fmodule-output=$(MOD)/std.compat.pcm -o $(MOD)/std.compat.o

$(MOD)/PPP.pcm $(MOD)/PPP.o &: include/PPP/PPP.ixx include/PPP/PPP_support.h $(MOD)/std.pcm
	@echo "  MODULE PPP"
	@$(CXX) $(CXXFLAGS) $(MODFLAGS) -x c++-module -c $< \
	    -fmodule-output=$(MOD)/PPP.pcm -o $(MOD)/PPP.o

# ---- programs: src/ch02/hello.cpp -> build/debug/src/ch02/hello -------------
SOURCES  := $(shell find src -name '*.cpp' 2>/dev/null)
PROGRAMS := $(patsubst %.cpp,$(BUILD)/%,$(SOURCES))

$(BUILD)/%: %.cpp $(PCH) $(MODULES) Makefile
	@mkdir -p $(@D)
	@echo "  CXX $<"
	@$(CXX) $(CXXFLAGS) $(MODFLAGS) -include-pch $(PCH) -MMD -MP -MF $@.d \
	    $< $(MODOBJS) $(LDFLAGS) -o $@

-include $(PROGRAMS:=.d)

# ---- commands ---------------------------------------------------------------
.PHONY: all build run modules new clean check-file
.DEFAULT_GOAL := all

all: modules $(PROGRAMS)

modules: $(MODULES) $(PCH) compile_flags.txt

check-file:
	@test -n "$(FILE)" || { echo "usage: make run FILE=src/ch02/hello.cpp"; exit 1; }
	@test -f "$(FILE)" || { echo "no such file: $(FILE)"; exit 1; }

build: check-file compile_flags.txt
	@$(MAKE) -s --no-print-directory $(BUILD)/$(basename $(FILE))

# (Ubuntu only installs a versioned llvm-symbolizer-NN; ASan needs it for
#  file:line in its reports.)
run: build
	@ASAN_SYMBOLIZER_PATH=$(LLVM_ROOT)/bin/llvm-symbolizer \
	    ./$(BUILD)/$(basename $(FILE)) $(ARGS)

new:
	@test -n "$(CH)" -a -n "$(NAME)" || { echo "usage: make new CH=03 NAME=drill"; exit 1; }
	@mkdir -p src/ch$(CH)
	@test ! -e src/ch$(CH)/$(NAME).cpp || { echo "src/ch$(CH)/$(NAME).cpp already exists"; exit 1; }
	@printf '%s\n' '// PPP3 chapter $(CH): $(NAME)' '#include "PPP.h"' '' \
	    'int main() {' '  cout << "Hello from chapter $(CH)!\n";' '}' > src/ch$(CH)/$(NAME).cpp
	@echo "created src/ch$(CH)/$(NAME).cpp"

clean:
	rm -rf build

# clangd reads compile_flags.txt, so the editor uses exactly these (debug) flags
# and the same prebuilt modules as the compiler.
compile_flags.txt: Makefile
	@printf '%s\n' -xc++ $(CXXFLAGS) \
	    -fprebuilt-module-path=build/$(MODE)/modules -include$(PRELUDE) > $@
