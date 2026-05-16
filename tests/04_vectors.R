# 04 - Vectors, indexing and names
scores <- c(alice = 91, bob = 72, cara = 85, dan = 64)
scores
scores["cara"]                 # index by name
scores[scores >= 80]           # logical mask
scores[-1]                     # negative index drops elements
names(scores)[scores < 75]
names(scores)[order(scores)]   # ranked lowest to highest
scores * 1.1                   # arithmetic keeps the names

# Building vectors
seq(0, 1, by = 0.25)
rep(c("x", "y"), times = 3)
rep(1:2, each = 2)
rev(1:5)
cumsum(1:10)
1:30                           # long vectors wrap with [index] labels

# Positions, sorting and set operations
x <- c(5, 3, 9, 1)
which(x > 4)
x[order(x)]
x[2] <- 100                    # replacement creates a modified copy
x
unique(c(3, 1, 3, 2, 1))
c(1, 5) %in% c(1, 2, 3)
x[0]                           # zero-length result
