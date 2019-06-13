
#ifdef HAVE_TETRA

#include "gridbased.hpp"
#include "communication.hpp" // comm_cart
#include "grid.hpp" // node_grid, node_pos, box_l
#include "domain_decomposition.hpp"
#include "utils/Vector.hpp"

#include <algorithm>
#include <regex>
#include <boost/mpi/collectives.hpp>

#ifndef NDEBUG
#define GRID_DEBUG
#pragma message "Building grid.cpp with debug code"
#endif

#ifdef GRID_DEBUG
#include <sys/signal.h>
#define ENSURE(cond)                                                          \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "Ensure `%s' in %s:%i (%s) failed.\n",            \
                    #cond, __FILE__, __LINE__, __FUNCTION__);                 \
            kill(0, SIGINT);                                                  \
        }                                                                     \
    } while (0)
#else
#define ENSURE(cond)
#endif


namespace generic_dd {
namespace grids {

template <typename T>
static void push_back_unique(std::vector<T>& v, const T& el)
{
  if (std::find(std::begin(v), std::end(v), el) == std::end(v)) {
    v.push_back(el);
  }
}


rank GridBasedGrid::gloidx_to_rank(int gloidx)
{
  auto m = gbox.midpoint(gloidx);
  return position_to_rank(m.data());
}

static std::array<int, 3> mpi_cart_get_dims(MPI_Comm comm)
{
  std::array<int, 3> dims, _dummy, _dummy2;
  MPI_Cart_get(comm, 3, dims.data(), _dummy.data(), _dummy2.data());
  return dims;
}

static std::array<int, 3> mpi_cart_get_coords(MPI_Comm comm)
{
  std::array<int, 3> coords, _dummy, _dummy2;
  MPI_Cart_get(comm, 3, _dummy.data(), _dummy2.data(), coords.data());
  return coords;
}

std::array<std::array<double, 3>, 8> GridBasedGrid::bounding_box(rank r)
{
  std::array<int, 3> c, off;
  MPI_Cart_coords(comm_cart, r, 3, c.data());

  std::array<std::array<double, 3>, 8> result;

  std::array<int, 3> dims = mpi_cart_get_dims(comm_cart);

  size_t i = 0;
  // Ranks holding the bounding box grid points of "r" = (c0, c1, c2) are:
  // (c0,     c1,     c2) upper right back corner,
  // (c0 - 1, c1,     c2) upper left back corner,
  // (c0,     c1 - 1, c2) lower right back corner,
  // (c0,     c1,     c2 - 1) upper right front corner,
  // (c0 - 1, c1 - 1, c2) lower left back corner
  // ... 2 more ...
  // (c0 - 1, c1 - 1, c2 - 1) lower left front corner
  // In total the set: {c0, c0 - 1} x {c1, c1 - 1} x {c2, c2 - 1}
  for (off[0] = 0; off[0] <= 1; ++off[0]) {
    for (off[1] = 0; off[1] <= 1; ++off[1]) {
      for (off[2] = 0; off[2] <= 1; ++off[2]) {
        int rank;
        std::array<int, 3> nc, mirror = {{0, 0, 0}};
        
        for (int d = 0; d < 3; ++d) {
          nc[d] = c[d] - off[d];

          // Periodically wrap to the correct processor
          // and save the wrapping to correct the grid point later.
          // Can only happen in negative direction.
          if (nc[d] < 0) {
            nc[d] = dims[d] - 1;
            mirror[d] = -1;
          }
        }

        MPI_Cart_rank(comm_cart, nc.data(), &rank);

        // Mirror the gridpoint back to where this subdomain is expecting it.
        for (int d = 0; d < 3; ++d)
          result[i][d] = gridpoints[rank][d] + mirror[d] * box_l[d];
        i++;
      }
    }
  }
  return result;
}

void GridBasedGrid::init_partitioning()
{
  is_regular_grid = true;

  // Copy data from grid.hpp
  for (int d = 0; d < 3; ++d) {
    gridpoint[d] = my_right[d];
    // NOTE:
    // If my_right[d] intersects a cell midpoint, currently both processes feel
    // responsible. We could round to circumvent this, i.e.
    // gridpoint[d] = std::floor(my_right[d] / gbox.cell_size()[d])
    //                    * gbox.cell_size()[d];
    // But this way, we cannot use grid.hpp for initially resolving
    // pos-to-proc, because its local_box_l would not be coherent to the domain
    // boundaries chosen by this line of code. Moreover, this code implies a
    // different "new" local_box_l for every process, thus making it hard to
    // initially resolve pos-to-proc. (Note that we initially need to resolve
    // all positions in the whole domain and not only the neighborhood.)
    //
    // Therefore, we use this hack and hope that no particle goes into the
    // "gap" caused by it. These particles will be resolved to the wrong
    // process.
    if (my_right[d] < box_l[d])
      gridpoint[d] -= 1e-6; // This is unlikely to hit any cell midpoint
  }
  
  init_neighbors();
  init_octagons();
}

void GridBasedGrid::init_neighbors()
{
  neighbor_ranks.clear();
  neighbor_idx.clear();

  std::array<int, 3> c, off, dims, _dummy;
  MPI_Cart_get(comm_cart, 3, dims.data(), _dummy.data(), c.data());

  std::vector<int> source_neigh, dest_neigh; // Send and receive neighborhood for repart
  int nneigh = 0;
  for (off[0] = -1; off[0] <= 1; ++off[0]) {
    for (off[1] = -1; off[1] <= 1; ++off[1]) {
      for (off[2] = -1; off[2] <= 1; ++off[2]) {
        std::array<int, 3> nc;

        for (int d = 0; d < 3; ++d) {
          nc[d] = c[d] + off[d];

          // Periodic wrap
          if (nc[d] < 0)
            nc[d] = dims[d] - 1;
          else if (nc[d] == dims[d])
            nc[d] = 0;
        }

        int r;
        MPI_Cart_rank(comm_cart, nc.data(), &r);

        // Insert "r" as a new neighbor if yet unseen.
        if (r == this_node)
          continue;
        if (neighbor_idx.find(r) == neighbor_idx.end()) {
          neighbor_ranks.push_back(r);
          neighbor_idx[r] = nneigh;
          nneigh++;
        }

        if (off[0] >= 0 && off[1] >= 0 && off[2] >= 0)
          push_back_unique(source_neigh, r);
        if (off[0] <= 0 && off[1] <= 0 && off[2] <= 0)
          push_back_unique(dest_neigh, r);
      }
    }
  }

  if (neighcomm != MPI_COMM_NULL)
    MPI_Comm_free(&neighcomm);

  source_neigh.push_back(this_node);
  dest_neigh.push_back(this_node);
  MPI_Dist_graph_create_adjacent(comm_cart, source_neigh.size(), source_neigh.data(),
                                 static_cast<const int*>(MPI_UNWEIGHTED), dest_neigh.size(),
                                 dest_neigh.data(), static_cast<const int*>(MPI_UNWEIGHTED),
                                 MPI_INFO_NULL, 0, &neighcomm);
}

void GridBasedGrid::init_octagons()
{
  boost::mpi::all_gather(comm_cart, gridpoint, gridpoints);

  my_dom = tetra::Octagon(bounding_box(this_node));
  
  neighbor_doms.clear();
  neighbor_doms.reserve(neighbor_ranks.size());

  for (size_t i = 0; i < neighbor_ranks.size(); ++i) {
    neighbor_doms.push_back(tetra::Octagon(bounding_box(neighbor_ranks[i])));
  }
}

void GridBasedGrid::reinit()
{
  nlocalcells = 0;
  nghostcells = 0;
  cells.clear();
  global_to_local.clear();
  exchange_vec.clear();
  
  // Reinit cells, nlocalcells, global_to_local
  // Simple loop over all global cells; TODO: optimize
  for (int i = 0; i < gbox.ncells(); ++i) {
    auto midpoint = gbox.midpoint(i);

    if (my_dom.contains(midpoint)) {
      cells.push_back(i);
      global_to_local[i] = nlocalcells;
      nlocalcells++;
    }
  }

  // FIXME: Support single cell
#ifdef GRID_DEBUG
  printf("[%i] nlocalcells: %i\n", this_node, nlocalcells);
#endif
  ENSURE(nlocalcells > 0);


  // Temporary storage for exchange descriptors.
  // Will be filled only for neighbors
  // and moved from later.
  exchange_vec.clear();
  exchange_vec.resize(neighbor_ranks.size());

  // Determine ghost cells and communication volume
  for (int i = 0; i < nlocalcells; i++) {
    for (int neighidx: gbox.full_shell_neigh_without_center(cells[i])) {
      rank owner = gloidx_to_rank(neighidx);

      if (owner == this_node)
        continue;
      
      // Add ghost cells only once to "cells" vector.
      if (global_to_local.find(neighidx) == std::end(global_to_local)) {
        // Add ghost cell to cells vector
        cells.push_back(neighidx);
        // Index mapping from global to ghost
        global_to_local[neighidx] = nlocalcells + nghostcells;
        // Number of ghost cells
        nghostcells++;
      }
      

      int idx = neighbor_idx[owner];
      // Initialize exdesc and add "rank" as neighbor if unknown.
      if (exchange_vec[idx].dest == -1)
        exchange_vec[idx].dest = owner;

      push_back_unique(exchange_vec[idx].recv, neighidx);
      push_back_unique(exchange_vec[idx].send, cells[i]);
    }
  }

#ifdef GRID_DEBUG
  printf("[%i] nghostcells: %i\n", this_node, nghostcells);
  ENSURE(n_nodes == 1 || nghostcells > 0);
#endif

  // All neighbors must be communicated with, otherwise something went wrong.
  // Sort and global_to_local.
  const auto glo_to_loc = [this](int i){
#ifdef GRID_DEBUG
      return global_to_local.at(i);
#else
      return global_to_local[i];
#endif
  };

  for (auto& v: exchange_vec) {
    ENSURE(v.dest != -1);

    std::sort(std::begin(v.recv), std::end(v.recv));
    std::transform(std::begin(v.recv), std::end(v.recv), std::begin(v.recv), glo_to_loc);

    std::sort(std::begin(v.send), std::end(v.send));
    std::transform(std::begin(v.send), std::end(v.send), std::begin(v.send), glo_to_loc);
  }
}

GridBasedGrid::GridBasedGrid(): mu(1.0), neighcomm(MPI_COMM_NULL)
{
  init_partitioning();
  reinit();
}

lidx GridBasedGrid::n_local_cells()
{
  return nlocalcells;
}

gidx GridBasedGrid::n_ghost_cells()
{
  return nghostcells;
}

nidx GridBasedGrid::n_neighbors()
{
  return neighbor_ranks.size();
}

rank GridBasedGrid::neighbor_rank(nidx i)
{
  return neighbor_ranks[i];
}

lgidx GridBasedGrid::cell_neighbor_index(lidx cellidx, int neigh)
{
  return global_to_local[gbox.neighbor(cells[cellidx], neigh)];
}


std::vector<GhostExchangeDesc> GridBasedGrid::get_boundary_info()
{
  return exchange_vec;
}

lidx GridBasedGrid::position_to_cell_index(double pos[3])
{
#ifdef GRID_DEBUG
  if (position_to_rank(pos) != this_node)
    throw std::domain_error("Particle not in local box");
#endif

  auto i = global_to_local[gbox.cell_at_pos(pos)];
#ifdef GRID_DEBUG
  if (i >= n_local_cells()) {
    auto r = position_to_rank(pos);
    std::cout << "GHOST_LAYER POSITION_TO_CELL detected." << std::endl;
    std::cout << "Particle: " << pos[0] << " " << pos[1] << " " << pos[2] << std::endl;
    std::cout << "Cell " << i << " nlocalcells: " << n_local_cells() << " nghostcells: " << n_ghost_cells() << std::endl;
    std::cout << "Position_to_rank: " << r << std::endl;

    std::cout << "Cell " << i << " has coords: ";
    auto mp = gbox.midpoint(cells[i]);
    std::cout << mp[0] << ", " << mp[1] << ", " << mp[2] << std::endl;


    throw std::runtime_error("Gridbased: Call to position_to_cell for ghost layer cell.");
  }
#endif

  return i;
}

rank GridBasedGrid::position_to_rank(double pos[3])
{
  // Do not attempt to resolve "pos" directly via grid.hpp.
  // Cell ownership is based on the cell midpoint.
  // So we need to consider the cell midpoint of the
  // owning cell here, too.
  auto mp = gbox.midpoint(gbox.cell_at_pos(pos));

  if (is_regular_grid) {
    // Use grid.hpp
    return map_position_node_array(Utils::Vector3d{mp});
  }

  if (my_dom.contains(mp))
    return this_node;
  for (int i = 0; i < n_neighbors(); ++i) {
    if (neighbor_doms[i].contains(mp))
      return neighbor_ranks[i];
  }

  throw std::runtime_error("Position unknown. Possibly a position outside of the neighborhood of this process.");
}

nidx GridBasedGrid::position_to_neighidx(double pos[3])
{
  rank rank = position_to_rank(pos);
  return neighbor_idx[rank];
}

std::array<double, 3> GridBasedGrid::cell_size()
{
  return gbox.cell_size();
}

std::array<int, 3> GridBasedGrid::grid_size()
{
  return gbox.grid_size();
}


std::array<double, 3> GridBasedGrid::center_of_load()
{
  int npart = 0;
  std::array<double, 3> c = {{0., 0., 0.}};

  for (const auto& p: local_cells.particles()) {
    npart++;
    for (int d = 0; d < 3; ++d)
      c[d] += p.r.p[d];
  }

  // If no particles: Use subdomain midpoint.
  // (Calculated as mispoint of all cells).
  if (npart == 0) {
    for (int i = 0; i < n_local_cells(); ++i) {
      auto mp = gbox.midpoint(cells[i]);
      for (int d = 0; d < 3; ++d)
        c[d] += mp[d];
      npart++; // Used as normalizer
    }
  }

  for (int d = 0; d < 3; ++d)
    c[d] /= npart;

  return c;
}

static int undirected_neighbor_count(MPI_Comm neighcomm) {
  int indegree = 0, outdegree = 0, weighted = 0;
  MPI_Dist_graph_neighbors_count(neighcomm, &indegree, &outdegree, &weighted);
  return indegree;
}

static double norm2(const double *v)
{
  return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static double norm2(const std::array<double, 3>& v)
{
  return norm2(v.data());
}

static double dist2(const std::array<double, 3>& v, const std::array<double, 3>& w)
{
  std::array<double, 3> vw;
  for (int d = 0; d < 3; ++d)
    vw[d] = v[d] - w[d];
  return norm2(vw);
}

bool GridBasedGrid::repartition(const repart::Metric& m,
                           std::function<void()> exchange_start_callback)
{
  // The node displacement is calculated according to
  // C. Begau, G. Sutmann, Comp. Phys. Comm. 190 (2015), p. 51 - 61 

  using Vec3d = std::array<double, 3>;
  using Vec3i = std::array<int, 3>;

  int nneigh = undirected_neighbor_count(neighcomm);

  auto weights = m();
  double lambda_p = std::accumulate(std::begin(weights), std::end(weights), 0.0);
  auto r_p = center_of_load();


  std::vector<double> lambda(nneigh);
  MPI_Neighbor_allgather(&lambda_p, 1, MPI_DOUBLE, lambda.data(), 1, MPI_DOUBLE, neighcomm);

  double lnormalizer = std::accumulate(lambda.begin(), lambda.end(), 0.0) / nneigh;

  std::vector<double> lambda_hat(nneigh);
  for (int i = 0; i < nneigh; ++i)
    lambda_hat[i] = lambda[i] / lnormalizer;


  std::vector<double> r(3 * nneigh);
  MPI_Neighbor_allgather(r_p.data(), 3, MPI_DOUBLE, r.data(), 3, MPI_DOUBLE, neighcomm);
  
  for (int i = 0; i < nneigh; ++i) {
    // Form "u"
    for (int d = 0; d < 3; ++d)
      r[3 * i + d] -= gridpoint[d];
    double len = norm2(&r[3 * i]);

    // Form "f"
    for (int d = 0; d < 3; ++d)
      r[3 * i + d] = (lambda_hat[i] - 1) * r[3 * i + d] / len;
  }

  const Vec3i coords = mpi_cart_get_coords(comm_cart);
  const Vec3i dims = mpi_cart_get_dims(comm_cart);

  Vec3d new_c = gridpoint;
  for (int d = 0; d < 3; ++d) {
    // Shift only non-boundary coordinates
    if (coords[d] == dims[d] - 1)
      continue;
    for (int i = 0; i < nneigh; ++i)
        new_c[d] += mu * r[3 * i + d];
  }

  // Note: Since we do not shift gridpoints over periodic boundaries,
  // f values from periodic neighbors are not considered.
  // (See if condition in above loop.)
  // Therefore, they do not need periodic mirroring.

  // Note 2: We do not need to consider neighbors multiple times even
  // if two processes neighbor themselves along multiple boundaries.
  // We have a Cartesian grid. That means that if a process
  // appears twice in the neighborhood, all do.
  // So we can safely neglect multiple neighbors.

#ifdef GRID_DEBUG
  std::cout << "[" << this_node << "] Old c: " << gridpoint[0] << "," << gridpoint[1] << "," << gridpoint[2] << std::endl;
  std::cout << "[" << this_node << "] New c: " << new_c[0] << "," << new_c[1] << "," << new_c[2] << std::endl;
#endif

  // Update gridpoint and gridpoints
  // Currently allgather. Can be done in 64 process neighborhood.
  gridpoint = new_c;

  auto old_gridpoints = gridpoints;
  gridpoints.clear();
  boost::mpi::all_gather(comm_cart, gridpoint, gridpoints);
  ENSURE(gridpoints.size() == n_nodes);

  // Check for admissibility of new grid.
  // We do not constrain the grid cells to be convex.
  // But the bare minimum that we have to enforce is that grid points do
  // not collide with each other.

  const auto cs = cell_size();
  auto min_cell_size = std::min(std::min(cs[0], cs[1]), cs[2]);

  int nconflicts = 0;

  auto bb = bounding_box(this_node);

  for (size_t i = 0; i < bb.size(); ++i)
    for (size_t j = i + 1; j < bb.size(); ++j)
      if (dist2(bb[i], bb[j]) < 2 * min_cell_size)
        nconflicts++;

  MPI_Allreduce(MPI_IN_PLACE, &nconflicts, 1, MPI_INT, MPI_SUM, comm_cart);


  if (nconflicts > 0) {
    std::cout << "Gridpoint update rejected because of node conflicts." << std::endl;
    ENSURE(0);
    gridpoints = old_gridpoints;
    gridpoint = gridpoints[this_node];
    return false;
  }

  is_regular_grid = false;

  init_octagons();
  exchange_start_callback();
  reinit();

  return true;
}

void GridBasedGrid::command(std::string s)
{
  static const std::regex mure("\\s*mu\\s*=\\s*(\\d+\\.|\\.\\d+|\\d+.\\d+)");
  std::smatch m;

  if (std::regex_match(s, m, mure)) {
    mu = std::strtod(m[1].str().c_str(), NULL);
    if (this_node == 0)
      std::cout << "Setting mu = " << mu << std::endl;
  }
}

}
}

#endif // HAVE_TETRA
