/**
 * @file ConfigUtils.h
 * @author Andrew McDaniel
 * @brief Declarations for configuration parsing helpers used throughout the simulator.
 *
 * Exposes shared utilities that translate scenario and workload metadata into strongly typed
 * values, centralising parsing logic for other components.
 */
#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace configUtils {

using json = nlohmann::json;

// Light-weight JSON helpers (declarations only; definitions in .cpp)
std::optional<double> tryGetNumber(const json& obj, std::string_view key);
std::optional<bool> tryGetBool(const json& obj, std::string_view key);
std::optional<std::string> tryGetString(const json& obj, std::string_view key);
std::optional<double> asNumber(const json& node);

uint64_t toMs(double value);
uint64_t toUs(double value);

std::pair<uint64_t, uint64_t> extractDurationRangeMs(
    const json& obj,
    std::string_view keyBase,
    std::pair<uint64_t, uint64_t> defaultRange);

uint64_t extractDurationMs(const json& obj, std::string_view keyBase, uint64_t defaultValue);
uint64_t extractDurationUs(const json& obj, std::string_view keyBase, uint64_t defaultValue);

int getIntOr(const json& obj, std::string_view key, int defaultValue);
std::string getStringOr(const json& obj, std::string_view key, std::string defaultValue);
bool getBoolOr(const json& obj, std::string_view key, bool defaultValue);

} // namespace configUtils
