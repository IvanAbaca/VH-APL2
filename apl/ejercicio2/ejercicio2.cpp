/*
43262563 Abaca, Ivan
43971511 Guardati, Francisco
43780360 Romero, Lucas Nicolas
45542385 Palmieri, Marco
41566741 Piegari, Lucila Quiroga
*/

#include <iostream>
#include <algorithm>
#include <fstream>
#include <random>
#include <iomanip>
#include <string>
#include <filesystem>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <condition_variable>
#include <queue>

using namespace std;
namespace fs = std::filesystem;

const int GENERADORES = 3;
const int BUFFER_SIZE = 10;
std::mutex mtx_buffer;
std::condition_variable cv_buffer_not_full;
std::queue<std::string> buffer;
std::atomic<int> id_global{1};               // contador global de IDs
std::atomic<int> paquetes_generados{0};  // contador de paquetes generados

// -------- Ayuda y parámetros por línea de comandos ------------
void mostrar_ayuda() {
    std::cout << "Uso: ./ejercicio2 [opciones]\n"
              << "Opciones:\n"
              << "  -d  --directorio <path>         Ruta del directorio a analizar (requerido)\n"
              << "  -g  --generadores <número>      Cantidad de threads a ejecutar concurrentemente para generar los archivos del directorio (Requerido)\n"
              << "  -c  --consumidores <número>     Cantidad de threads a ejecutar concurrentemente para procesar los archivos del directorio (Requerido)\n"
              << "  -p  --paquetes <número>         Cantidad de paquetes a generar (Requerido).\n"
              << "  -h  --help                      Muestra esta ayuda\n";
}

struct Args {
    std::string dir;
    int generadores = 0;
    int consumidores = 0;
    int paquetes = 0;
};

bool es_entero(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}

// -------- Inicialización y limpieza de directorio ------------
bool inicializar_directorios(const std::string& path) {
    if (!fs::exists(path) || !fs::is_directory(path)) {
        std::cerr << "Error: el directorio '" << path << "' no existe o no es un directorio válido.\n";
        return false; 
    }

    // SOLO se eliminan los archivos .paq del directorio
    // for (auto& p : fs::directory_iterator(path)) {

    //     if (p.is_regular_file() && p.path().extension() == ".paq") {
    //         fs::remove(p.path());
    //     }
    // }

    // elimina todos los archivos y subdirectorios dentro del directorio base
    for (const auto& entry : fs::directory_iterator(path)) {
        try {
            fs::remove_all(entry.path());
        } catch (const std::exception& e) {
            std::cerr << "Error al eliminar " << entry.path() << ": " << e.what() << "\n";
        }
    }

    // se crea el directorio 'procesados' para no volver a leer los archivos procesados
    fs::path procesadosDir = fs::path(path) / "procesados";
    if (!fs::exists(procesadosDir)) {
        fs::create_directory(procesadosDir);
    }

    return true;
}

// -------- Threads Generadores ------------
void generador(int total_paquetes, string path_directorio) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> peso_dist(0.0, 300.0);
    std::uniform_int_distribution<> destino_dist(1, 50);

    while (true) {
        int id_paquete = id_global.fetch_add(1);
        if (id_paquete > total_paquetes)
            break;

        double peso = peso_dist(gen);
        int destino = destino_dist(gen);

        std::ostringstream contenido;
        contenido << id_paquete << ";" << std::fixed << std::setprecision(2)
                  << peso << ";" << destino;

        std::string nombre_archivo = path_directorio + "/" + std::to_string(id_paquete) + ".paq";

        // esperar a que haya espacio en el buffer y bloquear el mutex
        std::unique_lock<std::mutex> lock(mtx_buffer);
        cv_buffer_not_full.wait(lock, [] {
            return buffer.size() < BUFFER_SIZE;
        });

        // Simular escritura al buffer (push del archivo)
        buffer.push(nombre_archivo);

        // crear el archivo con el contenido generado
        std::ofstream archivo(nombre_archivo);
        if (archivo.is_open()) {
            archivo << contenido.str() << "\n";
            archivo.close();
            paquetes_generados.fetch_add(1);
        } else {
            std::cerr << "Error al crear archivo: " << nombre_archivo << std::endl;
        }

        //libero el mutex del buffer :)
        lock.unlock();
        
        //esto es para notificar a los consumidores que hay un nuevo paquete disponible -- ver si es necesario
        // cv_buffer_not_full.notify_all(); 
        
    }
}


//MAIN
int main(int argc, char* argv[]) {

    /// validación de argumentos
    std::string directorio;
    int paquetes = -1, generadores = -1, consumidores = -1;
    bool paquetes_set = false, directorio_set = false, generadores_set = false, consumidores_set = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            mostrar_ayuda();
            return EXIT_SUCCESS;
        } else if ((arg == "-d" || arg == "--directorio") && i + 1 < argc) {
            directorio = argv[++i];
            directorio_set = true;
        } else if ((arg == "-p" || arg == "--paquetes") && i + 1 < argc) {
            std::string valor = argv[++i];
            if (!es_entero(valor)) {
                std::cerr << "Error: el numero de paquetes debe ser un número entero.\n";
                return EXIT_FAILURE;
            }
            paquetes = std::stoi(valor);
            paquetes_set = true;
            std::cout << "Paquetes a generar: " << paquetes << std::endl;
        } else if ((arg == "-g" || arg == "--generadores") && i + 1 < argc) {  
            std::string valor = argv[++i];
            if (!es_entero(valor)) {
                std::cerr << "Error: el número de generadores debe ser un número entero.\n";
                return EXIT_FAILURE;
            }
            generadores = std::stoi(valor);
            generadores_set = true;
        } else if ((arg == "-c" || arg == "--consumidores") && i + 1 < argc) {
            std::string valor = argv[++i];
            if (!es_entero(valor)) {
                std::cerr << "Error: el número de consumidores debe ser un número entero.\n";
                return EXIT_FAILURE;
            }
            consumidores = std::stoi(valor);
            consumidores_set = true;
        } else {
            std::cerr << "Parámetro no reconocido o incompleto: " << arg << "\n";
            mostrar_ayuda();
            return EXIT_FAILURE;
        }
    }

    //inicialización de directorio (acá también hace la validación de que el directorio exista)
    if (!inicializar_directorios(directorio)) {
    return EXIT_FAILURE;
    }

    /// threads generadores
    std::vector<std::thread> threads_generadores;
    for (int i = 0; i < generadores; ++i) {
        threads_generadores.emplace_back(generador, paquetes, directorio);
    }

    for (auto& t : threads_generadores) {
        t.join();
    }

    std::cout << "Generación completada: " << paquetes_generados.load() << " paquetes generados.\n";
    return 0;

    // Threads Consumidores

    // Buffer virtual compartido

    // Procesamiento y resumen


}