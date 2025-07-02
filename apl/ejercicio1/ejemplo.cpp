#include <iostream>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>

using namespace std;

int main()
{
    cout << "Ejemplo de fork()" << endl;

    int a = 0;
    a++;

    cout << "antes del fork " << a << endl;

    pid_t pid = fork();

    cout << "después del fork " << a << endl; //se ejecuta dos veces: a tiene el valor 1 en ambos

    if (pid == 0)
    {
        //Codigo unico para el proceso hijo
        cout << "Soy el proceso hijo " << getpid() << " y mi padre es " << getppid() << endl;
        a = 100;
        cout << "Hijo, a = " << a << endl;
    }
    else if (pid > 0)
    {
        cout << "Espero al proceso hijo ..." << endl;
        cout << ".........................." << endl;
        int status;
        wait(&status); // o waitpid(pid, &status, 0);
        cout << "El hijo ha terminado." << endl;
        cout << ".........................." << endl;
        //getchar(); //SI PONGO ESTO ACA PERO COMENTO EL WAIT DE ARRIBA, FABRICO UN ZOMBIE, YA QUE EL PROCESO HIJO TERMINA, PERO EL PADRE NO ESPERA SU TERMINACION
        // getchar(); //SI PONGO ESTO ACA, CON EL WAIT, EL PROCESO PADRE ESPERA A QUE EL HIJO TERMINE, POR LO TANTO, NO HAY ZOMBIE
        //Codigo unico para el proceso padre
        cout << "Soy el proceso padre " << getpid() << " y mi hijo es " << pid << endl;
        a = 50;
        cout << "Padre, a = " << a << endl;
    }
    else
    {
        cout << "Hubo error" << endl;
    }
    //SE EJECUTA TANTO EN EL PADRE COMO EN EL HIJO
    //Ambos procesos tienen su propia copia de la variable a
    cout << "Valor de a en el proceso " << getpid() << ": " << a << endl;
    getchar();
    return EXIT_SUCCESS;
}