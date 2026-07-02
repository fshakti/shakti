# graph module — in-memory knowledge graph (subject–predicate–object triples).

Run from the repo root (`make prod`; see [README](../README.md)), then:

```bash
export SHAKTI_LIB=$PWD/src/lib
./shakti examples/graph_demo.ie
```

Benchmarks (from repo root):

```bash
export SHAKTI_LIB=$PWD/src/lib
cd benchmarks && ../shakti run_graph.ie
# or graph lines only from the full suite:
SHAKTI_SYNTH_HEADLESS=1 SHAKTI_GOVEE_SKIP_LAN=1 SHAKTI_LISSEN_SKIP_NET=1 \
  ../shakti run.ie | grep 'BENCH.*graph'
```

## Example

[`examples/graph_demo.ie`](../examples/graph_demo.ie):

```ie
import graph

graph.create()
graph.add("Ada", "knows", "Bob")
graph.add("Bob", "knows", "Carol")

print(graph.query("Ada", "knows", "*"))
print(graph.path("Ada", "Carol", 4))
```

## Model

Each fact is a **triple** `(subject, predicate, object)` — the same RDF-style pattern used in enterprise knowledge graphs (e.g. Cambridge Semantics Anzo). Triples live in memory with indexes on subject, predicate, and object for fast pattern lookup.

Use `"*"` (or `""`) as a wildcard in queries.

## API

Module [`src/lib/graph.ie`](../src/lib/graph.ie):

| Function | Purpose |
|----------|---------|
| `graph.create()` | Create a graph (returns handle id) |
| `graph.add(s, p, o)` | Insert a triple |
| `graph.query(s, p, o)` | Pattern match; returns table |
| `graph.neighbors(node, direction)` | Edges touching `node` (`"out"`, `"in"`, `"both"`) |
| `graph.path(from, to, max_depth)` | Shortest path as list of node names |
| `graph.from_table(t, subj_col, pred, obj_col)` | Bulk load from a Shakti table |
| `graph.to_table(s, p, o)` | Export query matches as table |
| `graph.count()` | Number of triples |
| `graph.clear()` | Remove all triples |

## Builtins

Lower-level C builtins (handle id as first argument):

| Builtin | Returns |
|---------|---------|
| `graph_create()` | New graph id |
| `graph_add(g, s, p, o)` | Triple count after insert |
| `graph_query(g, s, p, o)` | Table with `subject`, `predicate`, `object` columns |
| `graph_neighbors(g, node, direction)` | Table of matching edges |
| `graph_path(g, from, to[, max_depth])` | List of node names |
| `graph_from_table(g, t, subj_col, pred, obj_col)` | Triple count after load |
| `graph_to_table(g, s, p, o)` | Same as `graph_query` |
| `graph_count(g)` | Triple count |
| `graph_clear(g)` | `0` |

## With SQL tables

`import graph` complements [`import sql`](SQL.md): use tables for structured rows, then `graph.from_table` to link entities by relationship.

See also [RUNTIME_API.md](RUNTIME_API.md) for the `table()` constructor and [EXAMPLES.md](EXAMPLES.md).
