CC ?= gcc
CFLAGS ?= -std=c99 -O3 -march=native -s -Wall -Wextra
DEBUG_CFLAGS ?= -std=c99 -g -O0 -Wall -Wextra

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
APPDIR ?= $(PREFIX)/share/applications

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

TARGET := cpe
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
	rm -f $(TARGET) $(TARGET)_debug

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -d $(DESTDIR)$(APPDIR)
	install -m 644 cpe.desktop $(DESTDIR)$(APPDIR)/cpe.desktop
	@which update-desktop-database >/dev/null 2>&1 && update-desktop-database $(DESTDIR)$(APPDIR) || true

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(APPDIR)/cpe.desktop
	@which update-desktop-database >/dev/null 2>&1 && update-desktop-database $(DESTDIR)$(APPDIR) || true

test: $(TARGET)
	@echo "--- Generating Test Images ---"
	python3 tests/generate_test_pngs.py
	python3 tests/generate_user_sample.py
	python3 tests/generate_complex_samples.py
	@echo "\n--- Testing Clean Prompt Text Extraction (Default) ---"
	./$(TARGET) tests/sample_user_prompt.png | grep -q "surrealist art Surrealism" && echo "✓ sample_user_prompt.png: User prompt text extracted successfully"
	./$(TARGET) -n tests/sample_user_prompt.png | grep -q "low quality, blurry" && echo "✓ sample_user_prompt.png: Negative prompt text extracted successfully"
	./$(TARGET) tests/sample_comfy.png | grep -q "astronaut riding a horse" && echo "✓ sample_comfy.png: Clean prompt text extracted successfully"
	./$(TARGET) tests/sample_sdxl.png | grep -q "cosmic nebula lion" && echo "✓ sample_sdxl.png: SDXL prompt text extracted successfully"
	./$(TARGET) tests/sample_flux.png | grep -q "cyberpunk city" && echo "✓ sample_flux.png: Flux prompt text extracted successfully"
	./$(TARGET) tests/sample_krea2.png | grep -qi "parakeet bird" && echo "✓ sample_krea2.png: Krea2 prompt text extracted successfully"
	./$(TARGET) tests/sample_itxt.png | grep -q "astronaut" && echo "✓ sample_itxt.png: iTXt Prompt text extracted successfully"
	@echo "\n--- Testing Raw JSON & Workflow Options ---"
	./$(TARGET) -r tests/sample_comfy.png | grep -q "KSampler" && echo "✓ -r / --raw: Raw prompt JSON graph extracted successfully"
	./$(TARGET) -w tests/sample_comfy.png | grep -q "links" && echo "✓ -w / --workflow: Workflow JSON extracted successfully"
	@echo "\n--- Testing Error Cases ---"
	@! ./$(TARGET) tests/sample_no_meta.png 2>/dev/null && echo "✓ sample_no_meta.png: Correctly returned exit code 1"
	@! ./$(TARGET) tests/sample_corrupt.png 2>/dev/null && echo "✓ sample_corrupt.png: Correctly returned exit code 1"
	@! ./$(TARGET) non_existent.png 2>/dev/null && echo "✓ non_existent.png: Correctly returned exit code 1"
	@echo "\n✓ All tests passed!"
