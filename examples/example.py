"""Python 3 subset examples runnable by CPython or `./shakti example.py`."""


def traced(f):
    def wrapper(x):
        print("call", x)
        return f(x)

    return wrapper


@traced
def square(x):
    return x * x


def clamp(x, low=0, high=10):
    if x < low:
        return low
    elif x > high:
        return high
    return x


# Arithmetic, defaults, keyword arguments, decorators, and one-argument lambda.
print("square", square(6))
print("clamp", clamp(15, high=12))
increment = lambda x: x + 1
print("lambda", increment(9))

# Lists, slices, tuple unpacking, dictionaries, loops, and augmented assignment.
values = [2, 4, 6, 8]
size = len(values)
print("slice", values[1:3])
first, second = 10, 20
print("tuple", first + second)
record = {"name": "shakti", "count": size}
print("dict", record["name"], record["count"])

total = 0
for i in range(len(values)):
    total += values[i]
print("for", total)

countdown = 3
while countdown > 0:
    print("while", countdown)
    countdown -= 1

name = "python"
print(f"f-string {name}:{size}")

# NumPy and pandas calls lowered to native Shakti vectors and tables.
import numpy as np
import pandas as pd

data = np.array([1, 2, 3, 4])
scaled = data * 2
frame = pd.DataFrame({"value": data, "scaled": scaled})
print(frame)
print("sum", np.sum(frame["scaled"]))
