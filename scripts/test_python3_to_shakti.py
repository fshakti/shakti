#!/usr/bin/env python3
"""Textual and runtime tests for examples/python3_to_shakti.py."""

from __future__ import annotations

import importlib.util
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHAKTI = ROOT / "shakti"
CONV_PATH = ROOT / "examples" / "python3_to_shakti.py"
LIB = ROOT / "lib"


def load_converter():
    spec = importlib.util.spec_from_file_location("python3_to_shakti", CONV_PATH)
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def normalize(text: str) -> str:
    return text.rstrip() + "\n"


def expect_transpile(mod, source: str, expected: str) -> None:
    got = normalize(mod.transpile(source, filename="case.py"))
    exp = normalize(expected)
    if got != exp:
        raise AssertionError(f"transpile mismatch\n--- got ---\n{got!r}\n--- expected ---\n{exp!r}")


def expect_error(mod, source: str, needle: str) -> None:
    try:
        mod.transpile(source, filename="bad.py")
    except mod.ConvertError as exc:
        msg = str(exc)
        if needle not in msg:
            raise AssertionError(f"expected {needle!r} in error {msg!r}") from None
        return
    raise AssertionError(f"expected ConvertError containing {needle!r}")


def run_shakti(code: str) -> str:
    if not SHAKTI.is_file():
        raise AssertionError(f"missing binary: {SHAKTI}")
    with tempfile.NamedTemporaryFile("w", suffix=".ie", delete=False) as fh:
        fh.write(code)
        path = fh.name
    try:
        env = os.environ.copy()
        env["SHAKTI_LIB"] = str(LIB)
        proc = subprocess.run(
            [str(SHAKTI), path],
            capture_output=True,
            text=True,
            env=env,
            check=False,
        )
    finally:
        os.unlink(path)
    if proc.returncode != 0:
        raise AssertionError(f"shakti failed ({proc.returncode}): {proc.stderr or proc.stdout}")
    return proc.stdout


def run_generated(mod, source: str) -> str:
    ie = mod.transpile(source, filename="run.py")
    return run_shakti(ie)


def main() -> int:
    mod = load_converter()

    expect_transpile(
        mod,
        "x = 1\nprint(x)\n",
        "x : 1\nprint(x)\n",
    )
    expect_transpile(
        mod,
        "if a == 1:\n    b = 2\nelif a != 3:\n    b = 4\nelse:\n    b = 5\n",
        "if (a = 1):\n    b : 2\nelif (a != 3):\n    b : 4\nelse:\n    b : 5\n",
    )
    expect_transpile(
        mod,
        "def f(n=2):\n    return n + 1\n",
        "def f(n:2):\n    return (n + 1)\n",
    )
    expect_transpile(
        mod,
        "print(f(k=1))\n",
        "print(f(k:1))\n",
    )
    expect_transpile(
        mod,
        "a, b = 1, 2\n",
        "a, b : (1, 2)\n",
    )
    expect_transpile(
        mod,
        "v = [0, 1, 2, 3][1:3]\n",
        "v : [0, 1, 2, 3][1:3]\n",
    )
    expect_transpile(
        mod,
        "import sql\n",
        "import sql\n",
    )
    expect_transpile(
        mod,
        "import numpy as np\nimport pandas as pd\n"
        "v = np.array([1, 2, 3])\n"
        't = pd.DataFrame({"value": v})\n'
        'print(np.sum(t["value"]))\n',
        "v : [1, 2, 3]\n"
        "t : table(value:v)\n"
        'print(sum(t["value"]))\n',
    )
    expect_transpile(
        mod,
        "c = a @ b\n",
        "c : mmul(a, b)\n",
    )
    expect_transpile(
        mod,
        '@trace\ndef square(n):\n    return n * n\n',
        '@trace\ndef square(n):\n    return (n * n)\n',
    )
    expect_transpile(
        mod,
        'xs = abs @ ys\n',
        # Python MatMult becomes mmul, not each
        'xs : mmul(abs, ys)\n',
    )
    expect_transpile(
        mod,
        'name = f"hi {x}"\n',
        'name : f"hi {x}"\n',
    )
    expect_transpile(
        mod,
        'd = {"k": "v"}\n',
        'd : {"k": "v"}\n',
    )
    expect_transpile(
        mod,
        'g = lambda x: x + 1\n',
        'g : (lambda x: (x + 1))\n',
    )
    expect_transpile(
        mod,
        '# retained\nx = 1  # inline\n',
        '# retained\nx : 1  # inline\n',
    )
    expect_transpile(
        mod,
        'def documented(x):\n    \"\"\"one line\"\"\"\n    return x\n',
        'def documented(x):\n    # one line\n    return x\n',
    )
    expect_transpile(
        mod,
        'for i in range(3):\n    print(i)\n',
        'for i in range(3):\n    print(i)\n',
    )
    expect_transpile(
        mod,
        'while i < 3:\n    i += 1\n',
        'while (i < 3):\n    i += 1\n',
    )

    expect_error(mod, "x = y = 1\n", "chained assignment")
    expect_error(mod, "if 1 < 2 < 3:\n    pass\n", "chained comparisons")
    expect_error(mod, "class C:\n    pass\n", "classes are unsupported")
    expect_error(mod, "from os import path\n", "from-import")
    expect_error(mod, "import numpy as numbers\nnumbers.fft(x)\n", "unsupported")
    expect_error(mod, "xs = [x for x in ys]\n", "comprehensions")
    expect_error(mod, "def f(*args):\n    pass\n", "variadic")
    expect_error(mod, "g = lambda a, b: a + b\n", "multi-argument lambdas")
    expect_error(mod, "x: int = 1\n", "annotated assignment")
    expect_error(mod, "if a is None:\n    pass\n", "comparison")

    out = run_generated(
        mod,
        """
def g(n=2):
    return n + 1

assert(g() == 3)
assert(g(5) == 6)

def kw(k):
    return k

assert(kw(k=1) == 1)

x = 1
assert(x == 1)

v = [0, 1, 2, 3][1:3]
assert(len(v) == 2)
assert(v[0] == 1)

a, b = 1, 2
assert(a == 1)
assert(b == 2)

d = {"k": "v"}
assert(d["k"] == "v")

total = 0
for i in range(3):
    total = total + i
assert(total == 3)

i = 0
while i < 2:
    i += 1
assert(i == 2)

m = [[1, 2], [3, 4]]
n = [[5, 6], [7, 8]]
p = m @ n
assert(p[0, 0] == 19)

print("python3_to_shakti runtime ok")
""",
    )
    if "python3_to_shakti runtime ok" not in out:
        raise AssertionError(f"runtime output missing marker: {out!r}")

    # Decorators end-to-end
    out2 = run_generated(
        mod,
        """
def deco(f):
    def wrap(x):
        return f(x) + 1
    return wrap

@deco
def h(x):
    return x * 2

assert(h(3) == 7)
print("decorator ok")
""",
    )
    if "decorator ok" not in out2:
        raise AssertionError(f"decorator runtime failed: {out2!r}")

    example_source = (ROOT / "example.py").read_text(encoding="utf-8")
    example_output = run_generated(mod, example_source)
    if "20" not in example_output:
        raise AssertionError(f"NumPy/pandas example failed: {example_output!r}")

    print("python3_to_shakti tests ok")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
