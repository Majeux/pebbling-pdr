#include "pdr-model.h"
#include <TextTable.h>
#include <z3++.h>
#include <z3_api.h>

namespace pdr
{
  Model::Model(z3::config& settings, const std::string& model_name,
                     const dag::Graph& G, int pebbles)
      : ctx(settings), literals(ctx), property(ctx), n_property(ctx), initial(ctx),
        transition(ctx), cardinality(ctx)
  {
    name = model_name;

    for (std::string node : G.nodes)
      literals.add_literal(node);
    literals.finish();

    for (const z3::expr& e : literals.currents())
      initial.push_back(!e);

    // load_pebble_transition_raw2(G);
    load_pebble_transition(G);

    final_pebbles = G.output.size();
    set_max_pebbles(pebbles);

    load_property(G);

    // z3::solver s(ctx);
    // z3::param_descrs p = s.get_param_descrs();
    // TextTable param_out(' ');
    // for (unsigned i = 0; i < p.size(); i++)
    // {
    //   z3::symbol sym = p.name(i);
    //   Z3_param_kind kind = p.kind(sym);
    //   std::string doc = p.documentation(sym);
    //   std::vector<std::string> row = {sym.str(), doc};
    //   param_out.addRow(row);
    // }
    //   std::cout << param_out << std::endl;
  }

  const z3::expr_vector& Model::get_transition() const { return transition; }
  const z3::expr_vector& Model::get_initial() const { return initial; }
  const z3::expr_vector& Model::get_cardinality() const { return cardinality; }

  void Model::load_pebble_transition(const dag::Graph& G)
  {
    for (int i = 0; i < literals.size(); i++) // every node has a transition
    {
      std::string name = literals(i).to_string();
      // pebble if all children are pebbled now and next
      // or unpebble if all children are pebbled now and next
      for (const std::string& child : G.get_children(name))
      {
        z3::expr child_node = ctx.bool_const(child.c_str());
        int child_i         = literals.indexof(child_node);

        transition.push_back(literals(i) || !literals.p(i) || literals(child_i));
        transition.push_back(!literals(i) || literals.p(i) || literals(child_i));
        transition.push_back(literals(i) || !literals.p(i) ||
                             literals.p(child_i));
        transition.push_back(!literals(i) || literals.p(i) ||
                             literals.p(child_i));
      }
    }
  }

  void Model::load_pebble_transition_raw1(const dag::Graph& G)
  {
    for (int i = 0; i < literals.size(); i++) // every node has a transition
    {
      std::string name     = literals(i).to_string();
      z3::expr parent_flip = literals(i) ^ literals.p(i);
      // pebble if all children are pebbled now and next
      // or unpebble if all children are pebbled now and next
      for (const std::string& child : G.get_children(name))
      {
        z3::expr child_node    = ctx.bool_const(child.c_str());
        int child_i            = literals.indexof(child_node);
        z3::expr child_pebbled = literals(child_i) & literals.p(child_i);

        transition.push_back(z3::implies(parent_flip, child_pebbled));
      }
    }
  }

  void Model::load_pebble_transition_raw2(const dag::Graph& G)
  {
    for (int i = 0; i < literals.size(); i++) // every node has a transition
    {
      std::string name     = literals(i).to_string();
      z3::expr parent_flip = literals(i) ^ literals.p(i);
      // pebble if all children are pebbled now and next
      // or unpebble if all children are pebbled now and next
      z3::expr_vector children_pebbled(ctx);
      for (const std::string& child : G.get_children(name))
      {
        z3::expr child_node = ctx.bool_const(child.c_str());
        int child_i         = literals.indexof(child_node);
        children_pebbled.push_back(literals(child_i));
        children_pebbled.push_back(literals.p(child_i));
      }
      transition.push_back(
          z3::implies(parent_flip, z3::mk_and(children_pebbled)));
    }
  }

  void Model::load_property(const dag::Graph& G)
  {
    // final nodes are pebbled and others are not
    for (const z3::expr& e : literals.currents())
    {
      if (G.is_output(e.to_string()))
        n_property.add_expression(e, literals);
      else
        n_property.add_expression(!e, literals);
    }
    n_property.finish();

    // final nodes are unpebbled and others are
    z3::expr_vector disjunction(ctx);
    for (const z3::expr& e : literals.currents())
    {
      if (G.is_output(e.to_string()))
        disjunction.push_back(!e);
      else
        disjunction.push_back(e);
    }
    property.add_expression(z3::mk_or(disjunction), literals);
    property.finish();
  }

  int Model::get_max_pebbles() const { return max_pebbles; }

  void Model::set_max_pebbles(int x)
  {
    max_pebbles = x;

    cardinality = z3::expr_vector(ctx);
    cardinality.push_back(z3::atmost(literals.currents(), max_pebbles));
    cardinality.push_back(z3::atmost(literals.nexts(), max_pebbles));
  }

  int Model::get_f_pebbles() const { return final_pebbles; }

  void Model::show(std::ostream& out) const
  {
    literals.show(out);
    out << "Transition Relation:" << std::endl << transition << std::endl;
    out << "property: " << std::endl;
    property.show(out);
    out << "not_property: " << std::endl;
    n_property.show(out);
  }
}
