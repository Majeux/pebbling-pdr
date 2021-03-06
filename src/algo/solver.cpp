#include "solver.h"
#include "frame.h"
#include <z3++.h>

namespace pdr
{
    Solver::Solver(z3::context& c, std::vector<z3::expr_vector> base)
        : ctx(c), internal_solver(ctx), base_assertions(std::move(base))
    {
        init();
    }

    void Solver::init()
    {
        internal_solver.set("sat.cardinality.solver", true);
        internal_solver.set("cardinality.solver", true);
        // consecution_solver.set("lookahead_simplify", true);
        for (const z3::expr_vector& v : base_assertions)
            internal_solver.add(v);

        cubes_start = std::accumulate(
            base_assertions.begin(), base_assertions.end(), 0,
            [](int agg, const z3::expr_vector& v) { return agg + v.size(); });
    }

    void Solver::reset()
    {
        internal_solver.reset();
        init();
    }

	// reset and automatically repopulate by blocking cubes
	// used by frames
    void Solver::reset(const CubeSet& cubes)
    {
		reset();
		for (const z3::expr_vector& cube : cubes)
			block(cube);
    }

    void Solver::block(const z3::expr_vector& cube)
    {
        z3::expr clause = z3::mk_or(z3ext::negate(cube));
        this->add(clause);
    }

    void Solver::block(const z3::expr_vector& cube, const z3::expr& act)
    {
        z3::expr clause = z3::mk_or(z3ext::negate(cube));
        this->add(clause | !act);
    }

    void Solver::add(const z3::expr& e) { internal_solver.add(e); }

    bool Solver::SAT(const z3::expr_vector& assumptions)
    {
        z3::check_result result = internal_solver.check(assumptions);
        if (result == z3::sat)
            return true;

        assert(result != z3::check_result::unknown);

        core_available = true;
        return false;
    }

    z3::model Solver::get_model() const { return internal_solver.get_model(); }

    z3::expr_vector Solver::unsat_core()
    {
        assert(core_available);
        z3::expr_vector core = internal_solver.unsat_core();
        core_available = false;
        return core;
    }

    std::string Solver::as_str(const std::string& header) const
    {
        std::string str(header);
        const z3::expr_vector asserts = internal_solver.assertions();

        auto it = asserts.begin();
        for (unsigned i = 0; i < cubes_start && it != asserts.end(); i++)
            it++;

        for (; it != asserts.end(); it++)
            str += fmt::format("- {}\n", (*it).to_string());

        return str;
    }
} // namespace pdr
