# Compilador
CXX = g++

# Archivos fuente
CLIENTE_SRC = cliente.cpp
SERVER_SRC = server.cpp

# Ejecutables
CLIENTE_BIN = cliente
SERVER_BIN = servidor

# Regla por defecto
all: $(CLIENTE_BIN) $(SERVER_BIN)

# Compilar cliente
$(CLIENTE_BIN): $(CLIENTE_SRC) ahorcado.h
	$(CXX) -o $(CLIENTE_BIN) $(CLIENTE_SRC)

# Compilar servidor
$(SERVER_BIN): $(SERVER_SRC) ahorcado.h
	$(CXX) -o $(SERVER_BIN) $(SERVER_SRC)

# Limpiar ejecutables
clean:
	rm -f $(CLIENTE_BIN) $(SERVER_BIN)
