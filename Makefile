all: build

build: clean compile

server:
	./ttts 15000

client:
	./ttt 0.0.0.0 15000

rsgnclient:
	./tttrsgn 0.0.0.0 15000

winclient:
	./tttwin 0.0.0.0 15000

breakclient:
	./tttbreak 0.0.0.0 15000

testProtocol:
	./protocoltest input.txt

compile:
	gcc -g -Wall -Werror -fsanitize=address -std=c99 ttts.c -pthread protocol.c -o ttts
	gcc ttt.c -o ttt
	gcc tttrsgn.c -o tttrsgn
	gcc tttwin.c -o tttwin
	gcc tttbreak.c -o tttbreak
	gcc protocoltest.c protocol.c -o protocoltest

clean:
	rm -f ttt
	rm -f ttts
	rm -f tttbreak
	rm -f tttwin
	rm -f tttrsgn
	rm -f protocoltest
	rm -f output.txt