#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#define SPDLOG_DEBUG_ON
#define SPDLOG_TRACE_ON

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <limits>
#include <cctype>
#include <algorithm>
#include <vector>

// External libs
#include <argparse/argparse.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

// Internal project headers
#include "Event.h"
#include "MetricsCollector.h"
#include "RunQueue.h"
#include "Scheduler.h"
#include "SimulationEngine.h"
#include "Task.h"
#include "ConfigUtils.h"

using json = nlohmann::json;

// Utility: read file into string
static std::string readFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Utility: load JSON config file (simple helper)
static json loadJson(const std::string& path)
{
    return json::parse(readFile(path));
}

static json yamlNodeToJson(const YAML::Node& node)
{
    if (!node)
        return nullptr;

    switch (node.Type()) {
    case YAML::NodeType::Null:
        return nullptr;
    case YAML::NodeType::Scalar: {
        const auto nodeString = node.as<std::string>();
        if (nodeString == "true" || nodeString == "false")
            return node.as<bool>();
        try {
            size_t index = 0;
            const auto value = std::stod(nodeString, &index);
            if (index == nodeString.size())
                return value;
        } catch (...) {
            // Not a double
        }
        try {
            return node.as<int64_t>();
        } catch (...) {
            // Not an int
        }
        // Must be a string, I guess
        return nodeString;
    }
    case YAML::NodeType::Sequence: {
        json arr = json::array();
        for (const auto& item : node) {
            arr.push_back(yamlNodeToJson(item));
        }
        return arr;
    }
    case YAML::NodeType::Map: {
        json obj = json::object();
        for (const auto& keyVal : node) {
            obj[keyVal.first.as<std::string>()] = yamlNodeToJson(keyVal.second);
        }
        return obj;
    }
    default:
        return nullptr;
    }
}

static json loadConfig(const std::string& path)
{
    const auto fileExtension = std::filesystem::path(path).extension().string();
    if (fileExtension == ".yaml" || fileExtension == ".yml") {
        SPDLOG_DEBUG("Parsing YAML config: {}", path);
        return yamlNodeToJson(YAML::LoadFile(path));
    }

    SPDLOG_DEBUG("Parsing JSON config: {}", path);
    return loadJson(path);
}

namespace {

std::string trimCopy(const std::string& value)
{
    const auto beginIterator = std::find_if_not(value.begin(), value.end(), [](unsigned char character) { return std::isspace(character) != 0; });
    const auto endIterator = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) { return std::isspace(character) != 0; }).base();
    if (beginIterator >= endIterator) {
        return std::string();
    }
    return std::string(beginIterator, endIterator);
}

std::string normalizePolicy(const std::string& rawValue)
{
    std::string trimmed = trimCopy(rawValue);
    std::transform(trimmed.begin(), trimmed.end(), trimmed.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return trimmed;
}

std::vector<std::string> parsePolicies(const std::string& rawPolicies)
{
    std::vector<std::string> result;
    std::stringstream policyStream(rawPolicies);
    std::string policyToken;
    while (std::getline(policyStream, policyToken, ',')) {
        std::string normalized = normalizePolicy(policyToken);
        if (!normalized.empty()) {
            result.push_back(normalized);
        }
    }
    if (result.empty()) {
        std::string fallback = normalizePolicy(rawPolicies);
        if (!fallback.empty()) {
            result.push_back(fallback);
        }
    }
    return result;
}

std::unique_ptr<IScheduler> createScheduler(const std::string& policy)
{
    if (policy == "fcfs") {
        return std::make_unique<FCFSScheduler>();
    }
    if (policy == "sjf" || policy == "shortest_job_first" || policy == "shortest-job-first") {
        return std::make_unique<SJFScheduler>();
    }
    if (policy == "rr" || policy == "round_robin" || policy == "round-robin") {
        return std::make_unique<RRScheduler>();
    }
    if (policy == "mlfq") {
        return std::make_unique<MLFQScheduler>();
    }
    return nullptr;
}

std::filesystem::path deriveOutputPath(const std::filesystem::path& basePath, const std::string& policy, bool multi)
{
    if (!multi) {
        return basePath;
    }

    std::filesystem::path directory = basePath.parent_path();
    std::string stem = basePath.stem().string();
    std::string extension = basePath.extension().string();

    if (stem.empty()) {
        stem = "trace";
    }
    if (extension.empty()) {
        extension = ".json";
    }

    std::filesystem::path filename = stem + "_" + policy + extension;
    if (!directory.empty()) {
        return directory / filename;
    }
    return filename;
}

} // namespace

int main(int argc, char* argv[])
{

    argparse::ArgumentParser program("scheduler_sim", "1.0");

    program.add_argument("--workload_file")
        .help("Path to workload definition file (JSON or YAML)")
        .default_value(std::string("data/workload.json"));

    program.add_argument("--scenario_file")
        .help("Path to scenario configuration file (JSON or YAML)")
        .default_value(std::string("data/scenarios/scenario_1.yml"));

    program.add_argument("--scheduler_policy")
        .help("Scheduling policy to use: fcfs, sjf, srtf, rr, priority, mlfq, edf")
        .default_value(std::string("fcfs"));

    program.add_argument("--duration")
        .help("Simulation duration in milliseconds")
        .scan<'i', int>()
        .default_value(10000);

    program.add_argument("--out_file")
        .help("Output JSON file for trace and metrics")
        .default_value(std::string("results/trace.json"));

    program.add_argument("--verbose")
        .help("Enable detailed simulation logging")
        .default_value(false)
        .implicit_value(true);

    try {
        program.parse_args(argc, argv);
        SPDLOG_TRACE("Raw CLI parsed without errors");
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Argument parsing failed: {}", e.what());
        return 1;
    }

    // --- Parse CLI arguments ---
    std::string workloadPath = program.get<std::string>("--workload_file");
    std::string scenarioPath = program.get<std::string>("--scenario_file");
    std::string policyArg = program.get<std::string>("--scheduler_policy");
    std::string outputPath = program.get<std::string>("--out_file");
    int duration = program.get<int>("--duration");
    bool verbose = program.get<bool>("--verbose");
    SPDLOG_TRACE("CLI values -> workload='{}', scenario='{}', policy='{}', duration={}ms, out='{}', verbose={}",
        workloadPath,
        scenarioPath,
        policyArg,
        duration,
        outputPath,
        verbose);

    // --- Configure logging ---
    // Log pattern: [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [sourcefile:line function] message
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%# %!] %v");
    spdlog::set_level(verbose ? spdlog::level::trace : spdlog::level::info);

    SPDLOG_INFO("=== Embedded CPU Scheduler Simulation ===");

    // --- Load scenario configuration (needed for duration override) ---
    json scenarioConfig = json::object();
    uint64_t scenarioRunDurationMs = 0;
    try {
        SPDLOG_INFO("Loading scenario config from {}", scenarioPath);
        scenarioConfig = loadConfig(scenarioPath);
        SPDLOG_TRACE("Scenario config loaded: {} keys", scenarioConfig.is_object() ? scenarioConfig.size() : 0);
        const json systemCfg = scenarioConfig.value("system", json::object());
        scenarioRunDurationMs = simcfg::extractDurationMs(systemCfg, "run_duration", 0);
    } catch (const std::exception& e) {
        SPDLOG_WARN("Failed to load scenario config '{}': {} (using defaults)", scenarioPath, e.what());
        scenarioConfig = json::object();
    }

    bool durationOverridden = false;
    if (!program.is_used("--duration") && scenarioRunDurationMs > 0) {
        if (scenarioRunDurationMs > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            SPDLOG_WARN("Scenario run_duration ({}) exceeds supported CLI range; clamping to {}", scenarioRunDurationMs, std::numeric_limits<int>::max());
            duration = std::numeric_limits<int>::max();
        } else {
            duration = static_cast<int>(scenarioRunDurationMs);
        }
        durationOverridden = true;
    }

    std::vector<std::string> policies = parsePolicies(policyArg);
    if (policies.empty()) {
        SPDLOG_ERROR("No scheduler policies specified");
        return 1;
    }
    const bool multiPolicy = policies.size() > 1;

    if (multiPolicy) {
        std::string joined;
        for (size_t i = 0; i < policies.size(); ++i) {
            if (i != 0) {
                joined += ", ";
            }
            joined += policies[i];
        }
        SPDLOG_INFO("Policies: {}", joined);
    } else {
        SPDLOG_INFO("Policy: {}", policies.front());
    }
    if (durationOverridden) {
        SPDLOG_INFO("Duration: {} ms (scenario override)", duration);
    } else {
        SPDLOG_INFO("Duration: {} ms", duration);
    }
    SPDLOG_INFO("Output: {}", outputPath);

    // --- Load workload configuration ---
    json workloadConfig = json::object();
    try {
        SPDLOG_INFO("Loading workload config from {}", workloadPath);
        workloadConfig = loadConfig(workloadPath);
        SPDLOG_TRACE("Workload config loaded: {} keys", workloadConfig.is_object() ? workloadConfig.size() : 0);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to load workload config '{}': {}", workloadPath, e.what());
        return 1;
    }

    std::filesystem::path baseOutputPath(outputPath);
    int failures = 0;

    for (const auto& policy : policies) {
        SPDLOG_INFO("--- Running policy '{}' ---", policy);

        auto scheduler = createScheduler(policy);
        if (!scheduler) {
            SPDLOG_ERROR("Unknown scheduler policy '{}'", policy);
            ++failures;
            continue;
        }

        SimulationEngine engine(std::move(scheduler));

        try {
            SPDLOG_TRACE("Configuring scenario");
            engine.configureScenario(scenarioConfig, static_cast<uint64_t>(duration));
            SPDLOG_TRACE("Scenario configuration completed");
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Scenario configuration failed for policy '{}': {}", policy, e.what());
            ++failures;
            continue;
        }

        try {
            SPDLOG_TRACE("Loading workload into engine");
            engine.loadWorkload(workloadConfig, static_cast<uint64_t>(duration));
            SPDLOG_TRACE("Workload load completed");
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Workload loading failed for policy '{}': {}", policy, e.what());
            ++failures;
            continue;
        }

        SPDLOG_INFO("Running simulation for {} ms", duration);
        try {
            SPDLOG_TRACE("Starting engine.run (policy '{}')", policy);
            engine.run(duration);
            SPDLOG_TRACE("engine.run completed successfully (policy '{}')", policy);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Simulation run failed for policy '{}': {}", policy, e.what());
            ++failures;
            continue;
        }

        json results;
        try {
            results = engine.metrics().buildReport();
            SPDLOG_TRACE("Exported metrics payload size (policy '{}'): {} bytes", policy, results.dump().size());
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Exporting metrics failed for policy '{}': {}", policy, e.what());
            ++failures;
            continue;
        }

        std::filesystem::path outputPathForPolicy = deriveOutputPath(baseOutputPath, policy, multiPolicy);

        try {
            if (outputPathForPolicy.has_parent_path() && !std::filesystem::exists(outputPathForPolicy.parent_path())) {
                SPDLOG_TRACE("Creating output directories: {}", outputPathForPolicy.parent_path().string());
                std::filesystem::create_directories(outputPathForPolicy.parent_path());
            }
            SPDLOG_TRACE("Writing results to '{}'", outputPathForPolicy.string());
            std::ofstream outFile(outputPathForPolicy);
            if (!outFile) {
                SPDLOG_ERROR("Failed to open output file '{}'", outputPathForPolicy.string());
                ++failures;
                continue;
            }
            outFile << std::setw(2) << results;
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Writing results failed for policy '{}': {}", policy, e.what());
            ++failures;
            continue;
        }

        SPDLOG_INFO("Simulation complete. Results saved to {}", outputPathForPolicy.string());
    }

    if (failures != 0) {
        SPDLOG_ERROR("{} scheduling run(s) failed", failures);
        return 1;
    }

    return 0;
}
