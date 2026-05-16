# 03 - Control flow
classify <- function(x) {
  if (x < 0) {
    "negative"
  } else if (x == 0) {
    "zero"
  } else {
    "positive"
  }
}
sapply(c(-5, 0, 3), classify)

# for loops run over any vector; loops and assignments return invisibly
total <- 0
for (v in c(10, 20, 30)) total <- total + v
total

# while loop: number of Collatz steps from 27 down to 1
collatz_steps <- function(n) {
  steps <- 0
  while (n != 1) {
    if (n %% 2 == 0) n <- n / 2 else n <- 3 * n + 1
    steps <- steps + 1
  }
  steps
}
collatz_steps(27)

# && and || short-circuit: the right-hand side is only evaluated when needed
FALSE && undefined_function()
TRUE || undefined_function()
has_big_first <- function(x) length(x) > 0 && x[1] > 10
has_big_first(c())
has_big_first(c(42, 1))

# Vectorised FizzBuzz with nested ifelse()
n <- 1:15
ifelse(n %% 15 == 0, "FizzBuzz", ifelse(n %% 3 == 0, "Fizz", ifelse(n %% 5 == 0, "Buzz", n)))
