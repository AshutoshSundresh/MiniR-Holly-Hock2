# 02 - Functions and closures
square <- function(x) x^2
square(1:5)

# Defaults are evaluated in the call frame, so they can use earlier arguments
area <- function(w, h = w) w * h
area(3)
area(3, 4)
area(h = 2, w = 5)          # names match first, then remaining arguments by position

# Recursion
fib <- function(n) if (n < 2) n else fib(n - 1) + fib(n - 2)
fib(20)

# Closures capture the environment they were created in
make_adder <- function(n) function(x) x + n
add10 <- make_adder(10)
add10(5)

# <<- assigns in the enclosing environment: a counter with private state
make_counter <- function() {
  count <- 0
  function() {
    count <<- count + 1
    count
  }
}
counter <- make_counter()
counter()
counter()
counter()

# Functions are values: pass them to sapply / lapply
sapply(1:6, function(i) i * i)
sapply(c(4, 9, 16), sqrt)
lapply(1:2, function(i) rep(i, i))
