#include "DarkmoonUtil.hpp"

#include "Config.hpp"
#include "PrintConfig.hpp"
#include "ProjectTask.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <optional>
#include <string_view>
#include <vector>
namespace Slic3r {
namespace {

constexpr std::array<std::string_view, 5> kDarkmoonPlateTempKeys = {
    "darkmoon_g10_plate_temp",
    "darkmoon_ice_plate_temp",
    "darkmoon_lux_plate_temp",
    "darkmoon_cfx_plate_temp",
    "darkmoon_satin_plate_temp"
};

constexpr std::array<std::string_view, 5> kDarkmoonInitialLayerPlateTempKeys = {
    "darkmoon_g10_plate_temp_initial_layer",
    "darkmoon_ice_plate_temp_initial_layer",
    "darkmoon_lux_plate_temp_initial_layer",
    "darkmoon_cfx_plate_temp_initial_layer",
    "darkmoon_satin_plate_temp_initial_layer"
};

constexpr std::array<std::string_view, 10> kDarkmoonAllTempKeys = {
    "darkmoon_g10_plate_temp",
    "darkmoon_ice_plate_temp",
    "darkmoon_lux_plate_temp",
    "darkmoon_cfx_plate_temp",
    "darkmoon_satin_plate_temp",
    "darkmoon_g10_plate_temp_initial_layer",
    "darkmoon_ice_plate_temp_initial_layer",
    "darkmoon_lux_plate_temp_initial_layer",
    "darkmoon_cfx_plate_temp_initial_layer",
    "darkmoon_satin_plate_temp_initial_layer"
};

constexpr std::array<DarkmoonPlateInfo, 5> kDarkmoonPlates = {{
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
     "bed_satin"}
}};

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

int default_lux_temperature(const std::string &filament_type_raw)
{
    auto tokens = tokenize_filament(filament_type_raw);

    if (has_token(tokens, "TPU"))
        return 1;
    if (has_token(tokens, "PLA"))
        return 60;
    if (has_token(tokens, "PCTG") || has_token(tokens, "PETG"))
        return 80;
    if (has_token(tokens, "ABS") || has_token(tokens, "ASA"))
        return 110;
    if (has_token(tokens, "PC") && !has_token(tokens, "PCT") && !has_token(tokens, "PETC"))
        return 100;
    if (has_token(tokens, "NYLON") || has_token(tokens, "PAHT") || has_token(tokens, "PPA") || has_token(tokens, "PA"))
        return 110;

    // Materials not listed are not recommended on Lux; use 0°C to flag unsupported.
    return 0;
}

} // namespace

const std::array<std::string_view, 5> &darkmoon_plate_temp_keys()
{
    return kDarkmoonPlateTempKeys;
}

const std::array<std::string_view, 5> &darkmoon_initial_layer_plate_temp_keys()
{
    return kDarkmoonInitialLayerPlateTempKeys;
}

const std::array<std::string_view, 10> &darkmoon_all_temp_keys()
{
    return kDarkmoonAllTempKeys;
}

bool is_darkmoon_plate_temp_key(std::string_view key)
{
    return std::find(kDarkmoonPlateTempKeys.begin(), kDarkmoonPlateTempKeys.end(), key) != kDarkmoonPlateTempKeys.end();
}

bool is_darkmoon_initial_layer_temp_key(std::string_view key)
{
    return std::find(kDarkmoonInitialLayerPlateTempKeys.begin(), kDarkmoonInitialLayerPlateTempKeys.end(), key) != kDarkmoonInitialLayerPlateTempKeys.end();
}

bool is_darkmoon_bed_temp_key(std::string_view key)
{
    return std::find(kDarkmoonAllTempKeys.begin(), kDarkmoonAllTempKeys.end(), key) != kDarkmoonAllTempKeys.end();
}

const std::array<DarkmoonPlateInfo, 5> &darkmoon_plates()
{
    return kDarkmoonPlates;
}

const DarkmoonPlateInfo *find_darkmoon_plate(BedType type)
{
    auto it = std::find_if(kDarkmoonPlates.begin(), kDarkmoonPlates.end(), [type](const DarkmoonPlateInfo &plate) {
        return plate.bed_type == type;
    });
    return it != kDarkmoonPlates.end() ? &(*it) : nullptr;
}

const DarkmoonPlateInfo *find_darkmoon_plate_by_machine_bed(MachineBedType type)
{
    auto it = std::find_if(kDarkmoonPlates.begin(), kDarkmoonPlates.end(), [type](const DarkmoonPlateInfo &plate) {
        return plate.machine_bed_type == type;
    });
    return it != kDarkmoonPlates.end() ? &(*it) : nullptr;
}

const DarkmoonPlateInfo *find_darkmoon_plate_by_slug(std::string_view slug)
{
    auto it = std::find_if(kDarkmoonPlates.begin(), kDarkmoonPlates.end(), [slug](const DarkmoonPlateInfo &plate) {
        return slug == plate.slug;
    });
    return it != kDarkmoonPlates.end() ? &(*it) : nullptr;
}

const DarkmoonPlateInfo *find_darkmoon_plate_by_temp_key(std::string_view key)
{
    auto it = std::find_if(kDarkmoonPlates.begin(), kDarkmoonPlates.end(), [key](const DarkmoonPlateInfo &plate) {
        return key == plate.bed_temp_key || key == plate.bed_temp_initial_layer_key;
    });
    return it != kDarkmoonPlates.end() ? &(*it) : nullptr;
}

bool is_darkmoon_bed(BedType type)
{
    return find_darkmoon_plate(type) != nullptr;
}

bool is_darkmoon_machine_bed(MachineBedType type)
{
    return find_darkmoon_plate_by_machine_bed(type) != nullptr;
}

bool is_darkmoon_temp_key(std::string_view key)
{
    return find_darkmoon_plate_by_temp_key(key) != nullptr;
}

void append_darkmoon_temperature_keys(std::vector<std::string> &target)
{
    target.reserve(target.size() + kDarkmoonPlates.size() * 2);
    for (const DarkmoonPlateInfo &plate : kDarkmoonPlates) {
        target.emplace_back(plate.bed_temp_key);
        target.emplace_back(plate.bed_temp_initial_layer_key);
    }
}

void append_darkmoon_initial_temperature_keys(std::vector<std::string> &target)
{
    target.reserve(target.size() + kDarkmoonPlates.size());
    for (const DarkmoonPlateInfo &plate : kDarkmoonPlates)
        target.emplace_back(plate.bed_temp_initial_layer_key);
}

void append_darkmoon_plate_slugs(std::vector<std::string> &slugs)
{
    slugs.reserve(slugs.size() + kDarkmoonPlates.size());
    for (const DarkmoonPlateInfo &plate : kDarkmoonPlates)
        slugs.emplace_back(plate.slug);
}

void append_darkmoon_plate_display_names(std::vector<std::string> &display_names)
{
    display_names.reserve(display_names.size() + kDarkmoonPlates.size());
    for (const DarkmoonPlateInfo &plate : kDarkmoonPlates)
        display_names.emplace_back(plate.display_name);
}

void append_darkmoon_bed_thumbnails(std::map<BedType, std::string> &thumbnails)
{
    for (const DarkmoonPlateInfo &plate : kDarkmoonPlates)
        thumbnails.emplace(plate.bed_type, plate.thumbnail_key);
}

std::pair<DarkmoonTexturePartInfo, DarkmoonTexturePartInfo> get_darkmoon_texture_parts(BedType bed_type)
{
    // Universal Darkmoon part1: Moon logo with "Darkmoon" text (same for all Darkmoon plates)
    DarkmoonTexturePartInfo darkmoon_part1 = {10, 52, 8.393f, 192, "darkmoon_part1.svg"};
    
    // Plate-specific part2: Contains the actual plate type name
    DarkmoonTexturePartInfo darkmoon_part2;
    
    switch (bed_type) {
        case BedType::btDarkmoonG10:
            darkmoon_part2 = {74, -10, 148, 12, "darkmoon_g10_part2.svg"};
            break;
        case BedType::btDarkmoonIce:
            darkmoon_part2 = {74, -10, 148, 12, "darkmoon_ice_part2.svg"};
            break;
        case BedType::btDarkmoonLux:
            darkmoon_part2 = {74, -10, 148, 12, "darkmoon_lux_part2.svg"};
            break;
        case BedType::btDarkmoonCFX:
            darkmoon_part2 = {74, -10, 148, 12, "darkmoon_cfx_part2.svg"};
            break;
        case BedType::btDarkmoonSatin:
            darkmoon_part2 = {74, -10, 148, 12, "darkmoon_satin_part2.svg"};
            break;
        default:
            // Fallback to generic Darkmoon part2
            darkmoon_part2 = {74, -10, 148, 12, "darkmoon_part2.svg"};
            break;
    }
    
    return std::make_pair(darkmoon_part1, darkmoon_part2);
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

int default_ice_temperature(const std::string &filament_type_raw)
{
    auto tokens = tokenize_filament(filament_type_raw);

    if (has_token(tokens, "TPU"))
        return 0;
    if (has_token(tokens, "PLA"))
        return 40;
    if (has_token(tokens, "PETG") || has_token(tokens, "PCTG"))
        return 45;

    // Materials not listed are not recommended on Ice; use 0°C to flag unsupported.
    return 0;
}

int default_cfx_temperature(const std::string &filament_type_raw)
{
    auto tokens = tokenize_filament(filament_type_raw);

    if (has_token(tokens, "TPU"))
        return 1;
    if (has_token(tokens, "PLA"))
        return 65;
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
        return 85;

    // Materials not listed are not recommended on CFX; use 0°C to flag unsupported.
    return 0;
}

int default_satin_temperature(const std::string &filament_type_raw)
{
    auto tokens = tokenize_filament(filament_type_raw);

    if (has_token(tokens, "TPU"))
        return 1;
    if (has_token(tokens, "PLA"))
        return 60;
    if (has_token(tokens, "PCTG") || has_token(tokens, "PETG") ||
        has_token(tokens, "PET-CF") || has_all_tokens(tokens, "PET", "CF"))
        return 80;
    if (has_token(tokens, "ABS") || has_token(tokens, "ASA"))
        return 110;
    if (has_token(tokens, "PC") && !has_token(tokens, "PCT") && !has_token(tokens, "PETC"))
        return 120;
    if (has_token(tokens, "NYLON") || has_token(tokens, "PAHT") || has_token(tokens, "PPA") || has_token(tokens, "PA"))
        return 105;
    if (is_token_pp(tokens))
        return 85;
    if (is_token_pet_only(tokens))
        return 105;
    if (has_token(tokens, "PPS"))
        return 105;

    // Materials not listed are not recommended on Satin; use 0°C to flag unsupported.
    return 0;
}

std::optional<int> default_darkmoon_temperature(const DarkmoonPlateInfo &plate, const std::string &filament_type_raw)
{
    switch (plate.kind) {
    case DarkmoonPlateKind::G10:
        return default_g10_temperature(filament_type_raw);
    case DarkmoonPlateKind::Ice:
        return default_ice_temperature(filament_type_raw);
    case DarkmoonPlateKind::Lux:
        return default_lux_temperature(std::string(filament_type_raw));
    case DarkmoonPlateKind::CFX:
        return default_cfx_temperature(filament_type_raw);
    case DarkmoonPlateKind::Satin:
        return default_satin_temperature(filament_type_raw);
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

void ensure_darkmoon_bed_temps(DynamicPrintConfig &config, size_t extruder_count)
{
    struct DarkmoonMapping {
        const char *darkmoon_key;
        const char *fallback_key;
    };

    static const DarkmoonMapping mappings[] = {
        {"darkmoon_g10_plate_temp",                 "cool_plate_temp"},
        {"darkmoon_g10_plate_temp_initial_layer",   "cool_plate_temp_initial_layer"},
        {"darkmoon_ice_plate_temp",                 "cool_plate_temp"},
        {"darkmoon_ice_plate_temp_initial_layer",   "cool_plate_temp_initial_layer"},
        {"darkmoon_lux_plate_temp",                 "hot_plate_temp"},
        {"darkmoon_lux_plate_temp_initial_layer",   "hot_plate_temp_initial_layer"},
        {"darkmoon_cfx_plate_temp",                 "hot_plate_temp"},
        {"darkmoon_cfx_plate_temp_initial_layer",   "hot_plate_temp_initial_layer"},
        {"darkmoon_satin_plate_temp",               "hot_plate_temp"},
        {"darkmoon_satin_plate_temp_initial_layer", "hot_plate_temp_initial_layer"}
    };

    extruder_count = std::max<size_t>(1, extruder_count);

    std::vector<std::string> filament_types;
    if (const auto *types_opt = config.opt<ConfigOptionStrings>("filament_type")) {
        filament_types = types_opt->values;
    }
    if (filament_types.empty())
        filament_types.assign(extruder_count, "PLA");
    if (filament_types.size() < extruder_count)
        filament_types.resize(extruder_count, filament_types.back());

    auto resize_to_extruders = [extruder_count](ConfigOptionInts *opt) {
        if (opt == nullptr)
            return;
        if (opt->values.empty())
            opt->values.assign(extruder_count, 0);
        else if (opt->values.size() < extruder_count)
            opt->values.resize(extruder_count, opt->values.back());
        else if (opt->values.size() > extruder_count)
            opt->values.resize(extruder_count);
    };

    auto is_placeholder = [](const ConfigOptionInts *opt) {
        return opt != nullptr && !opt->values.empty() &&
               std::all_of(opt->values.begin(), opt->values.end(), [](int v) {
                   return v == kDarkmoonPlaceholderTemp;
               });
    };

    for (const DarkmoonMapping &mapping : mappings) {
        ConfigOptionInts *dm_opt = config.opt<ConfigOptionInts>(mapping.darkmoon_key);
        const std::string key(mapping.darkmoon_key);
        bool need_fallback = (dm_opt == nullptr || dm_opt->values.empty() || is_placeholder(dm_opt));
        bool is_cfx   = key.find("darkmoon_cfx")   != std::string::npos;
        bool is_satin = key.find("darkmoon_satin") != std::string::npos;
        bool is_g10   = key.find("darkmoon_g10")   != std::string::npos;
        bool is_ice   = key.find("darkmoon_ice")   != std::string::npos;
        bool is_lux   = key.find("darkmoon_lux")   != std::string::npos;
        if (need_fallback || dm_opt->values.size() < extruder_count) {
            std::vector<int> values;
            bool filled = false;
            if (is_cfx || is_satin || is_g10 || is_ice || is_lux) {
                values.resize(extruder_count);
                filled = true;
                for (size_t idx = 0; idx < extruder_count; ++idx) {
                    int v = is_cfx ? default_cfx_temperature(filament_types[idx])
                                    : is_satin ? default_satin_temperature(filament_types[idx])
                                               : is_ice   ? default_ice_temperature(filament_types[idx])
                                               : is_lux   ? default_lux_temperature(std::string(filament_types[idx]))
                                                          : default_g10_temperature(filament_types[idx]);
                    if (v < 0) {
                        filled = false;
                        break;
                    }
                    values[idx] = v;
                }
            }

            if (!filled) {
                values.clear();
                if (const ConfigOptionInts *fallback = config.opt<ConfigOptionInts>(mapping.fallback_key); fallback && !fallback->values.empty())
                    values.assign(fallback->values.begin(), fallback->values.end());
                else
                    values.assign(extruder_count, 0);
            }

            dm_opt = config.option<ConfigOptionInts>(mapping.darkmoon_key, true);
            dm_opt->values = std::move(values);
        }

        resize_to_extruders(dm_opt);
    }
}

} // namespace Slic3r
