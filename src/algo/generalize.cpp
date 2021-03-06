#include <algorithm>
#include <cstddef>
#include <vector>
#include <z3++.h>

#include "pdr.h"
#include "string-ext.h"
#include "z3-ext.h"

namespace pdr
{
    //! s is inductive up until min-1. !s is included up until min
    int PDR::highest_inductive_frame(const z3::expr_vector& cube, int min,
                                     int max)
    {
        if (min <= 0 && !frames.inductive(cube, 0))
        {
            SPDLOG_LOGGER_TRACE(logger.spd_logger, "{}| Intersects I",
                                logger.tab());
            return -1; 
        }

        int highest = max;
        for (int i = std::max(1, min); i <= max; i++)
        {
            // clause was inductive up to this iteration
            if (!frames.inductive(cube, i))
            {
                highest = i - 1; // previous was greatest inductive frame
                break;
            }
        }

        SPDLOG_LOGGER_TRACE(logger.spd_logger,
                            "{}| highest inductive frame is {}", logger.tab(),
                            highest);
        return highest;
    }

    int PDR::highest_inductive_frame(const z3::expr_vector& cube, int min,
                                     int max, z3::expr_vector& core)
    {
        int result = highest_inductive_frame(cube, min, max);
        if (result >= 0 && result >= min) // if unsat result occurs
        {
            // F_result & !cube & T & cube' = UNSAT
            // => F_result & !cube & T & core' = UNSAT
            auto next_lits = [this](const z3::expr& e)
				{ return model.literals.literal_is_p(e); };
            auto to_current = [this](const z3::expr& e)
                { return model.literals(e); };

            core = frames.solver(result)->unsat_core(next_lits, to_current);

            // if I => !core, the subclause survives initiation and is inductive
            if (frames.init_solver.check(core) == z3::sat) 
                core = cube; /// I /=> !core, use original
        }
        else
            core = cube; // no core produced

        SPDLOG_LOGGER_TRACE(logger.spd_logger,
                            "{}| reduction by unsat core: {} -> {}",
                            logger.tab(), cube.size(), core.size());
        return result;
    }

    z3::expr_vector PDR::generalize(const z3::expr_vector& state, int level)
    {
        SPDLOG_LOGGER_TRACE(logger.spd_logger, "{}| generalize", logger.tab());
        logger.indent++;
        z3::expr_vector smaller_cube = MIC(state, level);
        logger.indent--;

        SPDLOG_LOGGER_TRACE(logger.spd_logger, "{}| reduction by MIC: {} -> {}",
                            logger.tab(), state.size(), smaller_cube.size());
        // SPDLOG_LOGGER_TRACE(log, "{}| final reduced cube = [{}]", TAB,
        // join(smaller_cube));
        return smaller_cube;
    }

    z3::expr_vector PDR::MIC(const z3::expr_vector& state, int level)
    {
		//used for sorting
        std::vector<z3::expr> cube = z3ext::convert(state);

        assert(std::is_sorted(cube.begin(), cube.end(), z3ext::expr_less()));
        unsigned attempts = 0;
        for (unsigned i = 0; i < cube.size() && attempts < mic_retries;)
        {
            std::vector<z3::expr> new_cube(cube.begin(), cube.begin() + i);
            new_cube.reserve(cube.size() - 1);
            new_cube.insert(new_cube.end(), cube.begin() + i + 1, cube.end());

            logger.indent++;
            if (down(new_cube, level)) 
            {
				// current literal was dropped, i now points to the next
                cube = std::move(new_cube);
                attempts = 0;
                // SPDLOG_LOGGER_TRACE(log, "{}| reduced cube: [{}]", TAB,
                // join(cube));
            }
            else
            {
                i++;
                attempts++;
            }
			logger.indent--;
        }

        return z3ext::convert(cube);
    }

    // state is sorted
    bool PDR::down(std::vector<z3::expr>& state, int level)
    {
        assert(std::is_sorted(state.begin(), state.end(), z3ext::expr_less()));
        auto is_current_in_state = [this, &state](const z3::expr& e)
        {
            return model.literals.literal_is_current(e) &&
                   std::binary_search(state.begin(), state.end(), e,
                                      z3ext::expr_less());
        };

        while (true)
        {
            z3::expr* const raw_state = state.data();
            if (frames.init_solver.check(state.size(), raw_state) == z3::sat)
                return false;

            if (Witness w = frames.counter_to_inductiveness(state, level))
            {
                // intersect the current states from the model with state
                std::vector<z3::expr> cti_intersect =
                    Solver::filter_witness_vector(*w, is_current_in_state);

                state = move(cti_intersect);
            }
            else
                return true;
        }
        return false;
    }
} // namespace pdr
