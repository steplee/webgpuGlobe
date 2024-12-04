#pragma once

#include <variant>
#include <string>
#include <unordered_map>
#include <cassert>
#include <cstdint>
#include <vector>
#include <spdlog/spdlog.h>

namespace wg {
    using GlobeOption = std::variant<std::string, double, std::vector<double>, std::vector<std::string>>;

    struct GlobeOptions {
        std::unordered_map<std::string, GlobeOption> opts;

        inline double getDouble(const std::string& key) const {
            auto it = opts.find(key);
			if (it == opts.end()) throw std::runtime_error(fmt::format("failed to get key '{}'", key));
            return std::get<double>(it->second);
        }
        inline std::vector<double> getDoubleVec(const std::string& key) const {
            auto it = opts.find(key);
			if (it == opts.end()) throw std::runtime_error(fmt::format("failed to get key '{}'", key));
            return std::get<std::vector<double>>(it->second);
        }
        inline std::vector<std::string> getStringVec(const std::string& key) const {
            auto it = opts.find(key);
			if (it == opts.end()) throw std::runtime_error(fmt::format("failed to get key '{}'", key));
            return std::get<std::vector<std::string>>(it->second);
        }
        inline std::string getString(const std::string& key) const {
            auto it = opts.find(key);
			if (it == opts.end()) throw std::runtime_error(fmt::format("failed to get key '{}'", key));
            return std::get<std::string>(it->second);
        }
    };

	GlobeOptions parseArgs(const char* argv[], int argc);

}
