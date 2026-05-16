# 08 - Statistics and reproducible randomness
x <- c(2, 4, 4, 4, 5, 5, 7, 9)
mean(x); var(x); sd(x)
max(x) - min(x)
x - mean(x)                    # centre the data
round(sd(x), 2)

# set.seed makes random draws reproducible (MiniR has its own small generator,
# so the actual numbers differ from GNU R's)
set.seed(42)
r1 <- runif(3)
set.seed(42)
r2 <- runif(3)
r1 == r2
all(r1 >= 0 & r1 < 1)
sort(sample(1:10))             # sample() without replacement is a permutation
