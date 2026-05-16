# 05 - Missing values (NA)
v <- c(4, NA, 7, NA, 1)
v + 1                          # NA propagates through arithmetic
sum(v)
sum(v, na.rm = TRUE)
mean(v, na.rm = TRUE)
is.na(v)
v[!is.na(v)]
ifelse(is.na(v), 0, v)

# Three-valued logic
NA > 1
NA && FALSE                    # FALSE no matter what the NA is
NA || TRUE

# Character NA is distinct from the string "NA"
words <- c("apple", NA, "cherry")
words
is.na(words)
nchar(words)
paste("fruit:", words)         # paste() spells it out as "NA"
