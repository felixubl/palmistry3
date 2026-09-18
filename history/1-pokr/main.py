import numpy as np


# sigmoid function
def nonlin(x, deriv=False):
    if deriv:
        return x * (1 - x)

    return 1 / (1 + np.exp(-x))


# input dataset
X = np.array([[0, 0, 1], [0, 1, 1], [1, 0, 1], [1, 1, 1]])

# output dataset
y = np.array([[0, 0, 1, 1]]).T

# seed random numbers to make a caluclation
np.random.seed(26)

# initialize weights randomly with mean 0
synapse_0 = 2 * np.random.random((3, 1)) - 1

for iter in range(10_000):
    # forward propagation
    level_0 = X
    level_1 = nonlin(np.dot(level_0, synapse_0))

    # how much did we miss?

    level_1_error = y - level_1

    # multiply how much we missed by the slope of the sigmoid at the values in l1
    level_1_delta = level_1_error * nonlin(level_1, True)

    # update weights
    synapse_0 += np.dot(level_0.T, level_1_delta)

print(f"Output after training: {level_1}")
