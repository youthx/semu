// SDL3 bindings and a small debugger window for semu.

#pragma runtime_checks("", off)
#pragma comment(linker, "/NODEFAULTLIB:MSVCRTD")

#include "sere/api/sere_mod.h"

#include <SDL3/SDL.h>

// Minimal Win32 declarations for loading SDL3.dll, instead of <windows.h>.
extern "C" __declspec(dllimport) void* __stdcall LoadLibraryA(const char* file_name);
extern "C" __declspec(dllimport) void* __stdcall GetProcAddress(void* module, const char* name);
extern "C" __declspec(dllimport) uint32_t __stdcall GetModuleFileNameA(void* module, char* path,
                                                                         uint32_t size);
// Declare memcpy without pulling in <string.h>.
extern "C" void* memcpy(void* dst, const void* src, size_t n);
extern "C" __declspec(dllimport) void* __stdcall CreateFileA(const char* name, uint32_t access,
                                                                uint32_t share, void* security,
                                                                uint32_t creation, uint32_t flags,
                                                                void* template_file);
extern "C" __declspec(dllimport) int32_t __stdcall ReadFile(void* handle, void* buffer,
                                                              uint32_t length, uint32_t* read,
                                                              void* overlapped);
extern "C" __declspec(dllimport) int32_t __stdcall WriteFile(void* handle, const void* buffer,
                                                               uint32_t length, uint32_t* written,
                                                               void* overlapped);
extern "C" __declspec(dllimport) int32_t __stdcall FlushFileBuffers(void* handle);
extern "C" __declspec(dllimport) int32_t __stdcall CloseHandle(void* handle);
extern "C" __declspec(dllimport) uint32_t __stdcall SetFilePointer(void* handle, int32_t distance,
                                                                     int32_t* high, uint32_t method);

static const uint32_t SEMU_GENERIC_READ = 0x80000000u;
static const uint32_t SEMU_GENERIC_WRITE = 0x40000000u;
static const uint32_t SEMU_FILE_SHARE_READ = 0x00000001u;
static const uint32_t SEMU_FILE_SHARE_WRITE = 0x00000002u;
static const uint32_t SEMU_OPEN_EXISTING = 3u;
static const uint32_t SEMU_OPEN_ALWAYS = 4u;
static const uint32_t SEMU_FILE_ATTRIBUTE_NORMAL = 0x00000080u;
static const uint32_t SEMU_FILE_BEGIN = 0u;
static void* const SEMU_INVALID_HANDLE = (void*)(intptr_t)-1;

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
static decltype(&SDL_LoadBMP) sdl_LoadBMP;
static decltype(&SDL_DestroySurface) sdl_DestroySurface;
static decltype(&SDL_SetWindowIcon) sdl_SetWindowIcon;
static decltype(&SDL_PollEvent) sdl_PollEvent;
static decltype(&SDL_ConvertEventToRenderCoordinates) sdl_ConvertEventToRenderCoordinates;
static decltype(&SDL_GetKeyboardState) sdl_GetKeyboardState;
static decltype(&SDL_SetWindowSize) sdl_SetWindowSize;

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
  SEMU_LOAD(LoadBMP)
  SEMU_LOAD(DestroySurface)
  SEMU_LOAD(SetWindowIcon)
  SEMU_LOAD(PollEvent)
  SEMU_LOAD(GetKeyboardState)
  SEMU_LOAD(ConvertEventToRenderCoordinates)
  SEMU_LOAD(SetWindowSize)

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

// --- memory-mapped storage devices ----------------------------------------

static const int32_t SRAM_SIZE = 8192;
static const int32_t BANK_SIZE = 8192;
static const int32_t BANK_COUNT = 4;
static const int32_t DISK_BLOCK_SIZE = 256;
static const int32_t DISK_BLOCK_COUNT = 1024;
static const int64_t STORAGE_NOT_MAPPED = -1;

static uint8_t storage_sram[SRAM_SIZE];
static uint8_t storage_prg[BANK_SIZE * BANK_COUNT];
static uint8_t storage_chr[BANK_SIZE * BANK_COUNT];
static uint8_t storage_block[DISK_BLOCK_SIZE];
static uint8_t storage_disk[DISK_BLOCK_SIZE * DISK_BLOCK_COUNT];
static int32_t storage_prg_bank = 0;
static int32_t storage_chr_bank = 0;
static int32_t storage_block_lo = 0;
static int32_t storage_block_mid = 0;
static int32_t storage_block_hi = 0;
static int32_t storage_status = 0x01;
static int32_t storage_command = 0;
static int64_t storage_busy_cycles = 0;
static bool storage_initialized = false;
static char storage_path_buffer[512];

static const char* storage_path(const char* name) {
  char executable[512];
  const uint32_t length = GetModuleFileNameA(nullptr, executable, sizeof(executable));
  int32_t directory_end = (int32_t)length;
  while (directory_end > 0 && executable[directory_end - 1] != '\\' && executable[directory_end - 1] != '/') {
    directory_end--;
  }
  if (directory_end <= 0) directory_end = 0;
  int32_t out = 0;
  while (out < directory_end && out < (int32_t)sizeof(storage_path_buffer) - 1) {
    storage_path_buffer[out] = executable[out];
    out++;
  }
  int32_t name_index = 0;
  while (name[name_index] != '\0' && out < (int32_t)sizeof(storage_path_buffer) - 1) {
    storage_path_buffer[out++] = name[name_index++];
  }
  storage_path_buffer[out] = '\0';
  return storage_path_buffer;
}

static bool storage_read_file(const char* path, uint8_t* buffer, uint32_t size) {
  void* handle = CreateFileA(path, SEMU_GENERIC_READ, SEMU_FILE_SHARE_READ | SEMU_FILE_SHARE_WRITE,
                             nullptr, SEMU_OPEN_EXISTING, 0, nullptr);
  if (handle == SEMU_INVALID_HANDLE) return false;
  uint32_t read = 0;
  const bool ok = ReadFile(handle, buffer, size, &read, nullptr) != 0;
  CloseHandle(handle);
  return ok && read == size;
}

static bool storage_write_file(const char* path, const uint8_t* buffer, uint32_t size) {
  void* handle = CreateFileA(path, SEMU_GENERIC_WRITE, SEMU_FILE_SHARE_READ | SEMU_FILE_SHARE_WRITE,
                             nullptr, SEMU_OPEN_ALWAYS, SEMU_FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == SEMU_INVALID_HANDLE) return false;
  int32_t high = 0;
  SetFilePointer(handle, 0, &high, SEMU_FILE_BEGIN);
  uint32_t written = 0;
  const bool ok = WriteFile(handle, buffer, size, &written, nullptr) != 0;
  FlushFileBuffers(handle);
  CloseHandle(handle);
  return ok && written == size;
}

static void storage_finish_command(void) {
  const int64_t block_id = storage_block_lo | (storage_block_mid << 8) | (storage_block_hi << 16);
  if (block_id < 0 || block_id >= DISK_BLOCK_COUNT) {
    storage_status = 0x80;
  } else if (storage_command == 0x01) {
    memcpy(storage_block, &storage_disk[block_id * DISK_BLOCK_SIZE], DISK_BLOCK_SIZE);
    storage_status = 0x01;
  } else if (storage_command == 0x02) {
    memcpy(&storage_disk[block_id * DISK_BLOCK_SIZE], storage_block, DISK_BLOCK_SIZE);
    storage_write_file(storage_path("semu.disk"), storage_disk, sizeof(storage_disk));
    storage_status = 0x01;
  } else if (storage_command == 0x03) {
    storage_write_file(storage_path("semu.sram"), storage_sram, sizeof(storage_sram));
    storage_write_file(storage_path("semu.chr"), storage_chr, sizeof(storage_chr));
    storage_write_file(storage_path("semu.disk"), storage_disk, sizeof(storage_disk));
    storage_status = 0x01;
  } else if (storage_command == 0x04) {
    storage_block[0] = 0xA5;
    storage_block[1] = 0x01;
    storage_block[2] = 0x00;
    storage_status = 0x01;
  } else {
    storage_status = 0x80;
  }
}

extern "C" void semu_storage_init(void) {
  if (storage_initialized) return;
  storage_initialized = true;
  for (int32_t i = 0; i < SRAM_SIZE; ++i) storage_sram[i] = 0;
  for (int32_t i = 0; i < BANK_SIZE * BANK_COUNT; ++i) {
    storage_prg[i] = 0xFF;
    storage_chr[i] = 0x00;
  }
  for (int32_t i = 0; i < DISK_BLOCK_SIZE * DISK_BLOCK_COUNT; ++i) storage_disk[i] = 0;
  storage_read_file(storage_path("semu.sram"), storage_sram, sizeof(storage_sram));
  storage_read_file(storage_path("semu.prg"), storage_prg, sizeof(storage_prg));
  storage_read_file(storage_path("semu.chr"), storage_chr, sizeof(storage_chr));
  storage_read_file(storage_path("semu.disk"), storage_disk, sizeof(storage_disk));
}

extern "C" void semu_storage_flush(void) {
  semu_storage_init();
  storage_write_file(storage_path("semu.sram"), storage_sram, sizeof(storage_sram));
  storage_write_file(storage_path("semu.chr"), storage_chr, sizeof(storage_chr));
  storage_write_file(storage_path("semu.disk"), storage_disk, sizeof(storage_disk));
}

extern "C" int64_t semu_storage_read(int64_t address) {
  semu_storage_init();
  const int32_t a = (int32_t)(address & 0xFFFF);
  if (a >= 0xA000 && a <= 0xBFFF) return storage_sram[a - 0xA000];
  if (a == 0x5D00) return storage_prg_bank;
  if (a == 0x5D01) return storage_chr_bank;
  if (a == 0x5D03) return 0x4D;
  if (a >= 0x8000 && a <= 0x9FFF) return storage_prg[storage_prg_bank * BANK_SIZE + a - 0x8000];
  if (a >= 0xC000 && a <= 0xDFFF) return storage_chr[storage_chr_bank * BANK_SIZE + a - 0xC000];
  if (a == 0x5E00) return storage_command;
  if (a == 0x5E01) return storage_status;
  if (a == 0x5E02) return storage_block_lo;
  if (a == 0x5E03) return storage_block_mid;
  if (a == 0x5E04) return storage_block_hi;
  if (a == 0x5E05) return 0xA5;
  if (a == 0x5E06) return 0x01;
  if (a == 0x5E07) return 0x00;
  if (a >= 0x5F00 && a <= 0x5FFF) return storage_block[a - 0x5F00];
  return STORAGE_NOT_MAPPED;
}

extern "C" int64_t semu_storage_write(int64_t address, int64_t value) {
  semu_storage_init();
  const int32_t a = (int32_t)(address & 0xFFFF);
  const uint8_t v = (uint8_t)(value & 0xFF);
  if (a >= 0xA000 && a <= 0xBFFF) {
    storage_sram[a - 0xA000] = v;
    storage_write_file(storage_path("semu.sram"), storage_sram, sizeof(storage_sram));
    return 1;
  }
  if (a == 0x5D00) { storage_prg_bank = v % BANK_COUNT; return 1; }
  if (a == 0x5D01) { storage_chr_bank = v % BANK_COUNT; return 1; }
  if (a >= 0x8000 && a <= 0x9FFF) return 1;
  if (a >= 0xC000 && a <= 0xDFFF) {
    storage_chr[storage_chr_bank * BANK_SIZE + a - 0xC000] = v;
    return 1;
  }
  if (a == 0x5E02) { storage_block_lo = v; return 1; }
  if (a == 0x5E03) { storage_block_mid = v; return 1; }
  if (a == 0x5E04) { storage_block_hi = v; return 1; }
  if (a >= 0x5F00 && a <= 0x5FFF) { storage_block[a - 0x5F00] = v; return 1; }
  if (a == 0x5E00) {
    storage_command = v;
    storage_status = 0x02;
    storage_busy_cycles = v == 0x02 ? 4000 : 2000;
    return 1;
  }
  return 0;
}

extern "C" void semu_storage_tick(int64_t cycles) {
  if (storage_busy_cycles <= 0) return;
  storage_busy_cycles -= cycles;
  if (storage_busy_cycles <= 0) storage_finish_command();
}

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

// Layout: 0 = tabbed (CPU/MEM tabs, left panel + screen), 1 = split (MEM left, screen centre, CPU right)
static int32_t layout = 0;
// Debug display tab used only in layout 0: 0 = CPU, 1 = Memory
static int32_t debug_tab = 0;
// Current panel draw x-offset; set before each draw call so helpers use it.
static float g_panel_offset = 6.0f;  // MARGIN value; updated at runtime
// Memory viewer parameters
static int64_t mem_view_start = 0x0000;
static int64_t mem_view_count = 128;
// Memory viewer buffer (max 256 bytes)
static uint8_t mem_view_buffer[256];
static int64_t mem_view_buffer_size = 0;

// --- layout (logical units, the renderer is scaled by UI_SCALE) ------------

static const float UI_SCALE = 2.0f;
static const float MARGIN = 6.0f;
static const float PANEL_W = 168.0f;
static const float LINE_H = 10.0f;
static const float TAB_H = 14.0f;
static const float BUTTON_H = 20.0f;

static const uint32_t COLOR_BG = 0x0E1116;
static const uint32_t COLOR_PANEL = 0x171B23;
static const uint32_t COLOR_BORDER = 0x2A3140;
static const uint32_t COLOR_TEXT = 0xD7DEE8;
static const uint32_t COLOR_DIM = 0x7B8798;
static const uint32_t COLOR_ACCENT = 0x6CC7FF;
static const uint32_t COLOR_GOOD = 0x7BD88F;
static const uint32_t COLOR_WARN = 0xFFD866;
static const uint32_t COLOR_BAD = 0xFF6B6B;

static float panel_x(void)   { return g_panel_offset; }
static float panel_y(void)   { return MARGIN + TAB_H; }
static float panel_h(void)   { return (float)screen_h * screen_zoom + MARGIN - TAB_H; }
static float button_y(void)  { return panel_y() + panel_h() - 50.0f; }
static float hint_y(void)    { return panel_y() + panel_h() - 24.0f; }
// In split layout: MEM(left) | screen(centre) | CPU(right)
static float screen_x(void)  { return layout == 0 ? MARGIN + PANEL_W + MARGIN
                                                   : MARGIN + PANEL_W + MARGIN; }
static float screen_y(void)  { return MARGIN; }
static float right_panel_x(void) { return MARGIN + PANEL_W + MARGIN + (float)screen_w * screen_zoom + MARGIN; }
static float base_window_w(void) { return MARGIN + PANEL_W + MARGIN + (float)screen_w * screen_zoom + MARGIN; }
static float split_window_w(void){ return MARGIN + PANEL_W + MARGIN + (float)screen_w * screen_zoom + MARGIN + PANEL_W + MARGIN; }

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
    {0.0f, 0.0f, 76.0f, BUTTON_H, "RUN", ACTION_TOGGLE_RUN},
    {0.0f, 0.0f, 40.0f, BUTTON_H, "STEP", ACTION_STEP},
    {0.0f, 0.0f, 44.0f, BUTTON_H, "RESET", ACTION_RESET},
};

static void update_buttons(void) {
  const float by = button_y();
  buttons[0].x = panel_x();                          buttons[0].y = by;
  buttons[1].x = panel_x() + buttons[0].w + 4.0f;   buttons[1].y = by;
  buttons[2].x = panel_x() + PANEL_W - buttons[2].w; buttons[2].y = by;
}

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

// Tab bar for layout 0, or a static header bar for layout 1.
static void draw_header_bar(float px, const char* label, bool active) {
  const float ty = MARGIN;
  fill_rect(px, ty, PANEL_W, TAB_H, active ? COLOR_ACCENT : COLOR_PANEL);
  fill_rect(px, ty, PANEL_W, 1.0f, COLOR_BORDER);
  fill_rect(px, ty, 1.0f, TAB_H, COLOR_BORDER);
  fill_rect(px + PANEL_W - 1.0f, ty, 1.0f, TAB_H, COLOR_BORDER);
  fill_rect(px, ty + TAB_H - 1.0f, PANEL_W, 1.0f, active ? COLOR_ACCENT : COLOR_BORDER);
  draw_text(label, px + 6.0f, ty + 3.0f, active ? COLOR_BG : COLOR_TEXT);
  // Layout toggle hint on the right
  const char* hint = layout == 0 ? "L:SPLIT" : "L:TABBED";
  const float hw = (float)(text_len(hint) * 8);
  draw_text(hint, px + PANEL_W - hw - 6.0f, ty + 3.0f, active ? COLOR_BG : COLOR_DIM);
}

static void draw_tabs(void) {
  if (layout == 1) return;  // split layout draws individual headers per panel
  const float tab_w = PANEL_W / 2.0f;
  const float ty = MARGIN;
  // CPU tab
  const uint32_t cpu_bg = (debug_tab == 0) ? COLOR_ACCENT : COLOR_PANEL;
  const uint32_t cpu_fg = (debug_tab == 0) ? COLOR_BG : COLOR_DIM;
  fill_rect(panel_x(), ty, tab_w, TAB_H, cpu_bg);
  fill_rect(panel_x(), ty, tab_w, 1.0f, COLOR_BORDER);
  fill_rect(panel_x(), ty, 1.0f, TAB_H, COLOR_BORDER);
  fill_rect(panel_x(), ty + TAB_H - 1.0f, tab_w, 1.0f, (debug_tab == 0) ? COLOR_ACCENT : COLOR_BORDER);
  draw_text("CPU", panel_x() + (tab_w - 24.0f) * 0.5f, ty + 3.0f, cpu_fg);
  // MEM tab
  const uint32_t mem_bg = (debug_tab == 1) ? COLOR_ACCENT : COLOR_PANEL;
  const uint32_t mem_fg = (debug_tab == 1) ? COLOR_BG : COLOR_DIM;
  fill_rect(panel_x() + tab_w, ty, tab_w, TAB_H, mem_bg);
  fill_rect(panel_x() + tab_w, ty, tab_w, 1.0f, COLOR_BORDER);
  fill_rect(panel_x() + tab_w + tab_w - 1.0f, ty, 1.0f, TAB_H, COLOR_BORDER);
  fill_rect(panel_x() + tab_w, ty + TAB_H - 1.0f, tab_w, 1.0f, (debug_tab == 1) ? COLOR_ACCENT : COLOR_BORDER);
  draw_text("MEM", panel_x() + tab_w + (tab_w - 24.0f) * 0.5f, ty + 3.0f, mem_fg);
}

// Panel fits ~19 chars per row (168px / 8px per char).
// Format: "XXXX: AA BB CC DD" = 18 chars -- exactly fits with 6px margin each side.
static void draw_memory_viewer(void) {
  fill_rect(panel_x(), panel_y(), PANEL_W, panel_h(), COLOR_PANEL);
  fill_rect(panel_x(), panel_y() + panel_h() - 1.0f, PANEL_W, 1.0f, COLOR_BORDER);
  fill_rect(panel_x(), panel_y(), 1.0f, panel_h(), COLOR_BORDER);
  fill_rect(panel_x() + PANEL_W - 1.0f, panel_y(), 1.0f, panel_h(), COLOR_BORDER);

  // Show start address in top-right
  char addr_label[8];
  int32_t alp = 0;
  write_hex(addr_label, alp, mem_view_start, 4);
  addr_label[alp] = '\0';
  draw_text(addr_label, panel_x() + PANEL_W - 6.0f - (float)(alp * 8), panel_y() + 4.0f, COLOR_DIM);

  float row = panel_y() + 4.0f;

  // 4 bytes per row: "XXXX: AA BB CC DD"
  const float max_row = panel_y() + panel_h() - 14.0f;
  const int32_t count = (int32_t)mem_view_buffer_size;
  for (int32_t i = 0; i < count && row + LINE_H < max_row; i += 4) {
    char line[24];
    int32_t lp = 0;
    write_hex(line, lp, (int32_t)mem_view_start + i, 4);
    line[lp++] = ':';
    for (int32_t j = 0; j < 4 && (i + j) < count; j++) {
      line[lp++] = ' ';
      write_hex(line, lp, mem_view_buffer[i + j], 2);
    }
    line[lp] = '\0';
    draw_text(line, panel_x() + 6.0f, row, COLOR_TEXT);
    row += LINE_H;
  }

  fill_rect(panel_x() + 6.0f, max_row - 2.0f, PANEL_W - 12.0f, 1.0f, COLOR_BORDER);
  draw_text("M: toggle tab  L: layout", panel_x() + 6.0f, max_row, COLOR_DIM);
}

// --- CPU state panel -------------------------------------------------------

static void draw_panel(int64_t a, int64_t x, int64_t y, int64_t sp, int64_t pc, int64_t p,
                       int64_t cycles, int64_t speed, int64_t state) {
  fill_rect(panel_x(), panel_y(), PANEL_W, panel_h(), COLOR_PANEL);
  fill_rect(panel_x(), panel_y() + panel_h() - 1.0f, PANEL_W, 1.0f, COLOR_BORDER);
  fill_rect(panel_x(), panel_y(), 1.0f, panel_h(), COLOR_BORDER);
  fill_rect(panel_x() + PANEL_W - 1.0f, panel_y(), 1.0f, panel_h(), COLOR_BORDER);

  float row = panel_y() + 4.0f;

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

  const float hy = hint_y();
  const uint32_t hint_col = screen_focus ? COLOR_BORDER : COLOR_DIM;
  if (screen_focus) {
    draw_text("ESC: release focus", panel_x() + 6.0f, hy, COLOR_ACCENT);
  } else {
    draw_text("SPC:run S:step L:layout", panel_x() + 6.0f, hy, hint_col);
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

  const float window_w = base_window_w();
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
  SDL_Surface* icon = sdl_LoadBMP(storage_path("assets\\semu-logo.bmp"));
  if (icon == nullptr) {
    icon = sdl_LoadBMP(storage_path("..\\assets\\semu-logo.bmp"));
  }
  if (icon != nullptr) {
    sdl_SetWindowIcon(window, icon);
    sdl_DestroySurface(icon);
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

  g_panel_offset = MARGIN;
  update_buttons();

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

static void storage_copy_packed8(uint8_t* destination, int64_t packed) {
  const uint64_t bits = (uint64_t)packed;
  for (int32_t i = 0; i < 8; ++i) {
    destination[i] = (uint8_t)((bits >> (i * 8)) & 0xFFu);
  }
}

// Stores eight consecutive framebuffer pixels packed little-endian.
extern "C" void semu_gfx_blit(int64_t index, int64_t packed) {
  const int64_t base = index * 8;
  if (base < 0 || base + 8 > (int64_t)screen_bytes) return;
  storage_copy_packed8(&screen_pixels[base], packed);
  screen_dirty = true;
}

// Stores 64 consecutive pixels (8 packed i64s) in one call — 8× fewer cross-language calls.
extern "C" void semu_gfx_blit64(int64_t index, int64_t p0, int64_t p1, int64_t p2, int64_t p3,
                                  int64_t p4, int64_t p5, int64_t p6, int64_t p7) {
  const int64_t base = index * 64;
  if (base < 0 || base + 64 > (int64_t)screen_bytes) return;
  uint8_t* dst = &screen_pixels[base];
  storage_copy_packed8(dst +  0, p0);
  storage_copy_packed8(dst +  8, p1);
  storage_copy_packed8(dst + 16, p2);
  storage_copy_packed8(dst + 24, p3);
  storage_copy_packed8(dst + 32, p4);
  storage_copy_packed8(dst + 40, p5);
  storage_copy_packed8(dst + 48, p6);
  storage_copy_packed8(dst + 56, p7);
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
          if (layout == 0) debug_tab = (debug_tab + 1) % 2;
          break;
        case SDL_SCANCODE_L: {
          layout = 1 - layout;
          const float new_w = (layout == 0) ? base_window_w() : split_window_w();
          const float new_h = MARGIN + (float)screen_h * screen_zoom + MARGIN;
          sdl_SetWindowSize(window, (int32_t)(new_w * UI_SCALE), (int32_t)(new_h * UI_SCALE));
          break;
        }
        default:
          break;
      }
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
      sdl_ConvertEventToRenderCoordinates(renderer, &event);
      if (inside_screen(event.button.x, event.button.y)) {
        screen_focus = !screen_focus;
      } else if (!screen_focus) {
        const float ty = MARGIN;
        if (layout == 0) {
          // Tab bar click in tabbed layout
          const float tab_w = PANEL_W / 2.0f;
          if (event.button.y >= ty && event.button.y < ty + TAB_H) {
            if (event.button.x >= MARGIN && event.button.x < MARGIN + tab_w) {
              debug_tab = 0;
            } else if (event.button.x >= MARGIN + tab_w && event.button.x < MARGIN + PANEL_W) {
              debug_tab = 1;
            }
          }
        }
        // RUN/STEP/RESET buttons (always in the CPU panel)
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

  if (layout == 0) {
    // Tabbed: single left panel showing CPU or MEM
    g_panel_offset = MARGIN;
    update_buttons();
    draw_tabs();
    if (debug_tab == 0) {
      draw_panel(a, x, y, sp, pc, p, cycles, speed, state);
    } else {
      draw_memory_viewer();
    }
  } else {
    // Split: MEM on left, CPU on right
    g_panel_offset = MARGIN;
    draw_header_bar(g_panel_offset, "MEM", false);
    draw_memory_viewer();
    g_panel_offset = right_panel_x();
    update_buttons();
    draw_header_bar(g_panel_offset, "CPU", true);
    draw_panel(a, x, y, sp, pc, p, cycles, speed, state);
    g_panel_offset = MARGIN;  // restore
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

// 1 = memory view is currently visible; Sere uses this to skip blit_memory when not needed.
extern "C" int64_t semu_gfx_mem_visible(void) {
  return (layout == 1 || debug_tab == 1) ? 1 : 0;
}

extern "C" void semu_gfx_blit_memory(int64_t index, int64_t packed) {
  int32_t pos = (int32_t)index * 8;
  if (pos + 8 <= 256) {
    memcpy(&mem_view_buffer[pos], &packed, 8);
    if (pos + 8 > (int32_t)mem_view_buffer_size) mem_view_buffer_size = pos + 8;
  }
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

static Sere_Object* boxed_blit64(Sere_Object* const* args, int32_t nargs) {
  semu_gfx_blit64(arg_i64(args, nargs, 0, 0), arg_i64(args, nargs, 1, 0), arg_i64(args, nargs, 2, 0),
                  arg_i64(args, nargs, 3, 0), arg_i64(args, nargs, 4, 0), arg_i64(args, nargs, 5, 0),
                  arg_i64(args, nargs, 6, 0), arg_i64(args, nargs, 7, 0), arg_i64(args, nargs, 8, 0));
  return Sere_None_New();
}

static Sere_Object* boxed_blit_memory(Sere_Object* const* args, int32_t nargs) {
  semu_gfx_blit_memory(arg_i64(args, nargs, 0, 0), arg_i64(args, nargs, 1, 0));
  return Sere_None_New();
}

static Sere_Object* boxed_mem_visible(Sere_Object* const* args, int32_t nargs) {
  return Sere_Long_FromI64(semu_gfx_mem_visible());
}

static Sere_Object* boxed_get_debug_tab(Sere_Object* const* args, int32_t nargs) {
  return Sere_Long_FromI64(semu_gfx_get_debug_tab());
}

static Sere_Object* boxed_set_debug_tab(Sere_Object* const* args, int32_t nargs) {
  semu_gfx_set_debug_tab(arg_i64(args, nargs, 0, 0));
  return Sere_None_New();
}

static Sere_Object* boxed_set_memory_view(Sere_Object* const* args, int32_t nargs) {
  semu_gfx_set_memory_view(arg_i64(args, nargs, 0, 0), arg_i64(args, nargs, 1, 0));
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
  Sere_DefineFunction("semu_gfx_blit64", boxed_blit64, 9);
  Sere_DefineFunction("semu_gfx_blit_memory", boxed_blit_memory, 2);
  Sere_DefineFunction("semu_gfx_mem_visible", boxed_mem_visible, 0);
  Sere_DefineFunction("semu_gfx_get_debug_tab", boxed_get_debug_tab, 0);
  Sere_DefineFunction("semu_gfx_set_debug_tab", boxed_set_debug_tab, 1);
  Sere_DefineFunction("semu_gfx_set_memory_view", boxed_set_memory_view, 2);
  Sere_DefineFunction("semu_gfx_frame", boxed_frame, 9);
}
