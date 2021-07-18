#ifndef Z3_EXT
#define Z3_EXT

#include <algorithm>
#include <z3++.h>
#include <vector>
#include <sstream>

namespace z3ext
{
	using std::vector;
	using std::string;
	using z3::expr;
	using z3::expr_vector;

	//z3::expr comparator
	struct expr_less 
	{
		bool operator() (const z3::expr& l, const z3::expr& r) const { return l.id() < r.id(); };
	};

	inline expr minus(const expr& e) { return e.is_not() ? e.arg(0) : !e; }

	// returns true if l c= r
	// assumes l and r are in sorted order
	inline bool subsumes(const expr_vector& l, const expr_vector& r) 
	{
		if (l.size() > r.size())
			return false;

		return std::includes(
				l.begin(), l.end(),
				r.begin(), r.end(),
				expr_less());
	}

	inline expr_vector negate(const expr_vector& lits) 
	{
		expr_vector negated(lits.ctx());
		for (const expr& e : lits)
			negated.push_back(minus(e));
		return negated;
	}

	inline expr_vector negate(const vector<expr>& lits) 
	{
		expr_vector negated(lits[0].ctx());
		for (const expr& e : lits)
			negated.push_back(minus(e));
		return negated;
	}

	inline expr_vector convert(vector<expr> vec) 
	{
		expr_vector converted(vec[0].ctx());
		for (const expr& e : vec)
			converted.push_back(std::move(e));
		return converted;
	}

	inline vector<expr> convert(const expr_vector& vec) 
	{
		vector<expr> converted; converted.reserve(vec.size());
		for (const expr& e : vec)
			converted.push_back(e);
		return converted;
	}

	inline expr_vector args(const expr& e) 
	{
		expr_vector vec(e.ctx());
		for (unsigned i = 0; i < e.num_args(); i++)
			vec.push_back(e.arg(i));
		return vec;
	}

	template<typename ExprVector>
	inline string join_expr_vec(const ExprVector& c, const string delimiter = ", ")
	{   
		//only join containers that can stream into stringstream
		if (c.size() == 0)
			return "";

		vector<string> strings; strings.reserve(c.size());
		std::transform(c.begin(), c.end(), std::back_inserter(strings),
				[](const z3::expr& i) { return i.to_string(); });

		bool first = true;
		unsigned largest;
		for (const string& s : strings)
		{
			if (s.length() > largest || first)
				largest = s.length();
			first = false;
		}

		first = true;
		std::stringstream ss; 
		for(const string& s : strings)
		{
			if (!first)
				ss << delimiter;
			first = false;
			ss << string(largest-s.length(), ' ') + s;
		}
		return ss.str();
	}


}
#endif //Z3_EXT
