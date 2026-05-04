/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "common/AnalyticalLauncher.hh"
#include "congestion_unaware/CongestionUnawareNetworkApi.hh"
#include <astra-network-analytical/congestion_unaware/Helper.h>

using namespace AstraSimAnalytical;
using namespace AstraSimAnalyticalCongestionUnaware;
using namespace NetworkAnalyticalCongestionUnaware;

int main(int argc, char* argv[]) {
    return run_analytical_backend<CongestionUnawareNetworkApi>(
        argc, argv,
        [](std::shared_ptr<NetworkAnalytical::EventQueue>) {},
        [](const NetworkAnalytical::NetworkParser& network_parser) {
            return construct_topology(network_parser);
        },
        [](std::shared_ptr<NetworkAnalytical::EventQueue> event_queue,
           const std::shared_ptr<Topology>& topology) {
            CongestionUnawareNetworkApi::set_event_queue(event_queue);
            CongestionUnawareNetworkApi::set_topology(topology);
        });
}
