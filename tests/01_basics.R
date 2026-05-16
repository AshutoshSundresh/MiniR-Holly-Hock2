# 01 - Arithmetic, vectors and recycling
# Everything in R is a vector; a "scalar" is just a vector of length 1.
1 + 2 * 3
2^10
7 %/% 2
-7 %% 3            # %% takes the sign of the divisor, as in R

# Arithmetic is vectorised: no loops needed
x <- c(2, 4, 6, 8)
x * 10
x + c(100, 200)    # the shorter vector is recycled

# Precedence follows R: unary minus binds looser than ^ but tighter than :
-2^2
-1:3
2^3^2              # ^ is right-associative

# Integers and doubles
class(5)
class(5L)
class(1:3)
1e3

# Printing uses 7 significant digits and one shared format per vector
1/3
c(1, 2.5, 10)
100000             # scientific when it is narrower than fixed notation
