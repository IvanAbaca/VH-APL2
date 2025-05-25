#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <netinet/in.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include <sstream>

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
    pthread_t thread;
    int aciertos = 0;
    std::chrono::steady_clock::time_point tiempo_inicio;
    std::chrono::steady_clock::time_point tiempo_fin;
};

std::vector<Jugador> jugadores;
std::mutex mutex_jugadores;
EstadoServidor estado_servidor = SERVIDOR_ESPERANDO_CONEXIONES;
std::string frase_global;
std::string archivo_frases;

std::vector<std::string> cargar_frases(const std::string& archivo) {
    std::ifstream file(archivo);
    std::vector<std::string> frases;
    std::string linea;

    while (std::getline(file, linea)) {
        if (!linea.empty())
            frases.push_back(linea);
    }

    return frases;
}

void* partida_jugador(void* arg) {
    Jugador* jugador = (Jugador*)arg;
    char buffer[BUFFER_SIZE];

    std::cout << "Comenzando partida para jugador '" << jugador->nickname << "'...\n";

    jugador->tiempo_inicio = std::chrono::steady_clock::now();
    std::string frase = frase_global;
    std::string progreso;
    for (char c : frase_global) {
        progreso += (c == ' ') ? ' ' : '_';
    }
    int errores = 0;
    std::vector<char> letras_usadas;

    while (true) {
        int bytes = recv(jugador->socket_fd, buffer, 1, 0);

        if (bytes <= 0) {
            jugador->tiempo_fin = std::chrono::steady_clock::now();
            std::cerr << "Jugador '" << jugador->nickname << "' se desconectó durante la partida.\n";
            std::lock_guard<std::mutex> lock(mutex_jugadores);
            jugador->estado = DESCONECTADO;
            close(jugador->socket_fd);
            return nullptr;
        }

        char letra = std::tolower(buffer[0]);

        // Evitar repetir letras
        if (std::find(letras_usadas.begin(), letras_usadas.end(), letra) != letras_usadas.end()) {
            std::string msg = "Letra ya usada.\n";
            send(jugador->socket_fd, msg.c_str(), msg.size(), 0);
            continue; // seguir escuchando
        }

        letras_usadas.push_back(letra);
        bool acierto = false;

        for (size_t i = 0; i < frase.size(); ++i) {
            if (frase[i] == letra) {
                progreso[i] = letra;
                acierto = true;
                if (acierto) jugador->aciertos++;
            }
        }

        std::string respuesta;
        if (acierto) {
            respuesta += "¡Acierto!\n";
        } else {
            errores++;
            respuesta += "¡Error!\n";
        }

        respuesta += "Frase: " + progreso + "\n";
        respuesta += "Errores: " + std::to_string(errores) + "/3\n";
        respuesta += "Letras usadas: ";
        for (char c : letras_usadas) respuesta += c;
        respuesta += "\n";

        // Fin del juego
        if (progreso == frase) {
            jugador->estado = TERMINADO;
            respuesta += "Ganaste 🎉\nFIN";
            jugador->tiempo_fin = std::chrono::steady_clock::now();
            send(jugador->socket_fd, respuesta.c_str(), respuesta.size(), 0);

            {
                std::lock_guard<std::mutex> lock(mutex_jugadores);

                bool todos_finalizados = std::all_of(jugadores.begin(), jugadores.end(),
                    [](const Jugador& j) {
                        return j.estado == TERMINADO || j.estado == DESCONECTADO;
                    });

                if (todos_finalizados) {
                    estado_servidor = SERVIDOR_ENVIANDO_RESULTADOS;

                    // Armar ranking
                    std::vector<Jugador> ranking = jugadores;

                    std::sort(ranking.begin(), ranking.end(), [](const Jugador& a, const Jugador& b) {
                        if (a.aciertos != b.aciertos)
                            return a.aciertos > b.aciertos;
                        auto ta = std::chrono::duration_cast<std::chrono::seconds>(a.tiempo_fin - a.tiempo_inicio).count();
                        auto tb = std::chrono::duration_cast<std::chrono::seconds>(b.tiempo_fin - b.tiempo_inicio).count();
                        return ta < tb;
                    });

                    std::ostringstream resultados;
                    resultados << "RESULTADOS_FINALES\n";

                    int posicion = 1;
                    for (const auto& j : ranking) {
                        std::string estado_str;
                        switch (j.estado) {
                            case TERMINADO: estado_str = "Terminó"; break;
                            case DESCONECTADO: estado_str = "Desconectado"; break;
                            default: estado_str = "Otro"; break;
                        }

                        auto duracion = std::chrono::duration_cast<std::chrono::seconds>(j.tiempo_fin - j.tiempo_inicio).count();

                        resultados << posicion++ << ". " << j.nickname
                                << " | Aciertos: " << j.aciertos
                                << " | Tiempo: " << duracion << "s"
                                << " | Estado: " << estado_str << "\n";
                    }

                    std::string mensaje_final = resultados.str();

                    // Enviar a todos los jugadores que terminaron bien
                    for (auto& j : jugadores) {
                        if (j.estado == TERMINADO) {
                            send(j.socket_fd, mensaje_final.c_str(), mensaje_final.size(), 0);
                            close(j.socket_fd);
                        }
                    }

                    std::cout << "\n📊 Resultados finales:\n" << mensaje_final;
                    std::cout << "\n♻️ Reiniciando servidor para nueva partida...\n";

                    jugadores.clear();
                    estado_servidor = SERVIDOR_ESPERANDO_CONEXIONES;
                }
            }

            return nullptr;
        }
        if (errores >= 3) {
            jugador->estado = TERMINADO;
            respuesta += "Perdiste 💀\nFIN";
            jugador->tiempo_fin = std::chrono::steady_clock::now();
            send(jugador->socket_fd, respuesta.c_str(), respuesta.size(), 0);

            {
                std::lock_guard<std::mutex> lock(mutex_jugadores);

                bool todos_finalizados = std::all_of(jugadores.begin(), jugadores.end(),
                    [](const Jugador& j) {
                        return j.estado == TERMINADO || j.estado == DESCONECTADO;
                    });

                if (todos_finalizados) {
                    estado_servidor = SERVIDOR_ENVIANDO_RESULTADOS;

                    // Armar ranking
                    std::vector<Jugador> ranking = jugadores;

                    std::sort(ranking.begin(), ranking.end(), [](const Jugador& a, const Jugador& b) {
                        if (a.aciertos != b.aciertos)
                            return a.aciertos > b.aciertos;
                        auto ta = std::chrono::duration_cast<std::chrono::seconds>(a.tiempo_fin - a.tiempo_inicio).count();
                        auto tb = std::chrono::duration_cast<std::chrono::seconds>(b.tiempo_fin - b.tiempo_inicio).count();
                        return ta < tb;
                    });

                    std::ostringstream resultados;
                    resultados << "RESULTADOS_FINALES\n";

                    int posicion = 1;
                    for (const auto& j : ranking) {
                        std::string estado_str;
                        switch (j.estado) {
                            case TERMINADO: estado_str = "Terminó"; break;
                            case DESCONECTADO: estado_str = "Desconectado"; break;
                            default: estado_str = "Otro"; break;
                        }

                        auto duracion = std::chrono::duration_cast<std::chrono::seconds>(j.tiempo_fin - j.tiempo_inicio).count();

                        resultados << posicion++ << ". " << j.nickname
                                << " | Aciertos: " << j.aciertos
                                << " | Tiempo: " << duracion << "s"
                                << " | Estado: " << estado_str << "\n";
                    }

                    std::string mensaje_final = resultados.str();

                    // Enviar a todos los jugadores que terminaron bien
                    for (auto& j : jugadores) {
                        if (j.estado == TERMINADO) {
                            send(j.socket_fd, mensaje_final.c_str(), mensaje_final.size(), 0);
                            close(j.socket_fd);
                        }
                    }

                    std::cout << "\n📊 Resultados finales:\n" << mensaje_final;
                    std::cout << "\n♻️ Reiniciando servidor para nueva partida...\n";

                    jugadores.clear();
                    estado_servidor = SERVIDOR_ESPERANDO_CONEXIONES;
                }
            }

            return nullptr;
        }

        // Enviar avance
        send(jugador->socket_fd, respuesta.c_str(), respuesta.size(), 0);
    }

    return nullptr;
}

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

                    std::vector<std::string> frases = cargar_frases(archivo_frases);
                    if (frases.empty()) {
                        std::cerr << "El archivo de frases está vacío.\n";
                        exit(1);
                    }

                    srand(time(nullptr));
                    frase_global = frases[rand() % frases.size()];
                    std::cout << "Frase seleccionada: " << frase_global << "\n";

                    for (auto& j : jugadores) {
                        const char* aviso = "PARTIDA_INICIADA\n";
                        send(j.socket_fd, aviso, strlen(aviso), 0);
                    }

                    for (auto& j : jugadores) {
                        if (j.estado == LISTO) {
                            j.estado = JUGANDO;
                            pthread_t hilo;
                            pthread_create(&hilo, nullptr, partida_jugador, &j);
                            j.thread = hilo; // opcional, si querés guardarlo
                            pthread_detach(hilo); // no vamos a hacer join
                        }
                    }
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

                        std::vector<std::string> frases = cargar_frases(archivo_frases);
                        if (frases.empty()) {
                            std::cerr << "El archivo de frases está vacío.\n";
                            exit(1);
                        }

                        srand(time(nullptr));
                        frase_global = frases[rand() % frases.size()];
                        std::cout << "Frase seleccionada: " << frase_global << "\n";

                        for (auto& j : jugadores) {
                            const char* aviso = "PARTIDA_INICIADA\n";
                            send(j.socket_fd, aviso, strlen(aviso), 0);
                        }

                        for (auto& j : jugadores) {
                            if (j.estado == LISTO) {
                                j.estado = JUGANDO;
                                pthread_t hilo;
                                pthread_create(&hilo, nullptr, partida_jugador, &j);
                                j.thread = hilo; // opcional, si querés guardarlo
                                pthread_detach(hilo); // no vamos a hacer join
                            }
                        }
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

int puerto = -1;
int max_usuarios = -1;

void mostrar_ayuda() {
    std::cout << "Uso: ./servidor [opciones]\n"
              << "Opciones:\n"
              << "  -p  --puerto <número>       Puerto de escucha (requerido)\n"
              << "  -u  --usuarios <cantidad>   Cantidad máxima de usuarios (requerido)\n"
              << "  -a  --archivo <archivo>     Archivo de frases (requerido)\n"
              << "  -h  --help                  Muestra esta ayuda\n";
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            mostrar_ayuda();
            return 0;
        } else if ((arg == "-p" || arg == "--puerto") && i + 1 < argc) {
            puerto = std::stoi(argv[++i]);
        } else if ((arg == "-u" || arg == "--usuarios") && i + 1 < argc) {
            max_usuarios = std::stoi(argv[++i]);
        } else if ((arg == "-a" || arg == "--archivo") && i + 1 < argc) {
            archivo_frases = argv[++i];
        } else {
            std::cerr << "Parámetro no reconocido: " << arg << "\n";
            mostrar_ayuda();
            return 1;
        }
    }

    // Validaciones
    if (puerto <= 0 || archivo_frases.empty() || max_usuarios <= 0) {
        std::cerr << "Faltan parámetros requeridos o son inválidos.\n\n";
        mostrar_ayuda();
        return 1;
    }

    int servidor_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (servidor_fd == -1) {
        std::cerr << "Error al crear socket\n";
        return 1;
    }

    sockaddr_in direccion{};
    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = INADDR_ANY;
    direccion.sin_port = htons(puerto);

    if (bind(servidor_fd, (sockaddr*)&direccion, sizeof(direccion)) < 0) {
        std::cerr << "Error en bind()\n";
        return 1;
    }

    if (listen(servidor_fd, max_usuarios) < 0) {
        std::cerr << "Error en listen()\n";
        return 1;
    }

    std::cout << "Servidor escuchando en el puerto " << puerto << "...\n";

    while (true) {
        int cliente_fd = accept(servidor_fd, nullptr, nullptr);
        if (cliente_fd < 0) {
            std::cerr << "Error al aceptar conexión\n";
            continue;
        }

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
