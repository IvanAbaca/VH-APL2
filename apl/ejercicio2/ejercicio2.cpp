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
#include <chrono>
#include <map>  // para el resumen

using namespace std;
namespace fs = std::filesystem;

// -------- Constantes y variables globales ------------
const int BUFFER_SIZE = 10;
std::mutex mtx_buffer; // mutex para proteger el acceso al buffer
std::condition_variable cv_buffer_not_full; // condición para esperar a que el buffer no esté lleno
std::condition_variable cv_buffer_not_empty; // condición para esperar a que el buffer no esté vacío
std::queue<std::string> buffer; // buffer para almacenar los nombres de archivos generados
std::atomic<int> id_global{1};               // contador global de IDs
std::atomic<int> paquetes_generados{0};  // contador de paquetes generados
std::mutex mtx_resultados; // mutex para proteger el acceso a los resultados
std::map<int, double> peso_por_sucursal; // acumulador de peso para el resumen por sucursal
std::atomic<bool> produccion_finalizada{false}; // bandera para indicar si la producción ha finalizado
std::atomic<int> paquetes_consumidos{0}; // contador de paquetes consumidos


struct Args {
    std::string dir;
    int generadores = 0;
    int consumidores = 0;
    int paquetes = 0;
};

bool es_entero(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}

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


        // crear el archivo con el contenido generado
        std::string nombre_archivo = path_directorio + "/" + std::to_string(id_paquete) + ".paq";
        std::ofstream archivo(nombre_archivo);
        if (archivo.is_open()) {
            archivo << contenido.str() << "\n";
            archivo.close();
            paquetes_generados.fetch_add(1);
        } else {
            std::cerr << "Error al crear archivo: " << nombre_archivo << std::endl;
            continue; // No continuar si no se pudo escribir
        }

        // esperar a que haya espacio en el buffer y bloquear el mutex
        std::unique_lock<std::mutex> lock(mtx_buffer);
        cv_buffer_not_full.wait(lock, [] {
            return buffer.size() < BUFFER_SIZE;
        });

        // poner el archivo en el buffer
        buffer.push(nombre_archivo);

        lock.unlock(); // desbloquear el mutex
        cv_buffer_not_empty.notify_one(); //notificar a los consumidores que hay un archivo nuevo en el buffer
    }
}

// -------- Threads Consumidores ------------
void consumidor(const std::string& path_directorio, std::map<int, std::pair<int, double>>& resumen_sucursal, std::mutex& mtx_resumen)
{
    while (true) {
        std::string archivo_path;

        // Espera y saca del buffer
        {
            std::unique_lock<std::mutex> lock(mtx_buffer);
            cv_buffer_not_empty.wait(lock, [] {
                return !buffer.empty() || produccion_finalizada;
            });

            if (buffer.empty() && produccion_finalizada)
                break;

            if (!buffer.empty()) {
                archivo_path = buffer.front();
                buffer.pop();
                cv_buffer_not_full.notify_one();
            } else {
                continue;
            }
        }

        // leer el archivo y procesar su contenido
        std::ifstream archivo(archivo_path);
        if (!archivo.is_open()) {
            std::cerr << "No se pudo abrir el archivo: " << archivo_path << "\n";
            continue;
        }

        std::string linea;
        std::getline(archivo, linea);
        archivo.close();

        int id, sucursal;
        double peso;
        std::string token;
        std::istringstream iss(linea);

        if (std::getline(iss, token, ';'))
            id = std::stoi(token);
        if (std::getline(iss, token, ';'))
            peso = std::stod(token);
        if (std::getline(iss, token, ';'))
            sucursal = std::stoi(token);

        // para el resumen por sucursal
        {
            std::lock_guard<std::mutex> lock(mtx_resumen);
            resumen_sucursal[sucursal].first += 1;     // incrementa el contador de paquetes para esa sucursal
            resumen_sucursal[sucursal].second += peso; // acumula peso
        }

        // mover archivo a procesados
        try {
            fs::path origen = archivo_path;
            fs::path destino = fs::path(path_directorio) / "procesados" / origen.filename();
            fs::rename(origen, destino);
            paquetes_consumidos.fetch_add(1);
        } catch (const std::exception& e) {
            std::cerr << "Error moviendo archivo " << archivo_path << ": " << e.what() << "\n";
        }
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

    // estructuras para resumen
    std::map<int, std::pair<int, double>> resumen_sucursal;
    std::mutex mtx_resumen;

    //hilos consumidores primero
    std::vector<std::thread> threads_consumidores;
    for (int i = 0; i < consumidores; ++i) {
        threads_consumidores.emplace_back(consumidor, directorio, std::ref(resumen_sucursal), std::ref(mtx_resumen));
    }

    //hilos generadores
    std::vector<std::thread> threads_generadores;
    for (int i = 0; i < generadores; ++i) {
        threads_generadores.emplace_back(generador, paquetes, directorio);
    }

    // cuando terminan los generadores, se unen los hilos
    for (auto& t : threads_generadores) {
        t.join();
    }

    std::cout << "Generación completada: " << paquetes_generados.load() << " paquetes generados.\n";

    // flag para indicarle a los consumidores que la producción ha finalizado 
    produccion_finalizada = true;
    cv_buffer_not_empty.notify_all();

    // esperar a que los consumidores terminen
    for (auto& t : threads_consumidores) {
        t.join();
    }

    // mostrar el resumen
    std::cout << "\nResumen por sucursal:\n";
    for (const auto& [sucursal, datos] : resumen_sucursal) {
        int cantidad = datos.first;
        double peso_total = datos.second;
        std::cout << "Sucursal " << sucursal
                << ": " << cantidad << " paquetes, "
                << std::fixed << std::setprecision(2)
                << peso_total << " kg\n";
    }

    return EXIT_SUCCESS;

}