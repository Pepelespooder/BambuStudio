#include "DarkmoonGuiUtil.hpp"

#include "GLTexture.hpp"
#include "libslic3r/Utils.hpp"

#include <wx/colour.h>
#include <wx/font.h>

#include <boost/filesystem.hpp>

#include <cmath>
#include <mutex>

namespace Slic3r {
namespace GUI {
namespace DarkmoonGui {

namespace {

std::once_flag g_font_resources_once;

void register_font_resources()
{
    const std::string font_dir = Slic3r::resources_dir() + "/fonts/";
    const std::array<const char *, 2> nanum_fonts = {
        "NanumGothic-Regular.ttf",
        "NanumGothic-Bold.ttf"
    };

    for (const char *font_name : nanum_fonts) {
        const boost::filesystem::path font_path(font_dir + font_name);
        if (!boost::filesystem::exists(font_path))
            continue;
        wxFont::AddPrivateFont(wxString::FromUTF8(font_path.string()));
    }
}

} // namespace

void ensure_font_resources()
{
    std::call_once(g_font_resources_once, register_font_resources);
}

wxColour to_wx_colour(const std::array<uint8_t, 4> &rgba)
{
    return wxColour(rgba[0], rgba[1], rgba[2], rgba[3]);
}

wxFont build_preferred_font(float point_size, bool bold)
{
    ensure_font_resources();

    const int rounded_size = static_cast<int>(std::lround(point_size));

    auto build_font = [&](const char *face_name) {
        wxFontInfo info(rounded_size);
        if (face_name != nullptr)
            info.FaceName(wxString::FromUTF8(face_name));
        if (bold)
            info = info.Bold();
        return wxFont(info);
    };

    wxFont font = build_font("NanumGothic");
    if (!font.IsOk())
        font = build_font("Nanum Gothic");
    if (!font.IsOk()) {
        wxFontInfo info(rounded_size);
        info.Family(wxFONTFAMILY_SWISS);
        if (bold)
            info = info.Bold();
        font = wxFont(info);
    }
    return font;
}

bool generate_texture(const TextParams &params, GLTexture &texture)
{
    if (params.text.empty())
        return false;

    ensure_font_resources();

    wxFont font = build_preferred_font(params.point_size, params.bold);
    if (!font.IsOk())
        return false;

    const wxColour background = to_wx_colour(params.background);
    const wxColour foreground = to_wx_colour(params.foreground);

    int texture_width  = 0;
    int texture_height = 0;
    int baseline       = 0;

    return texture.generate_texture_from_text(params.text, font, texture_width, texture_height,
                                              baseline, background, foreground, params.rotate_clockwise);
}

} // namespace DarkmoonGui
} // namespace GUI
} // namespace Slic3r
