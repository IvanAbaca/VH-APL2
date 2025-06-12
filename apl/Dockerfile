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

# Compilar el proyecto
RUN make clean && make

# Exponer el puerto por defecto
EXPOSE 5000

# Comando por defecto
CMD ["./servidor", "-p", "5000", "-u", "5", "-a", "frases.txt"]