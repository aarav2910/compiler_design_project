# Incremental and Parallel Compilation Engine

A C++17 build-engine prototype that detects changed files, tracks their dependencies, and compiles independent source files in parallel. Developed as the **BCSE307L Review 2 implementation prototype**.

The engine coordinates `g++` compilation and linking for a small demo application. It demonstrates how build systems reuse previously compiled object files to avoid rebuilding an entire project after every edit.

## Features

- **Incremental builds:** compare file-content hashes with a saved manifest to identify changes.
- **Dependency tracking:** parse local quoted includes and build a dependency graph.
- **Change propagation:** use breadth-first traversal to identify files affected by a changed header, including indirect dependents.
- **Parallel compilation:** organize affected source files into dependency-based batches and launch tasks with `std::async`.
- **Object-file reuse:** keep compiled objects in `.cache/` and link them with newly compiled objects.
- **Cache-hit reporting:** skip compilation and linking when no source files need rebuilding.

## Project Structure

```text
compiler_engine/
├── src/
│   └── engine.cpp       # Change detection, dependency graph, scheduling, and linking
├── demo/
│   ├── main.cpp         # Demo application entry point
│   ├── math_mod.h       # Arithmetic function declarations
│   ├── math_mod.cpp     # Addition and multiplication implementations
│   ├── utils.h         # Banner function declaration
│   └── utils.cpp       # Banner output implementation
└── README.md
```

Generated files include `.cache/manifest.txt`, `.cache/*.o`, `run_engine.exe`, and `app.exe`. Exclude these build artifacts from version control.

## Requirements

- A compiler with **C++17**, filesystem, and threading support.
- A working `g++` command on your `PATH`; the engine invokes it directly.
- A terminal opened in the project root.

The commands below target Linux, macOS, or WSL. The project uses `.exe` filenames even on Unix-like systems; the compiler still produces binaries for the system on which it runs. Native Windows support needs validation because dependency paths are constructed with `/` separators.

## Build and Run

### 1. Compile the engine

```bash
g++ -std=c++17 -pthread src/engine.cpp -o run_engine.exe
```

### 2. Build the demo application

```bash
./run_engine.exe
```

On the first build, the engine compiles all three demo source files, links `app.exe`, and saves file hashes in `.cache/manifest.txt`.

Run the engine from the project root: it resolves `demo/` and `.cache/` relative to the current working directory.

### 3. Run the demo

```bash
./app.exe
```

Expected application output:

```text
[SYSTEM] Incremental Engine Demo Active
3 + 6 = 9
3 * 5 = 15
```

**Output filename:** the engine currently mentions `app.out` in its logs, but the linker actually creates `app.exe`. Use the command above.

## Try Incremental Builds

After the initial build, make one change at a time and rerun `./run_engine.exe`:

| Change | Expected behavior |
| --- | --- |
| No files changed | Reports a cache hit; skips compilation and linking |
| Edit `demo/math_mod.cpp` | Recompiles `math_mod.cpp`, then relinks |
| Edit `demo/math_mod.h` | Recompiles `math_mod.cpp` and `main.cpp`, then relinks |
| Edit `demo/utils.h` | Recompiles `utils.cpp` and `main.cpp`, then relinks |
| Edit `demo/main.cpp` | Recompiles `main.cpp`, then relinks |

Even a comment change alters the content hash and triggers the corresponding rebuild. Compilation log order can vary because tasks run concurrently.

To force a full rebuild, delete the generated `.cache/` directory and run the engine again.

## How It Works

1. **Load build history:** read previous file hashes from `.cache/manifest.txt`.
2. **Scan files:** inspect `.cpp` and `.h` files directly inside `demo/` and parse local quoted includes.
3. **Detect changes:** hash tracked files and compare the results with the saved manifest.
4. **Find affected modules:** follow dependency-to-dependent edges to propagate changes, then select affected C++ source files.
5. **Schedule compilation:** form batches using an in-degree-based topological traversal and compile each batch concurrently.
6. **Link and save:** link the demo object files into `app.exe`; if linking succeeds, write the updated manifest.

In the included demo, headers determine which source files need rebuilding. The source files can compile independently and therefore share a parallel batch.

## Current Limitations

This is an educational prototype with a fixed demo layout:

- Input paths, output paths, and compiler commands are hardcoded; there are no command-line configuration options.
- Directory scanning is not recursive. Include parsing uses simple text matching rather than a full C++ preprocessor, so system includes and conditional compilation are not modeled.
- Cache checks use file contents only. Changes to compiler flags or compiler versions, deleted source files, and missing output files are not fully tracked.
- Individual compilation exit codes are not checked. A failed compilation can leave an older object available for linking, and build failures are not reliably reflected in the engine's exit status.
- Parallel task count is not capped, and dependency cycles are not explicitly reported.
- Shell command paths are not quoted, so filenames containing spaces are not supported reliably.

Potential extensions include compiler-generated dependency files, configurable paths and flags, stronger error handling, output validation, and a bounded worker pool.
