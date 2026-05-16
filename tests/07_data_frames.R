# 07 - Data frames and factors
people <- data.frame(
  name = c("Ana", "Ben", "Cy", "Dee"),
  age = c(34, 28, 45, 39),
  height = c(165.5, 180, 172.25, 158)
)
people
nrow(people)
names(people)
people$age
mean(people$height)
people[people$age > 30, ]      # filter rows (original row names are kept)
people[, "name"]               # a single column comes back as a vector
people[order(people$age), ]    # sort rows by a column

# Factors store categories as integer codes plus levels
sizes <- factor(c("small", "large", "medium", "small", "small"))
sizes
levels(sizes)
table(c("small", "large", "medium", "small", "small"))
