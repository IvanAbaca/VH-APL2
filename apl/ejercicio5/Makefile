all: Ejercicio5

Ejercicio5: clean servidor cliente

servidor: servidor.cpp
	g++ servidor.cpp -o servidor -pthread

cliente: cliente.cpp
	g++ cliente.cpp -o cliente

clean:
	rm -f servidor cliente

clear:
	clear