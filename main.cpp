/**
 * Live Dither Wallpaper - Cross-Platform Animated Wallpaper
 * 
 * Supports: Windows (Progman/WorkerW) and Linux X11 (root window pixmap)
 * 
 * Build on Windows: cl /O2 main.cpp /link OpenGL32.lib winmm.lib
 * Build on Linux:   g++ -O2 main.cpp -o live-dither-wp -lX11 -lXrandr -lXext
 * 
 * CLI: ./live-dither-wp [image] [algorithm] [threshold] [pixel_size] [max_fps] [profile] [chaos]
 *   image: path to background image (default: bg.jpg)
 *   algorithm: 0=static, 1=random, 2=wave
 *   threshold: 0-255 brightness threshold
 *   pixel_size: block size (default 1)
 *   max_fps: FPS limit (0 = unlimited, default 60)
 *   profile: 0=off, 1=on (print timing info)
 *   chaos: 0-100 randomness blend for wave
 */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <iostream>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <string>
#include <cstring>

/*
 * Platform Detection
 */
#if defined(_WIN32) || defined(_WIN64)
    #define PLATFORM_WINDOWS 1
    #define PLATFORM_X11 0
#elif defined(__linux__)
    #define PLATFORM_WINDOWS 0
    #define PLATFORM_X11 1
#else
    #error "Unsupported platform"
#endif

/*
 * Platform Headers
 */
#if PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <mmsystem.h>
    #include <gl/GL.h>
    // Only needed for MSVC
    #ifdef _MSC_VER
        #pragma comment(lib, "OpenGL32.lib")
        #pragma comment(lib, "winmm.lib")
    #endif
#endif

#if PLATFORM_X11
    #include <X11/Xlib.h>
    #include <X11/Xatom.h>
    #include <X11/keysym.h>
    #include <X11/extensions/Xrandr.h>
    #include <X11/extensions/shape.h>
    #include <sys/time.h>
    #include <unistd.h>
    #include <signal.h>
#endif

/*
 * Configuration
 */
const char* g_imagePath = "bg.jpg";  // Image file path
int g_algorithm = 2;      // 0=static, 1=random, 2=wave
int g_threshold = 40;     // brightness threshold
int g_pixelSize = 1;      // pixel block size
int g_maxFps = 60;        // Max FPS (0 = unlimited)
int g_profile = 1;        // Profiling output
int g_chaos = 10;         // Chaos/randomness blend (0-100)
float g_time = 0.0f;      // Animation time for wave algorithm
bool g_running = true;    // Main loop control


const float BLACK[3] = {0.0f, 0.0f, 0.0f};
const float ORANGE[3] = {1.0f, 0.549f, 0.0f};  // 255, 140, 0


int g_imgWidth = 0;
int g_imgHeight = 0;
int g_scaledWidth = 0;
int g_scaledHeight = 0;


enum PixelState : uint8_t {
    PIXEL_BLACK = 0,
    PIXEL_ORANGE = 1,
    PIXEL_AMBIGUOUS = 2
};


std::vector<PixelState> g_pixelStates;
std::vector<float> g_orangeProb;
std::vector<int> g_ambiguousIndices;
std::vector<uint8_t> g_scaledPixels;  // RGBA output buffer


const uint8_t BLACK_RGBA[4] = {0, 0, 0, 255};
const uint8_t ORANGE_RGBA[4] = {255, 140, 0, 255};

/*
 * Platform-Specific Globals
 */
#if PLATFORM_WINDOWS
    HWND g_hProgman = nullptr;
    HWND g_hShellView = nullptr;
    HWND g_hWorkerW = nullptr;
    HWND g_hMyWallpaper = nullptr;
    HDC g_hDC = nullptr;
    HGLRC g_hRC = nullptr;
    GLuint g_textureID = 0;
#endif

#if PLATFORM_X11
    Display* g_display = nullptr;
    Window g_root;
    Window g_window;  // Our desktop window
    GC g_gc;
    XImage* g_ximage = nullptr;
    char* g_imageData = nullptr;
    int g_screen;
#endif

/*
 * Fast Random Number Generator (Xorshift)
 */
static uint32_t g_rngState = 12345;

inline uint32_t fastRand() {
    g_rngState ^= g_rngState << 13;
    g_rngState ^= g_rngState >> 17;
    g_rngState ^= g_rngState << 5;
    return g_rngState;
}

inline float fastRandFloat() {
    return (float)(fastRand() & 0xFFFF) / 65535.0f;
}

/*
 * Image Loading and Preparation
 */
void loadAndPrepareImage(const char* filename, int screenWidth, int screenHeight) {
    int origWidth, origHeight, channels;
    unsigned char* data = stbi_load(filename, &origWidth, &origHeight, &channels, 3);
    
    if (!data) {
        std::cerr << "Failed to load image: " << filename << std::endl;
        return;
    }
    
    std::cout << "Loaded image: " << origWidth << "x" << origHeight << std::endl;
    std::cout << "Screen size: " << screenWidth << "x" << screenHeight << std::endl;
    
    g_imgWidth = screenWidth;
    g_imgHeight = screenHeight;
    g_scaledWidth = g_imgWidth / g_pixelSize;
    g_scaledHeight = g_imgHeight / g_pixelSize;
    
    std::cout << "Dither resolution: " << g_scaledWidth << "x" << g_scaledHeight << std::endl;
    
    int scaledPixels = g_scaledWidth * g_scaledHeight;
    g_pixelStates.resize(scaledPixels);
    g_orangeProb.resize(scaledPixels);
    g_ambiguousIndices.clear();
    g_ambiguousIndices.reserve(scaledPixels / 4);
    

    std::vector<float> imgFloat(scaledPixels * 3);
    

    for (int sy = 0; sy < g_scaledHeight; sy++) {
        for (int sx = 0; sx < g_scaledWidth; sx++) {
            float srcX = (float)sx / g_scaledWidth * origWidth;
            float srcY = (float)sy / g_scaledHeight * origHeight;
            
            int x0 = (int)srcX;
            int y0 = (int)srcY;
            int x1 = (x0 + 1 < origWidth) ? x0 + 1 : x0;
            int y1 = (y0 + 1 < origHeight) ? y0 + 1 : y0;
            
            float fx = srcX - x0;
            float fy = srcY - y0;
            
            int idx00 = (y0 * origWidth + x0) * 3;
            int idx01 = (y0 * origWidth + x1) * 3;
            int idx10 = (y1 * origWidth + x0) * 3;
            int idx11 = (y1 * origWidth + x1) * 3;
            
            float r = (data[idx00] * (1-fx) * (1-fy) + data[idx01] * fx * (1-fy) +
                       data[idx10] * (1-fx) * fy + data[idx11] * fx * fy) / 255.0f;
            float g = (data[idx00+1] * (1-fx) * (1-fy) + data[idx01+1] * fx * (1-fy) +
                       data[idx10+1] * (1-fx) * fy + data[idx11+1] * fx * fy) / 255.0f;
            float b = (data[idx00+2] * (1-fx) * (1-fy) + data[idx01+2] * fx * (1-fy) +
                       data[idx10+2] * (1-fx) * fy + data[idx11+2] * fx * fy) / 255.0f;
            
            // Apply brightness threshold
            if (g_threshold > 0) {
                float brightness = 0.299f * r + 0.587f * g + 0.114f * b;
                if (brightness < g_threshold / 255.0f) {
                    r = g = b = 0;
                }
            }
            
            int dstIdx = (sy * g_scaledWidth + sx) * 3;
            imgFloat[dstIdx] = r;
            imgFloat[dstIdx + 1] = g;
            imgFloat[dstIdx + 2] = b;
        }
    }
    
    stbi_image_free(data);
    

    for (int y = 0; y < g_scaledHeight; y++) {
        for (int x = 0; x < g_scaledWidth; x++) {
            int idx = (y * g_scaledWidth + x) * 3;
            int pixIdx = y * g_scaledWidth + x;
            
            float r = imgFloat[idx];
            float g = imgFloat[idx + 1];
            float b = imgFloat[idx + 2];
            
            float distBlack = sqrtf(r*r + g*g + b*b);
            float distOrange = sqrtf((r-1.0f)*(r-1.0f) + (g-0.549f)*(g-0.549f) + b*b);
            
            float totalDist = distBlack + distOrange;
            float orangeProb = (totalDist > 0.001f) ? (distBlack / totalDist) : 0.5f;
            
            const float AMBIG_LOW = 0.3f;
            const float AMBIG_HIGH = 0.7f;
            
            if (orangeProb < AMBIG_LOW) {
                g_pixelStates[pixIdx] = PIXEL_BLACK;
            } else if (orangeProb > AMBIG_HIGH) {
                g_pixelStates[pixIdx] = PIXEL_ORANGE;
            } else {
                g_pixelStates[pixIdx] = PIXEL_AMBIGUOUS;
                g_orangeProb[pixIdx] = orangeProb;
                g_ambiguousIndices.push_back(pixIdx);
            }
        }
    }
    

    g_scaledPixels.resize(g_scaledWidth * g_scaledHeight * 4);
    

    for (int y = 0; y < g_scaledHeight; y++) {
        for (int x = 0; x < g_scaledWidth; x++) {
            int pixIdx = y * g_scaledWidth + x;
            int idx = pixIdx * 4;
            if (g_pixelStates[pixIdx] == PIXEL_BLACK) {
                memcpy(&g_scaledPixels[idx], BLACK_RGBA, 4);
            } else {
                memcpy(&g_scaledPixels[idx], ORANGE_RGBA, 4);
            }
        }
    }
    
    std::cout << "Optimized: " << g_ambiguousIndices.size() << " ambiguous pixels out of " 
              << scaledPixels << " (" << (100.0f * g_ambiguousIndices.size() / scaledPixels) << "%)" << std::endl;
}

/*
 * Dithering Animation
 */
void ditherFrame() {
    if (g_algorithm == 0) return;  // Static - no animation
    
    for (int pixIdx : g_ambiguousIndices) {
        int idx = pixIdx * 4;
        bool isOrange;
        
        if (g_algorithm == 1) {
            // Random
            isOrange = fastRandFloat() < g_orangeProb[pixIdx];
        } else {
            // Wave with chaos blend
            int x = pixIdx % g_scaledWidth;
            int y = pixIdx / g_scaledWidth;
            
            static int lastY = -1;
            static float cachedSine = 0.0f;
            static float cachedChaos = 0.0f;
            static float invWidth = 0.0f;
            
            if (y != lastY) {
                float wavePhase = y * 0.8f - g_time * 2.0f;
                cachedSine = sinf(wavePhase);
                cachedChaos = g_chaos / 100.0f;
                invWidth = 2.0f / g_scaledWidth;
                lastY = y;
            }
            
            float normalizedX = x * invWidth - 1.0f;
            float waveThreshold = g_orangeProb[pixIdx] + (normalizedX - cachedSine) * 0.3f;
            
            if (cachedChaos > 0.0f) {
                float randomThreshold = g_orangeProb[pixIdx] + (fastRandFloat() - 0.5f) * 0.4f;
                waveThreshold = waveThreshold * (1.0f - cachedChaos) + randomThreshold * cachedChaos;
            }
            
            isOrange = waveThreshold > 0.5f;
        }
        
        memcpy(&g_scaledPixels[idx], isOrange ? ORANGE_RGBA : BLACK_RGBA, 4);
    }
    
    g_time += 0.016f;
}

/*
 * Windows Implementation
 */
#if PLATFORM_WINDOWS

BOOL CALLBACK FindWorkerWEnumProc(HWND hwnd, LPARAM lParam) {
    HWND* pTarget = reinterpret_cast<HWND*>(lParam);
    char className[256];
    GetClassNameA(hwnd, className, 256);
    if (strcmp(className, "WorkerW") == 0) {
        *pTarget = hwnd;
        return FALSE;
    }
    return TRUE;
}

BOOL CALLBACK FindShellViewEnumProc(HWND hwnd, LPARAM lParam) {
    HWND* pTarget = reinterpret_cast<HWND*>(lParam);
    char className[256];
    GetClassNameA(hwnd, className, 256);
    if (strcmp(className, "SHELLDLL_DefView") == 0) {
        *pTarget = hwnd;
        return FALSE;
    }
    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(g_hRC);
        ReleaseDC(hwnd, g_hDC);
        PostQuitMessage(0);
        g_running = false;
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void platformInit(int& screenWidth, int& screenHeight) {
    SetProcessDPIAware();
    
    HDC hdc = GetDC(nullptr);
    screenWidth = GetDeviceCaps(hdc, HORZRES);
    screenHeight = GetDeviceCaps(hdc, VERTRES);
    ReleaseDC(nullptr, hdc);
    
    g_hProgman = FindWindowA("Progman", nullptr);
    SendMessageTimeout(g_hProgman, 0x052C, 0, 0, SMTO_NORMAL, 100, nullptr);
    
    EnumChildWindows(g_hProgman, FindShellViewEnumProc, (LPARAM)&g_hShellView);
    EnumChildWindows(g_hProgman, FindWorkerWEnumProc, (LPARAM)&g_hWorkerW);
    
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "LiveDitherBG";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassA(&wc);
    
    DWORD exStyle = WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    
    g_hMyWallpaper = CreateWindowExA(
        exStyle, "LiveDitherBG", "LiveDitherBG",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, screenWidth, screenHeight,
        nullptr, nullptr, wc.hInstance, nullptr
    );
    
    if (g_hMyWallpaper) {
        SetParent(g_hMyWallpaper, g_hProgman);
        SetWindowLong(g_hMyWallpaper, GWL_STYLE, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
        SetLayeredWindowAttributes(g_hMyWallpaper, 0, 255, LWA_ALPHA);
        SetWindowPos(g_hMyWallpaper, g_hShellView, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (g_hWorkerW) SetWindowPos(g_hWorkerW, g_hMyWallpaper, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        std::cerr << "Failed to create window." << std::endl;
        exit(1);
    }
    
    g_hDC = GetDC(g_hMyWallpaper);
    
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    
    int format = ChoosePixelFormat(g_hDC, &pfd);
    SetPixelFormat(g_hDC, format, &pfd);
    
    g_hRC = wglCreateContext(g_hDC);
    wglMakeCurrent(g_hDC, g_hRC);
    
    glViewport(0, 0, screenWidth, screenHeight);
    
    glGenTextures(1, &g_textureID);
    glBindTexture(GL_TEXTURE_2D, g_textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glEnable(GL_TEXTURE_2D);
    
    timeBeginPeriod(1);
}

void platformRender() {
    glBindTexture(GL_TEXTURE_2D, g_textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_scaledWidth, g_scaledHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, g_scaledPixels.data());
    
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, 1.0f);
    glEnd();
    
    SwapBuffers(g_hDC);
}

void platformPollEvents() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) g_running = false;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void platformCleanup() {
    timeEndPeriod(1);
}

double platformGetTime() {
    static LARGE_INTEGER freq = {};
    static LARGE_INTEGER start = {};
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - start.QuadPart) / freq.QuadPart;
}

void platformSleep(int ms) {
    Sleep(ms);
}

#endif // PLATFORM_WINDOWS

/*
 * X11 Implementation
 */
#if PLATFORM_X11

// Locate xfdesktop window by title 'xfceliveDesktop' (requires patched xfdesktop)

static Window getXfceDesktopWindow(Display* display, Window root) {
    Window parent, *children;
    unsigned int nchildren;
    Window result = None;
    
    if (XQueryTree(display, root, &parent, &parent, &children, &nchildren)) {
        for (unsigned int i = 0; i < nchildren; i++) {
            char* name = nullptr;
            if (XFetchName(display, children[i], &name) > 0) {
                if (name && std::string(name) == "xfceliveDesktop") {
                    result = children[i];
                    XFree(name);
                    break;
                }
                XFree(name);
            }
        }
        if (children) XFree(children);
    }
    
    // Fallback to property check if name lookup fails
    if (result == None) {
        Atom atom = XInternAtom(display, "XFCE_DESKTOP_WINDOW", False);
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char* prop = nullptr;
        
        if (XGetWindowProperty(display, root, atom, 0, 1, False, XA_WINDOW,
                               &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success) {
            if (actual_type == XA_WINDOW && nitems == 1 && prop) {
                result = *((Window*)prop);
            }
            if (prop) XFree(prop);
        }
    }
    
    return result;
}

// Signal handler for graceful termination
static void signalHandler(int sig) {
    (void)sig;
    g_running = false;
}

// Xfconf helper functions for background configuration
static void setXfconfProp(std::string suffix, int val) {
    std::string v = std::to_string(val);
    std::string cmd = 
        "PREFIX=$(xfconf-query -c xfce4-desktop -l | grep last-image | head -n1 | sed 's/last-image//'); "
        "if [ ! -z \"$PREFIX\" ]; then "
        "  PROP=\"${PREFIX}" + suffix + "\"; "
        "  xfconf-query -c xfce4-desktop -p \"$PROP\" -s " + v + " 2>/dev/null || "
        "  xfconf-query -c xfce4-desktop -p \"$PROP\" -n -t int -s " + v + "; "
        "fi";
    system(cmd.c_str());
}

static void restoreXfconfSettings() {
    static bool restored = false;
    if (restored) return;
    restored = true;
    std::cout << "Restoring xfdesktop settings..." << std::endl;
    setXfconfProp("color-style", 0); // Solid
    setXfconfProp("image-style", 5); // Zoomed
    std::cout << "Restored." << std::endl;
}

void platformInit(int& screenWidth, int& screenHeight) {
    // Set up signal handlers for graceful termination
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Configure xfdesktop for transparency
    std::cout << "Configuring xfdesktop for transparency..." << std::endl;
    setXfconfProp("color-style", 3); // Transparent
    setXfconfProp("image-style", 0); // None
    
    // Register exit handler to restore settings
    atexit(restoreXfconfSettings);
    
    g_display = XOpenDisplay(nullptr);
    if (!g_display) {
        std::cerr << "Failed to open X display" << std::endl;
        exit(1);
    }
    
    g_screen = DefaultScreen(g_display);
    g_root = DefaultRootWindow(g_display);
    
    screenWidth = DisplayWidth(g_display, g_screen);
    screenHeight = DisplayHeight(g_display, g_screen);
    
    std::cout << "X11 Display opened: " << screenWidth << "x" << screenHeight << std::endl;
    
    Visual* visual = DefaultVisual(g_display, g_screen);
    int depth = DefaultDepth(g_display, g_screen);
    
    std::cout << "Using depth: " << depth << std::endl;
    
    Colormap colormap = XCreateColormap(g_display, g_root, visual, AllocNone);
    
    XSetWindowAttributes attrs;
    attrs.colormap = colormap;
    attrs.background_pixel = BlackPixel(g_display, g_screen);
    attrs.event_mask = ExposureMask | StructureNotifyMask;
    
    g_window = XCreateWindow(
        g_display, g_root,
        0, 0, screenWidth, screenHeight,
        0,                          // border width
        depth,                      // depth
        InputOutput,                // class
        visual,                     // visual
        CWColormap | CWBackPixel | CWEventMask,
        &attrs
    );
    
    if (!g_window) {
        std::cerr << "Failed to create X11 window" << std::endl;
        exit(1);
    }
    
    // Set _NET_WM_WINDOW_TYPE to DESKTOP
    Atom atom_type = XInternAtom(g_display, "_NET_WM_WINDOW_TYPE", False);
    Atom atom_desktop = XInternAtom(g_display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    XChangeProperty(g_display, g_window, atom_type, XA_ATOM, 32, PropModeReplace,
                    (unsigned char*)&atom_desktop, 1);
    

    
    XStoreName(g_display, g_window, "Live Dither Background");
    
    // Use XShape extension to make window input-transparent (click-through)
#ifdef ShapeInput
    XRectangle rect = {0, 0, 1, 1};
    Region rgn = XCreateRegion();
    XUnionRectWithRegion(&rect, rgn, rgn);
    XShapeCombineRegion(g_display, g_window, ShapeInput, 0, 0, rgn, ShapeSet);
    XDestroyRegion(rgn);
    std::cout << "XShape input transparency enabled" << std::endl;
#endif
    
    XMapWindow(g_display, g_window);
    
    Window xfdesktopWin = getXfceDesktopWindow(g_display, g_root);
    
    // Force proper desktop behavior via _NET_WM_STATE
    Atom atom_state = XInternAtom(g_display, "_NET_WM_STATE", False);
    Atom atoms[4];
    atoms[0] = XInternAtom(g_display, "_NET_WM_STATE_BELOW", False);
    atoms[1] = XInternAtom(g_display, "_NET_WM_STATE_SKIP_TASKBAR", False);
    atoms[2] = XInternAtom(g_display, "_NET_WM_STATE_SKIP_PAGER", False);
    atoms[3] = XInternAtom(g_display, "_NET_WM_STATE_STICKY", False);
    
    XChangeProperty(g_display, g_window, atom_state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char*)atoms, 4);

    // Configure window stacking order
    if (xfdesktopWin != None) {
        std::cout << "Found xfdesktop window: " << xfdesktopWin << std::endl;
        
        XLowerWindow(g_display, xfdesktopWin);
        XLowerWindow(g_display, g_window);
        
        // Activate show desktop mode to ensure proper layering
        std::cout << "Activating show desktop mode..." << std::endl;
        int ret = system("wmctrl -k on");
        if (ret != 0) std::cerr << "Warning: wmctrl call failed" << std::endl;
        
    } else {
        std::cout << "Warning: xfdesktop window not found. Icons may be hidden." << std::endl;
        XLowerWindow(g_display, g_window);
    }
    
    g_gc = XCreateGC(g_display, g_window, 0, nullptr);
    
    g_imageData = new char[screenWidth * screenHeight * 4];
    memset(g_imageData, 0, screenWidth * screenHeight * 4);
    
    g_ximage = XCreateImage(g_display, visual, depth, ZPixmap, 0,
                            g_imageData, screenWidth, screenHeight, 32, 0);
    
    if (!g_ximage) {
        std::cerr << "Failed to create XImage" << std::endl;
        exit(1);
    }
    
    XFlush(g_display);
    
    std::cout << "X11 desktop window initialized with XShape click-through" << std::endl;
}

void platformRender() {
    for (int y = 0; y < g_imgHeight; y++) {
        for (int x = 0; x < g_imgWidth; x++) {
            int srcX = x / g_pixelSize;
            int srcY = y / g_pixelSize;
            if (srcX >= g_scaledWidth) srcX = g_scaledWidth - 1;
            if (srcY >= g_scaledHeight) srcY = g_scaledHeight - 1;
            
            int srcIdx = (srcY * g_scaledWidth + srcX) * 4;
            int dstIdx = (y * g_imgWidth + x) * 4;
            
            // RGBA to BGRX conversion
            g_imageData[dstIdx + 0] = g_scaledPixels[srcIdx + 2];  // B
            g_imageData[dstIdx + 1] = g_scaledPixels[srcIdx + 1];  // G
            g_imageData[dstIdx + 2] = g_scaledPixels[srcIdx + 0];  // R
            g_imageData[dstIdx + 3] = 0;                           // X (padding)
        }
    }
    
    XPutImage(g_display, g_window, g_gc, g_ximage, 0, 0, 0, 0, g_imgWidth, g_imgHeight);
    XFlush(g_display);
}

void platformPollEvents() {
    while (XPending(g_display)) {
        XEvent event;
        XNextEvent(g_display, &event);
        if (event.type == DestroyNotify) {
            g_running = false;
        }
    }
}

void platformCleanup() {
    if (g_ximage) {
        g_ximage->data = nullptr;  // Prevent XDestroyImage from freeing our buffer
        XDestroyImage(g_ximage);
    }
    delete[] g_imageData;
    if (g_gc) { XFreeGC(g_display, g_gc); g_gc = nullptr; }
    if (g_window) XDestroyWindow(g_display, g_window);
    if (g_display) XCloseDisplay(g_display);
}

double platformGetTime() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

void platformSleep(int ms) {
    usleep(ms * 1000);
}

#endif // PLATFORM_X11

/*
 * Main Entry Point
 */
int main(int argc, char* argv[]) {
    srand((unsigned int)time(nullptr));
    
    // Handle --restore flag
    if (argc > 1 && (strcmp(argv[1], "--restore") == 0 || strcmp(argv[1], "-r") == 0)) {
#if PLATFORM_X11
        restoreXfconfSettings();
#else
        std::cout << "Restore is only needed on X11/XFCE." << std::endl;
#endif
        return 0;
    }
    
    // Parse CLI args: image algorithm threshold pixel_size max_fps profile chaos
    if (argc > 1) g_imagePath = argv[1];
    if (argc > 2) g_algorithm = atoi(argv[2]);
    if (argc > 3) g_threshold = atoi(argv[3]);
    if (argc > 4) g_pixelSize = atoi(argv[4]);
    if (argc > 5) g_maxFps = atoi(argv[5]);
    if (argc > 6) g_profile = atoi(argv[6]);
    if (argc > 7) g_chaos = atoi(argv[7]);
    
    // Validate
    if (g_algorithm < 0 || g_algorithm > 2) g_algorithm = 1;
    if (g_threshold < 0) g_threshold = 0;
    if (g_threshold > 255) g_threshold = 255;
    if (g_pixelSize < 1) g_pixelSize = 1;
    if (g_maxFps < 0) g_maxFps = 0;
    if (g_chaos < 0) g_chaos = 0;
    if (g_chaos > 100) g_chaos = 100;
    
    const char* algoNames[] = {"static", "random", "wave"};
    std::cout << "Live Dither Background" << std::endl;
#if PLATFORM_WINDOWS
    std::cout << "Platform: Windows" << std::endl;
#elif PLATFORM_X11
    std::cout << "Platform: X11" << std::endl;
#endif
    std::cout << "Image: " << g_imagePath << std::endl;
    std::cout << "Algorithm: " << algoNames[g_algorithm] << std::endl;
    std::cout << "Threshold: " << g_threshold << std::endl;
    std::cout << "Pixel Size: " << g_pixelSize << std::endl;
    std::cout << "Max FPS: " << (g_maxFps == 0 ? "unlimited" : std::to_string(g_maxFps)) << std::endl;
    std::cout << "Chaos: " << g_chaos << "%" << std::endl;
    std::cout << std::endl;
    
    int screenWidth, screenHeight;
    platformInit(screenWidth, screenHeight);
    
    loadAndPrepareImage(g_imagePath, screenWidth, screenHeight);
    
    if (g_pixelStates.empty()) {
        std::cerr << "Failed to load " << g_imagePath << std::endl;
        platformCleanup();
        return 1;
    }
    
    // Main loop
    double lastFrameTime = platformGetTime();
    double targetFrameTime = g_maxFps > 0 ? 1.0 / g_maxFps : 0.0;
    int frameCount = 0;
    double fpsTimer = 0.0;
    
    while (g_running) {
        platformPollEvents();
        
        double now = platformGetTime();
        double elapsed = now - lastFrameTime;
        
        if (g_maxFps == 0 || elapsed >= targetFrameTime) {
            ditherFrame();
            platformRender();
            lastFrameTime = now;
            frameCount++;
            
            // FPS counter
            if (g_profile) {
                fpsTimer += elapsed;
                if (fpsTimer >= 1.0) {
                    std::cout << "FPS: " << frameCount << std::endl;
                    frameCount = 0;
                    fpsTimer = 0.0;
                }
            }
        } else {
            int sleepMs = (int)((targetFrameTime - elapsed) * 1000.0);
            if (sleepMs > 0) platformSleep(sleepMs);
        }
    }
    
#if PLATFORM_X11
    restoreXfconfSettings();
#endif
    platformCleanup();
    return 0;
}
