#ifndef COVID_OUTPUT_HPP
#define COVID_OUTPUT_HPP

#include <utility>

struct Sim_output
{
    Sim_output() = default;

    Sim_output(std::vector<double> dmg_batches,
               double mean_dps, double std_dps)
            : dmg_batches(std::move(dmg_batches)), mean_dps(mean_dps), std_dps(std_dps) {}

    std::vector<double> dmg_batches;
    double mean_dps{};
    double std_dps{};
};

#endif // COVID_OUTPUT_HPP