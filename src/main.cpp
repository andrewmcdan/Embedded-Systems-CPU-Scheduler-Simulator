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
        const auto s = node.as<std::string>();
        if (s == "true" || s == "false")
            return node.as<bool>();
        try {
            size_t idx = 0;
            const auto value = std::stod(s, &idx);
            if (idx == s.size())
                return value;
        } catch (...) {
        }
        try {
            return node.as<int64_t>();
        } catch (...) {
        }
        return s;
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
        for (const auto& kv : node) {
            obj[kv.first.as<std::string>()] = yamlNodeToJson(kv.second);
        }
        return obj;
    }
    default:
        return nullptr;
    }
}

static json loadConfig(const std::string& path)
{
    const auto ext = std::filesystem::path(path).extension().string();
    if (ext == ".yaml" || ext == ".yml") {
        SPDLOG_DEBUG("Parsing YAML config: {}", path);
        return yamlNodeToJson(YAML::LoadFile(path));
    }

    SPDLOG_DEBUG("Parsing JSON config: {}", path);
    return loadJson(path);
}

int main(int argc, char* argv[])
{

    argparse::ArgumentParser program("scheduler_sim", "1.0");

    program.add_argument("--workload")
        .help("Path to workload definition file (JSON or YAML)")
        .default_value(std::string("data/workload.json"));

    program.add_argument("--scenario")
        .help("Path to scenario configuration file (JSON or YAML)")
        .default_value(std::string("data/scenarios/scenario_1.yml"));

    program.add_argument("--policy")
        .help("Scheduling policy to use: fcfs, sjf, srtf, rr, priority, mlfq, edf")
        .default_value(std::string("fcfs"));

    program.add_argument("--duration")
        .help("Simulation duration in milliseconds")
        .scan<'i', int>()
        .default_value(10000);

    program.add_argument("--out")
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
    std::string workloadPath = program.get<std::string>("--workload");
    std::string scenarioPath = program.get<std::string>("--scenario");
    std::string policyName = program.get<std::string>("--policy");
    std::string outputPath = program.get<std::string>("--out");
    int duration = program.get<int>("--duration");
    bool verbose = program.get<bool>("--verbose");
    SPDLOG_TRACE("CLI values -> workload='{}', scenario='{}', policy='{}', duration={}ms, out='{}', verbose={}",
        workloadPath,
        scenarioPath,
        policyName,
        duration,
        outputPath,
        verbose);

    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%# %!] %v");
    spdlog::set_level(verbose ? spdlog::level::trace : spdlog::level::info);

    SPDLOG_INFO("=== Embedded Scheduler Simulation ===");
    SPDLOG_INFO("Policy: {}", policyName);
    SPDLOG_INFO("Duration: {} ms", duration);
    SPDLOG_INFO("Output: {}", outputPath);

    // --- Load configuration files ---
    json workloadConfig = json::object();
    json scenarioConfig = json::object();

    try {
        SPDLOG_INFO("Loading workload config from {}", workloadPath);
        workloadConfig = loadConfig(workloadPath);
        SPDLOG_TRACE("Workload config loaded: {} keys", workloadConfig.is_object() ? workloadConfig.size() : 0);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to load workload config '{}': {}", workloadPath, e.what());
        return 1;
    }

    try {
        SPDLOG_INFO("Loading scenario config from {}", scenarioPath);
        scenarioConfig = loadConfig(scenarioPath);
        SPDLOG_TRACE("Scenario config loaded: {} keys", scenarioConfig.is_object() ? scenarioConfig.size() : 0);
    } catch (const std::exception& e) {
        SPDLOG_WARN("Failed to load scenario config '{}': {} (using defaults)", scenarioPath, e.what());
        scenarioConfig = json::object();
    }

    // --- Create scheduler based on user choice ---
    std::unique_ptr<IScheduler> scheduler;

    if (policyName == "fcfs")
        scheduler = std::make_unique<FCFSScheduler>();
    // else if (policyName == "sjf") scheduler = std::make_unique<SJFScheduler>();
    // else if (policyName == "srtf") scheduler = std::make_unique<SRTFScheduler>();
    // else if (policyName == "rr") scheduler = std::make_unique<RRScheduler>();
    // else if (policyName == "priority") scheduler = std::make_unique<PriorityScheduler>();
    // else if (policyName == "mlfq") scheduler = std::make_unique<MLFQScheduler>();
    // else if (policyName == "edf") scheduler = std::make_unique<EDFScheduler>();
    else {
        SPDLOG_ERROR("Unknown policy '{}'", policyName);
        return 1;
    }

    SPDLOG_TRACE("Constructing scheduler for policy '{}'", policyName);

    // --- Create simulation engine ---
    SPDLOG_INFO("Instantiating simulation engine with policy {}", policyName);
    SimulationEngine engine(std::move(scheduler));

    try {
        SPDLOG_TRACE("Loading workload into engine");
        engine.loadWorkload(workloadConfig);
        SPDLOG_TRACE("Workload load completed");
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Workload loading failed: {}", e.what());
        return 1;
    }

    try {
        SPDLOG_TRACE("Configuring scenario");
        engine.configureScenario(scenarioConfig);
        SPDLOG_TRACE("Scenario configuration completed");
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Scenario configuration failed: {}", e.what());
        return 1;
    }

    // Run simulation
    SPDLOG_INFO("Running simulation for {} ms", duration);
    try {
        SPDLOG_TRACE("Starting engine.run");
        engine.run(duration);
        SPDLOG_TRACE("engine.run completed successfully");
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Simulation run failed: {}", e.what());
        return 1;
    }

    // Export metrics and timeline
    json results;
    try {
        results = engine.exportResults();
        SPDLOG_TRACE("Exported results payload size: {} bytes", results.dump().size());
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exporting results failed: {}", e.what());
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

    if (verbose) {
        SPDLOG_DEBUG("{}", results.dump(2));
    } else {
        SPDLOG_TRACE("Verbose flag disabled; skipping pretty-print dump");
    }

    return 0;
}
