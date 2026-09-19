// SDL3 bindings and a small debugger window for semu.
//
// SDL3 is pulled out of SDL3.dll at runtime (LoadLibraryA + GetProcAddress), so
// the Sere executable never links an SDL3 import library: the MSYS2 build only
// ships MinGW import libraries, which do not mix with the MSVC/LLVM link Sere
// performs, and the debug CRT is not installed either.
//
// The plain `extern "C"` symbols below are what libs/graphics.sere binds
// against. sere_mod_init additionally registers boxed wrappers with the Sere
// runtime, so the same entry points are reachable from native callbacks.
//
// Only the Sere module API and SDL3 headers are included: <windows.h> and the
// CRT headers it drags in make the Debug build reference MSVCRTD.lib, which the
// Sere link step does not have. For the same reason the debug-only runtime
// checks are disabled and the debug CRT default library is dropped here.

#pragma runtime_checks("", off)
#pragma comment(linker, "/NODEFAULTLIB:MSVCRTD")

#include "sere/api/sere_mod.h"

#include <SDL3/SDL.h>

// Minimal Win32 declarations for loading SDL3.dll, instead of <windows.h>.
extern "C" __declspec(dllimport) void* __stdcall LoadLibraryA(const char* file_name);
extern "C" __declspec(dllimport) void* __stdcall GetProcAddress(void* module, const char* name);

// SDL3 entry points declared in SDL_test_font.h and SDL_timer.h, which are not
// worth including here because they pull in more CRT headers.
typedef uint64_t (*SemuGetTicksFn)(void);
typedef bool (*SemuRenderDebugTextFn)(SDL_Renderer* renderer, float x, float y, const char* text);

// --- SDL3 entry points -----------------------------------------------------
// decltype(&SDL_x) keeps every signature exactly as the headers declare it.

static decltype(&SDL_Init) sdl_Init;
static decltype(&SDL_Quit) sdl_Quit;
static decltype(&SDL_CreateWindow) sdl_CreateWindow;
static decltype(&SDL_DestroyWindow) sdl_DestroyWindow;
static decltype(&SDL_CreateRenderer) sdl_CreateRenderer;
static decltype(&SDL_DestroyRenderer) sdl_DestroyRenderer;
static decltype(&SDL_SetRenderVSync) sdl_SetRenderVSync;
static decltype(&SDL_SetRenderScale) sdl_SetRenderScale;
static decltype(&SDL_SetRenderDrawColor) sdl_SetRenderDrawColor;
static decltype(&SDL_RenderClear) sdl_RenderClear;
static decltype(&SDL_RenderFillRect) sdl_RenderFillRect;
static decltype(&SDL_RenderTexture) sdl_RenderTexture;
static decltype(&SDL_RenderPresent) sdl_RenderPresent;
static SemuRenderDebugTextFn sdl_RenderDebugText;
static SemuGetTicksFn sdl_GetTicks;
static decltype(&SDL_CreateTexture) sdl_CreateTexture;
static decltype(&SDL_UpdateTexture) sdl_UpdateTexture;
static decltype(&SDL_DestroyTexture) sdl_DestroyTexture;
static decltype(&SDL_PollEvent) sdl_PollEvent;
static decltype(&SDL_ConvertEventToRenderCoordinates) sdl_ConvertEventToRenderCoordinates;
static decltype(&SDL_GetKeyboardState) sdl_GetKeyboardState;

static bool load_sdl3(void) {
  void* library = LoadLibraryA("SDL3.dll");
  if (library == nullptr) {
    // Fallback so the emulator also runs straight out of the project tree.
    library = LoadLibraryA("C:\\msys64\\mingw64\\bin\\SDL3.dll");
  }
  if (library == nullptr) {
    return false;
  }

#define SEMU_LOAD(suffix)                                                          \
  sdl_##suffix = (decltype(&SDL_##suffix))GetProcAddress(library, "SDL_" #suffix); \
  if (sdl_##suffix == nullptr) {                                                   \
    return false;                                                                  \
  }

  SEMU_LOAD(Init)
  SEMU_LOAD(Quit)
  SEMU_LOAD(CreateWindow)
  SEMU_LOAD(DestroyWindow)
  SEMU_LOAD(CreateRenderer)
  SEMU_LOAD(DestroyRenderer)
  SEMU_LOAD(SetRenderVSync)
  SEMU_LOAD(SetRenderScale)
  SEMU_LOAD(SetRenderDrawColor)
  SEMU_LOAD(RenderClear)
  SEMU_LOAD(RenderFillRect)
  SEMU_LOAD(RenderTexture)
  sdl_RenderDebugText = (SemuRenderDebugTextFn)GetProcAddress(library, "SDL_RenderDebugText");
  if (sdl_RenderDebugText == nullptr) {
    return false;
  }
  sdl_GetTicks = (SemuGetTicksFn)GetProcAddress(library, "SDL_GetTicks");
  if (sdl_GetTicks == nullptr) {
    return false;
  }
  SEMU_LOAD(RenderPresent)
  SEMU_LOAD(CreateTexture)
  SEMU_LOAD(UpdateTexture)
  SEMU_LOAD(DestroyTexture)
  SEMU_LOAD(PollEvent)
  SEMU_LOAD(GetKeyboardState)
  SEMU_LOAD(ConvertEventToRenderCoordinates)

#undef SEMU_LOAD
  return true;
}

// --- actions and states shared with graphics.sere --------------------------

enum {
  ACTION_NONE = 0,
  ACTION_QUIT = 1,
  ACTION_TOGGLE_RUN = 2,
  ACTION_STEP = 3,
  ACTION_RESET = 4,
  ACTION_FASTER = 5,
  ACTION_SLOWER = 6,
};

enum {
  STATE_READY = 0,
  STATE_RUNNING = 1,
  STATE_PAUSED = 2,
  STATE_HALTED = 3,
  STATE_ERROR = 4,
  STATE_ENDED = 5,
};

// --- window and emulated screen -------------------------------------------

static SDL_Window* window = nullptr;
static SDL_Renderer* renderer = nullptr;
static SDL_Texture* screen_texture = nullptr;
static const int32_t MAX_SCREEN_BYTES = 512 * 512;
static uint8_t screen_pixels[MAX_SCREEN_BYTES];

static int32_t screen_w = 0;
static int32_t screen_h = 0;
static int32_t screen_bytes = 0;
static float screen_zoom = 2.0f;
static bool screen_dirty = false;

// While the screen is focused the keyboard belongs to the emulated CPU and the
// debugger shortcuts stay out of the way.
static bool screen_focus = false;

// Debug display tab: 0 = CPU state, 1 = Memory viewer
static int32_t debug_tab = 0;
// Memory viewer parameters
static int64_t mem_view_start = 0x0000;
static int64_t mem_view_count = 128;

// --- layout (logical units, the renderer is scaled by UI_SCALE) ------------

static const float UI_SCALE = 2.0f;
static const float MARGIN = 6.0f;
static const float PANEL_W = 168.0f;
static const float LINE_H = 10.0f;
static const float BUTTON_H = 20.0f;
static const float BUTTON_Y = 180.0f;
static const float HINT_Y = 206.0f;

static const uint32_t COLOR_BG = 0x0E1116;
static const uint32_t COLOR_PANEL = 0x171B23;
static const uint32_t COLOR_BORDER = 0x2A3140;
static const uint32_t COLOR_TEXT = 0xD7DEE8;
static const uint32_t COLOR_DIM = 0x7B8798;
static const uint32_t COLOR_ACCENT = 0x6CC7FF;
static const uint32_t COLOR_GOOD = 0x7BD88F;
static const uint32_t COLOR_WARN = 0xFFD866;
static const uint32_t COLOR_BAD = 0xFF6B6B;

static float panel_x(void) { return MARGIN; }
static float panel_y(void) { return MARGIN; }
static float screen_x(void) { return MARGIN + PANEL_W + MARGIN; }
static float screen_y(void) { return MARGIN; }
static float panel_h(void) { return (float)screen_h * screen_zoom; }

// --- small drawing helpers -------------------------------------------------

struct Button {
  float x;
  float y;
  float w;
  float h;
  const char* label;
  int32_t action;
};

static Button buttons[3] = {
    {0.0f, BUTTON_Y, 76.0f, BUTTON_H, "RUN", ACTION_TOGGLE_RUN},
    {0.0f, BUTTON_Y, 40.0f, BUTTON_H, "STEP", ACTION_STEP},
    {0.0f, BUTTON_Y, 44.0f, BUTTON_H, "RESET", ACTION_RESET},
};

static void set_color(uint32_t rgb) {
  sdl_SetRenderDrawColor(renderer, (uint8_t)((rgb >> 16) & 0xFF), (uint8_t)((rgb >> 8) & 0xFF),
                         (uint8_t)(rgb & 0xFF), 255);
}

static void fill_rect(float x, float y, float w, float h, uint32_t rgb) {
  SDL_FRect rect = {x, y, w, h};
  set_color(rgb);
  sdl_RenderFillRect(renderer, &rect);
}

static void draw_text(const char* text, float x, float y, uint32_t rgb) {
  set_color(rgb);
  sdl_RenderDebugText(renderer, x, y, text);
}

static int32_t text_len(const char* text) {
  int32_t length = 0;
  while (text[length] != '\0') {
    length++;
  }
  return length;
}

static void write_str(char* out, int32_t& pos, const char* text) {
  for (int32_t i = 0; text[i] != '\0'; ++i) {
    out[pos++] = text[i];
  }
}

static void write_hex(char* out, int32_t& pos, uint64_t value, int32_t digits) {
  static const char* alphabet = "0123456789ABCDEF";
  if (digits <= 0) {
    digits = 1;
  }
  for (int32_t i = digits - 1; i >= 0; --i) {
    out[pos++] = alphabet[(value >> (4 * i)) & 0xF];
  }
}

static void write_dec(char* out, int32_t& pos, uint64_t value) {
  char reversed[24];
  int32_t count = 0;
  if (value == 0) {
    reversed[count++] = '0';
  }
  while (value > 0) {
    reversed[count++] = (char)('0' + (int32_t)(value % 10));
    value /= 10;
  }
  while (count > 0) {
    out[pos++] = reversed[--count];
  }
}

// One "LABEL    value" row, with the value right aligned inside the panel.
static void draw_field(float y, const char* label, uint64_t value, int32_t digits, bool hex) {
  char text[24];
  int32_t pos = 0;
  if (hex) {
    write_hex(text, pos, value, digits);
  } else {
    write_dec(text, pos, value);
  }
  text[pos] = '\0';

  draw_text(label, panel_x() + 6.0f, y, COLOR_DIM);
  draw_text(text, panel_x() + PANEL_W - 6.0f - (float)(pos * 8), y, COLOR_TEXT);
}

// Status register bits, shown as N V - B D I Z C.
static void draw_flags(float y, int64_t status) {
  static const char* letters = "NV-BDIZC";
  for (int32_t i = 0; i < 8; ++i) {
    const int32_t bit = 7 - i;
    const bool on = ((status >> bit) & 1) != 0;
    char glyph[2];
    glyph[0] = letters[i];
    glyph[1] = '\0';
    draw_text(glyph, panel_x() + 6.0f + (float)(i * 13), y, on ? COLOR_GOOD : COLOR_BORDER);
  }
}

static const char* state_text(int64_t state) {
  if (state == STATE_ERROR) return "ERROR";
  if (state == STATE_ENDED) return "ENDED";
  if (state == STATE_HALTED) return "HALTED";
  if (state == STATE_PAUSED) return "PAUSED";
  if (state == STATE_RUNNING) return "RUNNING";
  return "READY";
}

static uint32_t state_color(int64_t state) {
  if (state == STATE_ERROR) return COLOR_BAD;
  if (state == STATE_ENDED) return COLOR_WARN;
  if (state == STATE_HALTED) return COLOR_DIM;
  if (state == STATE_PAUSED) return COLOR_WARN;
  if (state == STATE_RUNNING) return COLOR_GOOD;
  return COLOR_TEXT;
}

static void draw_button(const Button& button, const char* label, bool active) {
  fill_rect(button.x, button.y, button.w, button.h, active ? COLOR_ACCENT : COLOR_PANEL);
  fill_rect(button.x, button.y, button.w, 1.0f, COLOR_BORDER);
  fill_rect(button.x, button.y + button.h - 1.0f, button.w, 1.0f, COLOR_BORDER);
  fill_rect(button.x, button.y, 1.0f, button.h, COLOR_BORDER);
  fill_rect(button.x + button.w - 1.0f, button.y, 1.0f, button.h, COLOR_BORDER);

  const float text_w = (float)(text_len(label) * 8);
  draw_text(label, button.x + (button.w - text_w) * 0.5f, button.y + 6.0f,
            active ? COLOR_BG : COLOR_TEXT);
}

static bool button_hit(const Button& button, float x, float y) {
  return x >= button.x && x < button.x + button.w && y >= button.y && y < button.y + button.h;
}

// --- frames per second -----------------------------------------------------

static int64_t fps_drawn = 0;
static int64_t fps_value = 0;
static uint64_t fps_since = 0;

static void update_fps(void) {
  fps_drawn++;
  const uint64_t now = sdl_GetTicks();
  if (fps_since == 0) {
    fps_since = now;
    return;
  }
  const uint64_t elapsed = now - fps_since;
  if (elapsed >= 500) {
    fps_value = (fps_drawn * 1000) / (int64_t)elapsed;
    fps_drawn = 0;
    fps_since = now;
  }
}

// --- Memory viewer panel ---------------------------------------------------

static void draw_memory_viewer(void) {
  fill_rect(panel_x(), panel_y(), PANEL_W, panel_h(), COLOR_PANEL);
  fill_rect(panel_x(), panel_y(), PANEL_W, 1.0f, COLOR_BORDER);
  fill_rect(panel_x(), panel_y() + panel_h() - 1.0f, PANEL_W, 1.0f, COLOR_BORDER);
  fill_rect(panel_x(), panel_y(), 1.0f, panel_h(), COLOR_BORDER);
  fill_rect(panel_x() + PANEL_W - 1.0f, panel_y(), 1.0f, panel_h(), COLOR_BORDER);

  draw_text("MEMORY", panel_x() + 6.0f, panel_y() + 6.0f, COLOR_ACCENT);
  draw_text("VIEWER", panel_x() + 6.0f, panel_y() + 6.0f + LINE_H, COLOR_DIM);

  float row = panel_y() + 32.0f;

  // Display memory dump starting at mem_view_start
  char label[16];
  int32_t label_pos = 0;
  write_str(label, label_pos, "0x");
  write_hex(label, label_pos, mem_view_start, 4);
  label[label_pos] = '\0';

  draw_text("START", panel_x() + 6.0f, row, COLOR_DIM);
  draw_text(label, panel_x() + PANEL_W - 6.0f - (float)(label_pos * 8), row, COLOR_TEXT);
  row += LINE_H;

  // Show sample memory bytes (placeholder - actual memory would be passed from Sere)
  draw_text("(memory access", panel_x() + 6.0f, row, COLOR_DIM);
  row += LINE_H;
  draw_text("from Sere)", panel_x() + 6.0f, row, COLOR_DIM);
  row += LINE_H + 2.0f;

  fill_rect(panel_x() + 6.0f, row, PANEL_W - 12.0f, 1.0f, COLOR_BORDER);
  row += LINE_H;

  char count_text[16];
  int32_t count_pos = 0;
  write_dec(count_text, count_pos, mem_view_count);
  count_text[count_pos] = '\0';

  draw_text("BYTES", panel_x() + 6.0f, row, COLOR_DIM);
  draw_text(count_text, panel_x() + PANEL_W - 6.0f - (float)(count_pos * 8), row, COLOR_TEXT);
  row += LINE_H;

  const char* hint = "Press M to toggle CPU mode";
  draw_text(hint, panel_x() + 6.0f, HINT_Y, COLOR_DIM);
}

// --- CPU state panel -------------------------------------------------------

static void draw_panel(int64_t a, int64_t x, int64_t y, int64_t sp, int64_t pc, int64_t p,
                       int64_t cycles, int64_t speed, int64_t state) {
  fill_rect(panel_x(), panel_y(), PANEL_W, panel_h(), COLOR_PANEL);
  fill_rect(panel_x(), panel_y(), PANEL_W, 1.0f, COLOR_BORDER);
  fill_rect(panel_x(), panel_y() + panel_h() - 1.0f, PANEL_W, 1.0f, COLOR_BORDER);
  fill_rect(panel_x(), panel_y(), 1.0f, panel_h(), COLOR_BORDER);
  fill_rect(panel_x() + PANEL_W - 1.0f, panel_y(), 1.0f, panel_h(), COLOR_BORDER);

  draw_text("SEMU", panel_x() + 6.0f, panel_y() + 6.0f, COLOR_ACCENT);
  draw_text("6502 DEBUGGER", panel_x() + 6.0f, panel_y() + 6.0f + LINE_H, COLOR_DIM);

  float row = panel_y() + 32.0f;

  draw_field(row, "A", (uint64_t)(a & 0xFF), 2, true);
  row += LINE_H;
  draw_field(row, "X", (uint64_t)(x & 0xFF), 2, true);
  row += LINE_H;
  draw_field(row, "Y", (uint64_t)(y & 0xFF), 2, true);
  row += LINE_H;
  draw_field(row, "SP", (uint64_t)(sp & 0xFF), 2, true);
  row += LINE_H;
  draw_field(row, "PC", (uint64_t)(pc & 0xFFFF), 4, true);
  row += LINE_H;
  draw_field(row, "P", (uint64_t)(p & 0xFF), 2, true);
  row += LINE_H;
  draw_text("FLAGS", panel_x() + 6.0f, row, COLOR_DIM);
  row += LINE_H;
  draw_flags(row, p);
  row += LINE_H + 2.0f;

  fill_rect(panel_x() + 6.0f, row, PANEL_W - 12.0f, 1.0f, COLOR_BORDER);
  row += LINE_H;

  draw_field(row, "CYC", (uint64_t)cycles, 0, false);
  row += LINE_H;
  draw_field(row, "SPD", (uint64_t)speed, 0, false);
  row += LINE_H;
  draw_field(row, "FPS", (uint64_t)fps_value, 0, false);
  row += LINE_H;

  const char* status = state_text(state);
  draw_text("STATE", panel_x() + 6.0f, row, COLOR_DIM);
  draw_text(status, panel_x() + PANEL_W - 6.0f - (float)(text_len(status) * 8), row,
            state_color(state));
  row += LINE_H;

  const char* input = screen_focus ? "CPU" : "UI";
  draw_text("INPUT", panel_x() + 6.0f, row, COLOR_DIM);
  draw_text(input, panel_x() + PANEL_W - 6.0f - (float)(text_len(input) * 8), row,
            screen_focus ? COLOR_ACCENT : COLOR_DIM);

  // Focused: the controls are greyed out and only ESC or a click on the screen
  // gives the keyboard back to the debugger.
  const bool paused = (state == STATE_PAUSED) || (state == STATE_HALTED) || (state == STATE_ERROR) ||
                      (state == STATE_ENDED);
  draw_button(buttons[0], paused ? "RUN" : "PAUSE", !paused && !screen_focus);
  draw_button(buttons[1], "STEP", false);
  draw_button(buttons[2], "RESET", false);

  const uint32_t hint = screen_focus ? COLOR_BORDER : COLOR_DIM;
  if (screen_focus) {
    draw_text("KEYS GO TO CPU", panel_x() + 6.0f, HINT_Y, COLOR_ACCENT);
    draw_text("ESC OR CLICK ON", panel_x() + 6.0f, HINT_Y + 10.0f, hint);
    draw_text("THE SCREEN TO", panel_x() + 6.0f, HINT_Y + 20.0f, hint);
    draw_text("RELEASE FOCUS", panel_x() + 6.0f, HINT_Y + 30.0f, hint);
  } else {
    draw_text("SPACE RUN/PAUSE", panel_x() + 6.0f, HINT_Y, hint);
    draw_text("S STEP   R RESET", panel_x() + 6.0f, HINT_Y + 10.0f, hint);
    draw_text("UP/DOWN SPEED", panel_x() + 6.0f, HINT_Y + 20.0f, hint);
    draw_text("F FOCUS SCREEN", panel_x() + 6.0f, HINT_Y + 30.0f, hint);
  }
}

static bool inside_screen(float x, float y) {
  const float left = screen_x();
  const float top = screen_y();
  const float w = (float)screen_w * screen_zoom;
  const float h = (float)screen_h * screen_zoom;
  return x >= left && x < left + w && y >= top && y < top + h;
}

static void draw_screen(void) {
  const float x = screen_x();
  const float y = screen_y();
  const float w = (float)screen_w * screen_zoom;
  const float h = (float)screen_h * screen_zoom;

  if (screen_focus) {
    // Bright frame: the keyboard is driving the CPU, not the debugger.
    fill_rect(x - 4.0f, y - 4.0f, w + 8.0f, h + 8.0f, COLOR_ACCENT);
    fill_rect(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, COLOR_BG);
  } else {
    fill_rect(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, COLOR_BORDER);
  }

  SDL_FRect destination = {x, y, w, h};
  sdl_RenderTexture(renderer, screen_texture, nullptr, &destination);
}

// --- public API ------------------------------------------------------------

extern "C" int64_t semu_gfx_init(int64_t width, int64_t height, int64_t zoom) {
  if (width <= 0 || height <= 0 || zoom <= 0) {
    return 0;
  }
  if (!load_sdl3()) {
    return 0;
  }
  if (!sdl_Init(SDL_INIT_VIDEO)) {
    return 0;
  }

  screen_w = (int32_t)width;
  screen_h = (int32_t)height;
  screen_bytes = screen_w * screen_h;
  screen_zoom = (float)zoom;

  if (screen_bytes > MAX_SCREEN_BYTES) {
    return 0;
  }

  const float window_w = MARGIN + PANEL_W + MARGIN + (float)screen_w * screen_zoom + MARGIN;
  const float window_h = MARGIN + (float)screen_h * screen_zoom + MARGIN;

  window = sdl_CreateWindow("semu - 6502", (int32_t)(window_w * UI_SCALE),
                            (int32_t)(window_h * UI_SCALE), 0);
  if (window == nullptr) {
    return 0;
  }
  renderer = sdl_CreateRenderer(window, nullptr);
  if (renderer == nullptr) {
    return 0;
  }
  sdl_SetRenderScale(renderer, UI_SCALE, UI_SCALE);
  sdl_SetRenderVSync(renderer, 1);

  // The emulated screen is eight bits per pixel: three red, three green and two
  // blue, so it can be fed straight from the framebuffer bytes.
  screen_texture =
      sdl_CreateTexture(renderer, SDL_PIXELFORMAT_RGB332, SDL_TEXTUREACCESS_STREAMING, screen_w,
                        screen_h);
  if (screen_texture == nullptr) {
    return 0;
  }

  buttons[0].x = panel_x();
  buttons[1].x = panel_x() + buttons[0].w + 4.0f;
  buttons[2].x = panel_x() + PANEL_W - buttons[2].w;

  fps_since = sdl_GetTicks();
  return 1;
}

extern "C" void semu_gfx_shutdown(void) {
  if (screen_texture != nullptr) {
    sdl_DestroyTexture(screen_texture);
    screen_texture = nullptr;
  }
  if (renderer != nullptr) {
    sdl_DestroyRenderer(renderer);
    renderer = nullptr;
  }
  if (window != nullptr) {
    sdl_DestroyWindow(window);
    window = nullptr;
  }
  sdl_Quit();
}

// Stores eight consecutive framebuffer pixels packed little endian in `packed`.
extern "C" void semu_gfx_blit(int64_t index, int64_t packed) {
  if (screen_w == 0) {
    return;
  }
  const int64_t base = index * 8;
  if (base < 0 || base + 8 > (int64_t)screen_bytes) {
    return;
  }
  for (int32_t i = 0; i < 8; ++i) {
    screen_pixels[base + i] = (uint8_t)((packed >> (8 * i)) & 0xFF);
  }
  screen_dirty = true;
}

// Returns the action the user asked for, or ACTION_NONE.
extern "C" int64_t semu_gfx_poll(void) {
  if (renderer == nullptr) {
    return ACTION_QUIT;
  }

  int64_t action = ACTION_NONE;
  SDL_Event event;
  while (sdl_PollEvent(&event)) {
    if (event.type == SDL_EVENT_QUIT) {
      action = ACTION_QUIT;
    } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
      // While the screen holds focus every key belongs to the CPU: ESC (or F)
      // gives the keyboard back to the debugger and nothing else is consumed.
      if (screen_focus) {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE ||
            event.key.scancode == SDL_SCANCODE_F) {
          screen_focus = false;
        }
        continue;
      }
      switch (event.key.scancode) {
        case SDL_SCANCODE_ESCAPE:
          action = ACTION_QUIT;
          break;
        case SDL_SCANCODE_F:
          screen_focus = true;
          break;
        case SDL_SCANCODE_SPACE:
          action = ACTION_TOGGLE_RUN;
          break;
        case SDL_SCANCODE_S:
        case SDL_SCANCODE_N:
        case SDL_SCANCODE_F10:
          action = ACTION_STEP;
          break;
        case SDL_SCANCODE_R:
        case SDL_SCANCODE_F5:
          action = ACTION_RESET;
          break;
        case SDL_SCANCODE_RIGHTBRACKET:
        case SDL_SCANCODE_EQUALS:
        case SDL_SCANCODE_KP_PLUS:
          action = ACTION_FASTER;
          break;
        case SDL_SCANCODE_LEFTBRACKET:
        case SDL_SCANCODE_MINUS:
        case SDL_SCANCODE_KP_MINUS:
          action = ACTION_SLOWER;
          break;
        case SDL_SCANCODE_M:
          debug_tab = (debug_tab + 1) % 2;  // Toggle between CPU (0) and Memory (1)
          break;
        default:
          break;
      }
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
      sdl_ConvertEventToRenderCoordinates(renderer, &event);
      if (inside_screen(event.button.x, event.button.y)) {
        screen_focus = !screen_focus;
      } else if (!screen_focus) {
        for (int32_t i = 0; i < 3; ++i) {
          if (button_hit(buttons[i], event.button.x, event.button.y)) {
            action = buttons[i].action;
          }
        }
      }
    }
  }
  return action;
}

// Redraws the window: CPU state panel on the left, emulated screen on the right.
extern "C" void semu_gfx_frame(int64_t a, int64_t x, int64_t y, int64_t sp, int64_t pc, int64_t p,
                               int64_t cycles, int64_t speed, int64_t state) {
  if (renderer == nullptr) {
    return;
  }
  update_fps();

  if (screen_dirty) {
    sdl_UpdateTexture(screen_texture, nullptr, screen_pixels, screen_w);
    screen_dirty = false;
  }

  set_color(COLOR_BG);
  sdl_RenderClear(renderer);

  // Draw either CPU state or memory viewer based on active tab
  if (debug_tab == 0) {
    draw_panel(a, x, y, sp, pc, p, cycles, speed, state);
  } else {
    draw_memory_viewer();
  }
  draw_screen();

  sdl_RenderPresent(renderer);
}

// The pad state from the host keyboard, in the order the 2A03 shifts it out of
// $4016: A, B, Select, Start, Up, Down, Left, Right.
extern "C" int64_t semu_input_state(void) {
  if (sdl_GetKeyboardState == nullptr) {
    return 0;
  }
  int count = 0;
  const bool* keys = sdl_GetKeyboardState(&count);
  if (keys == nullptr) {
    return 0;
  }

  int64_t mask = 0;
  const bool cpu_input = screen_focus;
  if (keys[SDL_SCANCODE_X]) mask |= 0x01;
  if (cpu_input && keys[SDL_SCANCODE_SPACE]) mask |= 0x01;
  if (keys[SDL_SCANCODE_Z]) mask |= 0x02;
  if (cpu_input && keys[SDL_SCANCODE_J]) mask |= 0x02;
  if (keys[SDL_SCANCODE_RSHIFT]) mask |= 0x04;
  if (keys[SDL_SCANCODE_RETURN]) mask |= 0x08;
  if (keys[SDL_SCANCODE_UP]) mask |= 0x10;
  if (cpu_input && keys[SDL_SCANCODE_W]) mask |= 0x10;
  if (keys[SDL_SCANCODE_DOWN]) mask |= 0x20;
  if (cpu_input && keys[SDL_SCANCODE_S]) mask |= 0x20;
  if (keys[SDL_SCANCODE_LEFT]) mask |= 0x40;
  if (cpu_input && keys[SDL_SCANCODE_A]) mask |= 0x40;
  if (keys[SDL_SCANCODE_RIGHT]) mask |= 0x80;
  if (cpu_input && keys[SDL_SCANCODE_D]) mask |= 0x80;
  return mask;
}

// --- Memory viewer API -------------------------------------------------

extern "C" void semu_gfx_set_memory_view(int64_t start_addr, int64_t byte_count) {
  mem_view_start = start_addr & 0xFFFF;
  mem_view_count = byte_count > 0 ? byte_count : 1;
  if (mem_view_count > 256) {
    mem_view_count = 256;
  }
}

extern "C" int64_t semu_gfx_get_debug_tab(void) {
  return debug_tab;
}

extern "C" void semu_gfx_set_debug_tab(int64_t tab) {
  debug_tab = tab ? 1 : 0;
}

// --- Sere runtime registration ---------------------------------------------

static int64_t arg_i64(Sere_Object* const* args, int32_t nargs, int32_t index, int64_t fallback) {
  if (index >= nargs) {
    return fallback;
  }
  return Sere_Long_AsI64(args[index]);
}

static Sere_Object* boxed_init(Sere_Object* const* args, int32_t nargs) {
  return Sere_Long_FromI64(semu_gfx_init(arg_i64(args, nargs, 0, 0), arg_i64(args, nargs, 1, 0),
                                         arg_i64(args, nargs, 2, 1)));
}

static Sere_Object* boxed_shutdown(Sere_Object* const* args, int32_t nargs) {
  semu_gfx_shutdown();
  return Sere_None_New();
}

static Sere_Object* boxed_poll(Sere_Object* const* args, int32_t nargs) {
  return Sere_Long_FromI64(semu_gfx_poll());
}

static Sere_Object* boxed_input(Sere_Object* const* args, int32_t nargs) {
  return Sere_Long_FromI64(semu_input_state());
}

static Sere_Object* boxed_blit(Sere_Object* const* args, int32_t nargs) {
  semu_gfx_blit(arg_i64(args, nargs, 0, 0), arg_i64(args, nargs, 1, 0));
  return Sere_None_New();
}

static Sere_Object* boxed_frame(Sere_Object* const* args, int32_t nargs) {
  semu_gfx_frame(arg_i64(args, nargs, 0, 0), arg_i64(args, nargs, 1, 0), arg_i64(args, nargs, 2, 0),
                 arg_i64(args, nargs, 3, 0), arg_i64(args, nargs, 4, 0), arg_i64(args, nargs, 5, 0),
                 arg_i64(args, nargs, 6, 0), arg_i64(args, nargs, 7, 0), arg_i64(args, nargs, 8, 0));
  return Sere_None_New();
}

extern "C" void sere_mod_init(void) {
  Sere_DefineFunction("semu_gfx_init", boxed_init, 3);
  Sere_DefineFunction("semu_gfx_shutdown", boxed_shutdown, 0);
  Sere_DefineFunction("semu_gfx_poll", boxed_poll, 0);
  Sere_DefineFunction("semu_input_state", boxed_input, 0);
  Sere_DefineFunction("semu_gfx_blit", boxed_blit, 2);
  Sere_DefineFunction("semu_gfx_frame", boxed_frame, 9);
}
