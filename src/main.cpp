/**
 * @file main.cpp
 * @author Andrew McDaniel
 * @brief Command-line entry point for the scheduler simulator.
 *
 * Parses arguments, configures logging, loads scenarios and workloads, and executes selected
 * scheduling policies while exporting results.
 */
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// External libs
#include <argparse/argparse.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

// Internal project headers
#include "ConfigUtils.h"
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

struct PolicyDescriptor {
    std::string canonicalName;
    std::vector<std::string> aliases;
    std::function<std::unique_ptr<IScheduler>(const std::string&)> factory;
};

const std::vector<PolicyDescriptor> kPolicyDescriptors = {
    { "fcfs", {}, [](const std::string&) { return std::make_unique<FCFSScheduler>(); } },
    { "sjf", { "shortest_job_first", "shortest-job-first" }, [](const std::string&) { return std::make_unique<SJFScheduler>(); } },
    { "priority", { "priority_scheduling", "priority-scheduling" }, [](const std::string&) { return std::make_unique<PriorityScheduler>(); } },
    { "edf", { "earliest_deadline_first", "earliest-deadline-first" }, [](const std::string&) { return std::make_unique<EDFScheduler>(); } },
    { "linux", { "cfs", "cfs_linux" }, [](const std::string&) { return std::make_unique<LinuxScheduler>(); } },
    { "mlq", { "multi_level_queue", "multi-level-queue" }, [](const std::string&) { return std::make_unique<MLQScheduler>(); } },
    { "posix_rt", { "posix-rt", "rt", "sched_fifo", "sched_rr" }, [](const std::string& matched) {
        const bool rrMode = (matched == "sched_rr");
        return std::make_unique<PosixRTScheduler>(rrMode);
    } },
    { "priority_based", { "priority-aging", "priority_aging" }, [](const std::string&) { return std::make_unique<PriorityAgingScheduler>(); } },
    { "proportional", { "proportional_share", "weighted_fair" }, [](const std::string&) { return std::make_unique<ProportionalShareScheduler>(); } },
    { "rms", { "rate_monotonic", "rate-monotonic" }, [](const std::string&) { return std::make_unique<RMSScheduler>(); } },
    { "windows", { "win", "win32" }, [](const std::string&) { return std::make_unique<WindowsScheduler>(); } },
    { "rr", { "round_robin", "round-robin" }, [](const std::string&) { return std::make_unique<RRScheduler>(); } },
    { "mlfq", {}, [](const std::string&) { return std::make_unique<MLFQScheduler>(); } },
};

bool matchesPolicy(const PolicyDescriptor& descriptor, const std::string& normalizedPolicy)
{
    if (normalizedPolicy == descriptor.canonicalName) {
        return true;
    }
    return std::find(descriptor.aliases.begin(), descriptor.aliases.end(), normalizedPolicy) != descriptor.aliases.end();
}

const PolicyDescriptor* findPolicyDescriptor(const std::string& normalizedPolicy)
{
    for (const auto& descriptor : kPolicyDescriptors) {
        if (matchesPolicy(descriptor, normalizedPolicy)) {
            return &descriptor;
        }
    }
    return nullptr;
}

std::string canonicalPolicyList()
{
    std::vector<std::string> canonicalNames;
    canonicalNames.reserve(kPolicyDescriptors.size());
    for (const auto& descriptor : kPolicyDescriptors) {
        canonicalNames.push_back(descriptor.canonicalName);
    }
    std::sort(canonicalNames.begin(), canonicalNames.end());

    std::string joined;
    for (size_t i = 0; i < canonicalNames.size(); ++i) {
        if (i != 0) {
            joined += ", ";
        }
        joined += canonicalNames[i];
    }

    return joined;
}

std::string aliasPolicyList()
{
    std::vector<std::string> aliases;
    for (const auto& descriptor : kPolicyDescriptors) {
        aliases.insert(aliases.end(), descriptor.aliases.begin(), descriptor.aliases.end());
    }
    std::sort(aliases.begin(), aliases.end());
    aliases.erase(std::unique(aliases.begin(), aliases.end()), aliases.end());

    std::string joined;
    for (size_t i = 0; i < aliases.size(); ++i) {
        if (i != 0) {
            joined += ", ";
        }
        joined += aliases[i];
    }
    return joined;
}

std::string buildPolicyHelpText()
{
    const std::string canonical = canonicalPolicyList();
    const std::string aliases = aliasPolicyList();

    std::string help = "Scheduling policy to use (comma-separated to run multiple): " + canonical;
    if (!aliases.empty()) {
        help += ". Aliases also accepted: " + aliases;
    }
    return help;
}

spdlog::level::level_enum parseLogLevel(const std::string& rawValue)
{
    std::string lowered = trimCopy(rawValue);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (lowered == "trace")
        return spdlog::level::trace;
    if (lowered == "debug")
        return spdlog::level::debug;
    if (lowered == "info" || lowered == "information")
        return spdlog::level::info;
    if (lowered == "warn" || lowered == "warning")
        return spdlog::level::warn;
    if (lowered == "error" || lowered == "err")
        return spdlog::level::err;
    if (lowered == "critical" || lowered == "fatal")
        return spdlog::level::critical;
    if (lowered == "off" || lowered == "none")
        return spdlog::level::off;

    throw std::invalid_argument("Unrecognized log level: " + rawValue);
}

std::string logLevelToString(spdlog::level::level_enum level)
{
    switch (level) {
    case spdlog::level::trace:
        return "trace";
    case spdlog::level::debug:
        return "debug";
    case spdlog::level::info:
        return "info";
    case spdlog::level::warn:
        return "warn";
    case spdlog::level::err:
        return "error";
    case spdlog::level::critical:
        return "critical";
    case spdlog::level::off:
        return "off";
    default:
        return "unknown";
    }
}

std::unique_ptr<IScheduler> createScheduler(const std::string& policy)
{
    const std::string normalized = normalizePolicy(policy);
    const PolicyDescriptor* descriptor = findPolicyDescriptor(normalized);
    if (!descriptor) {
        return nullptr;
    }
    return descriptor->factory(normalized);
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
        .help(buildPolicyHelpText())
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

    program.add_argument("--log_file")
        .help("Path to the rotating log file")
        .default_value(std::string("logs/scheduler.log"));

    program.add_argument("--log_file_level")
        .help("Minimum log level for the file sink (trace, debug, info, warn, error, critical, off)")
        .default_value(std::string("debug"));

    program.add_argument("--log_stdout_level")
        .help("Minimum log level for stdout (trace, debug, info, warn, error, critical, off)")
        .default_value(std::string("info"));

    program.add_argument("--log_file_max_size_mb")
        .help("Maximum size of the log file before rotation (in MB)")
        .scan<'i', int>()
        .default_value(50);

    program.add_argument("--log_file_max_files")
        .help("Maximum number of rotated log files to keep")
        .scan<'i', int>()
        .default_value(5);

    program.add_argument("--results_subdir")
        .help("Optional subdirectory under the results directory (e.g. 'experiment_01')")
        .default_value(std::string());

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Argument parsing failed: " << e.what() << std::endl;
        return 1;
    }

    // --- Parse CLI arguments ---
    std::string workloadPath = program.get<std::string>("--workload_file");
    std::string scenarioPath = program.get<std::string>("--scenario_file");
    std::string policyArg = program.get<std::string>("--scheduler_policy");
    std::string outputPath = program.get<std::string>("--out_file");
    int duration = program.get<int>("--duration");
    bool verbose = program.get<bool>("--verbose");

    std::string logFilePath = program.get<std::string>("--log_file");
    std::string logFileLevelStr = program.get<std::string>("--log_file_level");
    std::string logStdoutLevelStr = program.get<std::string>("--log_stdout_level");
    int logFileMaxSizeMb = program.get<int>("--log_file_max_size_mb");
    int logFileMaxFiles = program.get<int>("--log_file_max_files");
    std::string resultsSubdir = program.get<std::string>("--results_subdir");

    resultsSubdir = trimCopy(resultsSubdir);
    if (!resultsSubdir.empty()) {
        std::filesystem::path subdirPath(resultsSubdir);
        if (subdirPath.is_absolute()) {
            std::cerr << "--results_subdir must be a relative path (received absolute path)" << std::endl;
            return 1;
        }
        for (const auto& part : subdirPath) {
            if (part == "..") {
                std::cerr << "--results_subdir cannot contain '..' segments" << std::endl;
                return 1;
            }
        }
    }

    std::filesystem::path outputPathFs(outputPath);
    if (!resultsSubdir.empty()) {
        std::filesystem::path baseDir;
        if (outputPathFs.has_filename()) {
            baseDir = outputPathFs.parent_path();
        } else {
            baseDir = outputPathFs;
        }
        if (baseDir.empty()) {
            baseDir = std::filesystem::path("results");
        }
        baseDir /= std::filesystem::path(resultsSubdir);
        if (outputPathFs.has_filename()) {
            outputPathFs = baseDir / outputPathFs.filename();
        } else {
            outputPathFs = baseDir;
        }
        outputPath = outputPathFs.generic_string();
    }

    const bool stdoutLevelExplicit = program.is_used("--log_stdout_level");
    const bool fileLevelExplicit = program.is_used("--log_file_level");

    spdlog::level::level_enum stdoutLevel;
    spdlog::level::level_enum fileLevel;
    try {
        stdoutLevel = parseLogLevel(logStdoutLevelStr);
        fileLevel = parseLogLevel(logFileLevelStr);
    } catch (const std::exception& ex) {
        std::cerr << "Invalid log level: " << ex.what() << std::endl;
        return 1;
    }

    if (verbose) {
        if (!stdoutLevelExplicit) {
            stdoutLevel = spdlog::level::debug;
        }
        if (!fileLevelExplicit) {
            fileLevel = spdlog::level::trace;
        }
    }

    if (logFileMaxSizeMb <= 0) {
        std::cerr << "log_file_max_size_mb must be greater than zero" << std::endl;
        return 1;
    }
    if (logFileMaxFiles <= 0) {
        std::cerr << "log_file_max_files must be greater than zero" << std::endl;
        return 1;
    }

    std::filesystem::path logFilePathFs(logFilePath);
    if (auto parent = logFilePathFs.parent_path(); !parent.empty() && !std::filesystem::exists(parent)) {
        try {
            std::filesystem::create_directories(parent);
        } catch (const std::exception& ex) {
            std::cerr << "Failed to create log directory '" << parent.string() << "': " << ex.what() << std::endl;
            return 1;
        }
    }

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(stdoutLevel);
    consoleSink->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    const std::size_t maxSizeBytes = static_cast<std::size_t>(logFileMaxSizeMb) * 1024ULL * 1024ULL;
    std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> fileSink;
    try {
        fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFilePathFs.string(), maxSizeBytes, static_cast<std::size_t>(logFileMaxFiles));
    } catch (const std::exception& ex) {
        std::cerr << "Failed to initialise log file sink '" << logFilePathFs.string() << "': " << ex.what() << std::endl;
        return 1;
    }
    fileSink->set_level(fileLevel);
    fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%# %!] %v");

    auto logger = std::make_shared<spdlog::logger>("scheduler_sim", spdlog::sinks_init_list { consoleSink, fileSink });
    const spdlog::level::level_enum globalLevel = std::min(stdoutLevel, fileLevel);
    logger->set_level(globalLevel);
    logger->flush_on(spdlog::level::err);
    spdlog::set_default_logger(logger);

    SPDLOG_INFO("Logging configured: stdout>={} file>={} ({})",
        logLevelToString(stdoutLevel),
        logLevelToString(fileLevel),
        logFilePathFs.string());
    SPDLOG_TRACE("Log rotation: max_size={} bytes, max_files={}", maxSizeBytes, logFileMaxFiles);

    SPDLOG_TRACE("CLI values -> workload='{}', scenario='{}', policy='{}', duration={}ms, out='{}', verbose={}",
        workloadPath,
        scenarioPath,
        policyArg,
        duration,
        outputPath,
        verbose);

    SPDLOG_INFO("=== Embedded CPU Scheduler Simulation ===");

    // --- Load scenario configuration (needed for duration override) ---
    json scenarioConfig = json::object();
    uint64_t scenarioRunDurationMs = 0;
    try {
        SPDLOG_INFO("Loading scenario config from {}", scenarioPath);
        scenarioConfig = loadConfig(scenarioPath);
        SPDLOG_TRACE("Scenario config loaded: {} keys", scenarioConfig.is_object() ? scenarioConfig.size() : 0);
        const json systemCfg = scenarioConfig.value("system", json::object());
        scenarioRunDurationMs = configUtils::extractDurationMs(systemCfg, "run_duration", 0);
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
    if (!resultsSubdir.empty()) {
        SPDLOG_INFO("Results subdirectory: {}", resultsSubdir);
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
            SPDLOG_ERROR("Unknown scheduler policy '{}' (supported: {})", policy, canonicalPolicyList());
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
