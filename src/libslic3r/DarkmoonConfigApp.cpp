#include "DarkmoonConfigApp.hpp"
#include "DarkmoonUtil.hpp"
#include "PrintConfig.hpp"
#include "Config.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <boost/log/trivial.hpp>

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
    if (!has_missing_darkmoon_temperatures(config))
        return false;

    return apply_dynamic_config(config, filament_type, extruder_count, printer_config);
}

std::map<std::string, std::vector<int>> DarkmoonConfigApp::generate_darkmoon_temperatures(
    const std::string &filament_type, 
    size_t extruder_count) {
    
    std::map<std::string, std::vector<int>> temperatures;
    
    if (filament_type.empty()) {
        return temperatures;
    }

    extruder_count = std::max<size_t>(1, extruder_count);
    
    // Generate temperatures for each darkmoon plate type
    for (const DarkmoonPlateInfo &plate : darkmoon_plates()) {
        // Generate temperatures for regular bed temp
        if (auto temp_values = default_darkmoon_temperatures(plate, std::vector<std::string>(extruder_count, filament_type))) {
            temperatures[plate.bed_temp_key] = *temp_values;
            // Use same temperatures for initial layer (this follows the pattern in existing presets)
            temperatures[plate.bed_temp_initial_layer_key] = *temp_values;
        }
    }

    return temperatures;
}

bool DarkmoonConfigApp::has_missing_darkmoon_temperatures(const DynamicPrintConfig &config) {
    // Check if any darkmoon temperature keys are missing or have placeholder values
    for (const DarkmoonPlateInfo &plate : darkmoon_plates()) {
        const ConfigOptionInts *temp_opt = config.opt<ConfigOptionInts>(plate.bed_temp_key);
        const ConfigOptionInts *initial_temp_opt = config.opt<ConfigOptionInts>(plate.bed_temp_initial_layer_key);
        
        // Check if missing or has placeholder values
        if (!temp_opt || temp_opt->values.empty() || 
            std::all_of(temp_opt->values.begin(), temp_opt->values.end(), 
                       [](int v) { return v == kDarkmoonPlaceholderTemp; })) {
            return true;
        }
        
        if (!initial_temp_opt || initial_temp_opt->values.empty() || 
            std::all_of(initial_temp_opt->values.begin(), initial_temp_opt->values.end(), 
                       [](int v) { return v == kDarkmoonPlaceholderTemp; })) {
            return true;
        }
    }
    
    return false;
}

bool DarkmoonConfigApp::update_filament_preset_file(const std::string &preset_path,
                                                   const std::string &filament_type) {
    // This function would update JSON preset files to include darkmoon temperatures
    // For this implementation, we'll focus on the runtime config application
    // rather than modifying preset files directly
    
    // In a production implementation, this would:
    // 1. Parse the JSON preset file
    // 2. Determine the filament type from the preset
    // 3. Generate appropriate darkmoon temperatures
    // 4. Add the missing darkmoon temperature keys to the JSON
    // 5. Write the updated JSON back to the file
    
    return false; // Not implemented for minimal changes approach
}

bool DarkmoonConfigApp::validate_darkmoon_config(const DynamicPrintConfig &config,
                                               std::vector<std::string> &validation_errors) {
    validation_errors.clear();
    
    // Check that filament type is set
    const auto *filament_types = config.opt<ConfigOptionStrings>("filament_type");
    if (!filament_types || filament_types->values.empty()) {
        validation_errors.push_back("filament_type is not set");
        return false;
    }
    
    // Check each darkmoon plate temperature configuration
    for (const DarkmoonPlateInfo &plate : darkmoon_plates()) {
        const ConfigOptionInts *temp_opt = config.opt<ConfigOptionInts>(plate.bed_temp_key);
        const ConfigOptionInts *initial_temp_opt = config.opt<ConfigOptionInts>(plate.bed_temp_initial_layer_key);
        
        if (!temp_opt || temp_opt->values.empty()) {
            validation_errors.push_back(std::string("Missing temperature configuration for ") + plate.bed_temp_key);
        }
        
        if (!initial_temp_opt || initial_temp_opt->values.empty()) {
            validation_errors.push_back(std::string("Missing initial layer temperature configuration for ") + plate.bed_temp_initial_layer_key);
        }
    }
    
    return validation_errors.empty();
}

std::map<std::string, std::map<std::string, int>> DarkmoonConfigApp::get_recommended_temperatures() {
    std::map<std::string, std::map<std::string, int>> recommendations;
    
    // Common filament types and their recommended darkmoon temperatures
    std::vector<std::string> common_filaments = {"PLA", "PETG", "ABS", "ASA", "TPU", "PC", "NYLON"};
    
    for (const std::string &filament : common_filaments) {
        std::map<std::string, int> temps;
        
        for (const DarkmoonPlateInfo &plate : darkmoon_plates()) {
            if (auto temp = default_darkmoon_temperature(plate, filament)) {
                temps[plate.display_name] = *temp;
            }
        }
        
        if (!temps.empty()) {
            recommendations[filament] = temps;
        }
    }
    
    return recommendations;
}

std::string DarkmoonConfigApp::determine_filament_type(const DynamicPrintConfig &config) {
    const auto *filament_types = config.opt<ConfigOptionStrings>("filament_type");
    if (filament_types && !filament_types->values.empty() && !filament_types->values[0].empty()) {
        return filament_types->values[0];
    }
    
    // Try to determine from other config options or return default
    return "PLA"; // Default fallback
}

void DarkmoonConfigApp::populate_darkmoon_key(DynamicPrintConfig &config,
                                            const std::string &darkmoon_key,
                                            const std::vector<int> &values) {
    if (values.empty()) return;
    
    ConfigOptionInts *opt = config.option<ConfigOptionInts>(darkmoon_key, true);
    opt->values = values;
}

bool DarkmoonConfigApp::is_darkmoon_supported_manufacturer(const DynamicPrintConfig *printer_config) {
    if (!printer_config) {
        return false; // No printer config provided, cannot determine manufacturer
    }

    // Check for family field in printer config
    const auto *family_opt = printer_config->opt<ConfigOptionString>("family");
    if (family_opt && !family_opt->value.empty()) {
        const std::string &family = family_opt->value;
        // Only allow darkmoon configuration for supported manufacturers
        return (family == "Creality" || family == "Prusa" || family == "Qidi" || 
                family == "BBL" || family == "Bambu Lab");
    }

    // Fallback: check printer model name for manufacturer identification
    const auto *printer_model_opt = printer_config->opt<ConfigOptionString>("printer_model");
    if (printer_model_opt && !printer_model_opt->value.empty()) {
        const std::string &model = printer_model_opt->value;
        // Check if model contains supported manufacturer names
        return (model.find("Creality") != std::string::npos ||
                model.find("Prusa") != std::string::npos ||
                model.find("Qidi") != std::string::npos ||
                model.find("Bambu") != std::string::npos ||
                model.find("BBL") != std::string::npos);
    }

    // If we can't determine the manufacturer, don't apply darkmoon configuration
    return false;
}

int DarkmoonConfigApp::get_display_temperature(const DynamicPrintConfig &config, 
                                             const std::string &darkmoon_temp_key, 
                                             int stored_value) {
    // Check if this is a darkmoon temperature key and the value is a placeholder
    if (!is_darkmoon_bed_temp_key(darkmoon_temp_key) || stored_value != kDarkmoonPlaceholderTemp) {
        return stored_value;
    }
    
    // Try to get the filament type from the config to calculate actual temperature
    const auto *filament_types = config.opt<ConfigOptionStrings>("filament_type");
    if (!filament_types || filament_types->values.empty() || filament_types->values[0].empty()) {
        return stored_value; // No filament type available, return placeholder
    }
    
    // Find the darkmoon plate info for this temperature key
    const DarkmoonPlateInfo *plate = find_darkmoon_plate_by_temp_key(darkmoon_temp_key);
    if (!plate) {
        return stored_value; // Not a valid darkmoon plate key
    }
    
    // Calculate the actual temperature for this filament type and plate
    if (auto calculated_temp = default_darkmoon_temperature(*plate, filament_types->values[0])) {
        return *calculated_temp;
    }
    
    // Calculation failed, return original value
    return stored_value;
}

bool DarkmoonConfigApp::is_darkmoon_calculated_default_change(const std::string &opt_key, 
                                                             const DynamicPrintConfig &edited_config, 
                                                             const DynamicPrintConfig &reference_config)
{
    // Only check darkmoon temperature keys
    if (!is_darkmoon_bed_temp_key(opt_key)) {
        return false;
    }

    // Get the values from both configs
    const ConfigOptionInts *edited_opt = edited_config.opt<ConfigOptionInts>(opt_key);
    const ConfigOptionInts *reference_opt = reference_config.opt<ConfigOptionInts>(opt_key);

    if (!edited_opt || !reference_opt) {
        return false;
    }

    // If reference config has placeholder values and edited config has calculated values,
    // then this is just a calculated default, not a user override
    bool reference_has_placeholders = !reference_opt->values.empty() && 
        std::all_of(reference_opt->values.begin(), reference_opt->values.end(), 
                   [](int v) { return v == kDarkmoonPlaceholderTemp; });

    if (reference_has_placeholders && !edited_opt->values.empty()) {
        // Check if the edited values match what would be calculated for this filament type
        const auto *filament_types = edited_config.opt<ConfigOptionStrings>("filament_type");
        if (filament_types && !filament_types->values.empty()) {
            const DarkmoonPlateInfo *plate = find_darkmoon_plate_by_temp_key(opt_key);
            if (plate) {
                if (auto expected_temps = default_darkmoon_temperatures(*plate, filament_types->values)) {
                    // Resize expected temps to match edited config size
                    std::vector<int> expected = *expected_temps;
                    if (expected.size() < edited_opt->values.size()) {
                        expected.resize(edited_opt->values.size(), expected.back());
                    } else if (expected.size() > edited_opt->values.size()) {
                        expected.resize(edited_opt->values.size());
                    }
                    
                    // If the edited values match expected calculated values, this is not a user override
                    bool is_calculated_default = std::equal(edited_opt->values.begin(), edited_opt->values.end(), expected.begin());
                    return is_calculated_default;
                }
            }
        }
    }

    return false;
}

} // namespace Slic3r