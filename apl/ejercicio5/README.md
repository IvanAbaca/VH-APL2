# generar imagen
docker build -t vh-apl2-image .

# generar container
docker run -it --name vh-apl2-container vh-apl2-image bash

# ejecutar en container
docker exec -it vh-apl2-container sh
cd ./ejercicio5

# Caso base Server
./servidor -p 5000 -u 3 -a frases.txt

# Caso base Cliente
./cliente -n uno -p 5000 -s 127.0.0.1
./cliente -n dos -p 5000 -s 127.0.0.1
./cliente -n tres -p 5000 -s 127.0.0.1
./cliente -n cuatro -p 5000 -s 127.0.0.1

# Casos de error que cubre:
./servidor -p 5000
./servidor -p -u 2 -a frases.txt
./servidor --puert 5000
./servidor -p asdf -u 2 -a frases.txt
./servidor -p -1 -u 2 -a frases.txt
./servidor -p 70000 -u 2 -a frases.txt
./servidor -p 5000 -u cero -a frases.txt
./servidor -p 5000 -u 0 -a frases.txt
./servidor -p 5000 -u 2 -a noexiste.txt