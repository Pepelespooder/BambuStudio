#ifndef slic3r_gui_darkmoon_gui_util_hpp_
#define slic3r_gui_darkmoon_gui_util_hpp_

#include <array>
#include <string>

class wxColour;
class wxFont;

namespace Slic3r {
namespace GUI {

class GLTexture;

namespace DarkmoonGui {

struct TextParams {
    std::string text;
    float point_size { 16.f };
    bool  bold { true };
    bool  rotate_clockwise { false };
    std::array<uint8_t, 4> foreground { { 179, 179, 179, 255 } };
    std::array<uint8_t, 4> background { { 0, 0, 0, 0 } };
};

// Ensure Nanum Gothic fonts bundled with the application are registered once per session.
void ensure_font_resources();

// Build a preferred Darkmoon font at the requested size / weight, falling back gracefully.
wxFont build_preferred_font(float point_size, bool bold);

// Convert RGBA arrays to wxColour for rendering helpers.
wxColour to_wx_colour(const std::array<uint8_t, 4> &rgba);

// Generate a text texture for Darkmoon UI elements. Returns false on failure.
bool generate_texture(const TextParams &params, GLTexture &texture);

} // namespace DarkmoonGui

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_gui_darkmoon_gui_util_hpp_
