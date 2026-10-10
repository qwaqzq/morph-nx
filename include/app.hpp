#pragma once

#include <switch.h>
#include <vector>
#include <string>
#include <optional>
#include <nanovg/framework/CMemPool.h>

struct NVGcontext;
#include "nro.hpp"

namespace morph {

// Grid layout constants.
static constexpr int GRID_COLS = 6;
static constexpr int GRID_ROWS = 3;
static constexpr int GRID_CELLS = GRID_COLS * GRID_ROWS; // 18

class App {
public:
    explicit App(const char* argv0 = "");
    ~App();

    void Run();

    static auto GetApp() -> App* { return s_instance; }
    NVGcontext* GetVg() const { return m_vg; }
    float GetGyroX() const { return m_gyro_x; }
    float GetGyroY() const { return m_gyro_y; }

private:
    // Per-frame input snapshot, filled by UpdateInput() and read by the debug overlay.
    struct FrameInput {
        float ax{}, ay{};   // left stick after deadzone (up is positive)
        int rdx{};          // right stick: -1 / 0 / 1
    };

    // ---- lifecycle ----
    void Init();
    void Poll();
    void Draw();
    void Shutdown();
    void ScanHomebrew();

    // Deletes all NVG image handles stored in m_entries, then clears the vector.
    void FreeEntryImages();

    void InitGraphics();
    void DestroyGraphics();

    // ---- selection / paging ----
    int  TotalEntries() const;
    int  PageCount() const;
    int  SelIndex() const;
    void ClampPage();
    void GotoPage(int page);
    void MoveSel(int dc, int dr);
    void Launch(int idx);

    // ---- settings menu ----
    void MenuActivate(int item, int dir);
    // Applet mode hides the background-colour row; rows map to item ids through these.
    int  MenuRowCount() const;
    int  MenuItemAt(int row) const;

    // ---- per-frame update ----
    void UpdateInput(float dt);
    void UpdateTouch();
    void UpdateAnimations(float dt);

    // ---- per-frame drawing ----
    void DrawGrid();

    // Favourites (R3): starred homebrew are kept at the front of the grid.
    void SortFavoritesFirst();

    // Custom colour picker (opened with Y on the background-colour row).
    void OpenPicker();
    void EditHexInPicker();
    void UpdatePicker(float dt);
    void DrawPicker();
    float m_pick_h{}, m_pick_s{}, m_pick_v{};
    bool  m_picker_open{false};
    void DrawPageArrows();
    void DrawTooltip(float dt);
    void DrawLegend();
    void DrawSettingsMenu(float dt);

    NVGcontext* m_vg{};
    PadState m_pad{};
    HidSixAxisSensorHandle m_sixaxis_handles[4]{};
    bool m_sixaxis_active[4]{false, false, false, false};
    float m_gyro_x{};
    float m_gyro_y{};
    float m_time_of_day{};
    float m_anim_time{};
    FrameInput m_in{};

    // Paging / touch / stick state
    int   m_page{0};
    float m_scroll{0.f};        // horizontal scroll in px; m_page * FB_WIDTH when at rest
    float m_drag_px{0.f};       // live finger offset while swiping
    bool  m_dragging{false};
    bool  m_touch_down{false};
    bool  m_touch_moved{false};
    float m_touch_sx{}, m_touch_sy{}, m_touch_cx{};
    int   m_rstick_prev{0};
    int   m_dp_dir{0};          // d-pad direction currently held (0 = none)
    float m_dp_t{0.f};          // time until the next d-pad repeat
    float m_acc_x{0.f}, m_acc_y{0.f}, m_prev_ax{0.f}, m_prev_ay{0.f};

    // Settings menu
    bool  m_menu_open{};
    int   m_menu_sel{0};
    float m_menu_anim{0.f};     // 0 = hidden, 1 = open (slide-in)
    std::string m_status{};     // result of the last menu action, shown inside the menu
    float m_name_scroll_t{0.f}; // marquee time for the shortcut name
    int   m_name_key{-1};

    // Cursor and cell animation
    int   m_sel_col{0};
    int   m_sel_row{0};
    bool  m_snap_sel{false};    // jump selection instead of gliding
    // Sized to GRID_CELLS so it can never silently go out of bounds.
    float m_anim_scale[GRID_CELLS]{};

    // Homebrew entries
    std::vector<NroEntry> m_entries;

    bool m_running{true};
    u64 m_last_tick{};
    char m_app_path[FS_MAX_PATH]{};

    bool IsAppletMode() const {
        return appletGetAppletType() != AppletType_Application;
    }

    bool IsHbmenu() const {
        return m_app_path[0] != '\0' &&
               strcasecmp(m_app_path, "/hbmenu.nro") == 0;
    }

    // Owns the command-buffer backing memory so it lives as long as the cmdbuf.
    std::optional<CMemPool::Handle> m_cmdmem;

    // Tooltip & grid animation state — kept as members so they reset on Init().
    float m_anim_col{0.f};
    float m_anim_row{0.f};
    float m_tip_alpha{0.f};
    float m_tip_offset_y{0.f};
    int   m_tip_prev_col{-1};
    int   m_tip_prev_row{-1};
    float m_tip_idle_time{0.f};
    float m_tip_scroll_time{0.f}; // resets when selection changes

    static App* s_instance;
};

} // namespace morph
