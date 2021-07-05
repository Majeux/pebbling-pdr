﻿#include <z3++.h>
#include <filesystem>
#include <memory>

#include "pdr-model.h"
#include "parse_bench.h"
#include "dag.h"

using namespace std;
using namespace z3;
using std::shared_ptr;

void test() 
{
	config settings;
	settings.set("unsat_core", true);
	settings.set("model", true);
	context ctx;

	solver test_solver = solver(ctx);
	test_solver.set("sat.cardinality.solver", true);

	expr_vector vars = expr_vector(ctx);
	vars.push_back(ctx.bool_const("a"));
	vars.push_back(ctx.bool_const("b"));

	test_solver.add(vars[0] & vars[1]);

	check_result result = test_solver.check(1, &(!vars[0]));

	if (result == sat)
		cout << "SAT" << endl;
	else if (result == unsat)
		cout << "UNSAT" << endl;
	else
		cout << "UNKNOWN" << endl;
}

int main()
{
	filesystem::path file = filesystem::current_path() / "benchmark" / "iscas85" / "bench" / "c17.bench";

	Graph G = parse_file(file.string());
	int max_pebbles = 4;

	cout << "Graph" << endl << G;

	config settings;
	settings.set("unsat_core", true);
	settings.set("model", true);
	shared_ptr<z3::context> ctx(new z3::context(settings));

	PDRModel model(ctx);

	model.load_model(G);

	return 0;
}
