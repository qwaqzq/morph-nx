#include "app.hpp"
#include "ui/renderer.hpp"
#include "ui/particles.hpp"
#include "nro.hpp"
#include "log.hpp"
#include "image.hpp"
#include "fs.hpp"
#include "settings.hpp"
#include "i18n.hpp"
#include "shortcuts.hpp"

#include <switch.h>
#include <nanovg.h>
#include <nanovg_dk.h>
#include <nanovg/dk_renderer.hpp>
#include <nanovg/framework/CMemPool.h>

#include <algorithm>
#include <array>
#include <optional>
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" { extern u32 __nx_applet_exit_mode; }

namespace morph {

App* App::s_instance{};

static constexpr unsigned FB_NUM = 2;
static constexpr unsigned FB_WIDTH = 1280;
static constexpr unsigned FB_HEIGHT = 720;
static constexpr unsigned CMDBUF_SIZE = 0x20000;

namespace {

// Grid: GRID_COLS x GRID_ROWS cells per page, pages laid out side by side.
constexpr float kCellSize = 180.f;
constexpr float kCellGap = 16.f;
constexpr int kCols = GRID_COLS;
constexpr int kRows = GRID_ROWS;
constexpr int kPerPage = kCols * kRows;
constexpr float kPageW = (float)FB_WIDTH;
constexpr float kGridW = kCols * kCellSize + (kCols - 1) * kCellGap;
constexpr float kGridH = kRows * kCellSize + (kRows - 1) * kCellGap;
constexpr float kGridX = (FB_WIDTH - kGridW) / 2.f;
constexpr float kGridY = (FB_HEIGHT - kGridH) / 2.f;

// Settings menu (slides in from the left)
constexpr int kMenuItems = 4;
constexpr float kMenuX = 24.f, kMenuY = 20.f, kMenuW = 520.f, kMenuH = 690.f;
constexpr float kMenuRowH = 64.f;

} // namespace

// Graphics state
static dk::UniqueDevice s_device;
static dk::UniqueQueue s_queue;
static dk::UniqueSwapchain s_swapchain;
static dk::UniqueCmdBuf s_cmdbuf;
static dk::UniqueMemBlock s_fb_memblock;
static dk::Image s_framebuffers[FB_NUM];
static DkCmdList s_framebuffer_cmdlists[FB_NUM];
static DkCmdList s_render_cmdlist;

static std::optional<CMemPool> s_pool_images;
static std::optional<CMemPool> s_pool_code;
static std::optional<CMemPool> s_pool_data;
static std::optional<nvg::DkRenderer> s_renderer;

static ui::ParticleSystem s_particles;
static bool s_particles_init = false;

// Keep custom fonts alive in memory if loaded
static std::vector<u8> s_custom_font_data;
static std::vector<u8> s_custom_fallback_font_data;

App::App(const char* argv0) {
    s_instance = this;

    // Strip "sdmc:" prefix hbloader prepends so path comparisons work cleanly.
    if (argv0 && std::strncmp(argv0, "sdmc:/", 6) == 0) {
        std::strncpy(m_app_path, argv0 + 5, sizeof(m_app_path) - 1);
    } else if (argv0) {
        std::strncpy(m_app_path, argv0, sizeof(m_app_path) - 1);
    }

    // When running as /hbmenu.nro, default to returning to HOME on normal exit.
    // This gets overridden back to 0 when the user actually launches a homebrew.
    if (IsHbmenu()) {
        __nx_applet_exit_mode = 1;
    }
}

App::~App() {
    s_instance = nullptr;
}

static void CreateFramebufferResources() {
    dk::ImageLayout fb_layout;
    dk::ImageLayoutMaker{s_device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(FB_WIDTH, FB_HEIGHT)
        .initialize(fb_layout);

    auto fb_size = fb_layout.getSize();
    auto fb_align = fb_layout.getAlignment();
    fb_size = (fb_size + fb_align - 1) & ~(fb_align - 1);

    s_fb_memblock = dk::MemBlockMaker{s_device, static_cast<uint32_t>(FB_NUM * fb_size)}
        .setFlags(DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
        .create();

    std::array<DkImage const*, FB_NUM> swapchain_images;
    for (unsigned i = 0; i < FB_NUM; i++) {
        s_framebuffers[i].initialize(fb_layout, s_fb_memblock, i * fb_size);
        swapchain_images[i] = &s_framebuffers[i];
    }

    s_swapchain = dk::SwapchainMaker{s_device, nwindowGetDefault(), swapchain_images}.create();

    for (unsigned i = 0; i < FB_NUM; i++) {
        dk::ImageView view{s_framebuffers[i]};
        s_cmdbuf.bindRenderTargets(&view);
        s_framebuffer_cmdlists[i] = s_cmdbuf.finishList();
    }

    s_cmdbuf.setViewports(0, DkViewport{0.0f, 0.0f, (float)FB_WIDTH, (float)FB_HEIGHT, 0.0f, 1.0f});
    s_cmdbuf.setScissors(0, DkScissor{0, 0, FB_WIDTH, FB_HEIGHT});
    s_cmdbuf.clearColor(0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 1.0f);
    s_render_cmdlist = s_cmdbuf.finishList();
}

void App::InitGraphics() {
    s_device = dk::DeviceMaker{}.create();
    s_queue = dk::QueueMaker{s_device}.setFlags(DkQueueFlags_Graphics).create();

    s_pool_images.emplace(s_device, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 16 * 1024 * 1024);
    s_pool_code.emplace(s_device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128 * 1024);
    s_pool_data.emplace(s_device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1 * 1024 * 1024);

    s_cmdbuf = dk::CmdBufMaker{s_device}.create();

    // Store the handle as a member — it must stay alive for the entire lifetime
    // of s_cmdbuf (which uses the underlying memory block).
    m_cmdmem = s_pool_data->allocate(CMDBUF_SIZE);
    s_cmdbuf.addMemory(m_cmdmem->getMemBlock(), m_cmdmem->getOffset(), m_cmdmem->getSize());

    CreateFramebufferResources();

    s_renderer.emplace(FB_WIDTH, FB_HEIGHT, s_device, s_queue, *s_pool_images, *s_pool_code, *s_pool_data);
    m_vg = nvgCreateDk(&*s_renderer, NVG_ANTIALIAS | NVG_STENCIL_STROKES);

    // Fonts
    PlFontData font_standard, font_extended;
    plGetSharedFontByType(&font_standard, PlSharedFontType_Standard);
    plGetSharedFontByType(&font_extended, PlSharedFontType_NintendoExt);

    int standard_font = -1;

    // Custom primary font
    {
        fs::FsNativeSd fs_font;
        if (R_SUCCEEDED(fs_font.read_entire_file("/config/morph/font.ttf", s_custom_font_data)) && !s_custom_font_data.empty()) {
            standard_font = nvgCreateFontMem(m_vg, "Standard",
                s_custom_font_data.data(), s_custom_font_data.size(), 0);
        }
    }

    if (standard_font < 0) {
        standard_font = nvgCreateFontMem(m_vg, "Standard",
            (unsigned char*)font_standard.address, font_standard.size, 0);
    }

    // Optional custom fallback
    {
        fs::FsNativeSd fs_font;
        if (R_SUCCEEDED(fs_font.read_entire_file("/config/morph/font_fallback.ttf", s_custom_fallback_font_data))
            && !s_custom_fallback_font_data.empty()) {
            auto fallback_font = nvgCreateFontMem(m_vg, "CustomFallback",
                s_custom_fallback_font_data.data(), s_custom_fallback_font_data.size(), 0);
            if (fallback_font >= 0) {
                nvgAddFallbackFontId(m_vg, standard_font, fallback_font);
            }
        }
    }

    // System fallback font — only needed when the primary "Standard" slot was
    // filled from a *custom* file.  If it was loaded from font_standard.address
    // directly, registering it again under a different name wastes a slot and
    // adds an unnecessary self-fallback cycle.
    if (standard_font >= 0 && !s_custom_font_data.empty()) {
        // Primary came from a custom TTF; add the system font as a true fallback.
        auto sys_fb = nvgCreateFontMem(m_vg, "SystemFB",
            (unsigned char*)font_standard.address, font_standard.size, 0);
        if (sys_fb >= 0) {
            nvgAddFallbackFontId(m_vg, standard_font, sys_fb);
        }
    }

    auto ext_font = nvgCreateFontMem(m_vg, "Extended",
        (unsigned char*)font_extended.address, font_extended.size, 0);
    if (ext_font >= 0) {
        nvgAddFallbackFontId(m_vg, standard_font, ext_font);
    }
}

void App::DestroyGraphics() {
    log_write("DestroyGraphics: start\n");

    if (s_queue) {
        s_queue.waitIdle();
        log_write("DestroyGraphics: queue idle\n");
    }

    FreeEntryImages();

    // NVG context must go before the command buffer it renders through.
    if (m_vg) {
        nvgDeleteDk(m_vg);
        m_vg = nullptr;
    }
    log_write("DestroyGraphics: NVG deleted\n");

    s_renderer.reset();
    log_write("DestroyGraphics: renderer reset\n");

    s_swapchain.destroy();
    log_write("DestroyGraphics: swapchain destroyed\n");

    m_cmdmem.reset();
    s_cmdbuf.destroy();
    for (auto& cl : s_framebuffer_cmdlists) cl = 0;
    s_render_cmdlist = 0;

    s_fb_memblock.destroy();

    s_pool_data.reset();
    s_pool_code.reset();
    s_pool_images.reset();

    // Closing the queue and device releases the GPU channel and NV services.
    s_queue.destroy();
    s_device.destroy();
    log_write("DestroyGraphics: device and queue destroyed\n");

    // Reset particle and animation state.
    s_particles_init = false;
    s_particles = {};

    m_anim_col = 0.f;
    m_anim_row = 0.f;
    m_tip_alpha = 0.f;
    m_tip_offset_y = 0.f;
    m_tip_prev_col = -1;
    m_tip_prev_row = -1;
    m_tip_idle_time = 0.f;
    m_tip_scroll_time = 0.f;

    log_write("DestroyGraphics: done\n");
}

void App::FreeEntryImages() {
    if (!m_vg) return;
    for (auto& e : m_entries) {
        if (e.image > 0) {
            nvgDeleteImage(m_vg, e.image);
            e.image = 0;
        }
    }
}

void App::ScanHomebrew() {
    // Free any GPU textures that were loaded for the previous entry list
    // before throwing the handles away.
    FreeEntryImages();
    m_entries.clear();
    nro_scan("/switch", m_entries);
    // Remember each homebrew's place in the scan, so un-starring puts it back there.
    for (std::size_t i = 0; i < m_entries.size(); i++) {
        m_entries[i].order = i;
    }
}

void App::Init() {
    log_file_init();
    log_write("=== MORPH2 START ===\n");

    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&m_pad);

    // Gyro: initialize all possible standard controller handles
    for (int i = 0; i < 4; i++) m_sixaxis_active[i] = false;

    // 0: Handheld mode (Switch Lite & Switch Handheld)
    if (R_SUCCEEDED(hidGetSixAxisSensorHandles(&m_sixaxis_handles[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld))) {
        if (R_SUCCEEDED(hidStartSixAxisSensor(m_sixaxis_handles[0]))) {
            m_sixaxis_active[0] = true;
        }
    }
    // 1: Pro Controller (Player 1 FullKey)
    if (R_SUCCEEDED(hidGetSixAxisSensorHandles(&m_sixaxis_handles[1], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey))) {
        if (R_SUCCEEDED(hidStartSixAxisSensor(m_sixaxis_handles[1]))) {
            m_sixaxis_active[1] = true;
        }
    }
    // 2 & 3: Joy-Con pair (Player 1 JoyDual: 2 handles = Left & Right)
    if (R_SUCCEEDED(hidGetSixAxisSensorHandles(&m_sixaxis_handles[2], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual))) {
        if (R_SUCCEEDED(hidStartSixAxisSensor(m_sixaxis_handles[2]))) {
            m_sixaxis_active[2] = true;
        }
        if (R_SUCCEEDED(hidStartSixAxisSensor(m_sixaxis_handles[3]))) {
            m_sixaxis_active[3] = true;
        }
    }

    log_write("Gyro init: handheld=%d, fullkey=%d, joydual_l=%d, joydual_r=%d\n",
              m_sixaxis_active[0], m_sixaxis_active[1], m_sixaxis_active[2], m_sixaxis_active[3]);

    hidInitializeTouchScreen();
    settings::Load();
    i18n::Init();
    InitGraphics();
    ScanHomebrew();
    for (auto& e : m_entries) {
        if (settings::IsFavorite(e.path.s)) e.has_star = true;
    }
    SortFavoritesFirst();

    m_last_tick = armGetSystemTick();
    log_write("Init complete\n");
}

void App::Shutdown() {
    log_write("Shutdown: start\n");
    for (int i = 0; i < 4; i++) {
        if (m_sixaxis_active[i]) {
            hidStopSixAxisSensor(m_sixaxis_handles[i]);
            m_sixaxis_active[i] = false;
        }
    }

    DestroyGraphics();
    log_file_exit();
}

void App::Poll() {
    padUpdate(&m_pad);

    m_gyro_x = 0.f;
    m_gyro_y = 0.f;

    HidSixAxisSensorState sixaxis{};
    u64 style_set = padGetStyleSet(&m_pad);
    bool read_success = false;

    if ((style_set & HidNpadStyleTag_NpadHandheld) && m_sixaxis_active[0]) {
        if (hidGetSixAxisSensorStates(m_sixaxis_handles[0], &sixaxis, 1) > 0) {
            read_success = true;
        }
    } else if ((style_set & HidNpadStyleTag_NpadFullKey) && m_sixaxis_active[1]) {
        if (hidGetSixAxisSensorStates(m_sixaxis_handles[1], &sixaxis, 1) > 0) {
            read_success = true;
        }
    } else if (style_set & HidNpadStyleTag_NpadJoyDual) {
        u64 attrib = padGetAttributes(&m_pad);
        if ((attrib & HidNpadAttribute_IsRightConnected) && m_sixaxis_active[3]) {
            if (hidGetSixAxisSensorStates(m_sixaxis_handles[3], &sixaxis, 1) > 0) {
                read_success = true;
            }
        } else if ((attrib & HidNpadAttribute_IsLeftConnected) && m_sixaxis_active[2]) {
            if (hidGetSixAxisSensorStates(m_sixaxis_handles[2], &sixaxis, 1) > 0) {
                read_success = true;
            }
        }
    }

    // Fallback: directly read Handheld handle (essential for Switch Lite)
    if (!read_success) {
        if (m_sixaxis_active[0] && hidGetSixAxisSensorStates(m_sixaxis_handles[0], &sixaxis, 1) > 0) {
            read_success = true;
        } else if (m_sixaxis_active[1] && hidGetSixAxisSensorStates(m_sixaxis_handles[1], &sixaxis, 1) > 0) {
            read_success = true;
        }
    }

    if (read_success) {
        m_gyro_x = sixaxis.angular_velocity.x;
        m_gyro_y = sixaxis.angular_velocity.y;
    }
}

void App::Draw() {
    auto slot = s_queue.acquireImage(s_swapchain);
    s_queue.submitCommands(s_framebuffer_cmdlists[slot]);
    s_queue.submitCommands(s_render_cmdlist);

    nvgBeginFrame(m_vg, FB_WIDTH, FB_HEIGHT, 1.0f);
    nvgFontFace(m_vg, "Standard");

    // Time of day
    {
        u64 ts = 0;
        TimeCalendarTime cal{};
        TimeCalendarAdditionalInfo info{};
        if (R_SUCCEEDED(timeGetCurrentTime(TimeType_LocalSystemClock, &ts))) {
            timeToCalendarTimeWithMyRule(ts, &cal, &info);
            m_time_of_day = ((float)cal.hour + (float)cal.minute / 60.f) / 24.f;
        }
    }

    u64 now = armGetSystemTick();
    float dt = (float)(now - m_last_tick) / (float)armGetSystemTickFreq();
    if (dt > 0.1f) dt = 0.016f;
    m_anim_time += dt;

    // Background
    // Night palette unless the optional time-of-day background is enabled.
    ui::DrawBackground(m_vg, FB_WIDTH, FB_HEIGHT,
                       settings::Get().time_bg ? m_time_of_day : 0.5f, // fixed noon: the theme shows its own colour
                       IsAppletMode(), m_anim_time, settings::Get().bg_preset, settings::Get().custom);

    // Particles
    if (!s_particles_init) {
        s_particles.Init(FB_WIDTH, FB_HEIGHT, 60);
        s_particles_init = true;
    }

    s_particles.Update(dt, m_gyro_x, m_gyro_y);
    s_particles.Draw(m_vg);

    ClampPage();
    UpdateInput(dt);
    UpdateTouch();
    UpdateAnimations(dt);

    DrawGrid();
    DrawPageArrows();
    DrawTooltip(dt);
    DrawLegend();
    DrawSettingsMenu(dt);
    if (m_picker_open) DrawPicker();

    nvgEndFrame(m_vg);
    s_queue.presentImage(s_swapchain, slot);
    m_last_tick = now;
}

int App::TotalEntries() const {
    return (int)m_entries.size();
}

int App::PageCount() const {
    const int n = TotalEntries();
    return n > 0 ? (n + kPerPage - 1) / kPerPage : 1;
}

int App::SelIndex() const {
    return m_page * kPerPage + m_sel_row * kCols + m_sel_col;
}

void App::ClampPage() {
    const int page_count = PageCount();
    if (m_page >= page_count) m_page = page_count - 1;
    if (m_page < 0) m_page = 0;
}

void App::GotoPage(int page) {
    if (page < 0 || page >= PageCount() || page == m_page) return;
    m_page = page;
    m_tip_prev_col = -1;  // retrigger tooltip
    m_snap_sel = true;    // cursor jumps, no glide across the page
}

// Cursor walks freely over empty cells; only the page edges change pages.
void App::MoveSel(int dc, int dr) {
    if (dr != 0) {
        const int nr = m_sel_row + dr;
        if (nr >= 0 && nr < kRows) m_sel_row = nr;
    }
    if (dc < 0) {
        if (m_sel_col > 0) {
            m_sel_col--;
        } else if (m_page > 0) {
            GotoPage(m_page - 1);
            m_sel_col = kCols - 1;
        }
    } else if (dc > 0) {
        if (m_sel_col < kCols - 1) {
            m_sel_col++;
        } else if (m_page + 1 < PageCount()) {
            GotoPage(m_page + 1);
            m_sel_col = 0;
        }
    }
}

void App::Launch(int idx) {
    if (idx < 0 || idx >= TotalEntries()) return;
    log_write("Launching: %s\n", m_entries[idx].path.s);
    log_write("envHasNextLoad: %d\n", envHasNextLoad());

    const Result rc = nro_launch(m_entries[idx].path);
    log_write("nro_launch rc: 0x%X\n", rc);

    if (R_SUCCEEDED(rc)) {
        log_write("Launch OK, exiting loop\n");
        // envSetNextLoad was already called inside nro_launch.
        // Mode 0 = do not return to HOS after exit.
        __nx_applet_exit_mode = 0;
        m_running = false;
    } else {
        log_write("Launch FAILED\n");
    }
}

// item: 0 = time-of-day background, 1 = background colour (dir = +1 / -1),
//       2 = hbmenu shortcut, 3 = shortcut for the selected homebrew.
// Menu item ids: 1 = background colour, 2 = hbmenu shortcut, 3 = shortcut for the selected homebrew.
// Applet mode hides the background colour row.
int App::MenuRowCount() const {
    return IsAppletMode() ? 2 : 3;
}

int App::MenuItemAt(int row) const {
    static constexpr int kIds[] = {1, 2, 3};
    return IsAppletMode() ? kIds[row + 1] : kIds[row];
}

void App::MenuActivate(int item, int dir) {
    auto& cfg = settings::Get();
    switch (item) {
    case 0:
        cfg.time_bg = !cfg.time_bg;
        settings::Save();
        break;
    case 1:
        OpenPicker();
        break;
    case 2:
        m_status = ShortcutResultText(CreateHbmenuShortcut());
        break;
    case 3: {
        const int idx = SelIndex();
        if (idx < 0 || idx >= TotalEntries()) {
            m_status = i18n::T(i18n::Str::StatusPickHomebrew);
            break;
        }
        m_status = ShortcutResultText(CreateShortcut(m_entries[idx]));
        break;
    }
    default:
        break;
    }
}

// The colour the background is showing right now: the picked one, or Ocean's top colour.
static themes::Rgb CurrentColour() {
    const int preset = themes::Clamp(settings::Get().bg_preset);
    return preset == themes::kCustomTheme ? settings::Get().custom : themes::kThemes[0].day_top;
}

void App::OpenPicker() {
    RgbToHsv(CurrentColour(), m_pick_h, m_pick_s, m_pick_v);
    m_picker_open = true;
}

// Type a colour with the Switch keyboard: six hex digits, e.g. 1E90FF.
void App::EditHexInPicker() {
    const auto c = CurrentColour();
    char initial[8];
    std::snprintf(initial, sizeof(initial), "%02X%02X%02X", c.r, c.g, c.b);

    SwkbdConfig kb{};
    if (R_FAILED(swkbdCreate(&kb, 0))) return;
    swkbdConfigMakePresetDefault(&kb);
    swkbdConfigSetHeaderText(&kb, i18n::T(i18n::Str::HexLabel));
    swkbdConfigSetInitialText(&kb, initial);
    swkbdConfigSetStringLenMax(&kb, 6);   // exactly six hex digits, no '#' to erase

    char out[16] = {};
    const Result rc = swkbdShow(&kb, out, sizeof(out));
    swkbdClose(&kb);
    if (R_FAILED(rc)) return;

    const char* digits = out[0] == '#' ? out + 1 : out;   // a leading '#' is accepted but never required
    unsigned r = 0, g = 0, b = 0;
    if (std::strlen(digits) != 6 || std::sscanf(digits, "%02x%02x%02x", &r, &g, &b) != 3) {
        return; // not a colour: keep the current one
    }

    auto& cfg = settings::Get();
    cfg.custom = {(unsigned char)r, (unsigned char)g, (unsigned char)b};
    cfg.bg_preset = themes::kCustomTheme;
    RgbToHsv(cfg.custom, m_pick_h, m_pick_s, m_pick_v);
}

// While the picker is open it owns the controls: sticks and d-pad move the colour live,
// B (or A) closes it and keeps the colour.
void App::UpdatePicker(float dt) {
    const u64 kd = padGetButtonsDown(&m_pad);
    if (kd & (HidNpadButton_B | HidNpadButton_Plus)) {
        m_picker_open = false;
        settings::Save();
        return;
    }
    if (kd & HidNpadButton_A) {
        EditHexInPicker();
        return;
    }

    auto axis = [](s32 raw) {
        constexpr float dz = 0.2f;
        const float v = raw / 32767.f;
        const float a = std::fabs(v);
        if (a < dz) return 0.f;
        const float r = (a - dz) / (1.f - dz);
        return v < 0.f ? -r : r;
    };
    const HidAnalogStickState ls = padGetStickPos(&m_pad, 0);
    const HidAnalogStickState rs = padGetStickPos(&m_pad, 1);
    const u64 kh = padGetButtons(&m_pad);

    // left stick (and d-pad): saturation / brightness
    float ds = axis(ls.x) + ((kh & HidNpadButton_Right) ? 1.f : 0.f) - ((kh & HidNpadButton_Left) ? 1.f : 0.f);
    float dv = axis(ls.y) + ((kh & HidNpadButton_Up) ? 1.f : 0.f) - ((kh & HidNpadButton_Down) ? 1.f : 0.f);
    const float dh = axis(rs.y);
    if (ds == 0.f && dv == 0.f && dh == 0.f) return; // nothing touched: keep the current colour

    m_pick_s = std::clamp(m_pick_s + ds * dt * 0.7f, 0.f, 1.f);
    m_pick_v = std::clamp(m_pick_v + dv * dt * 0.7f, 0.f, 1.f);
    m_pick_h = std::fmod(m_pick_h + dh * dt * 90.f + 360.f, 360.f); // right stick: hue, wraps

    auto& cfg = settings::Get();
    cfg.custom = themes::HsvToRgb(m_pick_h, m_pick_s, m_pick_v);
    cfg.bg_preset = themes::kCustomTheme;
}

void App::UpdateInput(float dt) {
    if (m_picker_open) {
        UpdatePicker(dt);
        return;
    }
    const int total_entries = TotalEntries();

    // ---- Buttons ----
    u64 kdown = padGetButtonsDown(&m_pad);
    u64 kheld = padGetButtons(&m_pad);

    if (kdown & HidNpadButton_Minus) {
        m_menu_open = !m_menu_open;
        m_menu_sel = 0;
    }
    if (kdown & (HidNpadButton_B | HidNpadButton_Plus)) {
        if (m_menu_open) {
            m_menu_open = false;
        } else {
            if (IsHbmenu()) {
                __nx_applet_exit_mode = 1;
            }
            m_running = false;
        }
    }

    // ---- Sticks ----
    // Left stick: analog cursor. Deadzone, dominant axis, a step fires on first deflection
    // and then repeats at a rate proportional to deflection.
    const s32 DZ = 16000;
    HidAnalogStickState ls = padGetStickPos(&m_pad, 0);
    HidAnalogStickState rs = padGetStickPos(&m_pad, 1);
    auto stick_axis = [](s32 raw) {
        constexpr float dz = 0.22f;
        float v = raw / 32767.f;
        float a = std::fabs(v);
        if (a < dz) return 0.f;
        float r = (a - dz) / (1.f - dz);
        return v < 0.f ? -r : r;
    };
    float ax = stick_axis(ls.x);
    float ay = stick_axis(ls.y);   // up is positive
    if (std::fabs(ax) > std::fabs(ay)) ay = 0.f; else ax = 0.f;

    if (ax == 0.f)            m_acc_x = 0.f;
    else if (m_prev_ax == 0.f) m_acc_x = ax > 0.f ? 1.f : -1.f;
    else                      m_acc_x += ax * 7.f * dt;

    if (ay == 0.f)            m_acc_y = 0.f;
    else if (m_prev_ay == 0.f) m_acc_y = ay > 0.f ? 1.f : -1.f;
    else                      m_acc_y += ay * 7.f * dt;

    int step_x = 0, step_y = 0;
    if (std::fabs(m_acc_x) >= 1.f) { step_x = m_acc_x > 0.f ? 1 : -1; m_acc_x = 0.f; }
    if (std::fabs(m_acc_y) >= 1.f) { step_y = m_acc_y > 0.f ? -1 : 1; m_acc_y = 0.f; } // up = row -1
    m_prev_ax = ax;
    m_prev_ay = ay;

    // Right stick: page flip
    int rdx = rs.x > DZ ? 1 : (rs.x < -DZ ? -1 : 0);

    // ---- D-pad: one step on press, then key-repeat while held ----
    int dp_x = 0, dp_y = 0; // dp_y: up = -1 (same convention as step_y)
    {
        // 1 = up, 2 = down, 3 = left, 4 = right
        int dir = 0;
        if (kheld & HidNpadButton_Up) dir = 1;
        else if (kheld & HidNpadButton_Down) dir = 2;
        else if (kheld & HidNpadButton_Left) dir = 3;
        else if (kheld & HidNpadButton_Right) dir = 4;

        bool fire = false;
        if (dir == 0) {
            m_dp_dir = 0;
            m_dp_t = 0.f;
        } else if (dir != m_dp_dir) {
            m_dp_dir = dir;
            m_dp_t = 0.35f; // initial delay before repeat
            fire = true;
        } else {
            m_dp_t -= dt;
            if (m_dp_t <= 0.f) {
                m_dp_t = 0.09f; // repeat interval
                fire = true;
            }
        }
        if (fire) {
            if (dir == 1) dp_y = -1;
            else if (dir == 2) dp_y = 1;
            else if (dir == 3) dp_x = -1;
            else if (dir == 4) dp_x = 1;
        }
    }

    if (m_menu_open) {
        // ---- Menu input (modal: grid ignores input while open) ----
        if (dp_y < 0 || step_y < 0) m_menu_sel = std::max(0, m_menu_sel - 1);
        if (dp_y > 0 || step_y > 0) m_menu_sel = std::min(MenuRowCount() - 1, m_menu_sel + 1);
        m_menu_sel = std::min(m_menu_sel, MenuRowCount() - 1);
        if (kdown & HidNpadButton_A) MenuActivate(MenuItemAt(m_menu_sel), 1);
    } else {
        // ---- Grid input ----
        if (kdown & HidNpadButton_A) {
            Launch(SelIndex());
        }
        if (dp_x || dp_y) MoveSel(dp_x, dp_y);
        if (step_x || step_y) MoveSel(step_x, step_y);

        if (kdown & HidNpadButton_ZL) GotoPage(m_page - 1);
        if (kdown & HidNpadButton_ZR) GotoPage(m_page + 1);
        if (rdx && rdx != m_rstick_prev) GotoPage(m_page + rdx);

        if (kdown & HidNpadButton_StickR) {
            int idx = SelIndex();
            if (idx < total_entries) {
                auto& e = m_entries[idx];
                e.has_star = !e.has_star.value_or(false);
                settings::SetFavorite(e.path.s, *e.has_star);

                // The cursor stays on the same cell; the list re-flows under it.
                SortFavoritesFirst();
            }
        }
    }
    m_rstick_prev = rdx;

    m_in.ax = ax;
    m_in.ay = ay;
    m_in.rdx = rdx;
}

void App::UpdateTouch() {
    if (m_picker_open) return;
    const int page_count = PageCount();
    const int total_entries = TotalEntries();

    // ---- Touch ----
    // Grid: tap selects instantly (tap again to launch); empty cells can be selected too.
    //       Horizontal swipe changes page.
    // Menu: tap an item to activate it, tap outside to close.
    HidTouchScreenState ts{};
    s32 touch_n = hidGetTouchScreenStates(&ts, 1);
    bool touching = touch_n > 0 && ts.count > 0;

    if (touching) {
        float tx = (float)ts.touches[0].x;
        float ty = (float)ts.touches[0].y;
        if (!m_touch_down) {
            m_touch_down = true;
            m_touch_moved = false;
            m_dragging = false;
            m_touch_sx = m_touch_cx = tx;
            m_touch_sy = ty;
        }
        m_touch_cx = tx;
        float dx = tx - m_touch_sx;
        float dy = ty - m_touch_sy;
        if (!m_touch_moved && (std::fabs(dx) > 12.f || std::fabs(dy) > 12.f)) m_touch_moved = true;

        if (!m_menu_open) {
            m_dragging = m_touch_moved && std::fabs(dx) > std::fabs(dy);
            float d = dx;
            if ((d > 0.f && m_page == 0) || (d < 0.f && m_page + 1 >= page_count)) d *= 0.3f;
            m_drag_px = m_dragging ? d : 0.f;
        }
    } else if (m_touch_down) {
        m_touch_down = false;

        if (m_menu_open) {
            bool inside = m_touch_sx >= kMenuX && m_touch_sx <= kMenuX + kMenuW &&
                          m_touch_sy >= kMenuY && m_touch_sy <= kMenuY + kMenuH;
            if (!m_touch_moved) {
                if (!inside) {
                    m_menu_open = false;
                } else {
                    float rel = m_touch_sy - (kMenuY + 70.f);
                    int i = (int)std::floor(rel / kMenuRowH);
                    float within = rel - i * kMenuRowH;
                    if (rel >= 0.f && i < MenuRowCount() && within <= 56.f) {
                        m_menu_sel = i;
                        MenuActivate(MenuItemAt(i), 1);
                    }
                }
            }
        } else if (m_dragging) {
            float dx = m_touch_cx - m_touch_sx;
            if (dx < -120.f)     GotoPage(m_page + 1);
            else if (dx > 120.f) GotoPage(m_page - 1);
            m_dragging = false;
            m_drag_px = 0.f;
        } else if (!m_touch_moved) {
            float ox = kGridX + m_page * kPageW - m_scroll;
            int tap_r = -1, tap_c = -1;
            for (int r = 0; r < kRows && tap_r < 0; r++) {
                for (int c = 0; c < kCols; c++) {
                    float cx = ox + c * (kCellSize + kCellGap);
                    float cy = kGridY + r * (kCellSize + kCellGap);
                    if (m_touch_sx >= cx && m_touch_sx <= cx + kCellSize &&
                        m_touch_sy >= cy && m_touch_sy <= cy + kCellSize) {
                        tap_r = r;
                        tap_c = c;
                        break;
                    }
                }
            }
            if (tap_r >= 0) {
                int idx = m_page * kPerPage + tap_r * kCols + tap_c;
                if (idx < total_entries && tap_r == m_sel_row && tap_c == m_sel_col) {
                    Launch(idx);
                } else {
                    m_sel_row = tap_r;
                    m_sel_col = tap_c;
                    m_anim_col = (float)tap_c; // cursor jumps, scale animates
                    m_anim_row = (float)tap_r;
                }
            }
        }
    }
}

void App::UpdateAnimations(float dt) {
    // ---- Scroll animation (snappy exponential ease toward the current page) ----
    if (m_dragging) {
        m_scroll = m_page * kPageW - m_drag_px;
    } else {
        m_scroll += (m_page * kPageW - m_scroll) * (1.f - std::exp(-22.f * dt));
    }

    // ---- Selection animation (snaps after page change / tap, glides otherwise) ----
    float lerp_speed = 8.f * dt;
    if (m_snap_sel) {
        m_anim_col = (float)m_sel_col;
        m_anim_row = (float)m_sel_row;
    } else {
        m_anim_col = ui::Lerp(m_anim_col, (float)m_sel_col, lerp_speed);
        m_anim_row = ui::Lerp(m_anim_row, (float)m_sel_row, lerp_speed);
    }

    for (int r = 0; r < kRows; r++) {
        for (int c = 0; c < kCols; c++) {
            int slot = r * kCols + c;
            float dist = std::sqrt((m_anim_col - c) * (m_anim_col - c) + (m_anim_row - r) * (m_anim_row - r));
            float target = (dist < 0.5f) ? 1.f : 0.f;
            m_anim_scale[slot] = m_snap_sel ? target : ui::Lerp(m_anim_scale[slot], target, lerp_speed);
        }
    }
    m_snap_sel = false;
}

 // Five-point star in the corner of a favourite cell.
static void DrawStarBadge(NVGcontext* vg, float cx, float cy, float outer) {
    // Dark disc behind the star: white icons would otherwise swallow a white star.
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, outer + 5.f);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 110));
    nvgFill(vg);

    // A star is concave, and this renderer fills concave paths badly (it showed up as a square).
    // Build it from convex pieces instead: a central pentagon plus five spike triangles.
    constexpr int kPoints = 5;
    constexpr float kInnerRatio = 0.45f;
    const float pi = 3.14159265f;
    float ox[kPoints], oy[kPoints], ix[kPoints], iy[kPoints];
    for (int i = 0; i < kPoints; i++) {
        const float ao = -pi / 2.f + i * 2.f * pi / kPoints;
        const float ai = ao + pi / kPoints;
        ox[i] = cx + outer * std::cos(ao);
        oy[i] = cy + outer * std::sin(ao);
        ix[i] = cx + outer * kInnerRatio * std::cos(ai);
        iy[i] = cy + outer * kInnerRatio * std::sin(ai);
    }

    nvgFillColor(vg, nvgRGBA(255, 255, 255, 200));
    nvgBeginPath(vg);
    for (int i = 0; i < kPoints; i++) {
        if (i == 0) nvgMoveTo(vg, ix[i], iy[i]); else nvgLineTo(vg, ix[i], iy[i]);
    }
    nvgClosePath(vg);
    nvgFill(vg);

    for (int i = 0; i < kPoints; i++) {
        const int prev = (i + kPoints - 1) % kPoints;
        nvgBeginPath(vg);
        nvgMoveTo(vg, ix[prev], iy[prev]);
        nvgLineTo(vg, ox[i], oy[i]);
        nvgLineTo(vg, ix[i], iy[i]);
        nvgClosePath(vg);
        nvgFill(vg);
    }
}

// Starred entries first; within each group the original scan order is kept.
void App::SortFavoritesFirst() {
    std::sort(m_entries.begin(), m_entries.end(), [](const NroEntry& a, const NroEntry& b) {
        const bool sa = a.has_star.value_or(false);
        const bool sb = b.has_star.value_or(false);
        if (sa != sb) return sa;
        return a.order < b.order;
    });
}

void App::DrawPicker() {
    constexpr float px = 340.f, py = 130.f, pw = 600.f, ph = 460.f;
    constexpr float sx = px + 28.f, sy = py + 64.f, sw = 360.f, sh = 290.f;
    constexpr float hx = sx + sw + 30.f, hw = 26.f;

    nvgBeginPath(m_vg);
    nvgRoundedRect(m_vg, px, py, pw, ph, 16.f);
    nvgFillColor(m_vg, nvgRGBA(8, 8, 10, 245));
    nvgFill(m_vg);

    nvgFontFace(m_vg, "Standard");
    nvgTextAlign(m_vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFontSize(m_vg, 24.f);
    nvgFillColor(m_vg, nvgRGBA(230, 238, 255, 255));
    nvgText(m_vg, px + 24.f, py + 18.f, i18n::T(i18n::Str::CustomColour), nullptr);

    // saturation / brightness square: white -> hue horizontally, transparent -> black vertically
    const auto hue = themes::HsvToRgb(m_pick_h, 1.f, 1.f);
    nvgBeginPath(m_vg);
    nvgRect(m_vg, sx, sy, sw, sh);
    nvgFillPaint(m_vg, nvgLinearGradient(m_vg, sx, sy, sx + sw, sy,
                                         nvgRGBA(255, 255, 255, 255), nvgRGBA(hue.r, hue.g, hue.b, 255)));
    nvgFill(m_vg);
    nvgBeginPath(m_vg);
    nvgRect(m_vg, sx, sy, sw, sh);
    nvgFillPaint(m_vg, nvgLinearGradient(m_vg, sx, sy, sx, sy + sh,
                                         nvgRGBA(0, 0, 0, 0), nvgRGBA(0, 0, 0, 255)));
    nvgFill(m_vg);

    // cursor on the square
    nvgBeginPath(m_vg);
    nvgCircle(m_vg, sx + m_pick_s * sw, sy + (1.f - m_pick_v) * sh, 9.f);
    nvgStrokeColor(m_vg, nvgRGBA(255, 255, 255, 230));
    nvgStrokeWidth(m_vg, 2.f);
    nvgStroke(m_vg);

    // hue bar: hue 0 at the bottom, 360 at the top (same direction as the right stick)
    for (int i = 0; i < 6; i++) {
        const auto c0 = themes::HsvToRgb(i * 60.f, 1.f, 1.f);
        const auto c1 = themes::HsvToRgb((i + 1) * 60.f, 1.f, 1.f);
        const float y0 = sy + sh - (i + 1) * sh / 6.f;
        const float y1 = sy + sh - i * sh / 6.f;
        nvgBeginPath(m_vg);
        nvgRect(m_vg, hx, y0, hw, y1 - y0);
        nvgFillPaint(m_vg, nvgLinearGradient(m_vg, hx, y0, hx, y1,
                                             nvgRGBA(c1.r, c1.g, c1.b, 255), nvgRGBA(c0.r, c0.g, c0.b, 255)));
        nvgFill(m_vg);
    }
    const float my = sy + sh - m_pick_h / 360.f * sh;
    nvgBeginPath(m_vg);
    nvgRect(m_vg, hx - 4.f, my - 3.f, hw + 8.f, 6.f);
    nvgFillColor(m_vg, nvgRGBA(255, 255, 255, 230));
    nvgFill(m_vg);

    // preview and the HEX of what is applied right now
    const auto cur = CurrentColour();
    nvgBeginPath(m_vg);
    nvgRoundedRect(m_vg, px + pw - 150.f, sy, 126.f, 70.f, 10.f);
    nvgFillColor(m_vg, nvgRGBA(cur.r, cur.g, cur.b, 255));
    nvgFill(m_vg);

    nvgFontSize(m_vg, 15.f);
    nvgFillColor(m_vg, nvgRGBA(255, 255, 255, 160));
    nvgText(m_vg, px + pw - 150.f, sy + 84.f, i18n::T(i18n::Str::HexLabel), nullptr);
    nvgBeginPath(m_vg);
    nvgRoundedRect(m_vg, px + pw - 150.f, sy + 104.f, 126.f, 44.f, 8.f);
    nvgFillColor(m_vg, nvgRGBA(20, 20, 24, 255));
    nvgFill(m_vg);
    char hex[16];
    std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", cur.r, cur.g, cur.b);
    nvgFontSize(m_vg, 22.f);
    nvgTextAlign(m_vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(m_vg, nvgRGBA(240, 245, 255, 255));
    nvgText(m_vg, px + pw - 150.f + 63.f, sy + 126.f, hex, nullptr);
    nvgTextAlign(m_vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    nvgFontSize(m_vg, 15.f);
    nvgFillColor(m_vg, nvgRGBA(255, 255, 255, 140));
    nvgText(m_vg, px + 24.f, py + ph - 34.f, i18n::T(i18n::Str::PickerHint), nullptr);
}

void App::DrawGrid() {
    const int total_entries = TotalEntries();
    const int page_count = PageCount();

    // ---- Draw all visible pages ----
    for (int p = 0; p < page_count; p++) {
        float ox = kGridX + p * kPageW - m_scroll;
        if (ox + kGridW < -40.f || ox > kPageW + 40.f) continue;

        for (int r = 0; r < kRows; r++) {
            for (int c = 0; c < kCols; c++) {
                int slot = r * kCols + c;
                int idx = p * kPerPage + slot;
                float cell_x = ox + c * (kCellSize + kCellGap);
                float cell_cy = kGridY + r * (kCellSize + kCellGap) + kCellSize / 2.f;
                float h = kCellSize;
                float cy_new = cell_cy;

                int icon = 0;
                if (idx < total_entries) {
                    auto& e = m_entries[idx];
                    if (!e.image && e.icon_size && e.icon_offset) {
                        auto icon_data = nro_get_icon(e.path, e.icon_size, e.icon_offset);
                        if (!icon_data.empty()) {
                            auto img = ImageLoadFromMemory(icon_data, ImageFlag_JPEG);
                            if (!img.data.empty()) {
                                e.image = nvgCreateImageRGBA(m_vg, img.w, img.h, 0, img.data.data());
                            }
                        }
                    }
                    icon = e.image;
                }

                float sel = 0.f;
                if (p == m_page) {
                    sel = m_anim_scale[slot];
                }
                ui::DrawGlassCell(m_vg, cell_x, cy_new - h / 2.f, kCellSize, h, 14.f,
                                  sel, m_gyro_x, m_gyro_y,
                                  nullptr, icon, m_anim_time + idx * 0.4f);
                if (idx < total_entries && m_entries[idx].has_star.value_or(false)) {
                    DrawStarBadge(m_vg, cell_x + kCellSize - 26.f, cy_new - h / 2.f + 26.f, 18.f);
                }
            }
        }
    }
}

void App::DrawPageArrows() {
    const int page_count = PageCount();

    // ---- Page arrows (only when another page exists that way) ----
    {
        float pulse = 0.5f + 0.5f * std::sin(m_anim_time * 4.f);
        auto arrow = [&](float x, float y, float dir) {
            float s = 14.f;
            nvgBeginPath(m_vg);
            nvgMoveTo(m_vg, x - dir * s * 0.5f, y - s);
            nvgLineTo(m_vg, x + dir * s * 0.5f, y);
            nvgLineTo(m_vg, x - dir * s * 0.5f, y + s);
            nvgClosePath(m_vg);
            nvgFillColor(m_vg, nvgRGBA(255, 255, 255, (unsigned char)(90 + 120 * pulse)));
            nvgFill(m_vg);
        };
        if (m_page + 1 < page_count) arrow(FB_WIDTH - 22.f, FB_HEIGHT / 2.f, 1.f);
        if (m_page > 0)              arrow(22.f, FB_HEIGHT / 2.f, -1.f);
    }
}

void App::DrawTooltip(float dt) {
    const int total_entries = TotalEntries();

    // ---- Tooltip for the selected cell ----
    {
        bool changed = (m_sel_col != m_tip_prev_col || m_sel_row != m_tip_prev_row);
        if (changed) {
            m_tip_alpha = 0.f;
            m_tip_offset_y = -15.f;
            m_tip_idle_time = 0.f;
            m_tip_scroll_time = 0.f;
            m_tip_prev_col = m_sel_col;
            m_tip_prev_row = m_sel_row;
        }

        m_tip_idle_time += dt;

        if (m_tip_idle_time > 0.4f) {
            m_tip_alpha = ui::Lerp(m_tip_alpha, 1.f, 5.f * dt);
            m_tip_offset_y = ui::Lerp(m_tip_offset_y, 0.f, 8.f * dt);
        }

        int sel_idx = SelIndex();
        if (m_tip_alpha > 0.05f && sel_idx < total_entries && !m_dragging && !m_menu_open) {
            auto& e = m_entries[sel_idx];
            float ox = kGridX + m_page * kPageW - m_scroll;
            float tx = ox + m_sel_col * (kCellSize + kCellGap);
            float cy = kGridY + m_sel_row * (kCellSize + kCellGap);
            float base_ty = cy - 58.f;
            bool show_below = (base_ty < 10.f);
            float ty = show_below
                ? (cy + kCellSize + 8.f - m_tip_offset_y)
                : (base_ty + m_tip_offset_y);

            nvgGlobalAlpha(m_vg, m_tip_alpha);
            float tw = kCellSize + 60.f;
            ui::DrawTooltip(m_vg, tx - 30.f, ty, tw, 50.f,
                            tx + kCellSize / 2.f, e.GetName(), e.GetAuthor(), show_below,
                            m_tip_scroll_time);
            nvgGlobalAlpha(m_vg, 1.f);
        }
    }
}

void App::DrawLegend() {
    // Button legend
    {
        nvgSave(m_vg);
        nvgFontFace(m_vg, "Standard");
        nvgFontSize(m_vg, 13.f);
        nvgFillColor(m_vg, nvgRGBA(255, 255, 255, 80));
        nvgTextAlign(m_vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
        nvgText(m_vg, 640.f, 708.f, i18n::T(i18n::Str::Legend), nullptr);
        nvgRestore(m_vg);
    }
}

void App::DrawSettingsMenu(float dt) {
    const int total_entries = TotalEntries();
    const int page_count = PageCount();

    // Settings menu: slides in from the left, eased toward open/closed
    m_menu_anim += ((m_menu_open ? 1.f : 0.f) - m_menu_anim) * (1.f - std::exp(-14.f * dt));
    if (m_menu_anim > 0.002f) {
        constexpr float py = kMenuY, pw = kMenuW, ph = kMenuH;
        float a = m_menu_anim;
        float px = kMenuX - (1.f - a) * (kMenuW + 80.f);

        nvgBeginPath(m_vg);
        nvgRoundedRect(m_vg, px, py, pw, ph, 16.f);
        nvgFillColor(m_vg, nvgRGBA(8, 8, 10, (unsigned char)(240 * a)));
        nvgFill(m_vg);
        nvgStrokeColor(m_vg, nvgRGBA(255, 255, 255, (unsigned char)(40 * a)));
        nvgStrokeWidth(m_vg, 1.f);
        nvgStroke(m_vg);

        nvgFontFace(m_vg, "Standard");
        nvgTextAlign(m_vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFontSize(m_vg, 26.f);
        nvgFillColor(m_vg, nvgRGBA(230, 238, 255, (unsigned char)(255 * a)));
        nvgText(m_vg, px + 24.f, py + 20.f, i18n::T(i18n::Str::Settings), nullptr);

        auto pill = [&](float y, bool on) {
            float bw = 66.f, bh = 28.f;
            float bx = px + pw - 32.f - bw;
            float by = y + 14.f;
            nvgBeginPath(m_vg);
            nvgRoundedRect(m_vg, bx, by, bw, bh, 14.f);
            nvgFillColor(m_vg, on ? nvgRGBA(90, 210, 140, 220) : nvgRGBA(120, 120, 140, 130));
            nvgFill(m_vg);
            nvgFontSize(m_vg, 14.f);
            nvgTextAlign(m_vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(m_vg, nvgRGBA(255, 255, 255, 255));
            nvgText(m_vg, bx + bw / 2.f, by + bh / 2.f, on ? i18n::T(i18n::Str::On) : i18n::T(i18n::Str::Off), nullptr);
            nvgTextAlign(m_vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        };

        for (int i = 0; i < MenuRowCount(); i++) {
            float y = py + 70.f + i * kMenuRowH;
            bool sel = (i == m_menu_sel);

            nvgBeginPath(m_vg);
            nvgRoundedRect(m_vg, px + 16.f, y, pw - 32.f, 56.f, 12.f);
            nvgFillColor(m_vg, sel ? nvgRGBA(255, 255, 255, 40) : nvgRGBA(255, 255, 255, 12));
            nvgFill(m_vg);
            if (sel) {
                nvgStrokeColor(m_vg, nvgRGBA(255, 255, 255, 120));
                nvgStrokeWidth(m_vg, 1.5f);
                nvgStroke(m_vg);
            }

            nvgFontSize(m_vg, 19.f);
            nvgFillColor(m_vg, nvgRGBA(255, 255, 255, 255));
            switch (MenuItemAt(i)) {
            case 1: {
                nvgText(m_vg, px + 32.f, y + 18.f, i18n::T(i18n::Str::BgColor), nullptr);
                char buf[16];
                const auto c = CurrentColour();
                std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
                nvgTextAlign(m_vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
                nvgText(m_vg, px + pw - 32.f, y + 18.f, buf, nullptr);
                nvgTextAlign(m_vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                break;
            }
            case 2:
                nvgText(m_vg, px + 32.f, y + 18.f, i18n::T(i18n::Str::ShortcutHbmenu), nullptr);
                break;
            case 3: {
                int idx = SelIndex();
                if (m_name_key != idx) {
                    m_name_key = idx;
                    m_name_scroll_t = 0.f;
                }
                const char* lead = i18n::T(i18n::Str::ShortcutCreateFor);
                float lx = px + 32.f, ly = y + 19.f;
                nvgFontSize(m_vg, 17.f);
                nvgFillColor(m_vg, nvgRGBA(255, 255, 255, 255));
                nvgText(m_vg, lx, ly, lead, nullptr);
                if (idx < total_entries) {
                    float adv = nvgTextBounds(m_vg, lx, ly, lead, nullptr, nullptr);
                    auto& e = m_entries[idx];
                    float ix = lx + adv + 10.f;
                    if (e.image > 0) {
                        nvgBeginPath(m_vg);
                        nvgRoundedRect(m_vg, ix, y + 15.f, 26.f, 26.f, 6.f);
                        nvgFillPaint(m_vg, nvgImagePattern(m_vg, ix, y + 15.f, 26.f, 26.f, 0, e.image, 1.f));
                        nvgFill(m_vg);
                    }

                    const char* name = e.GetName();
                    float nx = ix + 34.f;
                    float avail = (px + pw - 24.f) - nx;
                    nvgFontSize(m_vg, 18.f);
                    float nw = nvgTextBounds(m_vg, 0.f, 0.f, name, nullptr, nullptr);
                    m_name_scroll_t += dt;

                    // Overflow: scroll in a loop (pause, then move), clipped to the box
                    constexpr float kMarqueeGap = 40.f;
                    float off = 0.f;
                    bool overflow = nw > avail;
                    if (overflow) {
                        float period = nw + kMarqueeGap;
                        float t = std::max(0.f, m_name_scroll_t - 1.2f);
                        off = std::fmod(t * 30.f, period);
                    }

                    nvgSave(m_vg);
                    nvgScissor(m_vg, nx, y + 6.f, avail, 44.f);
                    // Bold: two passes 0.8px apart
                    for (int pass = 0; pass < 2; pass++) {
                        float bx = nx - off + pass * 0.8f;
                        nvgFillColor(m_vg, nvgRGBA(255, 255, 255, 255));
                        nvgText(m_vg, bx, ly + 1.f, name, nullptr);
                        if (overflow) {
                            nvgText(m_vg, bx + nw + kMarqueeGap, ly + 1.f, name, nullptr);
                        }
                    }
                    nvgRestore(m_vg);
                } else {
                    nvgFillColor(m_vg, nvgRGBA(255, 255, 255, 110));
                    nvgText(m_vg, lx, y + 36.f, i18n::T(i18n::Str::ShortcutPickInGrid), nullptr);
                }
                break;
            }
            default:
                break;
            }
        }

        // Status of the last action (stays inside the menu)
        if (!m_status.empty()) {
            nvgFontSize(m_vg, 16.f);
            nvgFillColor(m_vg, nvgRGBA(200, 230, 255, (unsigned char)(230 * a)));
            nvgText(m_vg, px + 24.f, py + ph - 70.f, m_status.c_str(), nullptr);
        }
        nvgFontSize(m_vg, 14.f);
        nvgFillColor(m_vg, nvgRGBA(255, 255, 255, (unsigned char)(110 * a)));
        nvgText(m_vg, px + 24.f, py + ph - 34.f, i18n::T(i18n::Str::MenuFooter), nullptr);
    }
}

void App::Run() {
    this->Init();

    while (this->m_running && appletMainLoop()) {
        this->Poll();
        this->Draw();
    }

    morph::s_queue.waitIdle();
    log_write("Run: calling Shutdown\n");
    Shutdown();
    log_write("Run: Shutdown complete, returning from Run()\n");
}

} // namespace morph
