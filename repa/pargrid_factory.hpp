/**
 * Copyright 2017-2019 Steffen Hirschmann
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

#include "grid_types.hpp"
#include "pargrid.hpp"
#include <memory>

namespace repa {
/** Grid factory method.
 * To be called on every node.
 */
std::unique_ptr<grids::ParallelLCGrid>
make_pargrid(GridType gt,
             const boost::mpi::communicator &comm,
             Vec3d box_size,
             double min_cell_size);

} // namespace repa
