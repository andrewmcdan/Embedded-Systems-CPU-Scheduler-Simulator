/**
 * @file ConfigUtils.cpp
 * @author Andrew McDaniel
 * @brief Implementation of configuration parsing utilities for scenarios and workloads.
 *
 * Converts JSON and YAML configuration data into strongly typed values consumed by the
 * simulation engine and helper tooling.
 */
#include "ConfigUtils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <spdlog/spdlog.h>

namespace configUtils {

/**
 * @brief Try to get a numeric field from a JSON object
 *
 * @param obj
 * @param key
 * @return std::optional<double>
 */
std::optional<double> tryGetNumber(const json& obj, std::string_view key)
{
    if (!obj.is_object()) {
        return std::nullopt;
    }
    const std::string keyStr(key);
    const auto it = obj.find(keyStr);
    if (it == obj.end() || it->is_null()) {
        return std::nullopt;
    }
    if (it->is_number_float()) {
        return it->get<double>();
    }
    if (it->is_number_integer()) {
        return static_cast<double>(it->get<int64_t>());
    }
    if (it->is_string()) {
        const std::string text = it->get<std::string>();
        try {
            size_t idx = 0;
            const double value = std::stod(text, &idx);
            if (idx == text.size()) {
                return value;
            }
        } catch (...) {
        }
    }
    SPDLOG_WARN("Field '{}' is not numeric; ignoring entry", keyStr);
    return std::nullopt;
}

/**
 * @brief Try to get a boolean field from a JSON object
 *
 * @param obj
 * @param key
 * @return std::optional<bool>
 */
std::optional<bool> tryGetBool(const json& obj, std::string_view key)
{
    if (!obj.is_object()) {
        return std::nullopt;
    }
    const std::string keyStr(key);
    const auto it = obj.find(keyStr);
    if (it == obj.end() || it->is_null()) {
        return std::nullopt;
    }
    if (it->is_boolean()) {
        return it->get<bool>();
    }
    if (it->is_string()) {
        std::string text = it->get<std::string>();
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (text == "true" || text == "1" || text == "yes") {
            return true;
        }
        if (text == "false" || text == "0" || text == "no") {
            return false;
        }
    }
    SPDLOG_WARN("Field '{}' is not boolean; ignoring entry", keyStr);
    return std::nullopt;
}

/**
 * @brief Try to get a string field from a JSON object
 *
 * @param obj
 * @param key
 * @return std::optional<std::string>
 */
std::optional<std::string> tryGetString(const json& obj, std::string_view key)
{
    if (!obj.is_object()) {
        return std::nullopt;
    }
    const std::string keyStr(key);
    const auto it = obj.find(keyStr);
    if (it == obj.end() || it->is_null()) {
        return std::nullopt;
    }
    if (it->is_string()) {
        return it->get<std::string>();
    }
    SPDLOG_WARN("Field '{}' is not a string; ignoring entry", keyStr);
    return std::nullopt;
}

/**
 * @brief Convert a JSON node to a number if possible
 *
 * @param node
 * @return std::optional<double>
 */
std::optional<double> asNumber(const json& node)
{
    if (node.is_number_float()) {
        return node.get<double>();
    }
    if (node.is_number_integer()) {
        return static_cast<double>(node.get<int64_t>());
    }
    if (node.is_string()) {
        const std::string text = node.get<std::string>();
        try {
            size_t idx = 0;
            const double value = std::stod(text, &idx);
            if (idx == text.size()) {
                return value;
            }
        } catch (...) {
        }
    }
    return std::nullopt;
}

// ---------- Duration Extraction Helpers ----------
/**
 * @brief Convert a double value to milliseconds
 *
 * @param value
 * @return uint64_t
 */
uint64_t toMs(double value)
{
    return static_cast<uint64_t>(std::llround(value));
}

/**
 * @brief Convert a double value to microseconds
 *
 * @param value
 * @return uint64_t
 */
uint64_t toUs(double value)
{
    return static_cast<uint64_t>(std::llround(value));
}

/**
 * @brief Extract a duration range (min/max) in milliseconds from a JSON object
 *
 * @param obj
 * @param keyBase
 * @param defaultRange
 * @return std::pair<uint64_t, uint64_t>
 */
std::pair<uint64_t, uint64_t> extractDurationRangeMs(const json& obj, std::string_view keyBase, std::pair<uint64_t, uint64_t> defaultRange)
{
    uint64_t minMs = defaultRange.first;
    uint64_t maxMs = defaultRange.second;

    auto clampRange = [&](double minValMs, double maxValMs) {
        uint64_t minCandidate = toMs(minValMs);
        uint64_t maxCandidate = toMs(maxValMs);
        if (minCandidate == 0 && maxCandidate != 0) {
            minCandidate = maxCandidate;
        }
        if (maxCandidate == 0 && minCandidate != 0) {
            maxCandidate = minCandidate;
        }
        if (minCandidate == 0 && maxCandidate == 0) {
            minCandidate = std::max<uint64_t>(1, defaultRange.first ? defaultRange.first : 1);
            maxCandidate = std::max<uint64_t>(minCandidate, defaultRange.second ? defaultRange.second : minCandidate);
        }
        if (minCandidate > maxCandidate) {
            std::swap(minCandidate, maxCandidate);
        }
        minMs = minCandidate;
        maxMs = maxCandidate;
    };

    auto readWithScale = [&](const json& container, const char* key, double scale) -> std::optional<double> {
        const auto it = container.find(key);
        if (it != container.end()) {
            if (auto value = asNumber(*it)) {
                return *value * scale;
            }
        }
        return std::nullopt;
    };

    auto parseNode = [&](const json& node, double defaultScale) -> bool {
        bool localFound = false;
        double localMin = 0.0;
        double localMax = 0.0;

        if (node.is_object()) {
            if (auto minVal = readWithScale(node, "min", defaultScale)) {
                localMin = *minVal;
                localFound = true;
            }
            if (auto minVal = readWithScale(node, "min_ms", 1.0)) {
                localMin = *minVal;
                localFound = true;
            }
            if (auto minVal = readWithScale(node, "min_us", 0.001)) {
                localMin = *minVal;
                localFound = true;
            }

            if (auto maxVal = readWithScale(node, "max", defaultScale)) {
                localMax = *maxVal;
                localFound = true;
            }
            if (auto maxVal = readWithScale(node, "max_ms", 1.0)) {
                localMax = *maxVal;
                localFound = true;
            }
            if (auto maxVal = readWithScale(node, "max_us", 0.001)) {
                localMax = *maxVal;
                localFound = true;
            }

            if (!localFound) {
                if (auto meanVal = readWithScale(node, "mean", defaultScale)) {
                    localMin = localMax = *meanVal;
                    localFound = true;
                } else if (auto meanValMs = readWithScale(node, "mean_ms", 1.0)) {
                    localMin = localMax = *meanValMs;
                    localFound = true;
                } else if (auto meanValUs = readWithScale(node, "mean_us", 0.001)) {
                    localMin = localMax = *meanValUs;
                    localFound = true;
                } else if (auto valueVal = readWithScale(node, "value", defaultScale)) {
                    localMin = localMax = *valueVal;
                    localFound = true;
                }
            }
        } else if (auto numeric = asNumber(node)) {
            localMin = localMax = *numeric * defaultScale;
            localFound = true;
        }

        if (localFound) {
            const double fallback = localMax != 0.0 ? localMax : (localMin != 0.0 ? localMin : static_cast<double>(defaultRange.second));
            const double resolvedMin = localMin != 0.0 ? localMin : fallback;
            const double resolvedMax = localMax != 0.0 ? localMax : fallback;
            clampRange(resolvedMin, resolvedMax);
        }
        return localFound;
    };

    const std::string base(keyBase);
    bool updated = false;

    if (const auto it = obj.find(base + "_ms"); it != obj.end()) {
        updated = parseNode(*it, 1.0);
    }
    if (!updated) {
        if (const auto it = obj.find(base + "_us"); it != obj.end()) {
            updated = parseNode(*it, 0.001);
        }
    }
    if (!updated) {
        if (const auto it = obj.find(base); it != obj.end()) {
            updated = parseNode(*it, 1.0);
            if (!updated) {
                updated = parseNode(*it, 0.001);
            }
        }
    }

    if (!updated) {
        clampRange(static_cast<double>(defaultRange.first), static_cast<double>(defaultRange.second));
    }

    return { minMs, maxMs };
}

/**
 * @brief Extract a duration in milliseconds from a JSON object
 *
 * @param obj
 * @param keyBase
 * @param defaultValue
 * @return uint64_t
 */
uint64_t extractDurationMs(const json& obj, std::string_view keyBase, uint64_t defaultValue)
{
    if (!obj.is_object()) {
        return defaultValue;
    }
    const std::string base(keyBase);
    const std::string msKey = base + "_ms";
    if (auto msVal = tryGetNumber(obj, msKey)) {
        return toMs(*msVal);
    }
    const std::string sKey = base + "_s";
    if (auto sVal = tryGetNumber(obj, sKey)) {
        return toMs(*sVal * 1000.0);
    }
    const std::string usKey = base + "_us";
    if (auto usVal = tryGetNumber(obj, usKey)) {
        return toMs(*usVal / 1000.0);
    }
    const auto usObjIt = obj.find(usKey);
    if (usObjIt != obj.end() && usObjIt->is_object()) {
        if (auto maxVal = tryGetNumber(*usObjIt, "max")) {
            return toMs(*maxVal / 1000.0);
        }
        if (auto meanVal = tryGetNumber(*usObjIt, "mean")) {
            return toMs(*meanVal / 1000.0);
        }
        if (auto valueVal = tryGetNumber(*usObjIt, "value")) {
            return toMs(*valueVal / 1000.0);
        }
        if (auto minVal = tryGetNumber(*usObjIt, "min")) {
            return toMs(*minVal / 1000.0);
        }
    }
    const auto baseIt = obj.find(base);
    if (baseIt != obj.end() && baseIt->is_object()) {
        const json& nested = *baseIt;
        if (auto meanMs = tryGetNumber(nested, "mean_ms")) {
            return toMs(*meanMs);
        }
        if (auto offsetMs = tryGetNumber(nested, "offset_ms")) {
            return toMs(*offsetMs);
        }
        if (auto meanUs = tryGetNumber(nested, "mean_us")) {
            return toMs(*meanUs / 1000.0);
        }
        if (nested.contains("type")) {
            SPDLOG_DEBUG("Duration '{}' described as distribution '{}'; using representative value", base, nested.value("type", std::string("unknown")));
        }
    }
    return defaultValue;
}

/**
 * @brief Extract a duration in microseconds from a JSON object
 *
 * @param obj
 * @param keyBase
 * @param defaultValue
 * @return uint64_t
 */
uint64_t extractDurationUs(const json& obj, std::string_view keyBase, uint64_t defaultValue)
{
    if (!obj.is_object()) {
        return defaultValue;
    }
    const std::string base(keyBase);
    const std::string usKey = base + "_us";
    if (auto usVal = tryGetNumber(obj, usKey)) {
        return toUs(*usVal);
    }
    const std::string msKey = base + "_ms";
    if (auto msVal = tryGetNumber(obj, msKey)) {
        return toUs(*msVal * 1000.0);
    }
    const auto baseIt = obj.find(base);
    if (baseIt != obj.end() && baseIt->is_object()) {
        const json& nested = *baseIt;
        if (auto valueUs = tryGetNumber(nested, "us")) {
            return toUs(*valueUs);
        }
        if (auto valueMs = tryGetNumber(nested, "ms")) {
            return toUs(*valueMs * 1000.0);
        }
    }
    return defaultValue;
}

/**
 * @brief Get an from a JSON object or return a default value
 *
 * @param obj
 * @param key
 * @param defaultValue
 * @return int
 */
int getIntOr(const json& obj, std::string_view key, int defaultValue)
{
    if (auto value = tryGetNumber(obj, key)) {
        return static_cast<int>(std::llround(*value));
    }
    return defaultValue;
}

/**
 * @brief Get a string from a JSON object or return a default value
 *
 * @param obj
 * @param key
 * @param defaultValue
 * @return std::string
 */
std::string getStringOr(const json& obj, std::string_view key, std::string defaultValue)
{
    if (auto value = tryGetString(obj, key)) {
        return *value;
    }
    return defaultValue;
}

/**
 * @brief Get a boolean from a JSON object or return a default value
 *
 * @param obj
 * @param key
 * @param defaultValue
 * @return bool
 */
bool getBoolOr(const json& obj, std::string_view key, bool defaultValue)
{
    if (auto value = tryGetBool(obj, key)) {
        return *value;
    }
    return defaultValue;
}

} // namespace configUtils
