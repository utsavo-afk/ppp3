# PPP3 workspace

Exercises for *Programming: Principles and Practice Using C++* (3rd ed.), using
C++20, LLVM 22 (clang, libc++, lld, lldb, clangd) and a plain Makefile.

```
src/ch02/hello.cpp        <- your programs, one folder per chapter
src/examples/             <- dbg() tour, PPP.h usage, #include style
include/PPP/              <- Stroustrup's PPP support files (PPP.h, PPP.ixx, ...)
include/dbg.h             <- dbg(...) pretty-printer
include/ppp_prelude.h     <- force-included into every program (see below)
build/debug/src/ch02/hello  <- the built program
```

## Everyday use

| Want to…               | VS Code                          | Terminal                               |
|------------------------|----------------------------------|----------------------------------------|
| build current file     | `F7` or `Ctrl+Shift+B`           | `make build FILE=src/ch02/hello.cpp`   |
| run current file       | `F6`                             | `make run FILE=src/ch02/hello.cpp`     |
| debug current file     | `F5` (breakpoints, stepping)     |                                        |
| new exercise           |                                  | `make new CH=03 NAME=drill`            |
| optimized build + run  | `Shift+F6`                       | `make MODE=release run FILE=...`       |
| pass program arguments |                                  | `make run FILE=... ARGS="a b"`         |

Debug builds come with AddressSanitizer, UBSan and libc++ range checks, so
`v[10]` on a 3-element vector stops right there with a clear message. Input
(`cin`) works in the integrated terminal, both for running and for debugging.

### Keyboard shortcuts

Build / run / debug the file that's open in the editor:

| Key            | What it does                                                    |
|----------------|-----------------------------------------------------------------|
| `F5`           | build, then debug with LLDB (stops at breakpoints)              |
| `F6`           | build + run (debug build: sanitizers + range checks)            |
| `Shift+F6`     | build + run optimized (release)                                 |
| `F7`           | build only                                                      |
| `Ctrl+Shift+B` | build only (VS Code's standard build key)                       |

While debugging (VS Code defaults):

| Key             | What it does                         |
|-----------------|--------------------------------------|
| `F9`            | toggle breakpoint on current line    |
| `F10`           | step over (next line)                |
| `F11`           | step into a function                 |
| `Shift+F11`     | step out of the function             |
| `F5`            | continue to the next breakpoint      |
| `Shift+F5`      | stop                                 |
| `Ctrl+Shift+F5` | restart                              |

Notes:

* `F5`/`F6`/`Shift+F6`/`F7` live in your **user** keybindings
  (`~/.config/Code/User/keybindings.json`), because VS Code has no
  per-project keybindings file. They only apply when the open file is inside
  `~/Workspace/ppp3`, so other projects keep their normal F-keys.
* Click into the editor first: these keys don't fire while the terminal has
  focus.
* They run the tasks in `.vscode/tasks.json` (`ppp: run active file`, ...),
  which you can also start from *Terminal → Run Task*.

## Writing programs

Any of these work, in any order and in any mix:

```cpp
import std;            // the book's style
#include "PPP.h"       // book support: range-checked vector/string, error(), narrow<>() ...
#include <vector>      // classic headers
import PPP;            // the PPP module directly
```

Note: `PPP.h` does `#define vector Checked_vector` (Stroustrup's own "disgusting
macro hack"), so after it you write `vector<int>` instead of `std::vector<int>`.

## dbg()

It's always available, with no include needed. It prints to stderr, with the
expression, its value and its type:

```cpp
dbg(x, v, matrix, head, p);    // several at once
int y = dbg(f(x)) + 1;         // returns its argument
```

```
[src/examples/dbg_demo.cpp:43 (main)] name = "Bjarne"  std::string
[src/examples/dbg_demo.cpp:52 (main)] matrix = std::vector<std::vector<int>> [3x3]
    [[  1,  2,  3],
     [ 40, 50, 60],
     [700,  8,  9]]
[src/examples/dbg_demo.cpp:63 (main)] p = Point{.x = 1.5, .y = -2}  Point
[src/examples/dbg_demo.cpp:68 (main)] head = 0x7b.. -> Node{.value = 1, .next = 0x7b.. -> Node{.value = 2, .next = nullptr}}  Node*
[src/examples/dbg_demo.cpp:86 (main)] Date{2024, 3, 14} = Date{.y_ = 2024, .m_ = 3, .d_ = 14}  Date
```

It handles numbers, chars, strings, enums (by name), pointers (it follows them,
and flags dangling ones instead of crashing), smart pointers, optional, variant,
pair/tuple, every container, stack/queue/priority_queue, 2-D arrays/vectors as
a matrix, structs (field names included), classes with private members, and
anything with an `operator<<`. Run `make run FILE=src/examples/dbg_demo.cpp`
for the full tour.

## Editor

* **clangd 22** gives completion, hover docs, go-to-definition, inlay hints,
  clang-tidy hints and semantic highlighting. It must be the same LLVM version
  as the compiler, because it loads the prebuilt `std`/`PPP` modules.
  `make modules` (it runs automatically when the folder opens) builds them and
  writes `compile_flags.txt` for clangd.
* **Format on save**: Google C++ style (`.clang-format`). Stroustrup's files in
  `include/PPP/` are excluded.
* **Semantic colors**: tuned for the built-in *Monokai* theme in
  `.vscode/settings.json`. Parameters are orange italic, types blue italic,
  std functions blue, members cream, constants and enumerators purple, macros
  green italic, and anything passed by mutable reference is underlined.
* **cppreference in hovers**: hovering any standard-library name (`vector`,
  `push_back`, `sort`, `cout`, ...) shows clangd's hover plus a cppreference
  section: the exact page link and its opening summary (fetched once, cached
  on disk; link-only when offline). Toggle with the `cppModules.cppreference.*`
  settings.
* **Module syntax highlighting + hovers**: VS Code's C++ grammar doesn't color
  `import std;` / `export module X;`, and clangd has no hover for module names.
  `tools/cpp-modules-syntax/` is a small local extension that adds both, plus
  the cppreference hovers (reinstall:
  `code --install-extension tools/cpp-modules-syntax/cpp-modules-syntax-0.1.0.vsix`).
* **clang-tidy** (`.clang-tidy`): bug-prone patterns, modernize hints, and
  Google naming (`ClassName`, `FunctionName`, `local_var`, `kConstant`,
  `member_`). The book itself uses Stroustrup naming (`Token_stream`,
  `get_token`), so drop `readability-identifier-naming` if those warnings get
  in the way.

## Why `ppp_prelude.h`?

Two LLVM 22 limitations shaped the setup:

1. clang can't parse a standard `#include` that comes *after* `import std;`.
2. clangd crashes when an `import` is hidden inside a header (the published
   `PPP.h` is just `import PPP;`).

So the Makefile force-includes `include/ppp_prelude.h` (every std header plus
`dbg.h`, precompiled once). After that, later `#include`s are no-ops and
`import std;` merges cleanly. `include/PPP/PPP.h` pulls in the same code as
`PPPheaders.h` instead of importing it. The other local edits to Stroustrup's
files are marked `[ppp3]`: fixes to `Checked_span` (its range check was
inverted and `operator[]` recursed forever), a few `inline`s, and
`using std::size_t`.

Known limitation: `import std.compat;` builds fine, but clangd shows a false
error on it.
