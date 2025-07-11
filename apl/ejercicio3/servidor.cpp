/*
43262563 Abaca, Ivan
43971511 Guardati, Francisco
43780360 Romero, Lucas Nicolas
45542385 Palmieri, Marco
41566741 Piegari, Lucila Quiroga
*/

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <csignal>
#include <algorithm>

#define FIFO_IMPRESION "/tmp/cola_impresion"
#define LOG_PATH "/tmp/impresiones.log"
#define BUFFER_SIZE 1024

using namespace std;
namespace fs = std::filesystem;

// Estados de impresión
enum Estado {
    OK,
    ARCHIVO_NO_ENCONTRADO,
    ARCHIVO_VACIO
};

// Variables globales para limpieza
int fd_impresion = -1;
ofstream logger;

// Función para mostrar ayuda
void mostrar_ayuda(const char* nombre_programa) {
    cout << "Uso: " << nombre_programa << " -i <cantidad_trabajos>\n";
    cout << "Opciones:\n";
    cout << "  -i, --impresiones    Cantidad de archivos a imprimir (requerido)\n";
    cout << "  -h, --help           Muestra esta ayuda\n";
}

bool es_entero(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}

// Función auxiliar para obtener timestamp
string obtener_timestamp() {
    time_t ahora = time(nullptr);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%d/%m/%Y a las %H:%M:%S", localtime(&ahora));
    return string(buffer);
}

// Función para limpiar recursos previos
void limpiar_archivos() {
    unlink(FIFO_IMPRESION);
    ofstream out(LOG_PATH, ios::trunc); // Trunca el archivo log
}

//Manejador de señal
void limpiar_y_salir(int signo) {
    if (fd_impresion != -1) {
        close(fd_impresion);
    }

    if (logger.is_open()) {
        logger << "Servidor finalizado por señal " << signo << " el día " << obtener_timestamp() << "\n";
        logger.close();
    }

    unlink(FIFO_IMPRESION);
    cout << "\nServidor interrumpido (señal " << signo << "). Archivos limpiados.\n";
    exit(EXIT_SUCCESS);
}

// Función para registrar impresión
void registrar_en_log(ofstream& logger, pid_t pid, const string& archivo, Estado estado, const string& contenido = "") {
    string mensaje =  "PID " + to_string(pid) + " procesó el archivo " + archivo + " el día " + obtener_timestamp() + "\n";
    logger << mensaje;
    cout << mensaje;
    if (estado == OK) {
        logger << contenido << "\n";
    } else if (estado == ARCHIVO_NO_ENCONTRADO) {
        logger << "ERROR: Archivo no encontrado.\n";
    } else if (estado == ARCHIVO_VACIO) {
        logger << "ERROR: Archivo vacío.\n";
    }
    logger << "-----------------------------------------\n";
    logger.flush();
}

// Función para enviar respuesta al cliente
void responder_a_cliente(pid_t pid, Estado estado) {
    string path_fifo_cliente = "/tmp/FIFO_" + to_string(pid);
    int fd_cliente = open(path_fifo_cliente.c_str(), O_WRONLY);
    if (fd_cliente == -1) {
        cerr << "No se pudo abrir FIFO del cliente: " << path_fifo_cliente << endl;
        return;
    }
    string msg = to_string(estado);
    write(fd_cliente, msg.c_str(), msg.size());
    close(fd_cliente);
}

int main(int argc, char* argv[]) {
    // Registrar señales
    signal(SIGINT, limpiar_y_salir);
    signal(SIGTERM, limpiar_y_salir);

    // --- Parseo de argumentos ---
    if(argc == 1){
        mostrar_ayuda(argv[0]);
        return EXIT_SUCCESS;
    }
    int cantidad_trabajos = -1;
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if((arg == "-h" || arg == "--help")){
            mostrar_ayuda(argv[0]);
            return EXIT_SUCCESS;
        }
        if ((arg == "-i" || arg == "--impresiones") && i + 1 < argc) {
            string valor = argv[++i];
            if (!es_entero(valor)) {
                cerr << "Error: el numero de paquetes debe ser un número entero.\n";
                return EXIT_FAILURE;
            }
            cantidad_trabajos = stoi(valor);
        } else {
            cerr << "Argumento desconocido: " << arg << "\n";
            mostrar_ayuda(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (cantidad_trabajos <= 0) {
        cerr << "Debe especificar una cantidad positiva de trabajos con -i\n";
        return EXIT_FAILURE;
    }

    // --- Preparación inicial ---
    limpiar_archivos();
    mkfifo(FIFO_IMPRESION, 0666);

    logger.open(LOG_PATH, ios::app);
    if (!logger.is_open()) {
        cerr << "No se pudo abrir el log de impresiones.\n";
        return EXIT_FAILURE;
    }

    cout << "Servidor iniciado. Esperando " << cantidad_trabajos << " trabajos...\n";

    // --- Bucle principal ---
    fd_impresion = open(FIFO_IMPRESION, O_RDONLY);
    if (fd_impresion == -1) {
        cerr << "Error al abrir FIFO de impresión\n";
        return EXIT_FAILURE;
    }

    char buffer[BUFFER_SIZE];
    int trabajos_procesados = 0;

    while (trabajos_procesados < cantidad_trabajos) {
        ssize_t bytes_leidos = read(fd_impresion, buffer, sizeof(buffer) - 1);

        if (bytes_leidos <= 0) continue;

        buffer[bytes_leidos] = '\0';
        string mensaje(buffer);

        // Espera formato "PID:/ruta/archivo"
        size_t sep = mensaje.find(':');
        if (sep == string::npos) {
            logger << "Mensaje inválido recibido: " << mensaje << "\n";
            continue;
        }

        pid_t pid = stoi(mensaje.substr(0, sep));
        string path_archivo = mensaje.substr(sep + 1);
        path_archivo.erase(path_archivo.find_last_not_of(" \n\r\t") + 1); // Trim final
    
        Estado estado;
        string contenido;

        if (!fs::is_regular_file(path_archivo)) {
            estado = ARCHIVO_NO_ENCONTRADO;
        } else if (fs::is_empty(path_archivo)) {
            estado = ARCHIVO_VACIO;
        } else {
            estado = OK;
            ifstream in(path_archivo);
            stringstream ss;
            ss << in.rdbuf();
            contenido = ss.str();
        }

        registrar_en_log(logger, pid, path_archivo, estado, contenido);
        responder_a_cliente(pid, estado);

        trabajos_procesados++;
    }

    // --- Finalización ---
    close(fd_impresion);
    unlink(FIFO_IMPRESION);
    cout << "Servidor finalizado.\n";
    return EXIT_SUCCESS;
}
