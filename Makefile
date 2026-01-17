
ifeq ($(OS),Windows_NT)
    PLATFORM := windows
else
    UNAME := $(shell uname -s)
    ifeq ($(UNAME),Linux)
        PLATFORM := linux
    else
        $(error Unsupported platform: $(UNAME))
    endif
endif

ifeq ($(PLATFORM),windows)
    TARGET := live-dither-bg.exe
else
    TARGET := live-dither-bg
endif

CXX := g++

CXXFLAGS := -O2 -std=c++17 -Wall

ifeq ($(PLATFORM),windows)
    CXXFLAGS += -DPLATFORM_WINDOWS=1
    LDFLAGS := -lopengl32 -lwinmm -lgdi32
else ifeq ($(PLATFORM),linux)
    CXXFLAGS += -DPLATFORM_X11=1
    LDFLAGS := -lX11 -lXrandr -lXext -lm
endif

SRCS := main.cpp

all: $(TARGET)

$(TARGET): $(SRCS) stb_image.h
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)
debug: CXXFLAGS += -g -DDEBUG
debug: $(TARGET)

clean:
ifeq ($(PLATFORM),windows)
	del /Q $(TARGET) 2>nul || exit 0
else
	rm -f $(TARGET)
endif

install: $(TARGET)
ifeq ($(PLATFORM),linux)
	install -m 755 $(TARGET) /usr/local/bin/
else
	@echo "Install not supported on Windows"
endif

run: $(TARGET)
	./$(TARGET)

.PHONY: all debug clean install run
