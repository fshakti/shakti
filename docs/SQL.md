# `sql` module

Enables in-memory **table SQL** statement syntax after `import sql`.

Build from the repo root (`make prod`; see [README](../README.md)), then:

```bash
export SHAKTI_LIB=$PWD/src/lib
./shakti examples/sql_demo.ie
```

## Example

[`examples/sql_demo.ie`](../examples/sql_demo.ie):

```ie
import sql

t : table(id:[1, 2, 3], dept:["sales", "eng", "sales"], amount:[10, 20, 30])

r : select amount by dept from t where amount > 15
totals : select sum amount by dept from t

create table u (id: 0, name: "")
insert into u (id, name) values (1, "ada")
update name : "ADA" from u where id = 1
delete from u where id = 2

j : t join u on id
```

## Statements

| Statement | Example |
|-----------|---------|
| Select | `select col by group from t where expr` |
| Aggregate | `select sum amount by dept from t` |
| Update | `update col : expr from t where expr` |
| Delete | `delete from t where expr` |
| Create | `create table name (col: default, ...)` |
| Insert | `insert into name (cols...) values (...)` |
| Join | `t1 join t2 on col` *(not yet implemented)* |

`by col1, col2` groups and sorts ascending. There is no separate `group by` / `order by` clause.

See also [RUNTIME_API.md](RUNTIME_API.md) for tables, `load("file.csv")`, and matrix types.
