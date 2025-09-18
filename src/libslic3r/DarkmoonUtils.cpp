#include "DarkmoonUtils.hpp"

#include "PrintConfig.hpp"
#include "ProjectTask.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r {
namespace {

std::vector<std::string> tokenize_filament(const std::string &input)
{
    std::vector<std::string> tokens;
    std::string token;
    token.reserve(input.size());
    for (char ch : input) {
        if (std::isalnum(static_cast<unsigned char>(ch)))
            token.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        else if (!token.empty()) {
            tokens.push_back(token);
            token.clear();
        }
    }
    if (!token.empty())
        tokens.push_back(token);
    return tokens;
}

bool has_token(const std::vector<std::string> &tokens, const char *token, bool allow_substring = true)
{
    for (const std::string &t : tokens) {
        if (t == token)
            return true;
        if (allow_substring && t.find(token) != std::string::npos)
            return true;
    }
    return false;
}

bool has_all_tokens(const std::vector<std::string> &tokens, const char *a, const char *b)
{
    return has_token(tokens, a) && has_token(tokens, b);
}

bool is_token_pet_only(const std::vector<std::string> &tokens)
{
    if (!has_token(tokens, "PET", false))
        return false;
    return !has_token(tokens, "PETG") && !has_token(tokens, "PCTG") && !has_all_tokens(tokens, "PET", "CF");
}

bool is_token_pp(const std::vector<std::string> &tokens)
{
    return has_token(tokens, "POLYPROPYLENE") || has_token(tokens, "PP", false);
}

int default_g10_temperature(const std::string &filament_type_raw)
{
    auto tokens = tokenize_filament(filament_type_raw);

    if (has_token(tokens, "TPU"))
        return 1;
    if (has_token(tokens, "PLA"))
        return 55;
    if (has_token(tokens, "PCTG") || has_token(tokens, "PETG"))
        return 70;
    if (has_token(tokens, "ABS") || has_token(tokens, "ASA"))
        return 110;

    // Materials not listed are not recommended on G10; use 0°C to flag unsupported.
    return 0;
}

int default_cfx_temperature(const std::string &filament_type_raw)
{
    auto tokens = tokenize_filament(filament_type_raw);

    if (has_token(tokens, "TPU"))
        return 0;
    if (has_token(tokens, "PLA"))
        return 60;
    if (has_token(tokens, "PCTG") || has_token(tokens, "PETG"))
        return 80;
    if (has_token(tokens, "PET-CF") || has_all_tokens(tokens, "PET", "CF"))
        return 105;
    if (has_token(tokens, "PPS"))
        return 110;
    if (has_token(tokens, "PC") && !has_token(tokens, "PCT") && !has_token(tokens, "PETC"))
        return 115;
    if (has_token(tokens, "PAHT") || has_token(tokens, "PPA") || has_token(tokens, "NYLON") || has_token(tokens, "PA"))
        return 105;
    if (has_token(tokens, "ABS") || has_token(tokens, "ASA"))
        return 110;
    if (is_token_pp(tokens))
        return 90;

    return -1;
}

int default_satin_temperature(const std::string &filament_type_raw)
{
    auto tokens = tokenize_filament(filament_type_raw);

    if (has_token(tokens, "TPU"))
        return 0;
    if (has_token(tokens, "PLA"))
        return 58;
    if (has_token(tokens, "PCTG") || has_token(tokens, "PETG"))
        return 75;
    if (has_token(tokens, "ABS") || has_token(tokens, "ASA"))
        return 105;
    if (has_token(tokens, "PC") && !has_token(tokens, "PCT") && !has_token(tokens, "PETC"))
        return 115;
    if (has_token(tokens, "NYLON") || has_token(tokens, "PAHT") || has_token(tokens, "PPA") || has_token(tokens, "PA"))
        return 105;
    if (is_token_pet_only(tokens))
        return 105;
    if (has_token(tokens, "PPS"))
        return 105;

    return -1;
}

const std::array<DarkmoonPlateInfo, 5> plates = {{
    {DarkmoonPlateKind::G10, BedType::btDarkmoonG10, MachineBedType::BED_TYPE_DARKMOON_G10,
     "darkmoon_g10", "Darkmoon G10 Garolite",
     "Bed temperature when the Darkmoon G10 Garolite plate is installed. Value 0 means the filament does not support this plate",
     "darkmoon_g10_plate_temp", "darkmoon_g10_plate_temp_initial_layer",
     "cool_plate_temp", "cool_plate_temp_initial_layer",
     "bed_cool"},
    {DarkmoonPlateKind::Ice, BedType::btDarkmoonIce, MachineBedType::BED_TYPE_DARKMOON_ICE,
     "darkmoon_ice", "Darkmoon Ice",
     "Bed temperature when the Darkmoon Ice plate is installed. Value 0 means the filament does not support this plate",
     "darkmoon_ice_plate_temp", "darkmoon_ice_plate_temp_initial_layer",
     "cool_plate_temp", "cool_plate_temp_initial_layer",
     "bed_cool"},
    {DarkmoonPlateKind::Lux, BedType::btDarkmoonLux, MachineBedType::BED_TYPE_DARKMOON_LUX,
     "darkmoon_lux", "Darkmoon Lux",
     "Bed temperature when the Darkmoon Lux plate is installed. Value 0 means the filament does not support this plate",
     "darkmoon_lux_plate_temp", "darkmoon_lux_plate_temp_initial_layer",
     "hot_plate_temp", "hot_plate_temp_initial_layer",
     "bed_cool"},
    {DarkmoonPlateKind::CFX, BedType::btDarkmoonCFX, MachineBedType::BED_TYPE_DARKMOON_CFX,
     "darkmoon_cfx", "Darkmoon CFX",
     "Bed temperature when the Darkmoon CFX plate is installed. Value 0 means the filament does not support this plate",
     "darkmoon_cfx_plate_temp", "darkmoon_cfx_plate_temp_initial_layer",
     "hot_plate_temp", "hot_plate_temp_initial_layer",
     "bed_cool"},
    {DarkmoonPlateKind::Satin, BedType::btDarkmoonSatin, MachineBedType::BED_TYPE_DARKMOON_SATIN,
     "darkmoon_satin", "Darkmoon Satin",
     "Bed temperature when the Darkmoon Satin plate is installed. Value 0 means the filament does not support this plate",
     "darkmoon_satin_plate_temp", "darkmoon_satin_plate_temp_initial_layer",
     "hot_plate_temp", "hot_plate_temp_initial_layer",
     "bed_cool"}
}};

} // namespace

const std::array<DarkmoonPlateInfo, 5>& darkmoon_plates()
{
    return plates;
}

const DarkmoonPlateInfo* find_darkmoon_plate(BedType type)
{
    auto it = std::find_if(plates.begin(), plates.end(), [type](const DarkmoonPlateInfo &plate) {
        return plate.bed_type == type;
    });
    return it != plates.end() ? &(*it) : nullptr;
}

const DarkmoonPlateInfo* find_darkmoon_plate(MachineBedType type)
{
    auto it = std::find_if(plates.begin(), plates.end(), [type](const DarkmoonPlateInfo &plate) {
        return plate.machine_bed_type == type;
    });
    return it != plates.end() ? &(*it) : nullptr;
}

const DarkmoonPlateInfo* find_darkmoon_plate_by_slug(std::string_view slug)
{
    auto it = std::find_if(plates.begin(), plates.end(), [slug](const DarkmoonPlateInfo &plate) {
        return slug == plate.slug;
    });
    return it != plates.end() ? &(*it) : nullptr;
}

const DarkmoonPlateInfo* find_darkmoon_plate_by_temp_key(std::string_view key)
{
    auto it = std::find_if(plates.begin(), plates.end(), [key](const DarkmoonPlateInfo &plate) {
        return key == plate.bed_temp_key || key == plate.bed_temp_initial_layer_key;
    });
    return it != plates.end() ? &(*it) : nullptr;
}

bool is_darkmoon_bed(BedType type)
{
    return find_darkmoon_plate(type) != nullptr;
}

bool is_darkmoon_machine_bed(MachineBedType type)
{
    return find_darkmoon_plate(type) != nullptr;
}

bool is_darkmoon_temp_key(std::string_view key)
{
    return find_darkmoon_plate_by_temp_key(key) != nullptr;
}

void append_darkmoon_temperature_keys(std::vector<std::string> &target)
{
    target.reserve(target.size() + plates.size() * 2);
    for (const DarkmoonPlateInfo &plate : plates) {
        target.emplace_back(plate.bed_temp_key);
        target.emplace_back(plate.bed_temp_initial_layer_key);
    }
}

void append_darkmoon_initial_temperature_keys(std::vector<std::string> &target)
{
    target.reserve(target.size() + plates.size());
    for (const DarkmoonPlateInfo &plate : plates)
        target.emplace_back(plate.bed_temp_initial_layer_key);
}

std::optional<int> default_darkmoon_temperature(const DarkmoonPlateInfo &plate, const std::string &filament_type_raw)
{
    switch (plate.kind) {
    case DarkmoonPlateKind::G10:
        return default_g10_temperature(filament_type_raw);
    case DarkmoonPlateKind::CFX: {
        int value = default_cfx_temperature(filament_type_raw);
        if (value < 0)
            return std::nullopt;
        return value;
    }
    case DarkmoonPlateKind::Satin: {
        int value = default_satin_temperature(filament_type_raw);
        if (value < 0)
            return std::nullopt;
        return value;
    }
    default:
        return std::nullopt;
    }
}

std::optional<std::vector<int>> default_darkmoon_temperatures(const DarkmoonPlateInfo &plate, const std::vector<std::string> &filament_types)
{
    std::vector<int> values;
    values.reserve(filament_types.size());
    for (const std::string &filament_type : filament_types) {
        std::optional<int> value = default_darkmoon_temperature(plate, filament_type);
        if (!value.has_value())
            return std::nullopt;
        values.push_back(*value);
    }
    return values;
}

} // namespace Slic3r
