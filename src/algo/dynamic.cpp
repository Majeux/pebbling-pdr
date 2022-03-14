#include "pdr-model.h"
#include "pdr.h"
#include <bits/types/FILE.h>
#include <cstddef>
#include <string>

namespace pdr
{
  /*
    returns true if the incremental algorithm must continue
    returns false if no strategy with fewer than max_pebbles exists.
  */
  bool PDR::decrement(bool reuse)
  {
    const Model& m = ctx.const_model();

    int max_pebbles = m.get_max_pebbles();
    int new_pebbles = shortest_strategy - 1;
    assert(new_pebbles > 0);
    assert(new_pebbles < max_pebbles);

    ctx.model().set_max_pebbles(new_pebbles);
    if (new_pebbles < m.get_f_pebbles()) // not enough to pebble final state
      return false;

    reset();
    results.extend();
    logger.whisper() << "retrying with " << new_pebbles << std::endl;
    if (!reuse)
      return true;

    // TODO separate staistics from dyn runs?
    frames.reset_frames(
        logger.stats,
        { m.property.currents(), m.get_transition(), m.get_cardinality() });

    logger.show(fmt::format("Dynamic: skip initiation. k = {}", k));
    // if we are reusing frames, the last propagation was k-1, repeat this
    int invariant = frames.propagate(k - 1, true);
    if (invariant >= 0)
    {
      results.current().invariant_index = invariant;
      return true;
    }
    return false;
  }

  bool PDR::increment_strategy(std::ofstream& strategy, std::ofstream& solver_dump)
  {
    const Model& m = ctx.const_model();
    int N          = m.get_f_pebbles(); // need at least this many pebbles
    while (true)
    {
      bool found_strategy = !run(true);
      if (found_strategy)
      {
        // N is minimal
        show_results(strategy);
        solver_dump << SEP3 << " iteration " << N << std::endl;
        show_solver(solver_dump);
        return true;
      }
      else
      {
        int maxp = m.get_max_pebbles();
        int newp = maxp + 1;
        assert(newp > 0);
        assert(maxp < newp);
        ctx.model().set_max_pebbles(newp);
        if (newp > m.get_max_pebbles())
          return false;
        // perform old F_1 propagation
        // for all cubes in old F_1 if no I -T-> cube, add to new F_1
        // start pdr again
      }
    }
  }
} // namespace pdr
