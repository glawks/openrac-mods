// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Experimental RAC2 path. Uses only host input, pad reports and play phases.
// No RAC1 game addresses or camera hook are used here. Direct camera,
// inventory/number keys and pixel-perfect RAC2 menu hover need separate work.

#include "openrac/mod.hpp"
#include "../kbm.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

namespace kbm_rac2 {
namespace {

using kbm::Action;
using kbm::Context;

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
        kbm::merge(pad_, report, size);
    }

private:
    bool bound(const openrac_input& in, Action action, bool play) const {
        if (!in.focused || in.overlay) return false;
        const bool menu = kbm::kActions[static_cast<int>(action)].group == kbm::Group::Menu;
        if (menu && play) return false;
        for (int key : binds_.of(action)) {
            if (key == 0) continue;
            if (kbm::is_mouse(key)) {
                // Pointer input only with the captured mouse in play, or over
                // the game picture in a menu.
                if (play ? !in.captured : !in.pointer_inside) continue;
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

    void step(const openrac_input& in, const std::uint8_t* report) {
        const int phase = mod_.play_status().phase;
        const bool play = phase == OPENRAC_PLAY_PLAYING;
        const bool menu = phase == OPENRAC_PLAY_MENU || phase == OPENRAC_PLAY_FRONT_END;
        const bool scene = phase == OPENRAC_PLAY_SCENE;

        const bool want_capture = capture_ && play && in.focused && !in.overlay;
        if (requested_capture_ != static_cast<int>(want_capture)) {
            mod_.capture_mouse(want_capture);
            requested_capture_ = static_cast<int>(want_capture);
        }

        // No carryover presses when changing gameplay, scene or menu.
        if (phase != previous_phase_) {
            keys_ = kbm::KeyStick{};
            look_.reset();
            ring_.reset();
            ring_open_ = false;
            pulses_.clear();
            repeats_ = {};
            previous_phase_ = phase;
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
        const auto on = [&](Action a) {
            return (held & kbm::bit(a)) != 0;
        };

        kbm::Virtual v{};
        if (play) {
            v.held = kbm::buttons_for(Context::Play, held, -1);

            const bool ring = on(Action::QuickSelect);
            if (ring) {
                v.held = static_cast<std::uint16_t>(v.held | kbm::kTriangle);
                if (!ring_open_) ring_.reset();
                if (in.captured) ring_.add(in.mouse_dx, in.mouse_dy, ring_dead_zone_);
                const auto direction = ring_.stick(ring_dead_zone_);
                // To be validated in RAC2: whether this ring reads left or right stick.
                v.left_x = direction.first;
                v.left_y = direction.second;
                keys_ = kbm::KeyStick{};
            } else {
                keys_.step(on(Action::MoveForward), on(Action::MoveBack),
                           on(Action::MoveLeft), on(Action::MoveRight),
                           on(Action::Walk), move_settings_);
                v.left_x = keys_.x;
                v.left_y = keys_.y;
            }
            ring_open_ = ring;

            // RAC2 has no exposed follow-camera override yet: emulate the
            // controller's right stick, with no persisted vertical tilt.
            const bool pad_look = report[2] < 88 || report[2] > 168
                                || report[3] < 88 || report[3] > 168;
            if (in.captured && !ring && !pad_look) {
                look_.step(in.mouse_dx, in.mouse_dy, true, look_settings_);
                v.right_x = look_.x;
                v.right_y = look_.y;
            } else {
                look_.reset();
            }
        } else if (menu) {
            v.held = kbm::buttons_for(Context::Menu, held, -1);
            if (phase == OPENRAC_PLAY_FRONT_END
                && (on(Action::MenuConfirm) || on(Action::Jump))) {
                // Title's Press Start prompt; menus also take Cross.
                v.held = static_cast<std::uint16_t>(v.held | kbm::kStart);
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
        } else if (scene) {
            v.held = kbm::buttons_for(Context::Cutscene, held, -1);
        }

        // Weapon hotkeys and wheel remain RAC1-only until RAC2's item
        // structures and two-page quick select are verified.
        pad_ = v;
    }

    openrac::Mod& mod_;
    kbm::Bindings binds_{};
    kbm::MoveTuning move_settings_{};
    kbm::LookTuning look_settings_{};
    kbm::KeyStick keys_{};
    kbm::MouseLook look_{};
    kbm::Ring ring_{};
    kbm::Latch latch_{};
    kbm::Pulser pulses_{};
    std::array<kbm::Repeat, 4> repeats_{};
    kbm::Virtual pad_{};
    std::uint32_t tick_ = std::numeric_limits<std::uint32_t>::max();
    int previous_phase_ = -1;
    int requested_capture_ = -1;
    bool capture_ = true;
    bool ring_open_ = false;
    float ring_dead_zone_ = 30.0f;
};

}  // namespace

void load(openrac::Mod& mod) {
    if (!mod.has_input() || !mod.has_play())
        throw openrac::Error("RAC2 keyboard/mouse requires the input and play APIs");
    auto keyboard = std::make_shared<Keyboard>(mod);
    mod.take_input(OPENRAC_INPUT_KEYBOARD | OPENRAC_INPUT_MOUSE);
    mod.on_pad([keyboard](int port, std::uint8_t* report, int size) {
        keyboard->on_pad(port, report, size);
    });
    mod.info("RAC2 experimental keyboard/mouse (pad-based camera; hotkeys not yet ported)");
}

}  // namespace kbm_rac2
