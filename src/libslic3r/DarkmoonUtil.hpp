#ifndef slic3r_DarkmoonUtil_hpp_
#define slic3r_DarkmoonUtil_hpp_

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r {

class DynamicPrintConfig;
enum BedType : int;
enum MachineBedType : int;

enum class DarkmoonPlateKind {
    G10,
    Ice,
    Lux,
    CFX,
    Satin
};

struct DarkmoonPlateInfo {
    DarkmoonPlateKind kind;
    BedType           bed_type;
    MachineBedType    machine_bed_type;
    const char       *slug;
    const char       *display_name;
    const char       *tooltip;
    const char       *bed_temp_key;
    const char       *bed_temp_initial_layer_key;
    const char       *fallback_temp_key;
    const char       *fallback_temp_initial_layer_key;
    const char       *thumbnail_key;
};

constexpr int kDarkmoonPlaceholderTemp = 45;

int default_g10_temperature(const std::string &filament_type_raw);
int default_ice_temperature(const std::string &filament_type_raw);
int default_lux_temperature(const std::string &filament_type_raw);
int default_cfx_temperature(const std::string &filament_type_raw);
int default_satin_temperature(const std::string &filament_type_raw);

void ensure_darkmoon_bed_temps(DynamicPrintConfig &config, size_t extruder_count);

// Apply dynamic Darkmoon temperatures, always overriding existing values
void apply_dynamic_darkmoon_bed_temps(DynamicPrintConfig &config, size_t extruder_count);

const std::array<std::string_view, 5> &darkmoon_plate_temp_keys();
const std::array<std::string_view, 5> &darkmoon_initial_layer_plate_temp_keys();
const std::array<std::string_view, 10> &darkmoon_all_temp_keys();

bool is_darkmoon_plate_temp_key(std::string_view key);
bool is_darkmoon_initial_layer_temp_key(std::string_view key);
bool is_darkmoon_bed_temp_key(std::string_view key);

const std::array<DarkmoonPlateInfo, 5> &darkmoon_plates();
const DarkmoonPlateInfo *find_darkmoon_plate(BedType type);
const DarkmoonPlateInfo *find_darkmoon_plate_by_machine_bed(MachineBedType type);
const DarkmoonPlateInfo *find_darkmoon_plate_by_slug(std::string_view slug);
const DarkmoonPlateInfo *find_darkmoon_plate_by_temp_key(std::string_view key);

bool is_darkmoon_bed(BedType type);
bool is_darkmoon_machine_bed(MachineBedType type);
bool is_darkmoon_temp_key(std::string_view key);

void append_darkmoon_temperature_keys(std::vector<std::string> &target);
void append_darkmoon_initial_temperature_keys(std::vector<std::string> &target);

std::optional<int> default_darkmoon_temperature(const DarkmoonPlateInfo &plate, const std::string &filament_type_raw);
std::optional<std::vector<int>> default_darkmoon_temperatures(const DarkmoonPlateInfo &plate, const std::vector<std::string> &filament_types);

struct DarkmoonTexturePartInfo {
    float x, y, w, h;
    std::string filename;
};

void append_darkmoon_plate_slugs(std::vector<std::string> &slugs);
void append_darkmoon_plate_display_names(std::vector<std::string> &display_names);
void append_darkmoon_bed_thumbnails(std::map<BedType, std::string> &thumbnails);

// Get bed thumbnail key for a Darkmoon plate by name
std::string get_darkmoon_bed_thumbnail_by_name(const std::string &plate_name);

// Get texture part information for a specific Darkmoon plate type
std::pair<DarkmoonTexturePartInfo, DarkmoonTexturePartInfo> get_darkmoon_texture_parts(BedType bed_type);

template <class Container>
void apply_darkmoon_bed_slugs(Container &targets)
{
    for (const DarkmoonPlateInfo &plate : darkmoon_plates())
        targets[static_cast<size_t>(plate.bed_type)] = plate.slug;
}

} // namespace Slic3r

#endif // slic3r_DarkmoonUtil_hpp_
