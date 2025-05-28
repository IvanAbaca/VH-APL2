#include <iostream>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>

using namespace std;

void imprimirMensaje(const string& nombre, const string& tabulation){
    cout << tabulation << "Soy el proceso " << nombre << " con PID=" << getpid() << ", mi padre es " << getppid() << endl;
}

int crearNieto(const int number){
    pid_t pid = fork();
    if (pid == 0){
        //Codigo unico para el proceso nieto
        imprimirMensaje("NIETO" + to_string(number), "        ");
        getchar(); // Espera para que el nieto no termine inmediatamente
    }else if(pid > 0){
        return pid;
    }else{
        cout << "Hubo error al crear el nieto" << endl;
    }
    return 0;
}

int crearZombie(){
    pid_t pid = fork();
    if (pid == 0)
    {
        imprimirMensaje("ZOMBIE", "        ");
        exit(EXIT_SUCCESS);
    }else if(pid > 0){
        return pid;
    }else{
        cout << "Hubo error al crear al zombie" << endl;
    }
    return 0;
}

int crearHijo1(){
    pid_t pid = fork();

    if (pid == 0){
        int status;
        imprimirMensaje("HIJO1", "    ");
        int pidnieto1 = crearNieto(1);
        if(pidnieto1>0){
            //ES HIJO1
            crearZombie(); //NO TENGO QUE HACER WAIT, POR LO TANTO NO ME INTERESA EL RETORNO
            int pidnieto2 = crearNieto(2);

            if(pidnieto2 > 0){
                //SIGUE SIENDO HIJO1
                waitpid(pidnieto2, &status, 0);
            }
            waitpid(pidnieto1, &status, 0);
            getchar();
        }
    }
    else if (pid > 0){
        //VUELVO AL PADRE
        return pid;
    }else{
        cout << "Hubo error" << endl;
    }
    return 0;
}

int main(){
    int status;
    int pid = crearHijo1();
    if(pid > 0){
        //ES PADRE
        imprimirMensaje("PADRE","");
        waitpid(pid, &status, 0);
    }
    return EXIT_SUCCESS;
}