CC ?= gcc
EXE ?=
CFLAGS ?= -std=c99 -O3 -s -Wall -Wextra
DEBUG_CFLAGS ?= -std=c99 -g -O0 -Wall -Wextra

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
APPDIR ?= $(PREFIX)/share/applications
ICONDIR ?= $(PREFIX)/share/icons/hicolor/256x256/apps
PIXMAPDIR ?= $(PREFIX)/share/pixmaps

# Check for system raylib or fallback to vendor directory
HAVE_SYS_RAYLIB := $(shell pkg-config --exists raylib 2>/dev/null && echo 1 || echo 0)

ifeq ($(HAVE_SYS_RAYLIB), 1)
    RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib)
    RAYLIB_LIBS := $(shell pkg-config --libs raylib)
else
    VENDOR_DIR := vendor/raylib
    RAYLIB_CFLAGS := -I$(VENDOR_DIR)/include
    RAYLIB_LIBS := $(VENDOR_DIR)/lib/libraylib.a -lm -lpthread -ldl -lrt -lX11
endif

TARGET := cpe$(EXE)
SRC := cpe.c

.PHONY: all clean debug install uninstall test vendor-raylib

all: $(TARGET)

$(TARGET): $(SRC) $(if $(filter 0,$(HAVE_SYS_RAYLIB)),vendor-raylib)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) $(SRC) $(RAYLIB_LIBS) -o $(TARGET)

debug: $(SRC) $(if $(filter 0,$(HAVE_SYS_RAYLIB)),vendor-raylib)
	$(CC) $(DEBUG_CFLAGS) $(RAYLIB_CFLAGS) $(SRC) $(RAYLIB_LIBS) -o $(TARGET)_debug

vendor-raylib:
	@if [ ! -f vendor/raylib/lib/libraylib.a ]; then \
		echo "Fetching Raylib v6.0 static library..."; \
		mkdir -p vendor/raylib && \
		cd vendor && \
		curl -sL https://github.com/raysan5/raylib/releases/download/6.0/raylib-6.0_linux_amd64.tar.gz | tar -xz && \
		cp -r raylib-6.0_linux_amd64/* raylib/ && \
		rm -rf raylib-6.0_linux_amd64; \
	fi

clean:
	rm -f cpe cpe.exe cpe_debug cpe_debug.exe $(TARGET) $(TARGET)_debug

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -d $(DESTDIR)$(APPDIR)
	install -m 644 cpe.desktop $(DESTDIR)$(APPDIR)/cpe.desktop
	install -d $(DESTDIR)$(ICONDIR)
	install -m 644 cpe.png $(DESTDIR)$(ICONDIR)/cpe.png
	install -d $(DESTDIR)$(PIXMAPDIR)
	install -m 644 cpe.png $(DESTDIR)$(PIXMAPDIR)/cpe.png
	@which update-desktop-database >/dev/null 2>&1 && update-desktop-database $(DESTDIR)$(APPDIR) || true
	@which gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t $(DESTDIR)$(PREFIX)/share/icons/hicolor 2>/dev/null || true

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(APPDIR)/cpe.desktop
	rm -f $(DESTDIR)$(ICONDIR)/cpe.png
	rm -f $(DESTDIR)$(PIXMAPDIR)/cpe.png
	@which update-desktop-database >/dev/null 2>&1 && update-desktop-database $(DESTDIR)$(APPDIR) || true
	@which gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t $(DESTDIR)$(PREFIX)/share/icons/hicolor 2>/dev/null || true

test: $(TARGET)
	@echo "--- Generating Test Images ---"
	python3 tests/generate_test_pngs.py
	python3 tests/generate_user_sample.py
	python3 tests/generate_complex_samples.py
	python3 tests/generate_invokeai_sample.py
	@echo "\n--- Testing Clean Prompt Text Extraction (Default) ---"
	@./$(TARGET) tests/sample_user_prompt.png | grep -q "surrealist art Surrealism" && echo "✓ sample_user_prompt.png: User prompt text extracted successfully"
	@./$(TARGET) -n tests/sample_user_prompt.png | grep -q "low quality, blurry" && echo "✓ sample_user_prompt.png: Negative prompt text extracted successfully"
	@./$(TARGET) tests/sample_comfy.png | grep -q "astronaut riding a horse" && echo "✓ sample_comfy.png: Clean prompt text extracted successfully"
	@./$(TARGET) tests/sample_sdxl.png | grep -q "cosmic nebula lion" && echo "✓ sample_sdxl.png: SDXL prompt text extracted successfully"
	@./$(TARGET) tests/sample_flux.png | grep -q "cyberpunk city" && echo "✓ sample_flux.png: Flux prompt text extracted successfully"
	@./$(TARGET) tests/sample_krea2.png | grep -qi "parakeet bird" && echo "✓ sample_krea2.png: Krea2 prompt text extracted successfully"
	@./$(TARGET) tests/sample_itxt.png | grep -q "astronaut" && echo "✓ sample_itxt.png: iTXt Prompt text extracted successfully"
	@./$(TARGET) tests/sample_invokeai.png | grep -q "cyberpunk artist" && echo "✓ sample_invokeai.png: InvokeAI prompt text extracted successfully"
	@./$(TARGET) tests/sample_invokeai_graph.png | grep -q "cyberpunk girl" && echo "✓ sample_invokeai_graph.png: InvokeAI graph prompt text extracted successfully"
	@./$(TARGET) tests/sample_invokeai_dream.png | grep -q "steampunk temple" && echo "✓ sample_invokeai_dream.png: InvokeAI dream prompt text extracted successfully"
	@echo "\n--- Testing Raw JSON & Workflow Options ---"
	@./$(TARGET) -r tests/sample_comfy.png | grep -q "KSampler" && echo "✓ -r / --raw: Raw prompt JSON graph extracted successfully"
	@./$(TARGET) -w tests/sample_comfy.png | grep -q "links" && echo "✓ -w / --workflow: Workflow JSON extracted successfully"
	@echo "\n--- Testing Error Cases ---"
	@if ./$(TARGET) tests/sample_no_meta.png >/dev/null 2>&1; then false; else echo "✓ sample_no_meta.png: Correctly returned exit code 1"; fi
	@if ./$(TARGET) tests/sample_corrupt.png >/dev/null 2>&1; then false; else echo "✓ sample_corrupt.png: Correctly returned exit code 1"; fi
	@if ./$(TARGET) non_existent.png >/dev/null 2>&1; then false; else echo "✓ non_existent.png: Correctly returned exit code 1"; fi
	@echo "\n✓ All tests passed!"
