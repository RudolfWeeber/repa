/**
 * Copyright 2017-2019 The repa authors
 *
 * This file is part of Repa.
 *
 * Repa is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Repa is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Repa.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <boost/mpi/communicator.hpp>
#include <memory>
#include <mpi.h>
#include <unordered_map>
#include <vector>

#include "pargrid.hpp"

#include "util/mpi_graph.hpp"

namespace repa {
namespace grids {
namespace diff_variants {

/**
 *
 * Flow calculation
 *
 */

template <typename T>
using PerNeighbor = std::vector<T>;

struct FlowCalculator {
    virtual PerNeighbor<double> compute_flow(boost::mpi::communicator,
                                             boost::mpi::communicator comm_cart,
                                             const std::vector<rank_type> &,
                                             double) const = 0;
};

struct FlowIterSetter {
    virtual void set_n_flow_iter(uint32_t nflow_iter) = 0;
};

struct BetaValueSetter {
    virtual void set_beta_value(double beta_value) = 0;
};

#define DIFFUSION_MAYBE_SET_NFLOW_ITER(obj, nflow_iter)                        \
    dynamic_cast<repa::grids::diff_variants::FlowIterSetter *>(obj)            \
        ->set_n_flow_iter(nflow_iter)

#define DIFFUSION_MAYBE_SET_BETA(obj, beta_value)                              \
    dynamic_cast<repa::grids::diff_variants::BetaValueSetter *>(obj)           \
        ->set_beta_value(beta_value)

/*
 * Determines the status of each process (underloaded, overloaded)
 * in the neighborhood given the local load and returns the volume of load
 * to send to each neighbor. On underloaded processes, returns a vector of
 * zeros.
 *
 * This call is collective on neighcomm.
 *
 * Default implementation follows [Willebeek Le Mair and Reeves, IEEE Tr.
 * Par. Distr. Sys. 4(9), Sep 1993] propose
 *
 * @param neighcomm Graph communicator which reflects the neighbor
 * relationship amongst processes (undirected edges), without edges to the
 *                  process itself.
 * @param load The load of the calling process.
 * @returns Vector of load values ordered according to the neighborhood
 *          ordering in neighcomm.
 */
struct WLMVolumeComputation : public FlowCalculator {
    PerNeighbor<double> compute_flow(boost::mpi::communicator neighcomm,
                                     boost::mpi::communicator comm_cart,
                                     const std::vector<rank_type> &neighbors,
                                     double load) const override;
};

struct SchornVolumeComputation : public FlowCalculator, public FlowIterSetter {
    virtual PerNeighbor<double>
    compute_flow(boost::mpi::communicator neighcomm,
                 boost::mpi::communicator comm_cart,
                 const std::vector<rank_type> &neighbors,
                 double load) const override;
    virtual void set_n_flow_iter(uint32_t nflow_iter) override;

protected:
    uint32_t _nflow_iter = 1;
};

struct SOCVolumeComputation : public FlowCalculator, public BetaValueSetter {
    virtual PerNeighbor<double>
    compute_flow(boost::mpi::communicator neighcomm,
                 boost::mpi::communicator comm_cart,
                 const std::vector<rank_type> &neighbors,
                 double load) const override;

    virtual void set_beta_value(double beta_value) override;

protected:
    double _beta = 1.8;

private:
    std::vector<double>
    construct_local_w(const std::vector<double> &world_load,
                      const std::vector<int> &world_load_rcounts,
                      const std::vector<int> &world_load_displs,
                      const std::vector<rank_type> &all_neighbors,
                      const std::vector<int> &all_neighbors_rcounts,
                      const std::vector<int> &all_neighbors_displs,
                      int j) const;
    std::vector<double> addition(const std::vector<double> &v1,
                                 const std::vector<double> &v2) const;
    std::vector<double> scalar(double scalar,
                               const std::vector<double> &v) const;
    std::vector<std::vector<double>>
    Matrix_scalar(double scalar,
                  const std::vector<std::vector<double>> &M) const;
    std::vector<double> multiply(const std::vector<std::vector<double>> &M,
                                 const std::vector<double> &v) const;

    mutable std::vector<std::vector<double>> _M;
    mutable std::vector<std::vector<double>> _prev_load;
};

struct SOVolumeComputation : public FlowCalculator, public BetaValueSetter {
    virtual PerNeighbor<double>
    compute_flow(boost::mpi::communicator neighcomm,
                 boost::mpi::communicator comm_cart,
                 const std::vector<rank_type> &neighbors,
                 double load) const override;

    virtual void set_beta_value(double beta_value) override;

protected:
    double _beta = 1.8;

private:
    mutable std::unordered_map<rank_type, double> _prev_deficiency;
};

struct SOFVolumeComputation : public FlowCalculator,
                              public FlowIterSetter,
                              public BetaValueSetter {
    virtual PerNeighbor<double>
    compute_flow(boost::mpi::communicator neighcomm,
                 boost::mpi::communicator comm_cart,
                 const std::vector<rank_type> &neighbors,
                 double load) const override;

    virtual void set_n_flow_iter(uint32_t nflow_iter) override;
    virtual void set_beta_value(double beta_value) override;

protected:
    double _beta = 1.8;
    uint32_t _nflow_iter = 1;
};

enum class FlowCalcKind { WILLEBEEK, SCHORN, SOC, SO, SOF };
std::unique_ptr<FlowCalculator> create_flow_calc(FlowCalcKind);

} // namespace diff_variants
} // namespace grids

} // namespace repa
