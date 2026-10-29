#pragma once
// Trait tables for the genome (spec section 10.6). Adding a value = add a row
// here (and art for it in Art.cpp). Weights are relative.
//
// Colour ramps are three shades, dark -> light, as 0xRRGGBB.

#include <stdint.h>

namespace pet {
namespace genes {

struct Ramp {
  uint32_t dark, mid, light;
};

// ---- body colours: 12 curated ramps (warm, cool, pastel, earthy)
constexpr Ramp BODY_RAMPS[] = {
    {0xB23A48, 0xF25F5C, 0xFF9B85},  // coral
    {0xC0571B, 0xF28C28, 0xFFC170},  // orange
    {0xB8860B, 0xF2C230, 0xFFE58A},  // sunflower
    {0xA63A6A, 0xE0609A, 0xFFA3C8},  // raspberry
    {0x23649E, 0x3C9BE0, 0x8DD0FF},  // sky
    {0x167068, 0x23A399, 0x6ED8C8},  // teal
    {0x3C8A3C, 0x66C25A, 0xA8E890},  // leaf
    {0x5E4A9E, 0x8C72D6, 0xC2AEFF},  // lavender
    {0xC27D9E, 0xF0A8C8, 0xFFD8EA},  // pastel pink
    {0x6A8FB8, 0x9CC0E8, 0xD2E8FF},  // pastel blue
    {0x7A5236, 0xA87650, 0xD6A87C},  // cocoa
    {0x6E6E3A, 0x9C9C58, 0xCACA8A},  // olive
};
constexpr uint8_t BODY_WEIGHTS[] = {10, 10, 8, 8, 10, 9, 9, 9, 7, 7, 8, 5};
constexpr int BODY_COUNT = sizeof(BODY_WEIGHTS);

// Rare special palettes (about 4% of pets).
enum Special : uint8_t { SPECIAL_NONE, SPECIAL_RAINBOW, SPECIAL_GLOW, SPECIAL_GOLD };
constexpr uint8_t SPECIAL_WEIGHTS[] = {96, 2, 1, 1};
constexpr Ramp GOLD_RAMP = {0x9C6B00, 0xE0A800, 0xFFE070};

// ---- belly / markings: light ramps; which are allowed on which body
constexpr Ramp BELLY_RAMPS[] = {
    {0xE8D8B0, 0xFFF4D6, 0xFFFFFF},  // cream
    {0xD8D8E0, 0xF4F4FA, 0xFFFFFF},  // white
    {0xF0B8C8, 0xFFD8E4, 0xFFF0F5},  // pale pink
    {0xE8E098, 0xFFF8B8, 0xFFFFE0},  // pale yellow
    {0xA8D0E8, 0xD0ECFF, 0xF0F8FF},  // pale blue
    {0x5A4636, 0x7E6450, 0xA48870},  // dark brown (for light bodies)
};
constexpr int BELLY_COUNT = 6;
// Bit i set = belly ramp i goes with that body (nothing muddy).
constexpr uint8_t BELLY_ALLOWED[BODY_COUNT] = {
    0b001011,  // coral: cream, white, pale yellow
    0b001011,  // orange
    0b100011,  // sunflower: cream, white, dark brown
    0b011011,  // raspberry
    0b011011,  // sky
    0b011011,  // teal
    0b001011,  // leaf
    0b011111,  // lavender
    0b110010,  // pastel pink: white, pale blue, dark brown
    0b100110,  // pastel blue: white, pale pink, dark brown
    0b001101,  // cocoa: cream, pale pink, pale yellow
    0b001011,  // olive
};

enum Markings : uint8_t { MARK_NONE, MARK_SPOTS, MARK_STRIPES, MARK_PATCHES, MARK_MASK, MARK_SOCKS };
constexpr uint8_t MARKINGS_WEIGHTS[] = {30, 18, 14, 14, 12, 12};
constexpr const char *MARKINGS_NAMES[] = {"none", "spots", "stripes", "patches", "mask", "socks"};

// ---- hair colour path: start (baby) and end (adult) ramps; elders grey toward white
constexpr Ramp HAIR_RAMPS[] = {
    {0xC8A040, 0xF0D070, 0xFFF0B0},  // blond
    {0x5A3A20, 0x8A5A30, 0xB88550},  // brown
    {0x202028, 0x3A3A48, 0x606070},  // black
    {0x9A3010, 0xD05A20, 0xF09050},  // ginger
    {0x204890, 0x3A70D0, 0x80B0FF},  // blue
    {0xA03070, 0xE060A8, 0xFFA0D0},  // pink
    {0x2A7A30, 0x40B048, 0x90E090},  // green
    {0xA0A0A8, 0xD0D0D8, 0xFFFFFF},  // white
};
constexpr uint8_t HAIR_WEIGHTS[] = {14, 16, 12, 12, 8, 8, 6, 4};
constexpr int HAIR_COUNT = sizeof(HAIR_WEIGHTS);
constexpr Ramp ELDER_HAIR = {0x9898A0, 0xC8C8D0, 0xF4F4F8};

constexpr Ramp EYE_RAMPS[] = {
    {0x2A1A10, 0x5A3A20, 0xA07850},  // brown
    {0x102A50, 0x2A60B0, 0x80B8FF},  // blue
    {0x103A18, 0x2A8A3A, 0x80D890},  // green
    {0x5A3A00, 0xC08A10, 0xFFD060},  // amber
    {0x3A1050, 0x8040B0, 0xC8A0FF},  // violet
    {0x500010, 0xB01030, 0xFF7090},  // red (rare)
};
constexpr uint8_t EYE_WEIGHTS[] = {30, 22, 18, 14, 12, 4};

enum HornColour : uint8_t { HORN_BONE, HORN_DARK, HORN_BODY, HORN_GOLD };
constexpr uint8_t HORN_COLOUR_WEIGHTS[] = {40, 30, 26, 4};
constexpr Ramp HORN_RAMPS[] = {
    {0xB8A888, 0xF0E8D0, 0xFFFFFF},
    {0x282830, 0x484858, 0x787890},
    {0, 0, 0},  // body ramp, filled in at runtime
    {0x9C6B00, 0xE0A800, 0xFFF0A0},
};

// ---- features
constexpr uint8_t EYES_WEIGHTS[] = {8, 88, 4};  // 1, 2, 3 eyes
constexpr uint8_t HORNS_WEIGHTS[] = {45, 25, 26, 4};  // 0..3 horns
enum HornShape : uint8_t { HORN_NUB, HORN_CURVED, HORN_STRAIGHT };
constexpr uint8_t HORN_SHAPE_WEIGHTS[] = {34, 33, 33};
enum Hair : uint8_t { HAIR_NONE, HAIR_SHORT, HAIR_LONG, HAIR_TUFT, HAIR_MOHAWK };
constexpr uint8_t HAIR_STYLE_WEIGHTS[] = {20, 26, 18, 20, 16};
constexpr const char *HAIR_NAMES[] = {"none", "short", "long", "tuft", "mohawk"};
enum Tail : uint8_t { TAIL_NONE, TAIL_STUB, TAIL_LONG, TAIL_CURLY, TAIL_FLUFFY };
constexpr uint8_t TAIL_WEIGHTS[] = {18, 24, 22, 18, 18};
constexpr const char *TAIL_NAMES[] = {"none", "stub", "long", "curly", "fluffy"};
enum Ears : uint8_t { EARS_ROUND, EARS_POINTY, EARS_FLOPPY, EARS_NONE };
constexpr uint8_t EARS_WEIGHTS[] = {30, 28, 22, 20};
constexpr const char *EARS_NAMES[] = {"round", "pointy", "floppy", "none"};

// ---- egg
enum EggPattern : uint8_t { EGG_PLAIN, EGG_SPOTS, EGG_STRIPES, EGG_ZIGZAG, EGG_SPECKLES, EGG_BANDS, EGG_HEARTS };
constexpr uint8_t EGG_PATTERN_WEIGHTS[] = {14, 18, 16, 14, 16, 18, 4};
constexpr const char *EGG_PATTERN_NAMES[] = {"plain", "spots", "stripes", "zigzag", "speckles", "bands", "hearts"};

// ---- names: two or three syllables plus an optional ending
constexpr const char *NAME_SYLLABLES[] = {"mo", "pi", "ku", "ta", "zu", "bo", "ri", "na", "lu", "fi",
                                          "po", "ki", "mi", "do", "ra", "yu", "be", "so", "chi", "no",
                                          "pa", "gu", "le", "ti", "wa", "ko", "su", "ne"};
constexpr const char *NAME_ENDINGS[] = {"", "", "", "n", "x", "bo", "py", "li", "s"};

}  // namespace genes
}  // namespace pet
