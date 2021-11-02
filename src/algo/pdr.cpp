#include "pdr.h"
#include "solver.h"
#include "stats.h"
#include "string-ext.h"
#include "z3-ext.h"

#include <algorithm>
#include <cassert>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <z3++.h>

namespace pdr
{
    PDR::PDR(PDRModel& m, bool d)
        : ctx(m.ctx), model(m), delta(d), logger(model.name),
          frames(delta, ctx, m, logger)
    {
    }

    void PDR::reset()
    {
        logger.indent = 0;
        bad = std::shared_ptr<State>();
        logger.stats = Statistics();
        frames_string = "None";
        solvers_string = "None";
    }

    void PDR::print_model(const z3::model& m)
    {
        std::cout << "model consts \{" << std::endl;
        for (unsigned i = 0; i < m.num_consts(); i++)
        {
            std::cout << "\t" << m.get_const_interp(m.get_const_decl(i));
        }
        std::cout << "}" << endl;
    }

    bool PDR::run(bool dynamic)
    {
        dynamic_cardinality = dynamic;
        reset();
        timer.reset();

        assert(k == frames.frontier());

        bool failed = false;
        std::cout << endl << "PDR start:" << endl;
        SPDLOG_LOGGER_INFO(logger.spd_logger, "");
        SPDLOG_LOGGER_INFO(logger.spd_logger, "NEW RUN\n");
        SPDLOG_LOGGER_INFO(logger.spd_logger, "PDR start");

        if (!dynamic || k == 0)
        {
            SPDLOG_LOGGER_INFO(logger.spd_logger, "Start initiation");
            logger.indent++;
            failed = !init();
            logger.indent--;
        }

        if (failed)
        {
            std::cout << "Failed initiation" << endl;
            SPDLOG_LOGGER_TRACE(logger.spd_logger, "Failed initiation");
            return finish(false);
        }
        std::cout << "Survived initiation" << endl;
        SPDLOG_LOGGER_INFO(logger.spd_logger, "Survived initiation");

        SPDLOG_LOGGER_INFO(logger.spd_logger, "Start iteration");
        logger.indent++;
        failed = !iterate();
        logger.indent--;

        if (failed)
        {
            std::cout << "Failed iteration" << endl;
            SPDLOG_LOGGER_TRACE(logger.spd_logger, "Failed iteration");
            return finish(false);
        }

        std::cout << "Property verified" << endl;
        SPDLOG_LOGGER_INFO(logger.spd_logger, "Property verified");
        return finish(true);
    }

    bool PDR::finish(bool result)
    {
        double final_time = timer.elapsed().count();
        std::cout << format("Total elapsed time {}", final_time) << endl;
        SPDLOG_LOGGER_INFO(logger.spd_logger, "Total elapsed time {}",
                           final_time);
        logger.stats.elapsed = final_time;

        store_frame_strings();
        if (dynamic_cardinality)
            store_frames();

        return result;
    }

    // returns true if the model survives initiation
    bool PDR::init()
    {
        assert(frames.frontier() == 0);

        SPDLOG_LOGGER_TRACE(logger.spd_logger, "Start initiation");
        z3::expr_vector notP = model.n_property.currents();
        if (frames.init_solver.check(notP))
        {
            std::cout << "I =/> P" << std::endl;
            z3::model counter = frames.solver(0)->get_model();
            print_model(counter);
            // TODO TRACE
            bad = std::make_shared<State>(model.get_initial());
            return false;
        }

        z3::expr_vector notP_next = model.n_property.nexts();
        if (frames.SAT(0, notP_next))
        { // there is a transitions from I to !P
            std::cout << "I & T =/> P'" << std::endl;
            z3::model witness = frames.solver(0)->get_model();
            z3::expr_vector bad_cube = Solver::filter_witness(
                witness, [this](const z3::expr& e)
                { return model.literals.atom_is_current(e); });
            bad = std::make_shared<State>(bad_cube);

            return false;
        }

        frames.extend();
        k = 1;
        assert(k == frames.frontier());

        return true;
    }

    bool PDR::iterate()
    {
        std::cout << SEP3 << endl;
        std::cout << "Start iteration" << endl;

        // I => P and I & T ⇒ P' (from init)
        while (true) // iterate over k, if dynamic this continues from last k
        {
            log_iteration();
            assert(k == frames.frontier());

            while (true) // exhaust all transitions to !P
            {
                Witness witness =
                    frames.get_trans_from_to(k, model.n_property.nexts(), true);

                if (witness)
                {
                    // F_i leads to violation, strengthen
                    auto extract_current = [this](const z3::expr& e)
                    { return model.literals.atom_is_current(e); };
                    z3::expr_vector cti_current =
                        Solver::filter_witness(*witness, extract_current);

                    log_cti(cti_current);

                    z3::expr_vector core(ctx);
                    int n = highest_inductive_frame(cti_current, (int)k - 1,
                                                    (int)k, core);
                    assert(n >= 0);

                    // F_n & T & !s => !s
                    // F_n & T => F_n+1
                    z3::expr_vector smaller_cti = generalize(core, n);
                    frames.remove_state(smaller_cti, n + 1);

                    if (not block(cti_current, n + 1, k))
                        return false;

                    std::cout << endl;
                }
                else // no more counter examples
                {
                    SPDLOG_LOGGER_TRACE(logger.spd_logger,
                                        "{}| no more counters at F_{}",
                                        logger.tab(), k);
                    break;
                }
            }

            frames.extend();

            sub_timer.reset();
            bool done = frames.propagate(k);
            double time = sub_timer.elapsed().count();
            log_propagation(k, time);

            k++;
            frames.log_solvers();

            if (done)
                return true;
        }
    }
    
    bool PDR::block(z3::expr_vector& cti, unsigned n, unsigned level)
    {
        SPDLOG_LOGGER_TRACE(logger.spd_logger, "{}| block", logger.tab());
        logger.indent++;

        unsigned period = 0;
        std::set<Obligation, std::less<Obligation>> obligations;
        if ((n + 1) <= level)
            obligations.emplace(n + 1, std::move(cti), 0);

        // forall (n, state) in obligations: !state->cube is inductive
        // relative to F[i-1]
        while (obligations.size() > 0)
        {
            sub_timer.reset();
            double elapsed;
            string branch;

            auto [n, state, depth] = *(obligations.begin());
            assert(n <= level);
            log_top_obligation(obligations.size(), n, state->cube);

            if (Witness w = frames.counter_to_inductiveness(state->cube, n))
            {
                // get predecessor from the witness
                auto extract_current = [this](const z3::expr& e)
                { return model.literals.atom_is_current(e); };
                z3::expr_vector pred_cube =
                    Solver::filter_witness(*w, extract_current);

                std::shared_ptr<State> pred =
                    std::make_shared<State>(pred_cube, state);
                log_pred(pred->cube);

                // state is at least inductive relative to F[n-2]
                z3::expr_vector core(ctx);
                int m = highest_inductive_frame(pred->cube, n - 1, level, core);
                // m in [n-1, level]
                if (m >= 0)
                {
                    z3::expr_vector smaller_pred = generalize(core, m);
                    frames.remove_state(smaller_pred, m + 1);

                    if (static_cast<unsigned>(m + 1) <= level)
                    {
                        log_state_push(m + 1, pred->cube);
                        obligations.emplace(m + 1, pred, depth + 1);
                    }
                }
                else // intersects with I
                {
                    bad = pred;
                    return false;
                }
                elapsed = sub_timer.elapsed().count();
                branch = "(pred)  ";
            }
            else
            {
                log_finish(state->cube);
                //! s is now inductive to at least F_n
                // see if !state is also inductive relative to some m >= n
                z3::expr_vector core(ctx);
                int m =
                    highest_inductive_frame(state->cube, n + 1, level, core);
                // m in [n-1, level]
                assert(static_cast<unsigned>(m + 1) > n);

                if (m >= 0)
                {
                    z3::expr_vector smaller_state = generalize(core, m);
                    // expr_vector smaller_state = generalize(state->cube,
                    // m);
                    frames.remove_state(smaller_state, m + 1);
                    obligations.erase(
                        obligations.begin()); // problem with & structured
                                              // binding??

                    if (static_cast<unsigned>(m + 1) <= level)
                    {
                        // push upwards until inductive relative to F_level
                        log_state_push(m + 1, state->cube);
                        obligations.emplace(m + 1, state, depth);
                    }
                }
                else
                {
                    bad = state;
                    return false;
                }
                elapsed = sub_timer.elapsed().count();
                branch = "(finish)";
            }
            log_obligation(branch, level, elapsed);
			elapsed = -1.0;

            // periodically write stats in case of long runs
            if (period >= 100)
            {
                period = 0;
                std::cout << "Stats written" << endl;
                SPDLOG_LOGGER_DEBUG(logger.spd_logger,
                                    logger.stats.to_string());
                logger.spd_logger->flush();
            }
            else
                period++;
        }

        logger.indent--;
        return true;
    }

    void PDR::store_frame_strings()
    {
        std::stringstream ss;

        ss << "Frames" << endl;
        ss << frames.blocked_str() << endl;

        frames_string = ss.str();

        ss = std::stringstream();

        ss << "Solvers" << endl;
        ss << frames.solvers_str() << endl;

        solvers_string = ss.str();
    }

    void PDR::show_results(std::ostream& out) const
    {
        out << fmt::format("Results pebbling strategy with {} pebbles for {}",
                           model.get_max_pebbles(), model.name)
            << endl;
        out << SEP2 << endl;

        if (bad)
        {
            out << "Bad state reached:" << endl;
            out << format("[ {} ]", z3ext::join_expr_vec(
                                        model.n_property.currents(), " & "))
                << endl
                << endl;

            out << "Reached from:" << endl;
            show_trace(out);
            out << SEP2 << endl;
        }
        else
        {
            out << fmt::format("No strategy for {} pebbles",
                               model.get_max_pebbles())
                << endl
                << endl;
        }

        out << frames_string << endl;
        out << SEP << endl;
        out << solvers_string << endl;
    }

    void PDR::show_trace(std::ostream& out) const
    {
        std::vector<std::tuple<unsigned, string, unsigned>> steps;

        std::shared_ptr<State> current = bad;
        auto count_pebbled = [](const z3::expr_vector& vec)
        {
            unsigned count = 0;
            for (const z3::expr& e : vec)
                if (!e.is_not())
                    count++;

            return count;
        };

        unsigned i = 0;
        while (current)
        {
            i++;
            steps.emplace_back(i, z3ext::join_expr_vec(current->cube),
                               count_pebbled(current->cube));
            current = current->prev;
        }
        unsigned i_padding = i / 10 + 1;

        out << format("{:>{}} |\t [ {} ]", 'I', i_padding,
                      z3ext::join_expr_vec(model.get_initial()))
            << endl;

        for (const auto& [num, vec, count] : steps)
            out << format("{:>{}} |\t [ {} ] No. pebbled = {}", num, i_padding,
                          vec, count)
                << endl;

        out << format("{:>{}} |\t [ {} ]", 'F', i_padding,
                      z3ext::join_expr_vec(model.n_property.currents()))
            << endl;
    }

    Statistics& PDR::stats() { return logger.stats; }


	// LOGGING AND STAT COLLECTION SHORTHANDS
	//
    void PDR::log_iteration()
    {
        std::cout << "###############" << endl;
        std::cout << "iterate frame " << k << endl;
        SPDLOG_LOGGER_TRACE(logger.spd_logger, "");
        SPDLOG_LOGGER_TRACE(logger.spd_logger, SEP3);
        SPDLOG_LOGGER_TRACE(logger.spd_logger, "{}| frame {}", logger.tab(), k);
    }

    void PDR::log_cti(const z3::expr_vector& cti)
    {
        SPDLOG_LOGGER_TRACE(logger.spd_logger, SEP2);
        SPDLOG_LOGGER_TRACE(logger.spd_logger, "{}| cti at frame {}",
                            logger.tab(), k);
        SPDLOG_LOGGER_TRACE(logger.spd_logger, "{}| [{}]", logger.tab(),
                            str::extend::join(cti));
    }

    void PDR::log_propagation(unsigned level, double time)
    {
        std::string msg = fmt::format("Propagation elapsed {}", time);
        SPDLOG_LOGGER_TRACE(logger.spd_logger, msg);
        std::cout << msg << endl;
        logger.stats.propagation.add_timed(level, time);
    }

	void PDR::log_top_obligation(size_t queue_size, unsigned top_level, const z3::expr_vector& top)
    {
        SPDLOG_LOGGER_TRACE(logger.spd_logger, SEP);
        SPDLOG_LOGGER_TRACE(logger.spd_logger, "{}| obligations pending: {}",
                            logger.tab(), queue_size);
        SPDLOG_LOGGER_TRACE(logger.spd_logger, "{}| top obligation",
                            logger.tab());
        logger.indent++;
        SPDLOG_LOGGER_TRACE(logger.spd_logger, "{}| {}, [{}]", logger.tab(), top_level,
                            str::extend::join(top));
        logger.indent--;
    }

    void PDR::log_pred(const z3::expr_vector& p)
    {
        SPDLOG_LOGGER_TRACE(logger.spd_logger,
                            "{}| predecessor:", logger.tab());
        logger.indent++;
        SPDLOG_LOGGER_TRACE(logger.spd_logger, "{}| [{}]", logger.tab(),
                            str::extend::join(p));
        logger.indent--;
    }

    void PDR::log_state_push(unsigned frame, const z3::expr_vector& p)
    {
        SPDLOG_LOGGER_TRACE(logger.spd_logger,
                            "{}| pred is inductive until F_{}", frame - 1,
                            logger.tab());
        SPDLOG_LOGGER_TRACE(logger.spd_logger,
                            "{}| push predecessor to level {}: [{}]",
                            logger.tab(), frame, str::extend::join(p));
    }

    void PDR::log_finish(const z3::expr_vector& s)
    {
        SPDLOG_LOGGER_TRACE(logger.spd_logger, "{}| finishing state",
                            logger.tab());
        logger.indent++;
        SPDLOG_LOGGER_TRACE(logger.spd_logger, "{}| [{}]", logger.tab(),
                            str::extend::join(s));
        logger.indent--;
    }

    void PDR::log_obligation(const std::string& type, unsigned l, double time)
    {
        logger.stats.obligations_handled.add_timed(l, time);
        std::string msg = fmt::format("Obligation {} elapsed {}", type, time);
        SPDLOG_LOGGER_TRACE(logger.spd_logger, msg);
        std::cout << msg << endl;
    }

} // namespace pdr
