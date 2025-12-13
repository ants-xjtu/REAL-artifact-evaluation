all: graph_division

cppflags=-g -O2 -std=c++20 -fPIC -fno-omit-frame-pointer -fopenmp -Wall

%: %.cc
	g++ ${cppflags} $^ -o $@

clean:
	rm -f graph_division
