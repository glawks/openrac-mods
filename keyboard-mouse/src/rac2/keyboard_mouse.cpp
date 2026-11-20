// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Experimental RAC2 path. Uses only host input, pad reports and play phases.
// No RAC1 game addresses or camera hook are used here. Direct camera,
// inventory/number keys and pixel-perfect RAC2 menu hover need separate work.

#include "openrac/games/rac2.hpp"
#include "openrac/mod.hpp"
#include "../kbm.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace kbm_rac2 {
namespace {

using kbm::Action;
using kbm::Context;

// This experimental hit test is deliberately limited to RAC2 NTSC's
// front-end main list. It never changes gameplay state, weapon selection,
// or the existing RAC1 mouse-menu implementation.
struct MainMenuHit {
    int selected = -1;
    int target = -1; // -1 = pointer outside all rows
};

bool plausible_guest(std::uint32_t address) {
    return address >= 0x00010000u && address < 0x01FF0000u
           && (address & 3u) == 0;
}

// RAC2's native menu is operated by pad directions. Give mouse motion a
// deliberate, single d-pad step rather than pretending a big drag is dozens
// of consecutive joystick presses. Small lateral jitter must not interfere
// with moving vertically through a list.
struct MenuMotion {
    float vertical = 0.0f;
    float horizontal = 0.0f;
    int cooldown = 0;

    void reset() {
        vertical = horizontal = 0.0f;
        cooldown = 0;
    }

    std::uint16_t step(float dx, float dy, float pixels_per_step) {
        if (!std::isfinite(dx) || !std::isfinite(dy)) {
            reset();
            return 0;
        }
        if (cooldown > 0) {
            --cooldown;
            // A long swipe cannot queue directions to fire later.
            vertical = horizontal = 0.0f;
            return 0;
        }

        const float limit = std::clamp(pixels_per_step, 30.0f, 200.0f);
        const float horizontal_limit = limit * 1.8f;
        const float ax = std::fabs(dx), ay = std::fabs(dy);
        if (ay > 0.0f && (ay >= ax * 0.45f || ax < 12.0f)) {
            vertical = std::clamp(vertical + std::clamp(dy, -limit, limit),
                                  -limit, limit);
            horizontal = 0.0f;
        } else if (ax >= 24.0f && ax >= ay * 3.0f) {
            // Only a deliberate, mostly horizontal motion changes a value.
            horizontal = std::clamp(horizontal + std::clamp(dx, -horizontal_limit, horizontal_limit),
                                    -horizontal_limit, horizontal_limit);
            vertical = 0.0f;
        } else {
            // Ignore small sideways drift; retain modest vertical intent.
            vertical = std::clamp(vertical + std::clamp(dy, -15.0f, 15.0f),
                                  -limit, limit);
            horizontal *= 0.5f;
        }
        if (std::fabs(vertical) >= limit) {
            const auto direction = vertical > 0.0f ? kbm::kDown : kbm::kUp;
            vertical = horizontal = 0.0f;
            cooldown = 12;  // release the button before another gesture
            return direction;
        }
        if (std::fabs(horizontal) >= horizontal_limit) {
            const auto direction = horizontal > 0.0f ? kbm::kRight : kbm::kLeft;
            vertical = horizontal = 0.0f;
            cooldown = 12;
            return direction;
        }
        return 0;
    }
};

class Keyboard {
public:
    explicit Keyboard(openrac::Mod& mod) : mod_(mod) {
        binds_ = kbm::read_bindings([&](const char* key) {
            return mod.setting<std::string>(key);
        });
        move_settings_.walk = static_cast<float>(mod.setting<double>("walk_speed"));
        move_settings_.rise_ticks =
            static_cast<float>(mod.setting<double>("stick_ramp_ms")) * 60.0f / 1000.0f;
        look_settings_.sensitivity =
            static_cast<float>(mod.setting<double>("mouse_sensitivity"));
        look_settings_.acceleration =
            static_cast<float>(mod.setting<double>("mouse_acceleration"));
        look_settings_.invert_y = mod.setting<bool>("invert_y");
        capture_ = mod.setting<bool>("capture_cursor");
        menu_mouse_ = mod.setting<bool>("menu_mouse");
        menu_step_pixels_ = static_cast<float>(mod.setting<double>("menu_step_pixels"));
        ring_dead_zone_ =
            static_cast<float>(mod.setting<double>("quick_select_dead_zone"));
    }

    void on_pad(int port, std::uint8_t* report, int size) {
        if (port != 0 || report == nullptr || size < 6) return;
        const openrac_input* in = mod_.api().input();
        if (in == nullptr) return;

        // A game can read the same pad more than once in one tick.
        if (in->tick != tick_) {
            tick_ = in->tick;
            step(*in, report);
        }
        // A game can change mode between two pad reads in the same host
        // tick. Do not rely on step()'s cached menu/play context to maintain
        // the physical Quick Select button: the game must always see
        // Triangle while its key is held in ordinary play or wheel mode.
        kbm::Virtual outgoing = pad_;
        const auto status = mod_.play_status();
        if (status.level >= 0) {
            const int mode = mod_.memory().read<std::int32_t>(game_mode_address());
            if (mode == 4) {
                // A cached movement or camera axis from earlier in the tick
                // must never carry over into the open weapon wheel.
                outgoing.right_x = outgoing.right_y = 0.0f;
                if (!quick_select_held(*in)) {
                    outgoing.left_x = outgoing.left_y = 0.0f;
                }
            }
            if ((mode == 0 || mode == 4) && quick_select_held(*in))
                outgoing.held = static_cast<std::uint16_t>(
                    outgoing.held | kbm::kTriangle);
        }
        kbm::merge(outgoing, report, size);
    }

private:
    // Read the player's configured Quick Select binding directly from the
    // physical device, without Context::Menu/Play latching or mouse capture
    // checks. A quick-select held across a menu-mode transition must not
    // be treated as a new press, nor silently blocked until it is released.
    bool quick_select_held(const openrac_input& in) const {
        if (!in.focused || in.overlay) return false;
        for (int key : binds_.of(Action::QuickSelect)) {
            if (key != 0 && openrac_bind_held(&in, key))
                return true;
        }
        return false;
    }

    bool bound(const openrac_input& in, Action action, bool play) const {
        if (!in.focused || in.overlay) return false;
        const bool menu = kbm::kActions[static_cast<int>(action)].group == kbm::Group::Menu;
        if (menu && play) return false;
        for (int key : binds_.of(action)) {
            if (key == 0) continue;
            if (kbm::is_mouse(key)) {
                // A held Quick Select mouse button must survive a transient
                // loss of relative mouse capture as RAC2 opens the wheel.
                // All other gameplay mouse actions still require capture.
                const bool wheel = play && action == Action::QuickSelect;
                if (play ? (!wheel && !in.captured) : !in.pointer_inside) continue;
            }
            if (openrac_bind_held(&in, key) || openrac_bind_pressed(&in, key))
                return true;
        }
        return false;
    }

    bool pressed(const openrac_input& in, Action action, bool play) const {
        if (!in.focused || in.overlay) return false;
        for (int key : binds_.of(action)) {
            if (key == 0) continue;
            if (kbm::is_mouse(key)
                && (play ? !in.captured : !in.pointer_inside)) continue;
            if (openrac_bind_pressed(&in, key)) return true;
        }
        return false;
    }

    // RAC2's menu-state globals have version-specific addresses. Both the
    // current-screen pointer and the GUI instance are set once the game's
    // interactive menu is active. Before that, its Press Start title needs
    // Start rather than Cross. These are read-only checks of the game's state.
    std::pair<std::uint32_t, std::uint32_t> frontend_state() const {
        const auto disc = mod_.version();
        const openrac::gaddr menu =
            disc == "ntsc2" ? 0x001F27C0u : 0x001F2840u;
        const openrac::gaddr gui =
            disc == "pal" ? 0x001A8DACu
            : disc == "ntsc2" ? 0x001A8D04u
                               : 0x001A8CFCu;
        return {
            mod_.memory().read<std::uint32_t>(menu + 0x14u),
            mod_.memory().read<std::uint32_t>(gui)
        };
    }

    // RAC2 NTSC main-menu list, grounded in OpenRAC's mod_pages.c and
    // rac2-decomp's option_list_draw_rows/list_menu_update. The list is
    // GUI+0x3E430; the child row renderer is list+0x188. Its on-screen
    // position pointer (+0x4C), row baseline (+0xA8), step (+0xAC), and
    // count (+0xB0) are read from the actual running menu.
    std::optional<MainMenuHit> main_menu_hit(const openrac_input& in) const {
        if (!menu_mouse_ || mod_.version() != "ntsc") return std::nullopt;
        const auto [shown, gui] = frontend_state();
        if (!plausible_guest(shown) || !plausible_guest(gui)) return std::nullopt;

        constexpr std::uint32_t kOriginalMainItems = 0x00261540u;
        constexpr std::uint32_t kOptionsItem = kOriginalMainItems + 2u * 20u + 12u;
        const auto options_page = mod_.memory().read<std::uint32_t>(kOptionsItem);
        if (!plausible_guest(options_page)) return std::nullopt;
        const auto main_page = mod_.memory().read<std::uint32_t>(options_page + 0xE0u);
        if (!plausible_guest(main_page) || shown != main_page)
            return std::nullopt;

        const auto list = gui + 0x3E430u;
        if (!plausible_guest(list) || !plausible_guest(list + 0x238u))
            return std::nullopt;
        const auto child = list + 0x188u;
        const int count = mod_.memory().read<std::int32_t>(child + 0xB0u);
        const int selected = mod_.memory().read<std::int32_t>(list + 0x16Cu);
        const int row_base = mod_.memory().read<std::int32_t>(child + 0xA8u);
        const int row_step = mod_.memory().read<std::int32_t>(child + 0xACu);
        const auto position = mod_.memory().read<std::uint32_t>(child + 0x4Cu);
        // Discard an uninitialised/animated page or implausible structures.
        if (count < 2 || count > 16 || selected < 0 || selected >= count
            || row_base < -400 || row_base > 400 || row_step < 18 || row_step > 120
            || !plausible_guest(position))
            return std::nullopt;
        const float x = mod_.memory().read<float>(position);
        const float y = mod_.memory().read<float>(position + 4u);
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0f || x > 512.0f
            || y < -300.0f || y > 500.0f)
            return std::nullopt;

        MainMenuHit result{selected, -1};
        if (!in.focused || in.overlay || !in.pointer_inside
            || !std::isfinite(in.pointer_x) || !std::isfinite(in.pointer_y)
            || in.pointer_x < 0.0f || in.pointer_x > 1.0f
            || in.pointer_y < 0.0f || in.pointer_y > 1.0f)
            return result;

        const float px = in.pointer_x * kbm::kScreenWidth;
        const float py = in.pointer_y * kbm::kScreenHeight;
        // The rows have a visual highlight but no full-width button widget.
        // Keep the hit region near their labels and avoid overlapping rows.
        const float half_height =
            std::clamp(static_cast<float>(row_step) * 0.38f, 9.0f, 23.0f);
        if (std::fabs(px - x) > 100.0f) return result;
        for (int i = 0; i < count; ++i) {
            const float middle = y + static_cast<float>(row_base + row_step * i) + 8.0f;
            if (std::fabs(py - middle) <= half_height) {
                result.target = i;
                break;
            }
        }
        return result;
    }

    bool mouse_confirm_held(const openrac_input& in) const {
        if (!in.focused || in.overlay || !in.pointer_inside) return false;
        for (int bind : binds_.of(Action::MenuConfirm))
            if (bind != 0 && kbm::is_mouse(bind) && openrac_bind_held(&in, bind))
                return true;
        return false;
    }

    bool mouse_confirm_pressed(const openrac_input& in) const {
        if (!in.focused || in.overlay || !in.pointer_inside) return false;
        for (int bind : binds_.of(Action::MenuConfirm))
            if (bind != 0 && kbm::is_mouse(bind) && openrac_bind_pressed(&in, bind))
                return true;
        return false;
    }

    bool keyboard_confirm_held(const openrac_input& in) const {
        for (const auto action : {Action::MenuConfirm, Action::Jump}) {
            for (int bind : binds_.of(action))
                if (bind != 0 && !kbm::is_mouse(bind) && openrac_bind_held(&in, bind))
                    return true;
        }
        return false;
    }

    void reset_main_hover() {
        hover_release_ = false;
        hover_pending_ = -1;
        hover_pending_ticks_ = 0;
        hover_logged_target_ = -2;
    }

    void apply_main_hover(const openrac_input& in, const MainMenuHit& hit,
                          kbm::Virtual& v) {
        const bool click = mouse_confirm_pressed(in) && !block_confirm_;
        const bool mouse_down = mouse_confirm_held(in);
        const bool keyboard_confirm = keyboard_confirm_held(in);

        // A mouse click must not activate a different highlighted item,
        // nor should clicking empty background activate the old selection.
        if (mouse_down && !keyboard_confirm)
            v.held = static_cast<std::uint16_t>(v.held & ~kbm::kCross);
        if (hover_pending_ != -1 && hover_pending_ != hit.target)
            hover_pending_ = -1;

        if (hit.target != hover_logged_target_) {
            hover_logged_target_ = hit.target;
            if (hit.target >= 0)
                mod_.info("RAC2 main hover: row=" + std::to_string(hit.target)
                          + ", selected=" + std::to_string(hit.selected));
        }
        if (hit.target < 0) {
            hover_release_ = false;
            hover_pending_ = -1;
            return;
        }

        if (click && (hit.selected != hit.target || hover_release_)) {
            hover_pending_ = hit.target;
            hover_pending_ticks_ = 36; // allow animation / multi-row movement
        }

        // Navigate using the game's normal d-pad, one pulse then one release.
        // We always read the game's actual selection before deciding whether
        // to step again; never write to its cursor or widget pointers.
        if (hit.selected != hit.target) {
            if (!hover_release_) {
                v.held = static_cast<std::uint16_t>(
                    v.held | (hit.selected < hit.target ? kbm::kDown : kbm::kUp));
                hover_release_ = true;
            } else {
                hover_release_ = false;
            }
            if (hover_pending_ >= 0 && --hover_pending_ticks_ <= 0)
                hover_pending_ = -1;
            return;
        }

        // Confirm on a later tick, after the menu has actually highlighted
        // the clicked row and the d-pad's previous press has been released.
        if (hover_release_) {
            hover_release_ = false;
            return;
        }
        if (hover_pending_ == hit.target && !block_confirm_) {
            v.held = static_cast<std::uint16_t>(v.held | kbm::kCross);
            hover_pending_ = -1;
        } else if (click) {
            v.held = static_cast<std::uint16_t>(v.held | kbm::kCross);
        }
    }

    // The boot executable's g_nGameState is *not* its address after a
    // level's program loads. OpenRAC's C host always uses
    // OPENRAC_DATA(RAC2_g_nGameState) to access the live state.
    // Resolve that relocation for every read rather than caching the
    // boot address (which can contain unrelated data in a level).
    openrac::gaddr game_mode_address() const {
        const openrac::gaddr boot =
            mod_.version() == "pal" ? 0x001A8C60u : 0x001A8BB0u;
        return mod_.relocate(boot);
    }

    // These globals are the same ones RAC2 saves as its eight Quick Select
    // assignments. The actual equip request is a hook point, not a native
    // function reliably discoverable by its human-readable name.
    std::array<std::int32_t, kbm::kQuickSelectSlots> quick_slots() const {
        const openrac::gaddr base =
            mod_.version() == "pal" ? 0x001A7438u : 0x001A73B8u;
        std::array<std::int32_t, kbm::kQuickSelectSlots> slots{};
        for (std::size_t i = 0; i < slots.size(); ++i)
            slots[i] = mod_.memory().read<std::int32_t>(base + 4u * static_cast<openrac::gaddr>(i));
        return slots;
    }

    bool item_owned(int item) const {
        // RAC2 has 56 item IDs; never call the game's equip function with
        // unowned/invalid items from empty or stale slots.
        if (item <= 0 || item >= 56) return false;
        const openrac::gaddr base =
            mod_.version() == "pal" ? 0x001A7B80u
            : mod_.version() == "ntsc2" ? 0x001A7B00u
                                         : 0x001A7AF8u;
        return mod_.memory().read<std::uint8_t>(base + static_cast<openrac::gaddr>(item)) != 0;
    }

    int currently_equipped() const {
        // Hero's first item slot (+0x28 is its item ID).
        const openrac::gaddr hero =
            (mod_.version() == "pal" ? 0x00189F20u : 0x00189EA0u) - 0x80u;
        return mod_.memory().read<std::int32_t>(hero + 0x1220u + 0x28u);
    }

    void equip_item(int item, std::uint32_t tick) {
        if (!item_owned(item)) return;
        using Equip = openrac::rac2::Item_Equip;
        // Native function("Item_Equip") is not exported by this game.
        // Take the known RAC2 hook point at mod load and call its original
        // chain when the player requests a weapon. This works on the pinned
        // SDK (before hook_entry was added in interface 1.21), without
        // writing into hero/item memory or opening Quick Select.
        const auto equip = mod_.original<Equip>();
        if (equip == nullptr) {
            if (!warned_equip_) {
                mod_.warn("RAC2 Item_Equip hook original unavailable; weapon switching disabled");
                warned_equip_ = true;
            }
            return;
        }
        const int result = equip(item);
        mod_.info("RAC2 equip request: item=" + std::to_string(item)
            + ", result=" + std::to_string(result));
        if (result != 0) {
            requested_item_ = item;
            requested_tick_ = tick;
        }
    }

    // A wheel event can contain several notches: honour configured bindings.
    int steps_pressed(const openrac_input& in, Action action) const {
        if (!in.focused || in.overlay || !in.captured) return 0;
        int count = 0;
        for (int bind : binds_.of(action)) {
            if (!bind) continue;
            const float wheel = openrac_bind_wheel(&in, bind);
            if (wheel >= 0.5f)
                count = std::max(count, std::clamp(static_cast<int>(std::ceil(wheel)), 1, 4));
            else if (openrac_bind_pressed(&in, bind))
                count = std::max(count, 1);
        }
        return count;
    }

    void weapon_keys(const openrac_input& in) {
        if (!in.focused || in.overlay || !in.captured) return;
        // Only in actual on-foot gameplay. Mode 4 is the weapon wheel, and
        // another mode can be a pause menu, vendor or dialog.
        if (mod_.memory().read<std::int32_t>(game_mode_address()) != 0) return;
        auto slots = quick_slots();
        for (int i = 0; i < kbm::kQuickSelectSlots; ++i) {
            const Action action = static_cast<Action>(static_cast<int>(Action::Weapon1) + i);
            if (!pressed(in, action, true)) continue;
            const int item = slots[static_cast<std::size_t>(i)];
            mod_.info("RAC2 weapon key " + std::to_string(i + 1)
                + ": item=" + std::to_string(item)
                + (item_owned(item) ? " owned" : " empty/unowned"));
            if (item_owned(item)) equip_item(item, in.tick);
            return;
        }
        // Ignore items in the ring that are not owned or no longer valid.
        for (auto& item : slots)
            if (!item_owned(item)) item = 0;
        const int forward = steps_pressed(in, Action::WeaponNext);
        const int backward = steps_pressed(in, Action::WeaponPrev);
        if (forward == backward) return;
        const int direction = forward > backward ? 1 : -1;
        const int count = std::max(forward, backward);
        int current = currently_equipped();
        if (requested_item_ > 0 && in.tick - requested_tick_ < 12)
            current = requested_item_;
        int next = current;
        for (int n = 0; n < count; ++n)
            next = kbm::cycle_item(slots, next, direction);
        mod_.info("RAC2 weapon wheel: " + std::to_string(count)
            + (direction > 0 ? " next" : " previous")
            + ", current=" + std::to_string(current)
            + ", requested=" + std::to_string(next));
        if (next > 0 && next != current) equip_item(next, in.tick);
    }

    void step(const openrac_input& in, const std::uint8_t* report) {
        const auto status = mod_.play_status();
        const int phase = status.phase;
        const int mode = status.level >= 0
            ? mod_.memory().read<std::int32_t>(game_mode_address()) : -1;
        // The wheel's mode is the source of truth, not the play-status
        // classifier. It can change between pad polls within a host tick.
        const bool quick_select_mode = status.level >= 0 && mode == 4;
        // The game mode wins over a stale phase reading within a frame:
        // mode 3 is a genuine menu, never ordinary gameplay.
        const bool play = (phase == OPENRAC_PLAY_PLAYING && mode == 0)
                          || quick_select_mode;
        const bool physical_wheel = quick_select_held(in);
        if (physical_wheel) {
            // Only log transitions, not every frame, so an intermittent
            // unexpected mode change can be diagnosed from the game log.
            if (last_wheel_mode_ != mode || last_wheel_phase_ != phase) {
                mod_.info("RAC2 Quick Select held: mode=" + std::to_string(mode)
                    + ", phase=" + std::to_string(phase)
                    + ", captured=" + std::to_string(static_cast<int>(in.captured)));
                last_wheel_mode_ = mode;
                last_wheel_phase_ = phase;
            }
        } else {
            last_wheel_mode_ = -2;
            last_wheel_phase_ = -2;
        }
        const bool menu = (phase == OPENRAC_PLAY_MENU && !quick_select_mode)
                          || phase == OPENRAC_PLAY_FRONT_END;
        const bool scene = phase == OPENRAC_PLAY_SCENE;
        const int input_phase = quick_select_mode ? OPENRAC_PLAY_PLAYING : phase;

        const bool want_capture = capture_ && play && in.focused && !in.overlay;
        if (requested_capture_ != static_cast<int>(want_capture)
            || (want_capture && !in.captured)) {
            // Retry when another front-end transition released capture
            // without changing our requested state.
            mod_.capture_mouse(want_capture);
            requested_capture_ = static_cast<int>(want_capture);
        }

        // No carryover presses when changing gameplay, scene or menu.
        if (input_phase != previous_phase_) {
            keys_ = kbm::KeyStick{};
            look_.reset();
            ring_.reset();
            ring_open_ = false;
            repeats_ = {};
            menu_motion_.reset();
            reset_main_hover();
            requested_item_ = 0;
            previous_phase_ = input_phase;
        }

        kbm::ActionSet held = 0;
        for (int i = 0; i < kbm::kActionCount; ++i) {
            const auto action = static_cast<Action>(i);
            if (bound(in, action, play))
                held |= kbm::bit(action);
        }
        const Context context = play ? Context::Play
                                : scene ? Context::Cutscene
                                        : Context::Menu;
        held = latch_.filter(context, held);
        // Quick Select is a hold action, not a click. In RAC2 the mode
        // changes as the wheel opens, and a context latch can incorrectly
        // suppress a still-held RMB. Reinsert it only in gameplay/wheel.
        if (play && physical_wheel)
            held |= kbm::bit(Action::QuickSelect);

        // The port labels both the Press Start title and the actual menu
        // FRONT_END, but they do not accept the same confirm button.
        bool interactive_frontend = false;
        if (phase == OPENRAC_PLAY_FRONT_END) {
            const auto [screen, gui] = frontend_state();
            interactive_frontend = screen != 0 && gui != 0;
            const int state = interactive_frontend ? 1 : 0;
            const bool confirm_held =
                (held & (kbm::bit(Action::Jump) | kbm::bit(Action::MenuConfirm))) != 0;
            if (state != last_frontend_) {
                // A press that advances from Press Start must be released
                // before it can choose New Game from the new menu.
                if (last_frontend_ >= 0 && confirm_held)
                    block_confirm_ = true;
                last_frontend_ = state;
                mod_.info(std::string("RAC2 front end: ")
                    + (interactive_frontend ? "menu" : "Press Start")
                    + " (screen=" + std::to_string(screen)
                    + ", gui=" + std::to_string(gui) + ")");
            }
            if (!confirm_held) block_confirm_ = false;
            if (block_confirm_)
                held &= ~(kbm::bit(Action::Jump) | kbm::bit(Action::MenuConfirm));
        } else {
            last_frontend_ = -1;
            block_confirm_ = false;
        }
        const auto on = [&](Action a) {
            return (held & kbm::bit(a)) != 0;
        };

        kbm::Virtual v{};
        if (play) {
            v.held = kbm::buttons_for(Context::Play, held, -1);

            const bool ring = on(Action::QuickSelect) && physical_wheel;
            if (ring || quick_select_mode) {
                if (ring) {
                    v.held = static_cast<std::uint16_t>(v.held | kbm::kTriangle);
                    if (!ring_open_) ring_.reset();
                    // When capture flickers off, keep the wheel held. The
                    // previous direction is retained until it returns.
                    if (in.captured)
                        ring_.add(in.mouse_dx, in.mouse_dy, ring_dead_zone_);
                    const auto direction = ring_.stick(ring_dead_zone_);
                    v.left_x = direction.first;
                    v.left_y = direction.second;
                }
                // Never turn WASD into walking while RAC2's weapon wheel is
                // on-screen, even if its mouse button disappeared for a tick.
                keys_ = kbm::KeyStick{};
            } else {
                keys_.step(on(Action::MoveForward), on(Action::MoveBack),
                           on(Action::MoveLeft), on(Action::MoveRight),
                           on(Action::Walk), move_settings_);
                v.left_x = keys_.x;
                v.left_y = keys_.y;
            }
            ring_open_ = ring;
            if (!ring && !quick_select_mode)
                weapon_keys(in);

            // RAC2 has no exposed follow-camera override yet: emulate the
            // controller's right stick, with no persisted vertical tilt.
            const bool pad_look = report[2] < 88 || report[2] > 168
                                || report[3] < 88 || report[3] > 168;
            if (in.captured && !ring && !quick_select_mode && !pad_look) {
                look_.step(in.mouse_dx, in.mouse_dy, true, look_settings_);
                v.right_x = look_.x;
                v.right_y = look_.y;
            } else {
                look_.reset();
            }
        } else if (menu) {
            v.held = kbm::buttons_for(Context::Menu, held, -1);
            if (phase == OPENRAC_PLAY_FRONT_END) {
                if (!interactive_frontend) {
                    // The Press Start/title screen accepts Start, not Cross.
                    v.held = static_cast<std::uint16_t>(v.held & ~kbm::kCross);
                    if (on(Action::MenuConfirm) || on(Action::Jump) || on(Action::Pause))
                        v.held = static_cast<std::uint16_t>(v.held | kbm::kStart);
                } else {
                    // In New Game / Load / Options, Cross selects and
                    // Triangle goes back. Never press Start at the same
                    // time as Cross. P remains an explicit Start fallback,
                    // useful for checking the reported screen state.
                    if (openrac_bind_held(&in, 19)) { // USB HID KeyP
                        v.held = static_cast<std::uint16_t>(
                            (v.held & ~kbm::kTriangle) | kbm::kStart);
                    }
                }
                if (pressed(in, Action::MenuConfirm, false)
                    || pressed(in, Action::Jump, false)) {
                    mod_.info(std::string("RAC2 confirm: ")
                        + (interactive_frontend ? "Cross/menu" : "Start/title")
                        + (block_confirm_ ? " (waiting for release)" : ""));
                }
            }
            const std::array<Action, 4> directions = {
                Action::MenuUp, Action::MenuDown,
                Action::MenuLeft, Action::MenuRight
            };
            const std::array<Action, 4> movement = {
                Action::MoveForward, Action::MoveBack,
                Action::MoveLeft, Action::MoveRight
            };
            const std::array<std::uint16_t, 4> buttons = {
                kbm::kUp, kbm::kDown, kbm::kLeft, kbm::kRight
            };
            for (std::size_t i = 0; i < directions.size(); ++i) {
                const bool direction = on(directions[i]) || on(movement[i]);
                // A wheel notch lasts one tick; repeated keys stay held.
                if (direction && repeats_[i].step(direction))
                    v.held = static_cast<std::uint16_t>(v.held | buttons[i]);
                if (!direction) repeats_[i].step(false);
            }

            // First, target RAC2 NTSC's actual title-list rows by pointer
            // position. Other discs and submenus retain the tested joystick
            // navigation fallback until their layouts are verified.
            const auto hovered =
                phase == OPENRAC_PLAY_FRONT_END && interactive_frontend
                    ? main_menu_hit(in) : std::nullopt;
            if (hovered.has_value()) {
                menu_motion_.reset();
                apply_main_hover(in, *hovered, v);
            } else {
                reset_main_hover();
                const bool interactive_menu =
                    phase != OPENRAC_PLAY_FRONT_END || interactive_frontend;
                if (interactive_menu && menu_mouse_ && in.pointer_inside
                    && in.focused && !in.overlay) {
                    const float dx = phase == OPENRAC_PLAY_FRONT_END ? 0.0f : in.mouse_dx;
                    v.held = static_cast<std::uint16_t>(
                        v.held | menu_motion_.step(dx, in.mouse_dy, menu_step_pixels_));
                } else {
                    menu_motion_.reset();
                }
            }
        } else if (scene) {
            v.held = kbm::buttons_for(Context::Cutscene, held, -1);
        }

        pad_ = v;
    }

    openrac::Mod& mod_;
    kbm::Bindings binds_{};
    kbm::MoveTuning move_settings_{};
    kbm::LookTuning look_settings_{};
    kbm::KeyStick keys_{};
    kbm::MouseLook look_{};
    kbm::RingPointer ring_{};
    kbm::Latch latch_{};
    MenuMotion menu_motion_{};
    std::array<kbm::Repeat, 4> repeats_{};
    kbm::Virtual pad_{};
    std::uint32_t tick_ = std::numeric_limits<std::uint32_t>::max();
    int previous_phase_ = -1;
    int requested_capture_ = -1;
    int last_frontend_ = -1;
    bool block_confirm_ = false;
    bool capture_ = true;
    bool menu_mouse_ = true;
    bool hover_release_ = false;
    int hover_pending_ = -1;
    int hover_pending_ticks_ = 0;
    int hover_logged_target_ = -2;
    float menu_step_pixels_ = 100.0f;
    bool ring_open_ = false;
    bool warned_equip_ = false;
    int last_wheel_mode_ = -2;
    int last_wheel_phase_ = -2;
    int requested_item_ = 0;
    std::uint32_t requested_tick_ = 0;
    float ring_dead_zone_ = 30.0f;
};

}  // namespace

void load(openrac::Mod& mod) {
    if (!mod.has_input() || !mod.has_play())
        throw openrac::Error("RAC2 keyboard/mouse requires the input and play APIs");
    // A passthrough hook retains the port's Item_Equip original, which we
    // can invoke from the keyboard and mouse pad callback. The normal
    // in-game equip path is passed through unchanged.
    mod.hook<openrac::rac2::Item_Equip>(
        [](auto original, std::int32_t item) -> std::int32_t {
            return original(item);
        });
    auto keyboard = std::make_shared<Keyboard>(mod);
    mod.take_input(OPENRAC_INPUT_KEYBOARD | OPENRAC_INPUT_MOUSE);
    mod.on_pad([keyboard](int port, std::uint8_t* report, int size) {
        keyboard->on_pad(port, report, size);
    });
    mod.info("RAC2 experimental keyboard/mouse (NTSC main-menu hover and click prototype)");
}

}  // namespace kbm_rac2
