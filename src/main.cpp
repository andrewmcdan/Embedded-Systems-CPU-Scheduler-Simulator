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

int main(int argc, char* argv[])
{
    try {
        argparse::ArgumentParser program("scheduler_sim", "1.0");

        program.add_argument("--workload")
            .help("Path to workload definition file (JSON or YAML)")
            .default_value(std::string("data/workload.json"));

        program.add_argument("--scenario")
            .help("Path to scenario configuration file (JSON or YAML)")
            .default_value(std::string("data/scenarios/scenario_1.json"));

        program.add_argument("--policy")
            .help("Scheduling policy to use: fcfs, sjf, srtf, rr, priority, mlfq, edf")
            .default_value(std::string("mlfq"));

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

        program.parse_args(argc, argv);

        // --- Parse CLI arguments ---
        std::string workloadPath = program.get<std::string>("--workload");
        std::string scenarioPath = program.get<std::string>("--scenario");
        std::string policyName = program.get<std::string>("--policy");
        std::string outputPath = program.get<std::string>("--out");
        int duration = program.get<int>("--duration");
        bool verbose = program.get<bool>("--verbose");

        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        spdlog::set_level(verbose ? spdlog::level::debug : spdlog::level::info);

        spdlog::info("=== Embedded Scheduler Simulation ===");
        spdlog::info("Policy: {}", policyName);
        spdlog::info("Duration: {} ms", duration);
        spdlog::info("Output: {}", outputPath);

        // --- Load configuration files ---
        json workloadConfig;
        json scenarioConfig;

        try {
            spdlog::info("Loading workload config from {}", workloadPath);
            workloadConfig = loadJson(workloadPath);
            spdlog::info("Loading scenario config from {}", scenarioPath);
            scenarioConfig = loadJson(scenarioPath);
        } catch (const std::exception& e) {
            spdlog::warn("Could not load config files ({}). Using defaults.", e.what());
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
            spdlog::error("Unknown policy '{}'", policyName);
            return 1;
        }

        // --- Create simulation engine ---
        spdlog::info("Instantiating simulation engine with policy {}", policyName);
        SimulationEngine engine(std::move(scheduler));

        // Load tasks into the simulation
        engine.loadWorkload(workloadConfig);
        engine.configureScenario(scenarioConfig);

        // Run simulation
        spdlog::info("Running simulation for {} ms", duration);
        engine.run(duration);

        // Export metrics and timeline
        json results = engine.exportResults();
        std::ofstream outFile(outputPath);
        outFile << std::setw(2) << results;
        outFile.close();

        spdlog::info("Simulation complete. Results saved to {}", outputPath);

        if (verbose) {
            spdlog::debug("{}", results.dump(2));
        }

        return 0;

    } catch (const std::exception& e) {
        spdlog::error("Fatal error: {}", e.what());
        return 1;
    }
}
