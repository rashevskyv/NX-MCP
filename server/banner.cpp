#include "banner.hpp"
#include "banner_font.hpp"
#include "console_lease.hpp"
#include <switch.h>
#include <algorithm>
#include <cstring>
#include <string>

extern "C" u64 __nx_vi_layer_id;
extern "C" void BootLog(const char* what, Result rc);  // main.cpp

namespace banner {

// Framebuffer in pixels and its place on the 1920x1080 layer canvas
// (scaled 1.5x, top centre).
static constexpr int kFbWidth  = 640;
static constexpr int kFbHeight = 32;
static constexpr int kLayerWidth  = kFbWidth * 3 / 2;
static constexpr int kLayerHeight = kFbHeight * 3 / 2;
static constexpr int kMaxChars = kFbWidth / kGlyphWidth - 2;

static ViDisplay   g_display;
static ViLayer     g_layer;
static NWindow     g_window;
static Framebuffer g_fb;
static bool        g_shown = false;

static Result AddToLayerStack(ViLayerStack stack) {
  const struct { u32 stack; u64 layer_id; } in = {stack, g_layer.layer_id};
  return serviceDispatchIn(viGetSession_IManagerDisplayService(), 6000, in);
}

static Result Show() {
  Result rc = viInitialize(ViServiceType_Manager);
  if (R_FAILED(rc)) return rc;
  if (R_SUCCEEDED(rc = viOpenDefaultDisplay(&g_display))) {
    if (R_SUCCEEDED(rc = viCreateManagedLayer(&g_display, (ViLayerFlags)0, 0, &__nx_vi_layer_id))) {
      if (R_SUCCEEDED(rc = viCreateLayer(&g_display, &g_layer))) {
        s32 z = 0;
        if (R_FAILED(viGetZOrderCountMax(&g_display, &z)) || z <= 0) z = 255;
        viSetLayerScalingMode(&g_layer, ViScalingMode_FitToLayer);
        viSetLayerZ(&g_layer, z);
        // TV and handheld screens only; not Screenshot, so captures stay clean.
        AddToLayerStack(ViLayerStack_Default);
        AddToLayerStack(ViLayerStack_Lcd);
        viSetLayerSize(&g_layer, kLayerWidth, kLayerHeight);
        viSetLayerPosition(&g_layer, (1920 - kLayerWidth) / 2.0f, 0.0f);
        if (R_SUCCEEDED(rc = nwindowCreateFromLayer(&g_window, &g_layer))) {
          if (R_SUCCEEDED(rc = framebufferCreate(&g_fb, &g_window, kFbWidth, kFbHeight,
                                                 PIXEL_FORMAT_RGBA_4444, 2))) {
            if (R_SUCCEEDED(rc = framebufferMakeLinear(&g_fb))) {
              g_shown = true;
              return 0;
            }
            framebufferClose(&g_fb);
          }
          nwindowClose(&g_window);
        }
        viDestroyManagedLayer(&g_layer);
      }
    }
    viCloseDisplay(&g_display);
  }
  viExit();
  return rc;
}

static void Hide() {
  if (!g_shown) return;
  framebufferClose(&g_fb);
  nwindowClose(&g_window);
  viDestroyManagedLayer(&g_layer);
  viCloseDisplay(&g_display);
  viExit();
  g_shown = false;
}

// UTF-8 to code points; malformed bytes become '?'.
static std::u32string Decode(const std::string& s) {
  std::u32string out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char)s[i];
    int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (len == 0 || i + len > s.size()) { out += U'?'; ++i; continue; }
    char32_t cp = len == 1 ? c : c & (0x7F >> len);
    for (int k = 1; k < len; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
    out += cp;
    i += len;
  }
  return out;
}

static const Glyph* Find(char32_t cp) {
  const Glyph* end = kGlyphs + sizeof(kGlyphs) / sizeof(kGlyphs[0]);
  const Glyph* g = std::lower_bound(kGlyphs, end, cp,
      [](const Glyph& a, char32_t c) { return a.code < c; });
  return (g != end && g->code == cp) ? g : nullptr;
}

static void Draw(const std::u32string& text) {
  u32 stride = 0;
  u16* fb = (u16*)framebufferBegin(&g_fb, &stride);
  const u32 row = stride / sizeof(u16);
  const u16 background = RGBA4(0x1, 0x1, 0x2, 0xD);
  const u16 ink        = RGBA4(0xF, 0xD, 0x3, 0xF);
  for (int y = 0; y < kFbHeight; ++y)
    std::fill(fb + y * row, fb + y * row + kFbWidth, background);

  size_t n = std::min(text.size(), (size_t)kMaxChars);
  int x0 = (kFbWidth - (int)n * kGlyphWidth) / 2;
  int y0 = (kFbHeight - kGlyphHeight) / 2;
  for (size_t i = 0; i < n; ++i) {
    const Glyph* g = Find(text[i]);
    if (!g) g = Find(U'?');
    for (int y = 0; y < kGlyphHeight; ++y)
      for (int x = 0; x < kGlyphWidth; ++x)
        if (g->rows[y] & (0x80 >> x))
          fb[(y0 + y) * row + x0 + (int)i * kGlyphWidth + x] = ink;
  }
  framebufferEnd(&g_fb);
}

static std::string BannerText() {
  nlohmann::json s = services::ConsoleLease::Instance().Status();
  if (!s.value("busy", false)) return {};
  long left = s.value("seconds_left", 0L);
  char clock[32];
  snprintf(clock, sizeof(clock), "%ld:%02ld", left / 60, left % 60);
  return "Працює агент: " + s.value("agent", std::string()) + " — " +
         s.value("task", std::string()) + " · " + clock;
}

static void Loop(void*) {
  std::string shown_text;
  bool logged_failure = false;
  while (true) {
    std::string text = BannerText();
    if (text.empty()) {
      Hide();
      shown_text.clear();
    } else {
      if (!g_shown) {
        Result rc = Show();
        if (R_FAILED(rc) && !logged_failure) { BootLog("banner: show failed", rc); logged_failure = true; }
      }
      if (g_shown && text != shown_text) {
        Draw(Decode(text));
        shown_text = text;
      }
    }
    svcSleepThread(500000000LL);
  }
}

void Start() {
  static Thread thread;
  alignas(0x1000) static u8 stack[0x4000];
  if (R_SUCCEEDED(threadCreate(&thread, Loop, nullptr, stack, sizeof(stack), 0x3F, -2)))
    threadStart(&thread);
}

}  // namespace banner
