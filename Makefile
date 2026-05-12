CXX = g++
CXXFLAGS = -I. -std=c++20 -Wall -O2

SRCS = cli_main.cpp runtime/Evaluator.cpp runtime/Parser.cpp runtime/Lexer.cpp \
       runtime/BuiltinMath.cpp runtime/BuiltinLogical.cpp runtime/BuiltinString.cpp \
       runtime/BuiltinRandom.cpp runtime/BuiltinVector.cpp runtime/BuiltinSubset.cpp \
       runtime/BuiltinSelection.cpp runtime/Print.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = minir

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
