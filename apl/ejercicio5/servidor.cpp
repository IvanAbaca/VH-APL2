#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <netinet/in.h>
#include <unistd.h>
#include <algorithm>

#define PUERTO 5000
#define BUFFER_SIZE 1024

enum EstadoJugador {
    ESPERANDO,
    LISTO,
    JUGANDO,
    DESCONECTADO,
    TERMINADO
};
enum EstadoServidor {
    SERVIDOR_ESPERANDO_CONEXIONES,
    SERVIDOR_ESPERANDO_LISTOS,
    SERVIDOR_JUGANDO,
    SERVIDOR_ENVIANDO_RESULTADOS
};
struct Jugador {
    int socket_fd;
    std::string nickname = "(sin_nick)";
    EstadoJugador estado = ESPERANDO;
};

std::vector<Jugador> jugadores;
std::mutex mutex_jugadores;
EstadoServidor estado_servidor = SERVIDOR_ESPERANDO_CONEXIONES;

void manejar_conexion(int cliente_fd) {
    char buffer[BUFFER_SIZE];

    // 1. Recibir nickname
    int bytes = recv(cliente_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes <= 0) {
        std::cerr << "Error al recibir nickname inicial.\n";
        close(cliente_fd);
        return;
    }
    buffer[bytes] = '\0';
    std::string nickname(buffer);

    // 2. Validar nickname único
    {
        std::lock_guard<std::mutex> lock(mutex_jugadores);
        for (const auto& j : jugadores) {
            if (j.nickname == nickname) {
                const char* msg = "Error: nickname ya registrado.\n";
                send(cliente_fd, msg, strlen(msg), 0);
                close(cliente_fd);
                std::cout << "Conexión rechazada por nickname duplicado: " << nickname << "\n";
                return;
            }
        }
    }

    // 3. Agregar jugador válido a la lista
    {
        std::lock_guard<std::mutex> lock(mutex_jugadores);
        Jugador nuevo_jugador;
        nuevo_jugador.socket_fd = cliente_fd;
        nuevo_jugador.nickname = nickname;
        nuevo_jugador.estado = ESPERANDO;
        jugadores.push_back(nuevo_jugador);
        std::cout << "Jugador conectado: " << nickname << "\n";
    }

    // 4. Confirmación de conexión
    const char* ok = "Conectado correctamente al servidor.\n";
    send(cliente_fd, ok, strlen(ok), 0);

    // 5. Esperar "listo"
    while (true) {
        bytes = recv(cliente_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            std::cerr << "Jugador '" << nickname << "' se desconectó.\n";

            {
                std::lock_guard<std::mutex> lock(mutex_jugadores);
                auto it = std::remove_if(jugadores.begin(), jugadores.end(),
                    [&](const Jugador& j) {
                        return j.nickname == nickname;
                    });
                jugadores.erase(it, jugadores.end());

                int total_listos = std::count_if(jugadores.begin(), jugadores.end(),
                    [](const Jugador& j) { return j.estado == LISTO; });

                std::cout << "Actualización: " << total_listos << "/" << jugadores.size()
                          << " jugadores listos tras desconexión.\n";

                if (!jugadores.empty() && total_listos == (int)jugadores.size()) {
                    estado_servidor = SERVIDOR_JUGANDO;
                    std::cout << "Todos los jugadores restantes están listos. ¡La partida comienza!\n";
                }
            }

            close(cliente_fd);
            return;
        }

        buffer[bytes] = '\0';
        std::string comando(buffer);

        if (comando == "listo" || comando == "LISTO") {
            std::lock_guard<std::mutex> lock(mutex_jugadores);
            for (auto& j : jugadores) {
                if (j.nickname == nickname && j.estado != LISTO) {
                    j.estado = LISTO;

                    int total_listos = std::count_if(jugadores.begin(), jugadores.end(),
                        [](const Jugador& j) { return j.estado == LISTO; });

                    std::cout << "Jugador '" << nickname << "' está LISTO (" 
                              << total_listos << "/" << jugadores.size() << ")\n";

                    if (total_listos == (int)jugadores.size()) {
                        estado_servidor = SERVIDOR_JUGANDO;
                        std::cout << "Todos los jugadores están listos. ¡La partida comienza!\n";
                    }

                    break;
                }
            }

            break; // salimos del bucle tras confirmar
        } else {
            const char* invalido = "Comando no válido. Escribí 'listo'.\n";
            send(cliente_fd, invalido, strlen(invalido), 0);
        }
    }

    // IMPORTANTE: no cerramos el socket, se mantiene para la partida futura
}

int main() {
    int servidor_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (servidor_fd == -1) {
        std::cerr << "Error al crear socket\n";
        return 1;
    }

    sockaddr_in direccion{};
    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = INADDR_ANY;
    direccion.sin_port = htons(PUERTO);

    if (bind(servidor_fd, (sockaddr*)&direccion, sizeof(direccion)) < 0) {
        std::cerr << "Error en bind()\n";
        return 1;
    }

    if (listen(servidor_fd, 10) < 0) {
        std::cerr << "Error en listen()\n";
        return 1;
    }

    std::cout << "Servidor escuchando en el puerto " << PUERTO << "...\n";

    while (true) {
        int cliente_fd = accept(servidor_fd, nullptr, nullptr);
        if (cliente_fd < 0) {
            std::cerr << "Error al aceptar conexión\n";
            continue;
        }

        // 🚫 Verificar estado global antes de aceptar lógicamente
        if (estado_servidor == SERVIDOR_JUGANDO || estado_servidor == SERVIDOR_ENVIANDO_RESULTADOS) {
            const char* rechazo = "El juego ya ha comenzado. Intente más tarde.\n";
            send(cliente_fd, rechazo, strlen(rechazo), 0);
            close(cliente_fd);
            std::cout << "Conexión rechazada: el juego ya está en curso.\n";
            continue;
        }

        std::thread hilo_cliente(manejar_conexion, cliente_fd);
        hilo_cliente.detach();
    }

    close(servidor_fd);
    return 0;
}
