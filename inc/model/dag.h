#ifndef DAG
#define DAG

#include <set>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <fmt/format.h>
#include <cassert>

#include "string-ext.h"

using std::set;
using std::map;
using std::vector;
using std::string;
using str::extensions::join;

namespace dag {
	struct Edge
	{
		string from;
		string to;

		Edge(const string& f, const string& t) : from(f), to(t) { }

		friend bool operator<(const Edge& lhs, const Edge& rhs)
		{
			if (lhs.from < rhs.from)
				return true; 
			if (rhs.from < lhs.from)
				return false;

			if (lhs.to < rhs.to)
				return true;
			return false;
		}

		friend std::stringstream& operator<<(std::stringstream& stream, Edge const& e) {
			stream << "(" << e.from << ", " << e.to << ")";
			return stream;
		}
	};

	class Graph
	{
		public:	
			set<string> input;
			set<string> nodes;
			set<string> output; //subet of nodes
			set<Edge> edges; //nodes X nodes
			map<string, vector<string>> children; //nodes X nodes
			string prefix = "";

			string node(string name) { return prefix + name; }
			
			Graph() { }

			void add_input(string name) { input.insert(node(name)); }

			void add_node(string name) { nodes.insert(node(name)); }

			void add_output(string name)
			{
				nodes.insert(node(name));
				output.insert(node(name));
			}

			void add_edges_to(vector<string> from, string to)
			{
				to = node(to);
				assert(nodes.find(to) != nodes.end());

				vector<string> to_children;
				to_children.reserve(from.size());

				for (string i : from)
				{
					string n = node(i);
					if (input.find(n) != input.end())
						continue;

					assert(nodes.find(n) != nodes.end());
					edges.emplace(n, to);
					to_children.push_back(n);
				}

				children.emplace(to, std::move(to_children));
			}

			friend std::ostream& operator<<(std::ostream& stream, Graph const& g) {
				stream << "DAG {";
				stream << std::endl;
				stream << "\tinput { " << join(g.input) << " }" << std::endl;
				stream << "\toutput { " << join(g.output) << " }" << std::endl;
				stream << "\tnodes { " << join(g.nodes) << " }" << std::endl;
				stream << "\tinput { " << join(g.edges) << " }" << std::endl;
				stream << "}" << std::endl;
				return stream;
			}

			bool is_output(const string& name) const { return output.find(name) != output.end(); }
	};
}

#endif
