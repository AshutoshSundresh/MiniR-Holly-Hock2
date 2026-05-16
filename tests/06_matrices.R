# 06 - Matrices (stored column-major, like R)
m <- matrix(1:6, nrow = 2)
m
dim(m)
t(m)
m %*% t(m)                     # matrix product
m * 10                         # elementwise, keeps the shape
m[2, ]                         # one row
m[, 3]                         # one column
m[m > 3]
rowSums(m)
colMeans(m)
diag(3)

b <- matrix(1:4, nrow = 2, byrow = TRUE)
b
cbind(1:3, c(10, 20, 30))
matrix(c(1.5, 2, 3.25, 40), 2) # each column gets its own number format
