import numpy as np
import pandas as pd

data = np.array([1, 2, 3, 4])
scaled = data * 2
frame = pd.DataFrame({"value": data, "scaled": scaled})

print(frame)
print(np.sum(frame["scaled"]))
