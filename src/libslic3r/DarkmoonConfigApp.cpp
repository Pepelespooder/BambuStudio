#include "DarkmoonConfigApp.hpp"
#include "DarkmoonUtil.hpp"
#include "PrintConfig.hpp"
#include "Config.hpp"

#include <algorithm>

namespace Slic3r {

bool DarkmoonConfigApp::apply_dynamic_config(DynamicPrintConfig &config, 
                                           const std::string &filament_type,
                                           size_t extruder_count,
                                           const DynamicPrintConfig *printer_config) {
    // Since we now have static temperatures in filament presets, this function is largely redundant
    // but we'll keep it for backward compatibility
    
    if (!is_darkmoon_supported_manufacturer(printer_config)) {
        return false; // Skip darkmoon configuration for unsupported manufacturers
    }

    // The static preset values should already be correct, so no dynamic application needed
    return true;
}

bool DarkmoonConfigApp::apply_dynamic_config_if_missing(DynamicPrintConfig &config,
                                                        const std::string &filament_type,
                                                        size_t extruder_count,
                                                        const DynamicPrintConfig *printer_config)
{
    // Since we now have static temperatures in filament presets, this is largely redundant
    return apply_dynamic_config(config, filament_type, extruder_count, printer_config);
}

std::string DarkmoonConfigApp::determine_filament_type(const DynamicPrintConfig &config) {
    const auto *filament_types = config.opt<ConfigOptionStrings>("filament_type");
    if (filament_types && !filament_types->values.empty()) {
        return filament_types->values[0];
    }
    return "PLA"; // Default fallback
}

bool DarkmoonConfigApp::is_darkmoon_supported_manufacturer(const DynamicPrintConfig *printer_config) {
    if (!printer_config) {
        return false;
    }
    
    const auto *printer_model = printer_config->opt<ConfigOptionString>("printer_model");
    if (!printer_model || printer_model->value.empty()) {
        return false;
    }
    
    const std::string &model = printer_model->value;
    
    // List of supported manufacturers
    const std::vector<std::string> supported = {
        "Creality", "Prusa", "Qidi", "BBL", "Bambu Lab", "QIDI"
    };
    
    for (const std::string &manufacturer : supported) {
        if (model.find(manufacturer) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

int DarkmoonConfigApp::get_display_temperature(const DynamicPrintConfig &config,
                                             const std::string &darkmoon_temp_key,
                                             int stored_value) {
    // Check if this is a darkmoon temperature key and the value is a placeholder
    if (!is_darkmoon_bed_temp_key(darkmoon_temp_key) || stored_value != kDarkmoonPlaceholderTemp) {
        return stored_value;
    }

    // Find the plate info for this temperature key
    const DarkmoonPlateInfo *plate = find_darkmoon_plate_by_temp_key(darkmoon_temp_key);
    if (!plate) {
        return stored_value;
    }

    // Get filament types from config
    const auto *filament_types = config.opt<ConfigOptionStrings>("filament_type");
    if (!filament_types || filament_types->values.empty()) {
        return stored_value;
    }
    
    // Calculate the actual temperature for this filament type and plate
    if (auto calculated_temp = default_darkmoon_temperature(*plate, filament_types->values[0])) {
        return *calculated_temp;
    }
    
    // Calculation failed, return original value
    return stored_value;
}

} // namespace Slic3r