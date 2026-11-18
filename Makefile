CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall
PORT ?= 8080

# ---- Web app (backend + dashboard) ----------------------------------------
all: wims_server

wims_server: server.cpp pbl.cpp
	$(CXX) $(CXXFLAGS) server.cpp -o wims_server

# Run the web dashboard
run: wims_server
	./wims_server $(PORT)

# ---- Original console app (unchanged) -------------------------------------
console: pbl.cpp
	$(CXX) $(CXXFLAGS) pbl.cpp -o wims_console

clean:
	rm -f wims_server wims_console

.PHONY: all run console clean
