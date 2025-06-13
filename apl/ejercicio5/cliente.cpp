/*
43262563 Abaca, Ivan
43971511 Guardati, Francisco
45542385 Palmieri, Marco
41566741 Piegari, Lucila Quiroga
43780360 Romero, Lucas Nicolas
*/

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <algorithm>
#include <netdb.h>
#include <signal.h>  // Agregado para manejo de señales

#define BUFFER_SIZE 1024

// Manejador de señales SIGINT
void manejador_sigint(int sig) {
    // Simplemente ignora la señal - no hace nada
    // Opcionalmente puedes mostrar un mensaje
    std::cout << "\n[SIGINT ignorado - usa comandos del juego para salir]\n";
}

bool es_entero(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}

void mostrar_ayuda() {
    std::cout << "Uso: ./cliente [opciones]\n"
              << "Opciones:\n"
              << "  -n  --nickname <nombre>     Nickname del jugador (requerido)\n"
              << "  -p  --puerto <número>       Puerto del servidor (requerido)\n"
              << "  -s  --servidor <IP>         IP del servidor (requerido)\n"
              << "  -h  --help                  Muestra esta ayuda\n";
}

int main(int argc, char* argv[]) {
    // Configurar el manejador de señales SIGINT al inicio del programa
    signal(SIGINT, manejador_sigint);
    
    std::string nickname, ip_servidor;
    int puerto = -1;
    bool nickname_set = false, puerto_set = false, ip_set = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            mostrar_ayuda();
            return 0;
        } else if ((arg == "-n" || arg == "--nickname") && i + 1 < argc) {
            nickname = argv[++i];
            nickname_set = true;
        } else if ((arg == "-p" || arg == "--puerto") && i + 1 < argc) {
            std::string valor = argv[++i];
            if (!es_entero(valor)) {
                std::cerr << "Error: el puerto debe ser un número entero.\n";
                return 1;
            }
            puerto = std::stoi(valor);
            puerto_set = true;
        } else if ((arg == "-s" || arg == "--servidor") && i + 1 < argc) {
            ip_servidor = argv[++i];
            ip_set = true;
        } else {
            std::cerr << "Parámetro no reconocido o incompleto: " << arg << "\n";
            mostrar_ayuda();
            return 1;
        }
    }

    if (!nickname_set || !puerto_set || !ip_set) {
        std::cerr << "Faltan parámetros requeridos.\n\n";
        mostrar_ayuda();
        return 1;
    }

    if (puerto <= 0 || puerto > 65535) {
        std::cerr << "Error: el puerto debe estar entre 1 y 65535.\n";
        return 1;
    }

    if (nickname.empty() || nickname.length() > 20) {
        std::cerr << "Error: el nickname debe tener entre 1 y 20 caracteres.\n";
        return 1;
    }

    // Validar que solo contenga letras a-z, A-Z
    for (char c : nickname) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
            std::cerr << "Error: el nickname solo puede contener letras (a-z, A-Z).\n";
            return 1;
        }
    }

    int cliente_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (cliente_fd == -1) {
        std::cerr << "Error al crear socket\n";
        return 1;
    }

    sockaddr_in servidor{};
    servidor.sin_family = AF_INET;
    servidor.sin_port = htons(puerto);
    servidor.sin_addr.s_addr = inet_addr(ip_servidor.c_str());

    if (connect(cliente_fd, (sockaddr*)&servidor, sizeof(servidor)) < 0) {
        std::cerr << "Error al conectar con el servidor\n";
        return 1;
    }

    // Enviar nickname inmediatamente
    send(cliente_fd, nickname.c_str(), nickname.size(), 0);

    // Esperar confirmación del servidor
    char buffer[BUFFER_SIZE];
    int bytes = recv(cliente_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        std::string respuesta(buffer);
        std::cout << "Servidor: " << respuesta;

        if (respuesta.find("Error") != std::string::npos ||
            respuesta.find("rechazada") != std::string::npos ||
            respuesta.find("El juego ya ha comenzado") != std::string::npos) {
            std::cout << "Conexión finalizada.\n";
            close(cliente_fd);
            return 0;
        }
    } else {
        std::cerr << "No se recibió respuesta del servidor\n";
        close(cliente_fd);
        return 1;
    }

    // Ingresar listo
    std::string input;
    while (true) {
        std::cout << "Escribí 'listo' para marcarte listo: ";
        std::getline(std::cin, input);
        if (input == "listo" || input == "LISTO") {
            send(cliente_fd, input.c_str(), input.size(), 0);
            break;
        } else {
            std::cout << "Comando no reconocido.\n";
        }
    }

    // Manejar ACK y esperar inicio de partida
    while (true) {
        bytes = recv(cliente_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            std::cerr << "Se perdió la conexión antes de que empiece la partida.\n";
            close(cliente_fd);
            return 1;
        }
        buffer[bytes] = '\0';
        std::string mensaje(buffer);

        // Si recibe ACK, responder
        if (mensaje.find("ACK") != std::string::npos) {
            const char* ack_response = "ACK_OK";
            send(cliente_fd, ack_response, strlen(ack_response), 0);
            std::cout << "Respondiendo verificación del servidor...\n";
            continue; // Seguir esperando
        }

        // Si recibe PARTIDA_INICIADA, salir del bucle
        if (mensaje.find("PARTIDA_INICIADA") != std::string::npos) {
            break;
        }

        // Mensaje inesperado
        std::cerr << "Mensaje inesperado del servidor: " << mensaje << "\n";
        close(cliente_fd);
        return 1;
    }

    std::cout << "\n🎮 ¡La partida ha comenzado!\n";

    // El socket permanece abierto para futuro juego
    while (true) {
        std::string input;
        std::cout << "Ingresá una letra: ";
        std::getline(std::cin, input);

        if (input.length() != 1 || !std::isalpha(input[0]) || 
            !((input[0] >= 'a' && input[0] <= 'z') || (input[0] >= 'A' && input[0] <= 'Z'))) {
            std::cout << "Entrada inválida. Ingresá una sola letra (a-z, A-Z).\n";
            continue;
        }

        char letra = std::tolower(input[0]);
        send(cliente_fd, &letra, 1, 0);

        char buffer[BUFFER_SIZE];
        int bytes = recv(cliente_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            std::cout << "Conexión cerrada por el servidor.\n";
            break;
        }

        buffer[bytes] = '\0';

        // Limpiar consola antes de mostrar respuesta
        #ifdef _WIN32
            system("cls");
        #else
            system("clear");
        #endif

        std::cout << buffer << "\n";

        // Verificar si el juego terminó
        if (std::string(buffer).find("FIN") != std::string::npos ||
            std::string(buffer).find("🚫 Partida terminada por administrador") != std::string::npos) {
            
            if (std::string(buffer).find("🚫 Partida terminada por administrador") != std::string::npos) {
                std::cout << "\n⏳ Presiona Enter para salir...";
                std::cin.get();
                break;
            }
            
            std::cout << "Esperando resultados finales...\n";

            // Esperar mensaje final del servidor
            char final_buffer[BUFFER_SIZE * 4]; // por si es largo
            int final_bytes = recv(cliente_fd, final_buffer, sizeof(final_buffer) - 1, 0);
            if (final_bytes > 0) {
                final_buffer[final_bytes] = '\0';
                std::string final_msg(final_buffer);

                if (final_msg.rfind("RESULTADOS_FINALES\n", 0) == 0) { // empieza con...
                    std::cout << "\n📊 Resultados:\n";
                    std::cout << final_msg.substr(std::string("RESULTADOS_FINALES\n").length()) << "\n";
                } else {
                    std::cerr << "Mensaje de resultados no reconocido:\n" << final_msg << "\n";
                }
            } else {
                std::cerr << "No se pudieron recibir los resultados finales.\n";
            }

            break;
        }
    }

    close(cliente_fd);
    return 0;
}