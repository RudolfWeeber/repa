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

#include "common_types.hpp"
#include <array>

namespace repa {
namespace util {

template <typename Ret, typename T1, typename T2>
constexpr Ret linearize(const T1 *c, const T2 *grid) noexcept
{
    // Cast in case "Ret" is a type capable of holding larger values than "T1"
    // or "T2".
    return (static_cast<Ret>(c[0]) * grid[1] + c[1]) * grid[2] + c[2];
}

template <typename Ret, typename T1, typename T2>
constexpr Ret linearize(const Vec3<T1> &c, const Vec3<T2> &grid) noexcept
{
    return linearize<Ret>(c.data(), grid.data());
}

template <typename T>
constexpr T linearize(const T *c, const T *grid) noexcept
{
    return ((c[0]) * grid[1] + c[1]) * grid[2] + c[2];
}

template <typename T>
constexpr T linearize(const Vec3<T> &c, const Vec3<T> &grid) noexcept
{
    return linearize<T>(c.data(), grid.data());
}

template <typename Idx3d, typename Idx1d>
constexpr Vec3<Idx3d> unlinearize(Idx1d cidx, const Vec3<Idx3d> &grid)
{
    return {static_cast<Idx3d>((cidx / grid[2]) / grid[1]),
             static_cast<Idx3d>((cidx / grid[2]) % grid[1]),
             static_cast<Idx3d>(cidx % grid[2])};
}

template <typename T>
constexpr Vec3<T> unlinearize(T cidx, const Vec3<T> &grid)
{
    return unlinearize<T, T>(cidx, grid);
}

} // namespace util
} // namespace repa
