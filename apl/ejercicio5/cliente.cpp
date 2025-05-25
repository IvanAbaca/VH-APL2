#include <iostream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

void mostrar_ayuda() {
    std::cout << "Uso: ./cliente [opciones]\n"
              << "Opciones:\n"
              << "  -n  --nickname <nombre>     Nickname del jugador (requerido)\n"
              << "  -p  --puerto <número>       Puerto del servidor (requerido)\n"
              << "  -s  --servidor <IP>         IP del servidor (requerido)\n"
              << "  -h  --help                  Muestra esta ayuda\n";
}

int main(int argc, char* argv[]) {
    std::string nickname, ip_servidor;
    int puerto = -1;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            mostrar_ayuda();
            return 0;
        } else if ((arg == "-n" || arg == "--nickname") && i + 1 < argc) {
            nickname = argv[++i];
        } else if ((arg == "-p" || arg == "--puerto") && i + 1 < argc) {
            puerto = std::stoi(argv[++i]);
        } else if ((arg == "-s" || arg == "--servidor") && i + 1 < argc) {
            ip_servidor = argv[++i];
        } else {
            std::cerr << "Parámetro no reconocido: " << arg << "\n";
            mostrar_ayuda();
            return 1;
        }
    }

    if (nickname.empty() || ip_servidor.empty() || puerto <= 0) {
        std::cerr << "Faltan parámetros requeridos.\n\n";
        mostrar_ayuda();
        return 1;
    }

    // 🔽 A partir de acá va tu código actual pero usando las variables nickname, puerto, ip_servidor

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
            respuesta.find("rechazada") != std::string::npos) {
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

    bytes = recv(cliente_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes <= 0) {
        std::cerr << "Se perdió la conexión antes de que empiece la partida.\n";
        close(cliente_fd);
        return 1;
    }
    buffer[bytes] = '\0';
    std::string mensaje(buffer);

    if (mensaje.find("PARTIDA_INICIADA") == std::string::npos) {
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

        if (input.empty()) continue;

        char letra = input[0];
        send(cliente_fd, &letra, 1, 0);

        char buffer[BUFFER_SIZE];
        int bytes = recv(cliente_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            std::cout << "Conexión cerrada por el servidor.\n";
            break;
        }

        buffer[bytes] = '\0';
        std::cout << "\nRespuesta del servidor:\n" << buffer << "\n";

        // Para este mini juego, el servidor enviará un mensaje de cierre
        if (std::string(buffer).find("FIN") != std::string::npos) {
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
