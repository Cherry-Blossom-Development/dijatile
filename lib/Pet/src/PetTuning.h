#pragma once
// Every tuning constant for Tilepet in one place (spec section 15).
// Times are real seconds unless the name says _H (game hours, which the
// debug time scale speeds up).

#include <stdint.h>

namespace pet {
namespace tune {

// ---------------------------------------------------------------- motion
constexpr float GRAV_LP_TAU_S = 0.25f;     // gravity estimate low-pass
constexpr float GRAV_INNOVATION_G = 0.3f;  // clip per-sample change so taps don't move gravity
constexpr float FAST_GRAV_TAU_S = 0.08f;   // gravity for the floor and physics
constexpr float RMS_TAU_S = 0.5f;          // motionRms / gyroRms window
constexpr float FAST_RMS_TAU_S = 0.1f;     // short window for pick-up and tap gating
constexpr float FLAT_TILT_DEG = 15.0f;
constexpr float FLAT_LEAVE_TILT_DEG = 20.0f;
constexpr float FLAT_STILL_RMS_G = 0.02f;
constexpr float FLAT_LEAVE_RMS_G = 0.05f;
constexpr float FLAT_STILL_GYRO_DPS = 3.0f;
constexpr float FLAT_HOLD_S = 2.0f;
constexpr float FACEDOWN_DEG = 150.0f;
constexpr float FACEDOWN_LEAVE_DEG = 140.0f;
constexpr float FACEDOWN_HOLD_S = 1.0f;
constexpr float UPRIGHT_DEG = 60.0f;
constexpr float UPRIGHT_STILL_RMS_G = 0.05f;
constexpr float INHAND_MIN_RMS_G = 0.02f;
constexpr float INHAND_MAX_RMS_G = 0.4f;
constexpr float INHAND_HOLD_S = 1.0f;
constexpr float PICKUP_RMS_G = 0.05f;
constexpr float PICKUP_TILT_DEG = 10.0f;
constexpr uint32_t PICKUP_TILT_WINDOW_MS = 300;
constexpr uint32_t SPIKE_HOLDOFF_MS = 400;  // after a jolt, before pick-up may fire
constexpr float SPIKE_G = 0.8f;
constexpr float SHAKE_RMS_G = 0.6f;
constexpr float SHAKE_REVERSAL_G = 1.2f;
constexpr int SHAKE_REVERSALS = 3;
constexpr uint32_t SHAKE_WINDOW_MS = 1000;
constexpr float SHAKE_DECAY_S = 1.0f;
constexpr uint32_t SHAKE_SUPPRESS_MS = 500;  // no tap / pick-up this long after shaking
constexpr float MARACA_PEAK_G = 0.5f;        // gentler reversals still count as maraca peaks
constexpr float SPIN_DPS = 180.0f;
constexpr float SPIN_HOLD_S = 0.5f;
constexpr float SPIN_LP_TAU_S = 0.1f;
constexpr float FREEFALL_G = 0.3f;
constexpr uint32_t FREEFALL_MS = 80;
constexpr float LANDING_SPIKE_G = 2.0f;
constexpr uint32_t LANDING_WINDOW_MS = 1500;
constexpr float TAP_JERK_G = 1.5f;
constexpr uint32_t TAP_MAX_MS = 40;  // 30 ms in the spec, plus one sample period
constexpr uint32_t TAP_LOCKOUT_MS = 150;
constexpr float TAP_QUIET_RMS_G = 0.05f;
constexpr float GYRO_BIAS_TAU_S = 5.0f;

// ---------------------------------------------------------------- sleep
constexpr float DROWSY_AFTER_S = 45.0f;
constexpr float SLEEP_AFTER_S = 90.0f;
constexpr float FACEDOWN_SLEEP_S = 10.0f;
constexpr float MIN_NAP_S = 300.0f;
constexpr float CRANKY_S = 30.0f;
constexpr float LIGHTS_OFF_MAX_ENERGY = 70.0f;
constexpr float WAKE_CRANKY_ENERGY = 30.0f;
constexpr float ROCK_MIN_G = 0.02f, ROCK_MAX_G = 0.1f;  // gentle rocking calms it

// ---------------------------------------------------------------- dizziness
constexpr float DIZZY_GAIN_PER_S = 0.9f;  // at shake intensity 1
constexpr float DIZZY_DECAY_S = 3.0f;     // from 1 to 0 once shaking stops
constexpr float NAUSEA_LEVEL = 0.8f;
constexpr float NAUSEA_AFTER_S = 3.0f;
constexpr float SCARED_WINDOW_S = 600.0f;  // two nausea episodes in this window -> scared
constexpr float SCARED_FOR_S = 300.0f;
constexpr float FLIP_DIZZY = 0.4f;  // repeated 180 degree flips
constexpr float FLIP_WINDOW_S = 10.0f;
constexpr float HARD_LANDING_DIZZY = 0.2f;

// ---------------------------------------------------------------- physics (logical px)
constexpr float G_SCALE_PX_S2 = 400.0f;  // 1 g
constexpr float TERMINAL_V = 260.0f;
constexpr float RESTITUTION = 0.3f;
constexpr float SLIDE_G = 0.35f;
constexpr float TUMBLE_G = 0.7f;
constexpr float DRIFT_MIN_G = 0.1f;
constexpr float DRIFT_PX_S_PER_G = 60.0f;
constexpr float FLOOR_SWITCH_G = 0.5f;
constexpr float FLOOR_SWITCH_DEG = 55.0f;
constexpr uint32_t FLOOR_SWITCH_MS = 250;
constexpr float FLAT_G = 0.25f;           // below this there is no clear "down"
constexpr float FLAT_SETTLE_G = 0.3f;     // pretend gravity toward the floor when flat
constexpr float FAST_ROTATE_DEG = 60.0f;
constexpr uint32_t FAST_ROTATE_MS = 300;
constexpr float HARD_LANDING_V = 170.0f;
constexpr float SETTLE_V = 40.0f;  // bounce speed below which a body comes to rest
constexpr float WALK_PX_S = 18.0f;
constexpr float RUN_PX_S = 40.0f;
constexpr float CLIMB_PX_S = 16.0f;
constexpr float CRAWL_PX_S = 9.0f;
constexpr float LANDING_S = 0.15f;
constexpr float WALL_PX = 4.0f;  // border drawn round the room
constexpr float PROP_FRICTION_G = 0.3f;
constexpr float BOWL_PROP_FRICTION_G = 0.6f;  // round bowl: props stay put on its slopes up to ~37 degrees
constexpr float POOP_HIT_V = 60.0f;
// water (aquatic pets): falls are slow and floaty, swimming is free movement
constexpr float WATER_GRAVITY = 0.35f;   // gravity scale on a sinking pet
constexpr float WATER_DRAG = 2.5f;       // 1/s, on a sinking pet
constexpr float WATER_PROP_DRAG = 6.0f;  // 1/s, props drift down slowly
constexpr float SWIM_ACCEL = 3.0f;       // 1/s, how fast a swimmer reaches its speed
constexpr float SWIM_ARRIVE_PX = 3.0f;
constexpr float SWIM_SPEED_MUL = 1.4f;       // swimming speed = walking speed x this
constexpr float SWIM_MAX_S = 20.0f;          // one swim trip at most this long
constexpr float BUBBLE_MIN_S = 0.8f, BUBBLE_MAX_S = 2.5f;  // ambient bubbles from the floor
constexpr float BUBBLE_RISE_MIN = 10.0f, BUBBLE_RISE_MAX = 18.0f;  // px/s

// ---------------------------------------------------------------- life (game hours)
constexpr float HUNGER_PER_H = 8.0f;
constexpr float HAPPY_PER_H = 5.0f;
constexpr float ENERGY_PER_H = 6.0f;
constexpr float ENERGY_SLEEP_PER_H = 20.0f;
constexpr float CLEAN_PER_H = 3.0f;
constexpr float POOP_CLEAN_PER_H = 20.0f;  // -10 per 30 min per poop
constexpr float POOP_HAPPY_PER_H = 2.0f;
constexpr float DANCE_HAPPY_PER_H = 30.0f;
constexpr float DANCE_ENERGY_PER_H = 12.0f;
constexpr float HEALTH_BAD_PER_H = 5.0f;
constexpr float HEALTH_GOOD_PER_H = 1.0f;
constexpr float MEAL_HUNGER = 40.0f, SNACK_HUNGER = 15.0f;
constexpr float FULL_HUNGER = 90.0f;  // refuses meals from here up
constexpr float MEAL_GRAMS = 4.0f, SNACK_GRAMS = 6.0f, GAME_GRAMS = 2.0f;
constexpr float POOP_MIN_H = 2.0f, POOP_MAX_H = 4.0f;
constexpr float MEAL_POOP_MIN_H = 20.0f / 60, MEAL_POOP_MAX_H = 40.0f / 60;
constexpr int MAX_POOPS = 6;
constexpr float SICK_BASE_PER_H = 0.01f;
constexpr float ATTENTION_LEVEL = 25.0f;
constexpr float ATTENTION_REPEAT_H = 0.25f;
constexpr float CARE_GRACE_H = 0.25f;
constexpr float CATCHUP_CAP_H = 8.0f;
constexpr float RUNAWAY_NEGLECT_H = 24.0f;
constexpr float ELDER_END_H = 7 * 24.0f;
constexpr float CLASSIC_DEATH_H = 12.0f;  // health at 0 this long (classic mode)
// Stage durations, game hours: egg, baby, child, teen, adult (elder lasts until the end).
constexpr float STAGE_H[5] = {2.0f / 60, 1.0f, 24.0f, 48.0f, 120.0f};
constexpr float EGG_HELD_SPEEDUP = 2.0f;
constexpr float NIGHT_START_H = 21.0f, NIGHT_END_H = 7.0f;

// ---------------------------------------------------------------- display / power
constexpr int LOGICAL_W = 120, LOGICAL_H = 120;
constexpr uint32_t FPS_ACTIVE = 30, FPS_IDLE = 15, FPS_ASLEEP = 5;
constexpr float DIM_AFTER_S = 120.0f;          // no input this long: dim to BRIGHT_DIM_PCT
constexpr uint8_t BRIGHT_DIM_PCT = 40;         // of the brightness setting
constexpr uint8_t SLEEP_DIM_PCT = 4;           // asleep: just visible (absolute); face down: off
constexpr uint8_t BRIGHT_SLEEP_PCT = 10;       // asleep, for a few seconds after a touch or key
constexpr float STATUS_HIDE_USB_S = 300.0f;    // on a computer's USB: hide the status bar after this idle
constexpr float UI_ROTATE_HOLD_S = 0.3f;
constexpr uint32_t SAVE_DEBOUNCE_MS = 60000;
constexpr int BATTERY_LOW_PCT = 15, BATTERY_EMPTY_PCT = 5;

// ---------------------------------------------------------------- audio
constexpr float SFX_DUCK = 0.5f;  // music gain while an effect plays (-6 dB)
constexpr uint32_t AMP_OFF_AFTER_MS = 2000;
constexpr int MASTER_VOL_DEFAULT = 6;
constexpr int ASLEEP_MUSIC_WAKE_VOL = 7;  // louder than this wakes a sleeping pet

// ---------------------------------------------------------------- dance / music analysis
constexpr uint32_t MIC_SAMPLE_RATE = 16000;
constexpr int32_t MIC_LATENCY_MS = 40;
constexpr int32_t DANCE_LATENCY_MS = 60;  // decode -> heard, SD playback
constexpr float LISTEN_TIMEOUT_S = 10.0f;
constexpr float LISTEN_LOST_S = 8.0f;
constexpr float LISTEN_SWAY_S = 4.0f;
constexpr float LISTEN_LOCK_S = 3.0f;
constexpr float LISTEN_MAX_MIN = 15.0f;
constexpr float AUTO_LISTEN_PERIOD_S = 120.0f;
constexpr float AUTO_LISTEN_SAMPLE_S = 3.0f;
constexpr float BAND_BASS_HZ = 150.0f, BAND_HIGH_HZ = 2000.0f;
constexpr float MIC_HPF_HZ = 40.0f, MIC_LOWMID_LO_HZ = 100.0f, MIC_LOWMID_HI_HZ = 400.0f;
constexpr float ONSET_THRESH = 1.4f;
constexpr float ONSET_FLOOR = 0.04f;
constexpr uint32_t ONSET_REFRACTORY_MS = 100;
constexpr float BPM_MIN = 70.0f, BPM_MAX = 180.0f;
constexpr float TEMPO_WINDOW_S = 6.0f;
constexpr float BEAT_CONF_MIN = 0.35f;
constexpr int MOVE_CHANGE_BARS = 2;
constexpr uint32_t MARACA_WINDOW_MS = 80;
constexpr int MARACA_MIN_BEATS = 4;
constexpr uint32_t TAP_ALONG_WINDOW_MS = 100;
constexpr int TAP_ALONG_SPECIAL = 8;
constexpr float QUIET_ENERGY = 0.08f;  // below this between songs the pet rests

// ---------------------------------------------------------------- talking
// The pet says what it needs (hungry, sad, sleepy, sick, a dirty room) every
// CHAT_NEED_* seconds, and chats now and then otherwise. Bold pets talk more
// often (the timer runs at 0.6 + 0.8 x bold), shy ones less.
constexpr float CHAT_NEED_MIN_S = 12.0f, CHAT_NEED_MAX_S = 20.0f;
constexpr float CHAT_IDLE_MIN_S = 25.0f, CHAT_IDLE_MAX_S = 45.0f;
constexpr float CHAT_SAY_S = 2.8f;
constexpr float GO_EAT_MAX_S = 10.0f;  // give up walking to the bowl and eat where it is

// ---------------------------------------------------------------- touch
constexpr uint32_t PICKUP_PRESS_MS = 800;
constexpr uint32_t POKE_WINDOW_MS = 2000;
constexpr int POKES_ANNOYED = 3;
constexpr uint32_t TICKLE_WINDOW_MS = 1000;
constexpr int TICKLE_TAPS = 4;
constexpr uint32_t DOUBLE_TAP_MS = 450;
constexpr float STROKE_MIN_PX = 14.0f;
constexpr float STROKE_MAX_PX_S = 160.0f;
constexpr float PET_HAPPY_PER_MIN = 6.0f;  // cap on happiness from petting

}  // namespace tune
}  // namespace pet
