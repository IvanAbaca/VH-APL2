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
