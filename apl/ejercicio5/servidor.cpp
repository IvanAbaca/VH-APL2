/*
43262563 Abaca, Ivan
43971511 Guardati, Francisco
45542385 Palmieri, Marco
41566741 Piegari, Lucila Quiroga
43780360 Romero, Lucas Nicolas
*/

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
#include <chrono>
#include <signal.h>
#include <sys/file.h>
#include <fcntl.h>
#include <errno.h>

#define PUERTO 5000
#define BUFFER_SIZE 1024
#define LOCKFILE_PATH "/tmp/servidor_ahorcado.lock"

int lockfile_fd = -1;  // Variable global para el file descriptor del lockfile

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

int puerto = -1;
int max_usuarios = -1;
int max_errores = 7;
int servidor_fd = -1;  // Socket del servidor (global para cleanup)
std::vector<Jugador> jugadores;
std::mutex mutex_jugadores;
EstadoServidor estado_servidor = SERVIDOR_ESPERANDO_CONEXIONES;
std::string frase_global;
std::string archivo_frases;
bool puerto_set = false, usuarios_set = false, archivo_set = false;
bool servidor_corriendo = true;

void cleanup_lockfile() {
    if (servidor_fd != -1) {
        close(servidor_fd);
        servidor_fd = -1;
    }
    if (lockfile_fd != -1) {
        close(lockfile_fd);
        unlink(LOCKFILE_PATH);
        lockfile_fd = -1;
    }
}

bool validar_frase(const std::string& frase) {
    if (frase.empty()) return false;
    
    int letras_jugables = 0;
    for (char c : frase) {
        if (c == ' ') {
            continue; // Espacios permitidos
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            letras_jugables++;
        } else {
            return false; // Carácter no válido
        }
    }
    
    return letras_jugables >= 3; // Al menos 3 letras jugables
}

bool validar_nickname(const std::string& nickname) {
    if (nickname.empty() || nickname.length() > 20) {
        return false;
    }
    
    for (char c : nickname) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
            return false; // Solo letras a-z, A-Z
        }
    }
    
    return true;
}

std::vector<std::string> cargar_frases(const std::string& archivo) {
    std::ifstream file(archivo);
    std::vector<std::string> frases;
    std::string linea;
    int linea_num = 1;

    while (std::getline(file, linea)) {
        // Limpiar espacios al inicio y final
        linea.erase(0, linea.find_first_not_of(" \t\r\n"));
        linea.erase(linea.find_last_not_of(" \t\r\n") + 1);
        
        if (!linea.empty()) {
            if (validar_frase(linea)) {
                frases.push_back(linea);
                std::cout << "Frase válida línea " << linea_num << ": " << linea << std::endl;
            } else {
                std::cerr << "ERROR: Frase inválida en línea " << linea_num << ": '" << linea << "'" << std::endl;
                std::cerr << "       Solo se permiten letras (a-z, A-Z) y espacios, mínimo 3 letras." << std::endl;
                exit(1);
            }
        }
        linea_num++;
    }

    return frases;
}

void verificar_jugadores_listos() {
    std::cout << "🔍 Verificando jugadores listos..." << std::endl;
    
    for (auto it = jugadores.begin(); it != jugadores.end();) {
        if (it->estado == LISTO) {
            // Enviar ACK
            const char* ack_msg = "ACK\n";
            if (send(it->socket_fd, ack_msg, 4, 0) < 0) {
                std::cout << "Jugador " << it->nickname << " desconectado (error enviando ACK)" << std::endl;
                close(it->socket_fd);
                it = jugadores.erase(it);
                continue;
            }
            
            // Esperar respuesta
            char buffer[BUFFER_SIZE];
            int bytes = recv(it->socket_fd, buffer, BUFFER_SIZE - 1, 0);
            if (bytes <= 0) {
                std::cout << "Jugador " << it->nickname << " desconectado (no respondió ACK)" << std::endl;
                close(it->socket_fd);
                it = jugadores.erase(it);
                continue;
            }
            
            buffer[bytes] = '\0';
            std::string respuesta(buffer);
            
            if (respuesta.find("ACK_OK") == std::string::npos) {
                std::cout << "Jugador " << it->nickname << " desconectado (respuesta inválida)" << std::endl;
                close(it->socket_fd);
                it = jugadores.erase(it);
                continue;
            }
            
            std::cout << "Jugador " << it->nickname << " respondió ACK correctamente" << std::endl;
        }
        ++it;
    }
}

void reiniciar_servidor() {
    std::cout << "\n♻️ Reiniciando servidor para nueva partida...\n";

    // Cerrar cualquier socket que pueda haber quedado abierto
    for (auto& j : jugadores) {
        if (j.socket_fd != -1) {
            close(j.socket_fd);
            j.socket_fd = -1;
        }
    }

    jugadores.clear();
    estado_servidor = SERVIDOR_ESPERANDO_CONEXIONES;
}

void enviar_resultados_finales() {
    estado_servidor = SERVIDOR_ENVIANDO_RESULTADOS;

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

    // Solo enviar a jugadores que terminaron normalmente (no por señal)
    for (auto& j : jugadores) {
        if (j.estado == TERMINADO && j.socket_fd != -1) {
            send(j.socket_fd, mensaje_final.c_str(), mensaje_final.size(), 0);
            close(j.socket_fd);
        }
    }

    std::cout << "\n📊 Resultados finales:\n" << mensaje_final;
    reiniciar_servidor();
}

std::string dibujo_ahorcado(int errores) {
    const std::vector<std::string> estados = {
        " +---+\n"
        " |   |\n"
        "     |\n"
        "     |\n"
        "     |\n"
        "     |\n"
        "=========\n",

        " +---+\n"
        " |   |\n"
        " O   |\n"
        "     |\n"
        "     |\n"
        "     |\n"
        "=========\n",

        " +---+\n"
        " |   |\n"
        " O   |\n"
        " |   |\n"
        "     |\n"
        "     |\n"
        "=========\n",

        " +---+\n"
        " |   |\n"
        " O   |\n"
        "/|   |\n"
        "     |\n"
        "     |\n"
        "=========\n",

        " +---+\n"
        " |   |\n"
        " O   |\n"
        "/|\\  |\n"
        "     |\n"
        "     |\n"
        "=========\n",

        " +---+\n"
        " |   |\n"
        " O   |\n"
        "/|\\  |\n"
        "/    |\n"
        "     |\n"
        "=========\n",

        " +---+\n"
        " |   |\n"
        " O   |\n"
        "/|\\  |\n"
        "/ \\  |\n"
        "     |\n"
        "=========\n"
    };

    if (errores < 0) errores = 0;
    if (errores >= (int)estados.size()) errores = estados.size() - 1;
    return estados[errores];
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

            // ✅ Verificar si todos finalizaron ahora
            bool todos_finalizados = std::all_of(jugadores.begin(), jugadores.end(),
                [](const Jugador& j) {
                    return j.estado == TERMINADO || j.estado == DESCONECTADO;
                });

            if (todos_finalizados) {
                enviar_resultados_finales();
            }

            return nullptr;
        }

        char letra = std::tolower(buffer[0]);
        if (!std::isalpha(letra)) {
            std::string msg = "Carácter inválido. Ingresá una letra del abecedario.\n";
            send(jugador->socket_fd, msg.c_str(), msg.size(), 0);
            continue;
        }


        // Evitar repetir letras
        if (std::find(letras_usadas.begin(), letras_usadas.end(), letra) != letras_usadas.end()) {
            std::string msg = "Letra ya usada.\n";
            send(jugador->socket_fd, msg.c_str(), msg.size(), 0);
            continue; // seguir escuchando
        }

        letras_usadas.push_back(letra);
        bool acierto = false;

        for (size_t i = 0; i < frase.size(); ++i) {
            if (std::tolower(frase[i]) == letra) {
                progreso[i] = frase[i];
                acierto = true;
            }
        }
        
        if (acierto) jugador->aciertos++;

        std::string respuesta;
        if (!acierto) {
            errores++;
        }
        respuesta += "\n" + dibujo_ahorcado(errores);

        respuesta += "Frase: " + progreso + "\n";
        respuesta += "Errores: " + std::to_string(errores) + "/" + std::to_string(max_errores) + "\n";
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
                    enviar_resultados_finales();
                } else {
                    // Si no todos terminaron, cerramos este socket individualmente
                    close(jugador->socket_fd);
                    jugador->socket_fd = -1;
                }
            }

            return nullptr;
        }
        if (errores >= max_errores) {
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
                    enviar_resultados_finales();
                } else {
                    // Si no todos terminaron, cerramos este socket individualmente
                    close(jugador->socket_fd);
                    jugador->socket_fd = -1;
                }
            }

            return nullptr;
        }

        // Enviar avance
        send(jugador->socket_fd, respuesta.c_str(), respuesta.size(), 0);
    }

    return nullptr;
}

void iniciar_partida() {
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
            j.thread = hilo;
            pthread_detach(hilo);
        }
    }
}

void mostrar_estado_listos() {
    // Verificar jugadores listos antes de contar
    verificar_jugadores_listos();
    
    int total_listos = std::count_if(jugadores.begin(), jugadores.end(),
        [](const Jugador& j) { return j.estado == LISTO; });

    std::cout << "Actualización: " << total_listos << "/" << jugadores.size()
              << " jugadores listos\n";

    if (!jugadores.empty() && total_listos == (int)jugadores.size()) {
        iniciar_partida();
    }
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

    // Validar nickname (solo a-z, A-Z, 1-20 caracteres)
    if (!validar_nickname(nickname)) {
        const char* msg = "Error: nickname inválido. Solo letras a-z, A-Z, máximo 20 caracteres.\n";
        send(cliente_fd, msg, strlen(msg), 0);
        close(cliente_fd);
        std::cout << "Conexión rechazada por nickname inválido: " << nickname << "\n";
        return;
    }

    // 2. PRIMERO: Verificar jugadores listos (limpiar desconectados)
    {
        std::lock_guard<std::mutex> lock(mutex_jugadores);
        verificar_jugadores_listos();
    }

    // 3. DESPUÉS: Validar nickname único
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

    // 4. Agregar jugador válido a la lista
    {
        std::lock_guard<std::mutex> lock(mutex_jugadores);
        Jugador nuevo_jugador;
        nuevo_jugador.socket_fd = cliente_fd;
        nuevo_jugador.nickname = nickname;
        nuevo_jugador.estado = ESPERANDO;
        jugadores.push_back(nuevo_jugador);
        std::cout << "Jugador conectado: " << nickname << "\n";
    }

    // 5. Confirmación de conexión
    const char* ok = "Conectado correctamente al servidor.\n";
    send(cliente_fd, ok, strlen(ok), 0);

    // 6. Esperar "listo"
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

                mostrar_estado_listos();
            }

            close(cliente_fd);
            return;
        }

        buffer[bytes] = '\0';
        std::string comando(buffer);
        std::transform(comando.begin(), comando.end(), comando.begin(), ::tolower);
        
        if (comando == "listo") {
            std::lock_guard<std::mutex> lock(mutex_jugadores);
            for (auto& j : jugadores) {
                if (j.nickname == nickname && j.estado != LISTO) {
                    j.estado = LISTO;

                    mostrar_estado_listos();
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

void mostrar_ayuda() {
    std::cout << "Uso: ./servidor [opciones]\n"
              << "Opciones:\n"
              << "  -p  --puerto <número>       Puerto de escucha (requerido)\n"
              << "  -u  --usuarios <cantidad>   Cantidad máxima de usuarios (requerido)\n"
              << "  -a  --archivo <archivo>     Archivo de frases (requerido)\n"
              << "  -h  --help                  Muestra esta ayuda\n";
}

bool es_entero(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}

void manejador_senales(int signal) {
    switch(signal) {
        case SIGINT:
            std::cout << "\n[SEÑAL] SIGINT recibida - Ignorando..." << std::endl;
            break;
            
        case SIGUSR1: {
            std::lock_guard<std::mutex> lock(mutex_jugadores);
            if (estado_servidor != SERVIDOR_JUGANDO) {
                std::cout << "\n[SEÑAL] SIGUSR1 - No hay partidas en curso, finalizando..." << std::endl;
                servidor_corriendo = false;
                cleanup_lockfile();
                exit(0);
            } else {
                std::cout << "\n[SEÑAL] SIGUSR1 - Hay partidas en curso, ignorando..." << std::endl;
            }
            break;
        }
        
        case SIGUSR2:
        case SIGTERM: {
            std::lock_guard<std::mutex> lock(mutex_jugadores);
            std::string signal_name = (signal == SIGUSR2) ? "SIGUSR2" : "SIGTERM";
            
            if (estado_servidor == SERVIDOR_JUGANDO) {
                std::cout << "\n[SEÑAL] " << signal_name << " - Finalizando partidas en curso..." << std::endl;
                
                // Notificar a todos los jugadores activos y cerrar conexiones
                for (auto& jugador : jugadores) {
                    if (jugador.estado == JUGANDO) {
                        std::string msg = "\n🚫 Partida terminada por administrador\n";
                        send(jugador.socket_fd, msg.c_str(), msg.size(), 0);
                        jugador.estado = TERMINADO;
                        jugador.tiempo_fin = std::chrono::steady_clock::now();
                        close(jugador.socket_fd);
                        jugador.socket_fd = -1; // Marcar como cerrado
                    }
                }
                
                // Mostrar resultados en servidor y terminar
                enviar_resultados_finales();
                cleanup_lockfile();
                exit(0);
            } else {
                std::cout << "\n[SEÑAL] " << signal_name << " - Terminando servidor..." << std::endl;
                cleanup_lockfile();
                exit(0);
            }
            break;
        }
    }
}

void setup_signal_handlers() {
    signal(SIGINT, manejador_senales);
    signal(SIGUSR1, manejador_senales);
    signal(SIGUSR2, manejador_senales);
    signal(SIGTERM, manejador_senales);
    signal(SIGPIPE, SIG_IGN); // Ignorar SIGPIPE
}

int main(int argc, char* argv[]) {
    // Configurar señales al inicio
    setup_signal_handlers();

    // Crear y obtener lock del archivo
    lockfile_fd = open(LOCKFILE_PATH, O_CREAT | O_RDWR, 0644);
    if (lockfile_fd == -1) {
        std::cerr << "Error al crear lockfile: " << strerror(errno) << "\n";
        return 1;
    }

    // Intentar obtener lock exclusivo sin bloquear
    if (flock(lockfile_fd, LOCK_EX | LOCK_NB) == -1) {
        if (errno == EWOULDBLOCK) {
            std::cerr << "Ya existe un proceso servidor corriendo en la computadora.\n";
        } else {
            std::cerr << "Error al obtener lock: " << strerror(errno) << "\n";
        }
        close(lockfile_fd);
        return 1;
    }

    // Procesamiento de argumentos
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            mostrar_ayuda();
            cleanup_lockfile();
            return 0;
        } else if ((arg == "-p" || arg == "--puerto") && i + 1 < argc) {
            std::string valor = argv[++i];
            if (!es_entero(valor)) {
                std::cerr << "Error: el puerto debe ser un número entero.\n";
                cleanup_lockfile();
                return 1;
            }
            puerto = std::stoi(valor);
            puerto_set = true;
        } else if ((arg == "-u" || arg == "--usuarios") && i + 1 < argc) {
            std::string valor = argv[++i];
            if (!es_entero(valor)) {
                std::cerr << "Error: la cantidad de usuarios debe ser un número entero.\n";
                cleanup_lockfile();
                return 1;
            }
            max_usuarios = std::stoi(valor);
            usuarios_set = true;
        } else if ((arg == "-a" || arg == "--archivo") && i + 1 < argc) {
            archivo_frases = argv[++i];
            archivo_set = true;
        } else {
            std::cerr << "Parámetro no reconocido o incompleto: " << arg << "\n";
            mostrar_ayuda();
            cleanup_lockfile();
            return 1;
        }
    }

    // Validaciones finales
    if (!puerto_set || !usuarios_set || !archivo_set) {
        std::cerr << "Faltan parámetros requeridos.\n\n";
        mostrar_ayuda();
        cleanup_lockfile();
        return 1;
    }

    if (puerto <= 0 || puerto > 65535) {
        std::cerr << "Error: el puerto debe estar entre 1 y 65535.\n";
        cleanup_lockfile();
        return 1;
    }

    if (max_usuarios <= 0) {
        std::cerr << "Error: la cantidad de usuarios debe ser mayor a cero.\n";
        cleanup_lockfile();
        return 1;
    }

    std::ifstream prueba(archivo_frases);
    if (!prueba.is_open()) {
        std::cerr << "Error: no se pudo abrir el archivo de frases: " << archivo_frases << "\n";
        cleanup_lockfile();
        return 1;
    }
    prueba.close();

    servidor_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (servidor_fd == -1) {
        std::cerr << "Error al crear socket\n";
        cleanup_lockfile();
        return 1;
    }

    int opt = 1;
    setsockopt(servidor_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));


    sockaddr_in direccion{};
    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = INADDR_ANY;
    direccion.sin_port = htons(puerto);

    if (bind(servidor_fd, (sockaddr*)&direccion, sizeof(direccion)) < 0) {
        std::cerr << "Error en bind()\n";
        cleanup_lockfile();
        return 1;
    }

    if (listen(servidor_fd, max_usuarios) < 0) {
        std::cerr << "Error en listen()\n";
        cleanup_lockfile();
        return 1;
    }

    std::cout << "Servidor escuchando en el puerto " << puerto << "...\n";
    std::cout << "🆔 PID del servidor: " << getpid() << "\n";
    std::cout << "📋 Comandos de control:\n";
    std::cout << "   • kill -USR1 " << getpid() << "       → Cierre si no hay partidas\n";
    std::cout << "   • kill -USR2 " << getpid() << "       → Terminar partidas y cerrar\n";
    std::cout << "   • kill -TERM " << getpid() << "       → Terminar servidor (igual que USR2)\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";

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

        // Verificar límite de usuarios
        {
            std::lock_guard<std::mutex> lock(mutex_jugadores);
            if ((int)jugadores.size() >= max_usuarios) {
                const char* rechazo = "Error: Servidor completo. Máximo de usuarios alcanzado.\n";
                send(cliente_fd, rechazo, strlen(rechazo), 0);
                close(cliente_fd);
                std::cout << "Conexión rechazada: servidor completo (" << jugadores.size() << "/" << max_usuarios << " usuarios)\n";
                continue;
            }
        }

        std::thread hilo_cliente(manejar_conexion, cliente_fd);
        hilo_cliente.detach();
    }

    cleanup_lockfile();
    return 0;
}