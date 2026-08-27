# ComfyPromptExtractor (CPE)

**ComfyPromptExtractor (CPE)** is an ultra-fast, zero-dependency C application for **Linux**, **Windows**, and **macOS** engineered to extract clean generation prompt text and workflow metadata from ComfyUI-generated PNG images with zero startup overhead.

![ComfyPromptExtractor GUI](docs/cpe_gui.png)

---

## Key Features

- 🎯 **Multi-Framework Prompt Extraction**: Automatically parses **ComfyUI**, **InvokeAI** (`sd-metadata`, `invokeai_metadata`, `invokeai_graph`, `Dream`), **SDXL**, **Flux**, **Krea2**, and **AUTOMATIC1111/WebUI** metadata to deliver **clean, raw human-readable prompt text** and workflow JSON structure.
- ⚡ **Instant Execution (< 1ms)**: Fast, sequential C stream parser reads only PNG chunk headers (`tEXt` and `iTXt`) and skips multi-megabyte `IDAT` pixel data with `fseek()`.
- 🔄 **Smart Context Detection**:
  - **Terminal / CLI Mode (TTY detected)**: Emits clean prompt text directly to `stdout` and exits immediately (`0` on success, `1` on error). Zero GUI initialization overhead.
  - **Desktop / GUI Mode (No TTY / File Manager)**: Launches a sleek, compact, borderless dark UI centered on screen.
- 📋 **Seamless Clipboard & Dismissal**:
  - **Copy Prompt**: Click **"Copy Prompt"**, press <kbd>Enter</kbd>, or press <kbd>Ctrl</kbd> + <kbd>C</kbd>.
  - **Copy Workflow**: Click **"Copy Workflow"** or press <kbd>Ctrl</kbd> + <kbd>W</kbd> to copy the full JSON workflow structure to the clipboard.
  - **Dismiss**: Press <kbd>Esc</kbd>, click **"Close"**, or simply click outside the window (instant close on focus loss).
- 📦 **Zero External Runtime Dependencies**: Single source file (`cpe.c`), tiny stripped binary (~960 KB), embedded lightweight JSON graph parser, and minimal Raylib GUI.
- 🌐 **Cross-Platform Support**: Native pre-built releases for **Linux** (x86_64), **Windows** (x86_64), and **macOS** (ARM64 / Apple Silicon).
- 🗂️ **Desktop Integration**: Includes FreeDesktop `cpe.desktop` specification for "Right Click -> Open With" in Linux file managers (Nautilus, Dolphin, Thunar, Nemo, etc.) with quick actions for Negative Prompt and Workflow JSON.

---

## Installation & Building

### Pre-built Releases
Download ready-to-run binaries from GitHub Releases:
- **Linux (x86_64)**: `cpe-linux-amd64.tar.gz` (includes `cpe`, `cpe.desktop`, icon)
- **Windows (x86_64)**: `cpe-windows-amd64.zip` (includes `cpe.exe`)
- **macOS (Apple Silicon arm64)**: `cpe-macos-arm64.tar.gz` (includes `cpe`)

### Prerequisites for Building from Source
- C99-compliant C compiler (`gcc`, `clang`, or MinGW)
- `make`

**Dependencies by Platform:**
- **Linux**: `libraylib-dev`, `libx11-dev`, `libgl1-mesa-dev` (or built automatically via `vendor/raylib`)
- **macOS**: Raylib via Homebrew (`brew install raylib`)
- **Windows**: GCC and Raylib via MSYS2 (`pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-raylib`)

### Compile
```bash
# Clone repository
git clone https://github.com/JRufer/ComfyPromptExtractor.git
cd ComfyPromptExtractor

# Compile optimized binary for your OS
make

# On Windows (MSYS2):
# make EXE=.exe
```

### Install System-Wide (Linux)
```bash
# Install to /usr/local/bin and install desktop handler
sudo make install

# Or install to user directory (~/.local)
make install PREFIX=~/.local
```

---

## Usage

### CLI Mode (Terminal)
When invoked from a terminal or shell pipeline, CPE automatically operates in CLI mode:

```bash
# Extract clean positive prompt text (default)
cpe image.png

# Extract negative prompt text
cpe -n image.png

# Extract raw prompt JSON graph
cpe -r image.png | jq .

# Extract raw ComfyUI workflow JSON
cpe -w image.png | jq .

# Force CLI mode in scripts
cpe --cli image.png > prompt.txt
```

### GUI Mode (Desktop / File Manager)
- **Desktop**: Right-click any ComfyUI PNG image in your file manager and select **"Open With -> ComfyPromptExtractor"**.
- **Terminal (Forced GUI)**:
  ```bash
  cpe --gui image.png
  ```

### Keyboard Shortcuts in GUI

| Shortcut | Action |
| :--- | :--- |
| <kbd>Enter</kbd> or <kbd>KP_Enter</kbd> | Copy prompt text to clipboard and exit |
| <kbd>Ctrl</kbd> + <kbd>C</kbd> | Copy prompt text to clipboard and exit |
| <kbd>Ctrl</kbd> + <kbd>W</kbd> | Copy entire workflow JSON structure to clipboard and exit |
| <kbd>Esc</kbd> | Close window without copying |
| <kbd>Click Away</kbd> | Close window immediately on focus loss |
| <kbd>Mouse Wheel</kbd> / <kbd>↑</kbd> <kbd>↓</kbd> | Scroll wrapped text |
| <kbd>Page Up</kbd> / <kbd>Page Down</kbd> | Scroll by page |

---

## Options Reference

```text
ComfyPromptExtractor (CPE) v1.1.0
Usage: cpe [options] <image.png>

Options:
  -p, --prompt       Extract clean prompt text (default)
  -n, --negative     Extract negative prompt text
  -r, --raw          Extract raw prompt JSON metadata graph
  -w, --workflow     Extract raw workflow JSON metadata
      --cli          Force CLI mode (print text directly to stdout)
      --gui          Force GUI mode (open compact UI window)
  -h, --help         Show this help message
  -v, --version      Show version information
```

---

## Running Tests & Benchmarks

```bash
# Run automated test suite
make test
```

### Benchmark Comparison
```bash
$ time cpe tests/sample_user_prompt.png > /dev/null
real    0m0.001s
user    0m0.001s
sys     0m0.000s
```

---

## Continuous Integration & Automated Draft Releases

This repository includes a cross-platform GitHub Actions workflow ([.github/workflows/release.yml](file:///home/jrufer/Development/ComfyPromptExtractor/.github/workflows/release.yml)) that triggers on every commit pushed to `main`:
1. **Multi-OS Build Matrix**: Compiles binaries across **Linux** (`ubuntu-latest`), **Windows** (`windows-latest` via MSYS2 / MinGW-w64), and **macOS** (`macos-latest` Apple Silicon ARM64).
2. **Automated Testing**: Runs the complete `make test` test suite on all platforms.
3. **Release Packaging**: Generates platform archives (`cpe-linux-amd64.tar.gz`, `cpe-windows-amd64.zip`, `cpe-macos-arm64.tar.gz`) along with standalone executables.
4. **Automated Draft Release**: Publishes a new **Draft Release** on GitHub containing pre-built assets for all operating systems.

---

## License
MIT License
