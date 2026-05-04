/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "common/AnalyticalLauncher.hh"
#include "congestion_aware/CongestionAwareNetworkApi.hh"
#include <astra-network-analytical/congestion_aware/Helper.h>

using namespace AstraSimAnalytical;
using namespace AstraSimAnalyticalCongestionAware;
using namespace NetworkAnalyticalCongestionAware;

int main(int argc, char* argv[]) {
    return run_analytical_backend<CongestionAwareNetworkApi>(
        argc, argv,
        [](std::shared_ptr<NetworkAnalytical::EventQueue> event_queue) {
            Topology::set_event_queue(event_queue);
        },
        [](const NetworkAnalytical::NetworkParser& network_parser) {
            return construct_topology(network_parser);
        },
        [](std::shared_ptr<NetworkAnalytical::EventQueue> event_queue,
           const std::shared_ptr<Topology>& topology) {
            CongestionAwareNetworkApi::set_event_queue(event_queue);
            CongestionAwareNetworkApi::set_topology(topology);
        });
}
