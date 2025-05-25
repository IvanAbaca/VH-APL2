#include <iostream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PUERTO 5001
#define BUFFER_SIZE 1024

int main(int argc, char* argv[]) {
    if (argc != 3 || std::string(argv[1]) != "--nickname") {
        std::cerr << "Uso: " << argv[0] << " --nickname <tu_nombre>\n";
        return 1;
    }

    std::string nickname = argv[2];
    int cliente_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (cliente_fd == -1) {
        std::cerr << "Error al crear socket\n";
        return 1;
    }

    sockaddr_in servidor{};
    servidor.sin_family = AF_INET;
    servidor.sin_port = htons(PUERTO);
    servidor.sin_addr.s_addr = inet_addr("127.0.0.1");

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
            break;
        }
    }

    close(cliente_fd);
    return 0;
}
