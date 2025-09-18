#ifndef slic3r_DarkmoonUtils_hpp_
#define slic3r_DarkmoonUtils_hpp_

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r {

enum BedType;
enum MachineBedType;

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

const std::array<DarkmoonPlateInfo, 5>& darkmoon_plates();

const DarkmoonPlateInfo* find_darkmoon_plate(BedType type);
const DarkmoonPlateInfo* find_darkmoon_plate(MachineBedType type);
const DarkmoonPlateInfo* find_darkmoon_plate_by_slug(std::string_view slug);
const DarkmoonPlateInfo* find_darkmoon_plate_by_temp_key(std::string_view key);

bool is_darkmoon_bed(BedType type);
bool is_darkmoon_machine_bed(MachineBedType type);
bool is_darkmoon_temp_key(std::string_view key);

void append_darkmoon_temperature_keys(std::vector<std::string> &target);
void append_darkmoon_initial_temperature_keys(std::vector<std::string> &target);

std::optional<int> default_darkmoon_temperature(const DarkmoonPlateInfo &plate, const std::string &filament_type_raw);
std::optional<std::vector<int>> default_darkmoon_temperatures(const DarkmoonPlateInfo &plate, const std::vector<std::string> &filament_types);

} // namespace Slic3r

#endif // slic3r_DarkmoonUtils_hpp_
