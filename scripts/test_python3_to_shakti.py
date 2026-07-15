#!/usr/bin/env python3
"""Textual and runtime tests for examples/s2p.ie."""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHAKTI = ROOT / "shakti"
S2P = ROOT / "examples" / "s2p.ie"
LIB = ROOT / "lib"


class ConvertError(Exception):
    pass


def normalize(text: str) -> str:
    return text.rstrip() + "\n"


def transpile(source: str, filename: str = "case.py") -> str:
    if not SHAKTI.is_file():
        raise AssertionError(f"missing binary: {SHAKTI}")
    if not S2P.is_file():
        raise AssertionError(f"missing converter: {S2P}")
    with tempfile.TemporaryDirectory() as td:
        src_path = Path(td) / Path(filename).name
        out_path = Path(td) / "out.ie"
        src_path.write_text(source, encoding="utf-8")
        env = os.environ.copy()
        env["SHAKTI_LIB"] = str(LIB)
        proc = subprocess.run(
            [str(SHAKTI), str(S2P), str(src_path), "-o", str(out_path)],
            capture_output=True,
            text=True,
            env=env,
            check=False,
        )
        if proc.returncode != 0:
            msg = (proc.stderr or proc.stdout or "").strip()
            if msg.startswith("Error: "):
                msg = msg[len("Error: ") :]
            raise ConvertError(msg or f"s2p failed ({proc.returncode})")
        return out_path.read_text(encoding="utf-8")


def expect_transpile(source: str, expected: str) -> None:
    got = normalize(transpile(source, filename="case.py"))
    exp = normalize(expected)
    if got != exp:
        raise AssertionError(f"transpile mismatch\n--- got ---\n{got!r}\n--- expected ---\n{exp!r}")


def expect_error(source: str, needle: str) -> None:
    try:
        transpile(source, filename="bad.py")
    except ConvertError as exc:
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


def run_generated(source: str) -> str:
    return run_shakti(transpile(source, filename="run.py"))


def main() -> int:
    expect_transpile(
        "x = 1\nprint(x)\n",
        "x : 1\nprint(x)\n",
    )
    expect_transpile(
        "if a == 1:\n    b = 2\nelif a != 3:\n    b = 4\nelse:\n    b = 5\n",
        "if (a = 1):\n    b : 2\nelif (a != 3):\n    b : 4\nelse:\n    b : 5\n",
    )
    expect_transpile(
        "def f(n=2):\n    return n + 1\n",
        "def f(n:2):\n    return (n + 1)\n",
    )
    expect_transpile(
        "print(f(k=1))\n",
        "print(f(k:1))\n",
    )
    expect_transpile(
        "a, b = 1, 2\n",
        "a, b : (1, 2)\n",
    )
    expect_transpile(
        "v = [0, 1, 2, 3][1:3]\n",
        "v : [0, 1, 2, 3][1:3]\n",
    )
    expect_transpile(
        "import sql\n",
        "import sql\n",
    )
    expect_transpile(
        "import numpy as np\nimport pandas as pd\n"
        "v = np.array([1, 2, 3])\n"
        't = pd.DataFrame({"value": v})\n'
        'print(np.sum(t["value"]))\n',
        "v : [1, 2, 3]\n"
        "t : table(value:v)\n"
        'print(sum(t["value"]))\n',
    )
    expect_transpile(
        "c = a @ b\n",
        "c : mmul(a, b)\n",
    )
    expect_transpile(
        "@trace\ndef square(n):\n    return n * n\n",
        "@trace\ndef square(n):\n    return (n * n)\n",
    )
    expect_transpile(
        "xs = abs @ ys\n",
        # Python MatMult becomes mmul, not each
        "xs : mmul(abs, ys)\n",
    )
    expect_transpile(
        'name = f"hi {x}"\n',
        'name : f"hi {x}"\n',
    )
    expect_transpile(
        'd = {"k": "v"}\n',
        'd : {"k": "v"}\n',
    )
    expect_transpile(
        "g = lambda x: x + 1\n",
        "g : (lambda x: (x + 1))\n",
    )
    expect_transpile(
        "# retained\nx = 1  # inline\n",
        "# retained\nx : 1  # inline\n",
    )
    expect_transpile(
        'def documented(x):\n    """one line"""\n    return x\n',
        "def documented(x):\n    # one line\n    return x\n",
    )
    expect_transpile(
        "for i in range(3):\n    print(i)\n",
        "for i in range(3):\n    print(i)\n",
    )
    expect_transpile(
        "while i < 3:\n    i += 1\n",
        "while (i < 3):\n    i += 1\n",
    )
    expect_transpile(
        "import numpy as np\n"
        "m = np.mean(v)\n"
        "lo = np.min(v)\n"
        "hi = np.max(v)\n"
        "r = np.sqrt(v)\n",
        "m : avg(v)\n"
        "lo : min(v)\n"
        "hi : max(v)\n"
        "r : sqrt(v)\n",
    )
    expect_transpile(
        "import pandas as pd\ns = pd.Series([1, 2, 3])\n",
        "s : [1, 2, 3]\n",
    )
    expect_transpile(
        "x = 10\nx /= 2\n",
        "x : 10\nx /= 2\n",
    )
    expect_transpile(
        "for p in enumerate(xs):\n    print(p)\n",
        "for p in enumerate(xs):\n    print(p)\n",
    )
    expect_transpile(
        "def outer(x):\n    def inner(y):\n        return y + 1\n    return inner(x)\n",
        "def outer(x):\n    def inner(y):\n        return (y + 1)\n    return inner(x)\n",
    )
    expect_transpile(
        "a = -x\nb = not y\n",
        "a : (-x)\nb : (not y)\n",
    )
    expect_transpile(
        "r = a // b % c\n",
        "r : ((a // b) % c)\n",
    )

    expect_error("x = y = 1\n", "chained assignment")
    expect_error("if 1 < 2 < 3:\n    pass\n", "chained comparisons")
    expect_error("class C:\n    pass\n", "classes are unsupported")
    expect_error("from os import path\n", "from-import")
    expect_error("import numpy as numbers\nnumbers.fft(x)\n", "unsupported")
    expect_error("xs = [x for x in ys]\n", "comprehensions")
    expect_error("def f(*args):\n    pass\n", "variadic")
    expect_error("g = lambda a, b: a + b\n", "multi-argument lambdas")
    expect_error("x: int = 1\n", "annotated assignment")
    expect_error("if a is None:\n    pass\n", "comparison")
    expect_error("async def f():\n    pass\n", "async")
    expect_error("with open('x') as f:\n    pass\n", "With is unsupported")
    expect_error("def f():\n    yield 1\n", "Yield is unsupported")
    expect_error("import numpy as np\nx = np.fft(v)\n", "numpy.fft is unsupported")
    expect_error("import pandas as pd\nx = pd.read_csv('f')\n", "pandas.read_csv is unsupported")
    expect_error("for i, v in enumerate(xs):\n    print(v)\n", "tuple unpacking in for-loops")

    out = run_generated(
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

    out2 = run_generated(
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

    out3 = run_generated(
        """
import numpy as np

data = np.array([2, 4, 6, 8])
assert(np.sum(data) == 20)
assert(np.min(data) == 2)
assert(np.max(data) == 8)

def outer(x):
    def inner(y):
        return y + 1
    return inner(x) + 1

assert(outer(3) == 5)

acc = 0
xs = [10, 20, 30]
for i in range(len(xs)):
    acc = acc + i + xs[i]

assert(acc == 63)
print("numpy runtime ok")
""",
    )
    if "numpy runtime ok" not in out3:
        raise AssertionError(f"numpy runtime failed: {out3!r}")

    example_source = (ROOT / "python.py").read_text(encoding="utf-8")
    example_output = run_generated(example_source)
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
