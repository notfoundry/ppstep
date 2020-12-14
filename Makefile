
pp: src/ppstep.cpp src/client.hpp src/client_fwd.hpp src/server.hpp src/view.hpp src/utils.hpp external/linenoise/linenoise.c
	g++ -g -std=c++17 src/ppstep.cpp external/linenoise/linenoise.c -I external -lboost_system -lboost_filesystem -lboost_thread -lboost_wave -o pp