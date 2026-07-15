#!/usr/bin/env python3
"""Convert a strict Python 3 subset to Shakti (.ie) source."""

from __future__ import annotations

import argparse
import ast
import io
import sys
import tokenize
from typing import Iterable, Sequence


class ConvertError(Exception):
    def __init__(self, message: str, node: ast.AST | None = None, filename: str = "<stdin>") -> None:
        self.message = message
        self.node = node
        self.filename = filename
        line = getattr(node, "lineno", None) if node is not None else None
        col = getattr(node, "col_offset", None) if node is not None else None
        where = filename
        if line is not None:
            where = f"{filename}:{line}"
            if col is not None:
                where = f"{where}:{col + 1}"
        super().__init__(f"{where}: {message}")


class Converter(ast.NodeVisitor):
    RESERVED = {
        "def", "return", "if", "elif", "else", "while", "for", "in",
        "break", "continue", "and", "or", "not", "import", "try", "except",
        "finally", "as", "lambda", "pass", "class", "global", "del", "raise",
        "with", "yield", "select", "update", "delete", "by", "from", "where",
        "create", "insert", "into", "values", "join", "on", "True", "False",
        "None",
    }
    BINOPS = {
        ast.Add: "+",
        ast.Sub: "-",
        ast.Mult: "*",
        ast.Div: "/",
        ast.FloorDiv: "//",
        ast.Mod: "%",
        ast.Pow: "**",
    }
    UNAOPS = {
        ast.USub: "-",
        ast.Not: "not ",
    }
    CMPOPS = {
        ast.Eq: "=",
        ast.NotEq: "!=",
        ast.Lt: "<",
        ast.LtE: "<=",
        ast.Gt: ">",
        ast.GtE: ">=",
        ast.In: "in",
        ast.NotIn: "not in",
    }
    AUGOPS = {
        ast.Add: "+=",
        ast.Sub: "-=",
        ast.Mult: "*=",
        ast.Div: "/=",
    }

    def __init__(self, filename: str = "<stdin>", source: str = "") -> None:
        self.filename = filename
        self._indent = 0
        self.module_aliases: dict[str, str] = {}
        self.comments: dict[int, str] = {}
        self.comment_columns: dict[int, int] = {}
        self.inline_comments: dict[int, str] = {}
        self._last_comment_line = 0
        if source:
            self._collect_comments(source)

    def _collect_comments(self, source: str) -> None:
        try:
            tokens = tokenize.generate_tokens(io.StringIO(source).readline)
            for token in tokens:
                if token.type != tokenize.COMMENT:
                    continue
                line, col = token.start
                physical = token.line or ""
                if physical[:col].strip():
                    self.inline_comments[line] = token.string
                else:
                    self.comments[line] = token.string
                    self.comment_columns[line] = col
        except (IndentationError, tokenize.TokenError):
            # ast.parse reports the authoritative syntax error.
            return

    def fail(self, message: str, node: ast.AST | None = None) -> None:
        raise ConvertError(message, node, self.filename)

    def identifier(self, name: str, node: ast.AST) -> str:
        if name in self.RESERVED:
            self.fail(f"identifier {name!r} is reserved in Shakti", node)
        return name

    def convert(self, tree: ast.AST) -> str:
        if not isinstance(tree, ast.Module):
            self.fail("expected module", tree)
        body = list(tree.body)
        out: list[str] = []
        i = 0
        while i < len(body):
            stmt = body[i]
            if (
                i == 0
                and isinstance(stmt, ast.Expr)
                and isinstance(stmt.value, ast.Constant)
                and isinstance(stmt.value.value, str)
            ):
                for line in sorted(self.comments):
                    if self._last_comment_line < line < stmt.lineno:
                        out.append(self.comments[line])
                        self._last_comment_line = line
                for line in stmt.value.value.splitlines() or [""]:
                    out.append(f"# {line}" if line else "#")
                i += 1
                continue
            out.extend(self.stmt(stmt).splitlines())
            i += 1
        for line in sorted(self.comments):
            if line > self._last_comment_line and self.comment_columns.get(line) == 0:
                out.append(self.comments[line])
                self._last_comment_line = line
        text = "\n".join(out)
        return text + ("\n" if text and not text.endswith("\n") else ("\n" if not text else ""))

    def pad(self, lines: Iterable[str]) -> list[str]:
        prefix = "    " * self._indent
        return [prefix + line if line else line for line in lines]

    def block(self, stmts: Sequence[ast.stmt], node: ast.AST) -> list[str]:
        if not stmts:
            self.fail("empty block", node)
        self._indent += 1
        try:
            lines: list[str] = []
            for stmt in stmts:
                lines.extend(self.stmt(stmt).splitlines())
            return lines
        finally:
            self._indent -= 1

    def stmt(self, node: ast.stmt) -> str:
        lines: list[str] = []
        node_line = getattr(node, "lineno", 0)
        for line in sorted(self.comments):
            if self._last_comment_line < line < node_line:
                lines.extend(self.pad([self.comments[line]]))
                self._last_comment_line = line
        rendered = self._stmt(node)
        inline = self.inline_comments.get(node_line)
        if inline and "\n" not in rendered:
            rendered = f"{rendered}  {inline}"
        lines.extend(rendered.splitlines())
        return "\n".join(lines)

    def _stmt(self, node: ast.stmt) -> str:
        if isinstance(node, ast.Assign):
            return self.assign_stmt(node)
        if isinstance(node, ast.AugAssign):
            return self.aug_assign_stmt(node)
        if isinstance(node, ast.AnnAssign):
            self.fail("annotated assignment is unsupported", node)
        if isinstance(node, ast.FunctionDef):
            return self.function_def(node)
        if isinstance(node, ast.AsyncFunctionDef):
            self.fail("async functions are unsupported", node)
        if isinstance(node, ast.ClassDef):
            self.fail("classes are unsupported", node)
        if isinstance(node, ast.Return):
            if node.value is None:
                return self.pad(["return"])[0]
            return self.pad([f"return {self.expr(node.value)}"])[0]
        if isinstance(node, ast.If):
            return self.if_stmt(node)
        if isinstance(node, ast.While):
            return self.while_stmt(node)
        if isinstance(node, ast.For):
            return self.for_stmt(node)
        if isinstance(node, ast.AsyncFor):
            self.fail("async for is unsupported", node)
        if isinstance(node, ast.Break):
            return self.pad(["break"])[0]
        if isinstance(node, ast.Continue):
            return self.pad(["continue"])[0]
        if isinstance(node, ast.Pass):
            return self.pad(["pass"])[0]
        if isinstance(node, ast.Import):
            return self.import_stmt(node)
        if isinstance(node, ast.ImportFrom):
            self.fail("from-import is unsupported", node)
        if isinstance(node, ast.Expr):
            return self.pad([self.expr(node.value)])[0]
        if isinstance(node, (ast.Delete,)):
            self.fail("delete is unsupported", node)
        if isinstance(node, ast.Assert):
            if node.msg is not None:
                self.fail("assert message is unsupported", node)
            return self.pad([f"assert({self.expr(node.test)})"])[0]
        if isinstance(node, (ast.Raise, ast.Try, ast.With, ast.AsyncWith)):
            self.fail(f"{type(node).__name__} is unsupported", node)
        if isinstance(node, (ast.Global, ast.Nonlocal)):
            self.fail(f"{type(node).__name__.lower()} is unsupported", node)
        if isinstance(node, ast.NamedExpr):
            self.fail("walrus operator is unsupported", node)
        self.fail(f"unsupported statement {type(node).__name__}", node)

    def assign_stmt(self, node: ast.Assign) -> str:
        if len(node.targets) != 1:
            self.fail("chained assignment is unsupported", node)
        target = self.target(node.targets[0])
        value = self.expr(node.value)
        return self.pad([f"{target} : {value}"])[0]

    def aug_assign_stmt(self, node: ast.AugAssign) -> str:
        op = self.AUGOPS.get(type(node.op))
        if op is None:
            self.fail(f"augmented operator {type(node.op).__name__} is unsupported", node)
        return self.pad([f"{self.target(node.target)} {op} {self.expr(node.value)}"])[0]

    def function_def(self, node: ast.FunctionDef) -> str:
        if node.args.posonlyargs or node.args.kwonlyargs or node.args.vararg or node.args.kwarg:
            self.fail("variadic or keyword-only parameters are unsupported", node)
        if node.returns is not None:
            self.fail("return annotations are unsupported", node)
        params = self.params(node.args)
        name = self.identifier(node.name, node)
        lines = self.pad([*(self.decorator_line(d) for d in node.decorator_list), f"def {name}({params}):"])
        body = list(node.body)
        if (
            body
            and isinstance(body[0], ast.Expr)
            and isinstance(body[0].value, ast.Constant)
            and isinstance(body[0].value.value, str)
        ):
            doc = body.pop(0)
            prefix = "    " * (self._indent + 1)
            for line in doc.value.value.splitlines() or [""]:
                lines.append(prefix + (f"# {line}" if line else "#"))
        if not body:
            body = [ast.Pass()]
        lines.extend(self.block(body, node))
        return "\n".join(lines)

    def decorator_line(self, node: ast.expr) -> str:
        return f"@{self.expr(node)}"

    def params(self, args: ast.arguments) -> str:
        defaults = args.defaults
        start = len(args.args) - len(defaults)
        parts: list[str] = []
        for i, arg in enumerate(args.args):
            if arg.annotation is not None:
                self.fail("parameter annotations are unsupported", arg)
            name = self.identifier(arg.arg, arg)
            if i >= start:
                parts.append(f"{name}:{self.expr(defaults[i - start])}")
            else:
                parts.append(name)
        return ", ".join(parts)

    def if_stmt(self, node: ast.If) -> str:
        lines = self.pad([f"if {self.expr(node.test)}:"])
        lines.extend(self.block(node.body, node))
        orelse = node.orelse
        while len(orelse) == 1 and isinstance(orelse[0], ast.If):
            child = orelse[0]
            lines.extend(self.pad([f"elif {self.expr(child.test)}:"]))
            lines.extend(self.block(child.body, child))
            orelse = child.orelse
        if orelse:
            lines.extend(self.pad(["else:"]))
            lines.extend(self.block(orelse, node))
        return "\n".join(lines)

    def while_stmt(self, node: ast.While) -> str:
        if node.orelse:
            self.fail("while-else is unsupported", node)
        lines = self.pad([f"while {self.expr(node.test)}:"])
        lines.extend(self.block(node.body, node))
        return "\n".join(lines)

    def for_stmt(self, node: ast.For) -> str:
        if node.orelse:
            self.fail("for-else is unsupported", node)
        if isinstance(node.target, (ast.Tuple, ast.List)):
            # Shakti binds a single loop variable per iteration; destructuring
            # targets (for a, b in ...) would emit code that does not run.
            self.fail("tuple unpacking in for-loops is unsupported", node.target)
        target = self.target(node.target)
        lines = self.pad([f"for {target} in {self.expr(node.iter)}:"])
        lines.extend(self.block(node.body, node))
        return "\n".join(lines)

    def import_stmt(self, node: ast.Import) -> str:
        if len(node.names) != 1:
            self.fail("multiple imports in one statement are unsupported", node)
        alias = node.names[0]
        if alias.name in {"numpy", "pandas"}:
            bound = alias.asname or alias.name
            self.module_aliases[bound] = alias.name
            return ""
        if alias.asname is not None:
            self.fail("import aliases are unsupported", node)
        return self.pad([f"import {alias.name}"])[0]

    def target(self, node: ast.expr, allow_tuple: bool = False) -> str:
        if isinstance(node, ast.Name):
            return self.identifier(node.id, node)
        if isinstance(node, ast.Attribute):
            if isinstance(node.value, ast.Name) and node.value.id in self.module_aliases:
                module = self.module_aliases[node.value.id]
                self.fail(f"{module}.{node.attr} is unsupported outside a mapped call", node)
            return f"{self.expr(node.value)}.{self.identifier(node.attr, node)}"
        if isinstance(node, ast.Subscript):
            return self.subscript(node)
        if allow_tuple and isinstance(node, (ast.Tuple, ast.List)):
            if any(isinstance(elt, (ast.Starred,)) for elt in node.elts):
                self.fail("starred unpacking is unsupported", node)
            if not node.elts:
                self.fail("empty unpacking is unsupported", node)
            if any(not isinstance(elt, ast.Name) for elt in node.elts):
                self.fail("nested unpacking is unsupported", node)
            return ", ".join(self.identifier(elt.id, elt) for elt in node.elts)  # type: ignore[union-attr]
        if isinstance(node, (ast.Tuple, ast.List)):
            if any(isinstance(elt, (ast.Starred,)) for elt in node.elts):
                self.fail("starred unpacking is unsupported", node)
            if not node.elts or any(not isinstance(elt, ast.Name) for elt in node.elts):
                self.fail("nested or empty unpacking is unsupported", node)
            return ", ".join(self.identifier(elt.id, elt) for elt in node.elts)  # type: ignore[union-attr]
        self.fail(f"unsupported assignment target {type(node).__name__}", node)

    def expr(self, node: ast.expr) -> str:
        if isinstance(node, ast.Constant):
            return self.constant(node)
        if isinstance(node, ast.Name):
            return self.identifier(node.id, node)
        if isinstance(node, ast.Attribute):
            if isinstance(node.value, ast.Name) and node.value.id in self.module_aliases:
                module = self.module_aliases[node.value.id]
                self.fail(f"{module}.{node.attr} is unsupported outside a mapped call", node)
            return f"{self.expr(node.value)}.{self.identifier(node.attr, node)}"
        if isinstance(node, ast.Subscript):
            return self.subscript(node)
        if isinstance(node, ast.Call):
            return self.call(node)
        if isinstance(node, ast.BinOp):
            return self.binop(node)
        if isinstance(node, ast.UnaryOp):
            return self.unaryop(node)
        if isinstance(node, ast.BoolOp):
            return self.boolop(node)
        if isinstance(node, ast.Compare):
            return self.compare(node)
        if isinstance(node, ast.IfExp):
            return f"{self.expr(node.body)} if {self.expr(node.test)} else {self.expr(node.orelse)}"
        if isinstance(node, ast.Lambda):
            return self.lambda_expr(node)
        if isinstance(node, ast.List):
            return "[" + ", ".join(self.expr(elt) for elt in node.elts) + "]"
        if isinstance(node, ast.Tuple):
            if len(node.elts) == 1:
                return f"({self.expr(node.elts[0])},)"
            return "(" + ", ".join(self.expr(elt) for elt in node.elts) + ")"
        if isinstance(node, ast.Dict):
            if any(k is None for k in node.keys):
                self.fail("dict unpacking is unsupported", node)
            parts = [f"{self.expr(k)}: {self.expr(v)}" for k, v in zip(node.keys, node.values)]
            return "{" + ", ".join(parts) + "}"
        if isinstance(node, ast.Set):
            self.fail("set literals are unsupported", node)
        if isinstance(node, (ast.ListComp, ast.SetComp, ast.DictComp, ast.GeneratorExp)):
            self.fail("comprehensions are unsupported", node)
        if isinstance(node, ast.Starred):
            self.fail("starred expressions are unsupported", node)
        if isinstance(node, ast.JoinedStr):
            return self.joined_str(node)
        if isinstance(node, ast.FormattedValue):
            self.fail("formatted value outside f-string", node)
        if isinstance(node, ast.NamedExpr):
            self.fail("walrus operator is unsupported", node)
        if isinstance(node, ast.Yield) or isinstance(node, ast.YieldFrom) or isinstance(node, ast.Await):
            self.fail(f"{type(node).__name__} is unsupported", node)
        self.fail(f"unsupported expression {type(node).__name__}", node)

    def constant(self, node: ast.Constant) -> str:
        value = node.value
        if value is None:
            return "None"
        if isinstance(value, bool):
            return "True" if value else "False"
        if isinstance(value, int):
            return str(value)
        if isinstance(value, float):
            return repr(value)
        if isinstance(value, str):
            return self.string_literal(value)
        if isinstance(value, bytes):
            self.fail("bytes literals are unsupported", node)
        if isinstance(value, complex):
            self.fail("complex literals are unsupported", node)
        self.fail(f"unsupported constant {type(value).__name__}", node)

    def string_literal(self, value: str) -> str:
        return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t") + '"'

    def joined_str(self, node: ast.JoinedStr) -> str:
        parts: list[str] = []
        for value in node.values:
            if isinstance(value, ast.Constant) and isinstance(value.value, str):
                parts.append(
                    value.value.replace("\\", "\\\\")
                    .replace("{", "{{")
                    .replace("}", "}}")
                    .replace('"', '\\"')
                    .replace("\n", "\\n")
                    .replace("\t", "\\t")
                )
                continue
            if isinstance(value, ast.FormattedValue):
                if value.conversion != -1 or value.format_spec is not None:
                    self.fail("f-string conversions/format specs are unsupported", value)
                parts.append("{" + self.expr(value.value) + "}")
                continue
            self.fail("unsupported f-string part", value)
        return 'f"' + "".join(parts) + '"'

    def subscript(self, node: ast.Subscript) -> str:
        return f"{self.expr(node.value)}[{self.slice_expr(node.slice)}]"

    def slice_expr(self, node: ast.AST) -> str:
        if isinstance(node, ast.Slice):
            lower = self.expr(node.lower) if node.lower else ""
            upper = self.expr(node.upper) if node.upper else ""
            if node.step is not None:
                step = self.expr(node.step)
                return f"{lower}:{upper}:{step}"
            return f"{lower}:{upper}"
        if isinstance(node, ast.Tuple):
            return ", ".join(self.slice_expr(elt) for elt in node.elts)
        return self.expr(node)  # type: ignore[arg-type]

    def call(self, node: ast.Call) -> str:
        lowered = self.lower_library_call(node)
        if lowered is not None:
            return lowered
        if node.keywords and any(kw.arg is None for kw in node.keywords):
            self.fail("**kwargs is unsupported", node)
        if any(isinstance(arg, ast.Starred) for arg in node.args):
            self.fail("*args is unsupported", node)
        args = [self.expr(arg) for arg in node.args]
        for kw in node.keywords:
            assert kw.arg is not None
            args.append(f"{self.identifier(kw.arg, kw.value)}:{self.expr(kw.value)}")
        return f"{self.expr(node.func)}(" + ", ".join(args) + ")"

    def lower_library_call(self, node: ast.Call) -> str | None:
        if not isinstance(node.func, ast.Attribute) or not isinstance(node.func.value, ast.Name):
            return None
        module = self.module_aliases.get(node.func.value.id)
        if module is None:
            return None
        name = node.func.attr
        if module == "numpy":
            if name in {"array", "asarray"}:
                if len(node.args) != 1 or node.keywords:
                    self.fail(f"numpy.{name} requires one argument without dtype/options", node)
                return self.expr(node.args[0])
            mappings = {
                "sum": "sum",
                "mean": "avg",
                "min": "min",
                "max": "max",
                "dot": "dot",
                "matmul": "mmul",
                "abs": "abs",
                "sqrt": "sqrt",
                "exp": "exp",
                "log": "log",
                "sin": "sin",
                "cos": "cos",
                "tan": "tan",
            }
            target = mappings.get(name)
            if target is None:
                self.fail(f"numpy.{name} is unsupported", node)
            if node.keywords:
                self.fail(f"numpy.{name} options are unsupported", node)
            return f"{target}(" + ", ".join(self.expr(arg) for arg in node.args) + ")"
        if module == "pandas":
            if name == "Series":
                if len(node.args) != 1 or node.keywords:
                    self.fail("pandas.Series requires one argument", node)
                return self.expr(node.args[0])
            if name != "DataFrame":
                self.fail(f"pandas.{name} is unsupported", node)
            if len(node.args) != 1 or node.keywords or not isinstance(node.args[0], ast.Dict):
                self.fail("pandas.DataFrame requires one dictionary argument", node)
            data = node.args[0]
            columns: list[str] = []
            for key, value in zip(data.keys, data.values):
                if not isinstance(key, ast.Constant) or not isinstance(key.value, str):
                    self.fail("DataFrame column names must be string literals", key or data)
                if not key.value.isidentifier():
                    self.fail("DataFrame column names must be valid identifiers", key)
                columns.append(f"{self.identifier(key.value, key)}:{self.expr(value)}")
            return "table(" + ", ".join(columns) + ")"
        return None

    def binop(self, node: ast.BinOp) -> str:
        if isinstance(node.op, ast.MatMult):
            return f"mmul({self.expr(node.left)}, {self.expr(node.right)})"
        op = self.BINOPS.get(type(node.op))
        if op is None:
            self.fail(f"operator {type(node.op).__name__} is unsupported", node)
        return f"({self.expr(node.left)} {op} {self.expr(node.right)})"

    def unaryop(self, node: ast.UnaryOp) -> str:
        if isinstance(node.op, ast.UAdd):
            self.fail("unary plus is unsupported", node)
        if isinstance(node.op, ast.Invert):
            self.fail("bitwise invert is unsupported", node)
        op = self.UNAOPS.get(type(node.op))
        if op is None:
            self.fail(f"unary operator {type(node.op).__name__} is unsupported", node)
        value = self.expr(node.operand)
        if isinstance(node.op, ast.Not):
            return f"(not {value})"
        return f"({op}{value})"

    def boolop(self, node: ast.BoolOp) -> str:
        op = "and" if isinstance(node.op, ast.And) else "or"
        return "(" + f" {op} ".join(self.expr(v) for v in node.values) + ")"

    def compare(self, node: ast.Compare) -> str:
        if len(node.ops) != 1 or len(node.comparators) != 1:
            self.fail("chained comparisons are unsupported", node)
        op = self.CMPOPS.get(type(node.ops[0]))
        if op is None:
            self.fail(f"comparison {type(node.ops[0]).__name__} is unsupported", node)
        return f"({self.expr(node.left)} {op} {self.expr(node.comparators[0])})"

    def lambda_expr(self, node: ast.Lambda) -> str:
        if node.args.posonlyargs or node.args.kwonlyargs or node.args.vararg or node.args.kwarg:
            self.fail("variadic or keyword-only lambda parameters are unsupported", node)
        if len(node.args.args) > 1:
            self.fail("multi-argument lambdas are unsupported", node)
        if any(arg.annotation is not None for arg in node.args.args):
            self.fail("lambda annotations are unsupported", node)
        params = self.params(node.args)
        return f"(lambda {params}: {self.expr(node.body)})"


def transpile(source: str, filename: str = "<stdin>") -> str:
    try:
        tree = ast.parse(source, filename=filename)
    except SyntaxError as exc:
        where = filename
        if exc.lineno is not None:
            where = f"{filename}:{exc.lineno}"
            if exc.offset is not None:
                where = f"{where}:{exc.offset}"
        raise ConvertError(exc.msg or "syntax error", filename=where) from exc
    return Converter(filename, source).convert(tree)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Convert a strict Python 3 subset to Shakti.")
    parser.add_argument("input", nargs="?", help="Python source file (default: stdin)")
    parser.add_argument("-o", "--output", help="Write Shakti source to this path")
    args = parser.parse_args(argv)

    filename = args.input if args.input else "<stdin>"
    if args.input:
        with open(args.input, encoding="utf-8") as fh:
            source = fh.read()
    else:
        source = sys.stdin.read()

    try:
        result = transpile(source, filename=filename)
    except ConvertError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    if args.output:
        with open(args.output, "w", encoding="utf-8") as fh:
            fh.write(result)
    else:
        sys.stdout.write(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
