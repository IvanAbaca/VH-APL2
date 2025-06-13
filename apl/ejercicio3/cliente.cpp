/*
43262563 Abaca, Ivan
43971511 Guardati, Francisco
43780360 Romero, Lucas Nicolas
45542385 Palmieri, Marco
41566741 Piegari, Lucila Quiroga
*/

#include <iostream>
#include <fstream>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <filesystem>
#include <csignal>

#define FIFO_IMPRESION "/tmp/cola_impresion"
#define BUFFER_SIZE 1024

using namespace std;
namespace fs = std::filesystem;

// Estados de impresión
enum Estado {
    OK,
    ARCHIVO_NO_ENCONTRADO,
    ARCHIVO_VACIO
};

void mostrar_ayuda(const char* nombre_programa) {
    cout << "Uso: " << nombre_programa << " -a <archivo_a_imprimir>\n";
    cout << "Opciones:\n";
    cout << "  -a, --archivo     Ruta del archivo a imprimir (requerido)\n";
    cout << "  -h, --help        Muestra esta ayuda\n";
}

int main(int argc, char* argv[]) {
    // --- Parseo de argumentos ---
    if (argc == 1) {
        mostrar_ayuda(argv[0]);
        return EXIT_SUCCESS;
    }

    string path_archivo;
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            mostrar_ayuda(argv[0]);
            return EXIT_SUCCESS;
        }
        if ((arg == "-a" || arg == "--archivo") && i + 1 < argc) {
            path_archivo = argv[++i];
        } else {
            cerr << "Argumento desconocido o incompleto: " << arg << "\n";
            mostrar_ayuda(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (path_archivo.empty()) {
        cerr << "Debe especificar un archivo con -a\n";
        return EXIT_FAILURE;
    }

    // --- Validación de archivo ---
    // VALIDACIONES QUE DEBEN HACERSE EN EL SERVIDOR
    // if (!fs::exists(path_archivo)) {
    //     cerr << "Error: El archivo no existe.\n";
    //     return EXIT_FAILURE;
    // }

    // if (fs::is_empty(path_archivo)) {
    //     cerr << "Error: El archivo está vacío.\n";
    //     return EXIT_FAILURE;
    // }

    // --- Preparación FIFO cliente ---
    pid_t pid = getpid();
    string fifo_cliente = "/tmp/FIFO_" + to_string(pid);

    if (mkfifo(fifo_cliente.c_str(), 0666) == -1) {
        perror("Error al crear FIFO cliente");
        return EXIT_FAILURE;
    }

    // --- Envío de mensaje al servidor ---
    int fd_impresion = open(FIFO_IMPRESION, O_WRONLY);
    if (fd_impresion == -1) {
        cerr << "No se pudo abrir FIFO de impresión. ¿Está corriendo el servidor?\n";
        unlink(fifo_cliente.c_str());
        return EXIT_FAILURE;
    }

    string mensaje = to_string(pid) + ":" + path_archivo + "\n";
    write(fd_impresion, mensaje.c_str(), mensaje.size());
    close(fd_impresion);

    // --- Esperar respuesta del servidor ---
    int fd_respuesta = open(fifo_cliente.c_str(), O_RDONLY);
    if (fd_respuesta == -1) {
        cerr << "No se pudo abrir FIFO de respuesta del servidor.\n";
        unlink(fifo_cliente.c_str());
        return EXIT_FAILURE;
    }

    char buffer[BUFFER_SIZE];
    ssize_t leido = read(fd_respuesta, buffer, sizeof(buffer) - 1);
    buffer[leido > 0 ? leido : 0] = '\0';
    close(fd_respuesta);
    unlink(fifo_cliente.c_str());

    // --- Mostrar resultado ---
    int respuesta = atoi(buffer);
    if (respuesta == OK) {
        cout << "Impresión completada correctamente.\n";
    } else if (respuesta == ARCHIVO_NO_ENCONTRADO) {
        cout << "Error: El servidor no encontró el archivo.\n";
    } else if (respuesta == ARCHIVO_VACIO) {
        cout << "Error: El servidor rechazó el archivo por estar vacío.\n";
    } else {
        cout << "Respuesta desconocida del servidor: " << respuesta << "\n";
    }

    return EXIT_SUCCESS;
}
