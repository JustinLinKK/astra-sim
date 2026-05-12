/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#pragma once

#include "astra-sim/analytical/AnalyticalConfig.hh"
#include "astra-sim/analytical/AnalyticalModelFactory.hh"
#include "astra-sim/common/Logging.hh"
#include "astra-sim/system/Sys.hh"
#include "common/CmdLineParser.hh"
#include <astra-network-analytical/common/EventQueue.h>
#include <astra-network-analytical/common/NetworkParser.h>
#include <remote_memory_backend/analytical/AnalyticalRemoteMemory.hh>
#include <memory>
#include <optional>
#include <vector>

namespace AstraSimAnalytical {

struct AnalyticalCliConfig {
    std::string analytical_config_path;
    std::string workload_configuration;
    std::string comm_group_configuration;
    std::string system_configuration;
    std::string remote_memory_configuration;
    std::string network_configuration;
    std::string logging_configuration;
    std::string logging_folder;
    int num_queues_per_dim;
    double compute_scale;
    double comm_scale;
    double injection_scale;
    bool rendezvous_protocol;
};

inline AnalyticalCliConfig parse_analytical_cli_config(
    const CmdLineParser& cmd_line_parser) {
    return AnalyticalCliConfig{
        cmd_line_parser.get<std::string>("analytical-config"),
        cmd_line_parser.get<std::string>("workload-configuration"),
        cmd_line_parser.get<std::string>("comm-group-configuration"),
        cmd_line_parser.get<std::string>("system-configuration"),
        cmd_line_parser.get<std::string>("remote-memory-configuration"),
        cmd_line_parser.get<std::string>("network-configuration"),
        cmd_line_parser.get<std::string>("logging-configuration"),
        cmd_line_parser.get<std::string>("logging-folder"),
        cmd_line_parser.get<int>("num-queues-per-dim"),
        cmd_line_parser.get<double>("compute-scale"),
        cmd_line_parser.get<double>("comm-scale"),
        cmd_line_parser.get<double>("injection-scale"),
        cmd_line_parser.get<bool>("rendezvous-protocol"),
    };
}

template <typename NetworkApiT,
          typename InitializeTopologyFn,
          typename BuildTopologyFn,
          typename ConfigureNetworkFn>
int run_analytical_backend(int argc,
                           char* argv[],
                           InitializeTopologyFn initialize_topology,
                           BuildTopologyFn build_topology,
                           ConfigureNetworkFn configure_network) {
    auto cmd_line_parser = CmdLineParser(argv[0]);
    cmd_line_parser.parse(argc, argv);
    const auto cli_config = parse_analytical_cli_config(cmd_line_parser);

    std::optional<AstraSim::AnalyticalConfig> analytical_config;
    if (cli_config.analytical_config_path != "empty") {
        analytical_config = AstraSim::AnalyticalConfig::load_from_file(
            cli_config.analytical_config_path);
    }

    if (analytical_config.has_value() &&
        analytical_config->mode !=
            AstraSim::AnalyticalMode::serving_disagg_colocated &&
        analytical_config->mode != AstraSim::AnalyticalMode::serving_scale) {
        AstraSim::LoggerFactory::init(cli_config.logging_configuration,
                                      cli_config.logging_folder);
        auto model = AstraSim::AnalyticalModelFactory::create(
            *analytical_config, AstraSim::AnalyticalContext{{}, argv[0]});
        model->run();
        AstraSim::LoggerFactory::shutdown();
        return 0;
    }

    auto workload_configuration = cli_config.workload_configuration;
    auto comm_group_configuration = cli_config.comm_group_configuration;
    auto system_configuration = cli_config.system_configuration;
    auto remote_memory_configuration = cli_config.remote_memory_configuration;
    auto network_configuration = cli_config.network_configuration;
    auto logging_configuration = cli_config.logging_configuration;
    auto logging_folder = cli_config.logging_folder;
    auto num_queues_per_dim = cli_config.num_queues_per_dim;
    auto injection_scale = cli_config.injection_scale;
    auto comm_scale = cli_config.comm_scale;
    auto rendezvous_protocol = cli_config.rendezvous_protocol;

    if (analytical_config.has_value()) {
        if (analytical_config->mode ==
            AstraSim::AnalyticalMode::serving_disagg_colocated) {
            const auto& serving = *analytical_config->serving_disagg_colocated;
            workload_configuration = serving.workload_configuration;
            comm_group_configuration = serving.comm_group_configuration;
            system_configuration = serving.system_configuration;
            remote_memory_configuration = serving.remote_memory_configuration;
            network_configuration = serving.network_configuration;
            logging_configuration = serving.logging_configuration;
            logging_folder = serving.logging_folder;
            num_queues_per_dim = serving.num_queues_per_dim;
            injection_scale = serving.injection_scale;
            comm_scale = serving.comm_scale;
            rendezvous_protocol = serving.rendezvous_protocol;
        } else {
            const auto& serving = *analytical_config->serving_scale;
            workload_configuration = serving.workload_configuration;
            comm_group_configuration = serving.comm_group_configuration;
            system_configuration = serving.system_configuration;
            remote_memory_configuration = serving.remote_memory_configuration;
            network_configuration = serving.network_configuration;
            logging_configuration = serving.logging_configuration;
            logging_folder = serving.logging_folder;
            num_queues_per_dim = serving.num_queues_per_dim;
            injection_scale = serving.injection_scale;
            comm_scale = serving.comm_scale;
            rendezvous_protocol = serving.rendezvous_protocol;
        }
    }

    AstraSim::LoggerFactory::init(logging_configuration, logging_folder);

    const auto event_queue = std::make_shared<NetworkAnalytical::EventQueue>();
    initialize_topology(event_queue);

    const auto network_parser =
        NetworkAnalytical::NetworkParser(network_configuration);
    const auto topology = build_topology(network_parser);
    configure_network(event_queue, topology);

    const auto npus_count = topology->get_npus_count();
    const auto npus_count_per_dim = topology->get_npus_count_per_dim();
    const auto dims_count = topology->get_dims_count();

    auto network_apis = std::vector<std::unique_ptr<NetworkApiT>>();
    const auto memory_api = std::make_unique<Analytical::AnalyticalRemoteMemory>(
        remote_memory_configuration);
    auto systems = std::vector<AstraSim::Sys*>();
    std::unique_ptr<AstraSim::AnalyticalModel> model;
    auto queues_per_dim = std::vector<int>();
    for (auto i = 0; i < dims_count; i++) {
        queues_per_dim.push_back(num_queues_per_dim);
    }

    const bool create_workload = !analytical_config.has_value();

    for (int i = 0; i < npus_count; i++) {
        auto network_api = std::make_unique<NetworkApiT>(i);
        auto* const system =
            new AstraSim::Sys(i, workload_configuration, comm_group_configuration,
                              system_configuration, memory_api.get(),
                              network_api.get(), npus_count_per_dim,
                              queues_per_dim, injection_scale, comm_scale,
                              rendezvous_protocol, create_workload);
        network_apis.push_back(std::move(network_api));
        systems.push_back(system);
    }

    if (create_workload) {
        for (int i = 0; i < npus_count; i++) {
            systems[i]->workload->fire();
        }
    } else {
        model = AstraSim::AnalyticalModelFactory::create(
            *analytical_config, AstraSim::AnalyticalContext{systems, argv[0]});
        model->run();
    }

    while (!event_queue->finished()) {
        event_queue->proceed();
    }

    for (auto* system : systems) {
        delete system;
    }

    AstraSim::LoggerFactory::shutdown();
    return 0;
}

}  // namespace AstraSimAnalytical
