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


using namespace std;
namespace fs = std::filesystem;


std::atomic<int> id_global{1};               // contador global de IDs
int total_paquetes = 0;                      // total de paquetes a generar
std::string directorio_base;                 // directorio donde se generan los archivos
std::atomic<int> paquetes_generados{0};      // para estadística, opcional

// Ayuda y parámetros por línea de comandos
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

//MAIN
int main(int argc, char* argv[]) {

    std::string directorio;
    int paquetes = -1, generadores = -1, consumidores = -1;
    bool paquetes_set = false, directorio_set = false, generadores_set = false, consumidores_set = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            mostrar_ayuda();
            return 0;
        } else if ((arg == "-d" || arg == "--directorio") && i + 1 < argc) {
            directorio = argv[++i];
            directorio_set = true;
        } else if ((arg == "-p" || arg == "--paquetes") && i + 1 < argc) {
            std::string valor = argv[++i];
            if (!es_entero(valor)) {
                std::cerr << "Error: el numero de paquetes debe ser un número entero.\n";
                return 1;
            }
            paquetes = std::stoi(valor);
            paquetes_set = true;
        } else if ((arg == "-g" || arg == "--generadores") && i + 1 < argc) {  
            std::string valor = argv[++i];
            if (!es_entero(valor)) {
                std::cerr << "Error: el número de generadores debe ser un número entero.\n";
                return 1;
            }
            generadores = std::stoi(valor);
            generadores_set = true;
        } else if ((arg == "-c" || arg == "--consumidores") && i + 1 < argc) {
            std::string valor = argv[++i];
            if (!es_entero(valor)) {
                std::cerr << "Error: el número de consumidores debe ser un número entero.\n";
                return 1;
            }
            consumidores = std::stoi(valor);
            consumidores_set = true;
        } else {
            std::cerr << "Parámetro no reconocido o incompleto: " << arg << "\n";
            mostrar_ayuda();
            return 1;
        }
    }
}

// Inicialización y limpieza de directorio
void inicializarDirectorios(const std::string& path) {
    if (!fs::exists(path)) {
        std::cout << "El directorio no existe. Creando: " << path << "\n";
        fs::create_directories(path);
        return;
    }

    // se eliminan los archivos .paq del directorio
    for (auto& p : fs::directory_iterator(path)) {

        //DUDA: tiene que eliminar todos los archivos .paq o TODOS los que están en el directorio?

        //if (p.is_regular_file() && p.path().extension() == ".paq") {
            fs::remove(p.path());
        //}
    }

    // se crea el directorio 'procesados' para no volver a leer los archivos procesados
    fs::path procesadosDir = fs::path(path) / "procesados";
    if (!fs::exists(procesadosDir)) {
        fs::create_directory(procesadosDir);
    }
}

//Generar archivos de paquetes
void generarArchivos() {
    // para generar los valores aleatorios para los archivos
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> peso_dist(0.0, 300.0);
    std::uniform_int_distribution<> destino_dist(1, 50);

    while (true) {
        int id_paquete = id_global++;
        if (id_paquete > total_paquetes) break;

        double peso = peso_dist(gen);
        int destino = destino_dist(gen);


        /// CORREGIR: directorio_base debe ser una variable global o pasada como parámetro
        std::string nombre_archivo = directorio_base + "/" + std::to_string(id_paquete) + ".paq";

        {
            std::ofstream file(nombre_archivo);
            file << id_paquete << ";" << std::fixed << std::setprecision(2) << peso << ";" << destino << "\n";
            file.close();
        }

        // sem_wait(&sem_espacios);
        // {
        //     std::lock_guard<std::mutex> lock(mutex_buffer);
        //     buffer.push(nombre_archivo);
        // }
        // sem_post(&sem_items);
        ++paquetes_generados;
    }
}




// Thread Generadores

// Thread Consumidores

// Buffer virtual compartido

// Sincronización (mutex + condición)

// Procesamiento y resumen