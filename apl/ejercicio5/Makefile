all: clean servidor cliente clear

servidor: servidor.cpp
	g++ servidor.cpp -o servidor -pthread

cliente: cliente.cpp
	g++ cliente.cpp -o cliente

clean:
	rm -f servidor cliente

clear:
	clear