#include "frame.h"
#include "solver.h"
#include "z3-ext.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <fmt/core.h>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>
#include <z3++.h>

namespace pdr
{
  Frame::Frame(unsigned i, Logger& l) : level(i), logger(l) {}

  Frame::Frame(unsigned i, z3::context& c,
               const std::vector<z3::expr_vector>& assertions, Logger& l)
      : level(i), logger(l), solver(std::make_unique<Solver>(c, assertions))
  {
  }

  void Frame::set_stats(Statistics& s) { logger.stats = s; }

  bool Frame::blocked(const z3::expr_vector& cube)
  {
    for (const z3::expr_vector& blocked_cube : blocked_cubes)
    {
      if (z3ext::subsumes(blocked_cube, cube))
      {
        SPDLOG_LOGGER_TRACE(logger.spd_logger, "already blocked in F{} by {}",
                            level, z3ext::join_expr_vec(blocked_cube));
        return true; // equal or stronger clause found
      }
    }
    return false;
  }

  unsigned Frame::remove_subsumed(const z3::expr_vector& cube)
  {
    // return 0;
    unsigned before = blocked_cubes.size();
    // auto new_end = std::remove_if(blocked_cubes.begin(),
    // blocked_cubes.end(),
    // 		[&cube](const expr_vector& blocked) { return
    // z3ext::subsumes(cube, blocked); });
    for (auto it = blocked_cubes.begin(); it != blocked_cubes.end();)
    {
      if (z3ext::subsumes(cube, *it))
      {
        it = blocked_cubes.erase(it);
      }
      else
        it++;
    }
    // blocked_cubes.erase(new_end, blocked_cubes.end());
    return before - blocked_cubes.size();
  }

  // interface
  //
  // cube is sorted by id()
  // block cube unless it, or a stronger version, is already blocked
  bool Frame::block(const z3::expr_vector& cube)
  {
    bool inserted = blocked_cubes.insert(cube).second;
    assert(inserted);
    return true;
  }

  void Frame::block_in_solver(const z3::expr_vector& cube)
  {
    assert(solver);
    solver->block(cube);
  }

  // assumes vectors in 'blocked_cubes' are sorted
  bool Frame::equals(const Frame& f) const
  {
    if (this->blocked_cubes.size() != f.blocked_cubes.size())
      return false;

    auto l_cube = this->blocked_cubes.begin();
    auto r_cube = f.blocked_cubes.begin();
    auto l_end  = this->blocked_cubes.end();
    auto r_end  = f.blocked_cubes.end();
    for (; l_cube != l_end && r_cube != r_end; l_cube++, r_cube++)
    {
      // if l_cubes* != r_cubes* -> return false
      if (l_cube->size() != r_cube->size())
        return false;

      auto l_lit = l_cube->begin();
      auto r_lit = r_cube->begin();
      for (; l_lit != l_cube->end() && r_lit != r_cube->end(); l_lit++, r_lit++)
        if ((*l_lit).id() != (*r_lit).id())
          return false;
    }
    return true;
  }

  std::vector<z3::expr_vector> Frame::diff(const Frame& f) const
  {
    std::vector<z3::expr_vector> out;
    std::set_difference(blocked_cubes.begin(), blocked_cubes.end(),
                        f.blocked_cubes.begin(), f.blocked_cubes.end(),
                        std::back_inserter(out), z3ext::expr_vector_less());
    return out;
  }

  const CubeSet& Frame::get_blocked() const { return blocked_cubes; }
  bool Frame::empty() const { return blocked_cubes.size() == 0; }
  Solver* Frame::get_solver() const { return solver.get(); }

  std::string Frame::blocked_str() const
  {
    std::string str(fmt::format("blocked cubes level {}\n", level));
    for (const z3::expr_vector& e : blocked_cubes)
      str += fmt::format("- {}\n", z3ext::join_expr_vec(e, " & "));

    return str;
  }

} // namespace pdr
