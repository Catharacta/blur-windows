#pragma once

#include "../effects/IBlurEffect.h"
#include <string>
#include <memory>
#include <vector>
#include <fstream>
#include <sstream>

namespace blurwindow {

// Forward declarations
std::unique_ptr<IBlurEffect> CreateGaussianBlur();
std::unique_ptr<IBlurEffect> CreateKawaseBlur();
std::unique_ptr<IBlurEffect> CreateBoxBlur();

/// Simple JSON-like config parser (no external dependencies)
class ConfigManager {
public:
    struct EffectConfig {
        std::string type;
        std::string params;
    };

    struct PipelineConfig {
        int version = 1;
        std::vector<EffectConfig> effects;
    };

    /// Save pipeline configuration to file
    static bool SavePipeline(const std::vector<std::pair<std::string, std::string>>& effects, const char* path) {
        std::ofstream file(path);
        if (!file.is_open()) return false;

        file << "{\n";
        file << "  \"version\": 1,\n";
        file << "  \"pipeline\": [\n";

        for (size_t i = 0; i < effects.size(); i++) {
            file << "    { \"type\": \"" << effects[i].first << "\", ";
            file << "\"params\": " << effects[i].second << " }";
            if (i < effects.size() - 1) file << ",";
            file << "\n";
        }

        file << "  ]\n";
        file << "}\n";

        return true;
    }

    /// Load pipeline configuration from file
    static PipelineConfig LoadPipeline(const char* path) {
        PipelineConfig config;
        
        std::ifstream file(path);
        if (!file.is_open()) return config;

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json = buffer.str();

        // Simple JSON parsing (handles our specific format)
        config = ParsePipelineJson(json);
        
        return config;
    }

    /// Create effect from type name
    static std::unique_ptr<IBlurEffect> CreateEffect(const std::string& type) {
        if (type == "gaussian" || type == "Gaussian") {
            return CreateGaussianBlur();
        } else if (type == "kawase" || type == "Kawase") {
            return CreateKawaseBlur();
        } else if (type == "box" || type == "Box") {
            return CreateBoxBlur();
        }
        return nullptr;
    }

private:
    static PipelineConfig ParsePipelineJson(const std::string& json) {
        PipelineConfig config;
        
        // Find "pipeline" array
        size_t pipelineStart = json.find("\"pipeline\"");
        if (pipelineStart == std::string::npos) return config;

        size_t arrayStart = json.find('[', pipelineStart);
        size_t arrayEnd = json.rfind(']');
        if (arrayStart == std::string::npos || arrayEnd == std::string::npos) return config;

        std::string arrayContent = json.substr(arrayStart + 1, arrayEnd - arrayStart - 1);

        // Parse each effect object
        size_t pos = 0;
        while ((pos = arrayContent.find('{', pos)) != std::string::npos) {
            size_t objEnd = arrayContent.find('}', pos);
            if (objEnd == std::string::npos) break;

            std::string obj = arrayContent.substr(pos, objEnd - pos + 1);
            
            EffectConfig effectConfig;
            
            // Extract type
            size_t typePos = obj.find("\"type\"");
            if (typePos != std::string::npos) {
                size_t valueStart = obj.find('\"', typePos + 6);
                size_t valueEnd = obj.find('\"', valueStart + 1);
                if (valueStart != std::string::npos && valueEnd != std::string::npos) {
                    effectConfig.type = obj.substr(valueStart + 1, valueEnd - valueStart - 1);
                }
            }

            // Extract params (as raw JSON)
            size_t paramsPos = obj.find("\"params\"");
            if (paramsPos != std::string::npos) {
                size_t paramsStart = obj.find('{', paramsPos);
                size_t paramsEnd = obj.rfind('}');
                if (paramsStart != std::string::npos && paramsEnd > paramsStart) {
                    effectConfig.params = obj.substr(paramsStart, paramsEnd - paramsStart + 1);
                }
            }

            if (!effectConfig.type.empty()) {
                config.effects.push_back(effectConfig);
            }

            pos = objEnd + 1;
        }

        return config;
    }
};

} // namespace blurwindow
