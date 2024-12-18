#include <iostream>
#include <sys/ipc.h>
#include <sys/msg.h>

#define NUM_COOKS 5
#define TABLE_CAPACITY 5
#define MAX_WEIGHT 20

#define PUSTY 1
#define PELNY 2

/*
ftok: Generuje unikalny klucz dla kolejki komunikatów.
msgget: Tworzy lub pobiera kolejkę komunikatów.
msgsnd: Wysyła komunikat do kolejki.
msgrcv: Odbiera komunikat z kolejki.
msgctl: Zarządza kolejką komunikatów (np. usuwa ją).
*/


int main(){


    return 0;
}