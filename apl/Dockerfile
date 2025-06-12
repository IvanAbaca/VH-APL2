# Usar imagen base de Ubuntu con herramientas de desarrollo
FROM ubuntu:24.10

# Instalar dependencias necesarias
RUN apt-get update && apt-get install -y \
    g++ \
    make \
    && rm -rf /var/lib/apt/lists/*

# Crear directorio de trabajo
WORKDIR /app

# Copiar archivos específicos del ejercicio5
COPY ejercicio5/ .

# Compilar el proyecto (sin clear)
RUN make clean
RUN g++ servidor.cpp -o servidor -pthread
RUN g++ cliente.cpp -o cliente

# Exponer el puerto por defecto
EXPOSE 5000