# Imagen base recomendada
FROM ubuntu:24.10

# Actualizar el sistema e instalar dependencias necesarias
RUN apt-get update && \
apt-get install -y build-essential

# Crear el directorio /apl dentro del contenedor
RUN mkdir /apl

# Copiar todo el contenido del trabajo práctico al contenedor
COPY . .

# Establecer el directorio de trabajo
WORKDIR /apl

# Compilar todos los ejercicios
RUN make -C ejercicio1
RUN make -C ejercicio2
RUN make -C ejercicio3
RUN make -C ejercicio4
RUN make -C ejercicio5

# Comando por defecto al iniciar el contenedor
CMD ["tail", "-f", "/dev/null"]
