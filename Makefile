tetris: tetris.cc
	g++ -std=c++11 -Wall -Wextra -Wpedantic -o $@ $< -lncurses

clean:
	rm -rf tetris
