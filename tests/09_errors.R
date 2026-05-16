# 09 - Errors stop evaluation, as in R
safe_divide <- function(a, b) if (b == 0) NA else a / b
safe_divide(10, 4)
safe_divide(10, 0)

f <- function(x) {
  y <- x * 2
  undefined_helper(y)          # the error aborts the whole block...
  "never reached"
}
f(1)
"...and the rest of the script"
