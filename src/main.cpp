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
    std::string policyName = program.get<std::string>("--scheduler_policy");
    std::string outputPath = program.get<std::string>("--out_file");
    int duration = program.get<int>("--duration");
    bool verbose = program.get<bool>("--verbose");
    SPDLOG_TRACE("CLI values -> workload='{}', scenario='{}', policy='{}', duration={}ms, out='{}', verbose={}",
        workloadPath,
        scenarioPath,
        policyName,
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

    SPDLOG_INFO("Policy: {}", policyName);
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

    // --- Create scheduler based on user choice ---
    std::unique_ptr<IScheduler> scheduler;

    if (policyName == "fcfs")
        scheduler = std::make_unique<FCFSScheduler>();
    // TODO: implement other schedulers
    // else if (policyName == "sjf") scheduler = std::make_unique<SJFScheduler>();
    // else if (policyName == "srtf") scheduler = std::make_unique<SRTFScheduler>();
    // else if (policyName == "rr") scheduler = std::make_unique<RRScheduler>();
    // else if (policyName == "priority") scheduler = std::make_unique<PriorityScheduler>();
    // else if (policyName == "mlfq") scheduler = std::make_unique<MLFQScheduler>();
    // else if (policyName == "edf") scheduler = std::make_unique<EDFScheduler>();
    else {
        SPDLOG_ERROR("Unknown scheduler policy '{}'", policyName);
        return 1;
    }

    // --- Create simulation engine ---
    SPDLOG_INFO("Instantiating simulation engine with scheduler policy {}", policyName);
    SimulationEngine engine(std::move(scheduler));

    try {
        SPDLOG_TRACE("Configuring scenario");
        engine.configureScenario(scenarioConfig, static_cast<uint64_t>(duration));
        SPDLOG_TRACE("Scenario configuration completed");
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Scenario configuration failed: {}", e.what());
        return 1;
    }

    try {
        SPDLOG_TRACE("Loading workload into engine");
        engine.loadWorkload(workloadConfig, static_cast<uint64_t>(duration));
        SPDLOG_TRACE("Workload load completed");
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Workload loading failed: {}", e.what());
        return 1;
    }

    // Run simulation
    SPDLOG_INFO("Running simulation for {} ms", duration);
    try {
        SPDLOG_TRACE("Starting engine.run");
        // Run the simulation
        engine.run(duration);
        SPDLOG_TRACE("engine.run completed successfully");
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Simulation run failed: {}", e.what());
        return 1;
    }

    // Export metrics and timeline
    json results;
    try {
        results = engine.metrics().buildReport();
        SPDLOG_TRACE("Exported metrics payload size: {} bytes", results.dump().size());
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exporting metrics failed: {}", e.what());
        return 1;
    }

    try {
        // if the output directory does not exist, create it
        std::filesystem::path outPath(outputPath);
        if (outPath.has_parent_path() && !std::filesystem::exists(outPath.parent_path())) {
            SPDLOG_TRACE("Creating output directories: {}", outPath.parent_path().string());
            std::filesystem::create_directories(outPath.parent_path());
        }
        SPDLOG_TRACE("Writing results to '{}'", outputPath);
        std::ofstream outFile(outputPath);
        if (!outFile) {
            SPDLOG_ERROR("Failed to open output file '{}'", outputPath);
            return 1;
        }
        outFile << std::setw(2) << results;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Writing results failed: {}", e.what());
        return 1;
    }

    SPDLOG_INFO("Simulation complete. Results saved to {}", outputPath);

    // if (verbose) {
    //     SPDLOG_DEBUG("{}", results.dump(2));
    // }

    return 0;
}
