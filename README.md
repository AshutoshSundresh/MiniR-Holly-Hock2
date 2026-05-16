# MiniR-Holly-Hock2

MiniR-Holly-Hock2 is a small, self-contained **R-like interpreter** written in modern C++ (C++20).
It implements a REPL that reads R code from standard input, lexes and parses it into an internal AST representation, and then evaluates it with R-style semantics for vectors lists, matrices, data frames, closures, and a set of built-in functions.

The project is designed to:

- Model a useful subset of the R language.
- Be portable and compact enough to target constrained or embedded environments, specifically the fx-CP400, fx-CP400+E, and the fx-CG500.
- Avoid heavy dependencies on the C++ standard library containers and strings.

## Utility 

Right now, MiniR-Holly-Hock2 provides:

- An interactive **CLI REPL** (`minir` / `minir.exe`) that accepts R-like code.
- A custom **lexer** and **parser** for a substantial subset of R syntax:
  - Identifiers, numbers, strings.
  - Keywords such as `if`, `else`, `for`, `in`, `while`, `function`, `TRUE`, `FALSE`, `NA`, `NULL`, etc.
  - Operators like `+`, `-`, `*`, `/`, `^`, `==`, `!=`, `<`, `>`, `<=`, `>=`,
    assignment operators (`<-`, `=`, `<<-`), indexing operators (`[`, `[[`, `$`),
    and other R-style operators (e.g., `:`, `%%`, `%/%`, `%*%`, `%in%`).
- A tree-walking **evaluator** that supports:
  - R-like primitive types (logical, integer, double, character, etc.).
  - Vectors, lists, matrices, data frames, and attributes (e.g., `names`, `dim`, `class`).
  - Environments, symbol lookup, and lexical scoping.
  - Control flow (`if`/`else`, `for`, `while`).
  - Function calls, including user-defined closures and many built-in functions.
  - Vectorized arithmetic with R-like recycling rules (where implemented).

The CLI entry point (`cli_main.cpp`) implements a read–eval–print loop with multi-line support: it keeps reading additional lines while parentheses/braces/brackets are unbalanced. You can exit the REPL with `exit`, `quit`, or `q()`.

## Building

- A C++20-capable compiler.
- A standard POSIX-like `make` (for the provided `Makefile`), except when building manually with MSVC.

## Usage

Once `minir` / `minir.exe` is built, run it and type R-like code at the prompt.
Here are three example usage patterns (using standard R semantics):

### 1) Arithmetic and simple variables

```r
1+1
x <- 5
x
```

### 2) Sequences, replication, and coercion

```r
1:10
seq(from=0, to=1, length.out=6)
rep(c(1,2), each=3)
sequence(c(3,1,4))
c("a", 1, TRUE, NA)
```

### 3) Matrices, data frames, and indexing

```r
M <- matrix(1:12, nrow=3, ncol=4)
which(M > 8, arr.ind=TRUE)
rowSums(M)
colMeans(M)

df <- data.frame(Height=c(60,65,70), Weight=c(120,150,180))
df$Height
df[df$Weight > 130, ]
```

## Running scripts

`minir file.R` runs a script without the interactive prompt, printing visible results
and stopping at the first error. Add `--echo` to get an R-style transcript, with each
statement shown before its output:

```
$ minir --echo tests/02_functions.R
> # 02 - Functions and closures
> square <- function(x) x^2
> square(1:5)
[1]  1  4  9 16 25
...
```

## Tests

`tests/` holds annotated example scripts covering each area of the language. Each one
has an expected transcript in `tests/expected/`:

| Script | Covers |
|---|---|
| `01_basics.R` | arithmetic, recycling, precedence, integer vs double, number printing |
| `02_functions.R` | defaults, named arguments, recursion, closures, `<<-`, `sapply`/`lapply` |
| `03_control_flow.R` | `if`/`else`, `for`, `while`, short-circuit `&&`/`||`, vectorised FizzBuzz |
| `04_vectors.R` | names, logical/negative/name indexing, `seq`/`rep`, `order`, `%in%` |
| `05_missing_values.R` | NA propagation, `na.rm`, three-valued logic, character NA |
| `06_matrices.R` | `matrix`, `t`, `%*%`, row/column subsetting, `rowSums`, `diag` |
| `07_data_frames.R` | data frames, filtering and sorting rows, factors, `table` |
| `08_stats_random.R` | `mean`/`var`/`sd`, reproducible `set.seed`/`runif`/`sample` |
| `09_errors.R` | errors aborting a function body and the script |

Run them all with `make test`, or `sh tests/run_tests.sh` after building with
`build_msvc.bat` (Git Bash works on Windows). `sh tests/run_tests.sh --update`
regenerates the expected transcripts after an intentional output change.
