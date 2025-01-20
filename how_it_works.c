#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <pthread.h>
#include <stdbool.h>

#define NUM_COOKS 5
#define TABLE_CAPACITY 5
#define MAX_WEIGHT 20
#define MAX_PORTION_WEIGHT MAX_WEIGHT/NUM_COOKS

//inicjalizacja mutexow
pthread_mutex_t forks[NUM_COOKS];
pthread_mutex_t table_lock;

//ini stołu waga/kolejka komunikatow
int table_weight = 0;
int table_msgid;

//struktura porcji - to jest waga wysylana jest jako komunikat z mtype
struct portion {
    long mtype;
    int weight;
};

//Opis miejsca na stole - pusty = nie ma porcji na miejscu, pelny = porcja lezy na stole 
#define PUSTY 1
#define PELNY 2

//podnoszenie widelcy, zawsze kucharz bierze najpierw widelec o swoim indeksie, nastepnie o indeksie o 1 wiekszym (jesli jest ostatnim kucharzem to pierwszy widelec = idx 0 bo stol jest okragly)
void pickup_forks(int cook_id) {
    int first_fork = cook_id;
    int second_fork = (cook_id + 1) % NUM_COOKS;
    
    if (first_fork > second_fork) {
        int temp = first_fork;
        first_fork = second_fork;
        second_fork = temp;
    }
    
    //blokujemy mutexami 2 podniesione widelce
    pthread_mutex_lock(&forks[first_fork]);
    pthread_mutex_lock(&forks[second_fork]);
    printf("Cook %d picked up forks %d and %d\n", cook_id, first_fork, second_fork);
}


//odkladanie widelcy, odkladalmy tak samo jak podnosimy
void putdown_forks(int cook_id) {
    int first_fork = cook_id;
    int second_fork = (cook_id + 1) % NUM_COOKS;
    
    if (first_fork > second_fork) {
        int temp = first_fork;
        first_fork = second_fork;
        second_fork = temp;
    }
    
    //odblokowujemy mutexy ktore blokuja uzycie widelcy przez inny watek
    pthread_mutex_unlock(&forks[second_fork]);
    pthread_mutex_unlock(&forks[first_fork]);
    printf("Cook %d put down forks %d and %d\n", cook_id, first_fork, second_fork);
}


//przygotowanie porcji - sleep na koncu symuluje przygotowywanie procji, po to zeby wszystko na raz sie nie robilo
void prepare_portion(int cook_id) {
    printf("Cook %d is preparing a portion...\n", cook_id);
    sleep(rand() % 2 + 1);
}


//konsumpcja porcji analogiczna do przygotwywania
void consume_portion(int cook_id, int weight) {
    printf("Cook %d is consuming a portion of weight %d...\n", cook_id, weight);
    sleep(rand() % 2 + 1);
}


//funkcja wykonywana przez kazdy watek kucharzy. (konieczne jest zwracanie i przyjomwanie w argumencie void *)
void *cook(void *arg) {
    int cook_id = *(int *)arg; //id kucharza(arg) ktore jest void zamieniamy na zmienna int
    
    struct portion elem;
    
    while (1) {
        //czy przygotowac procje czy skonsumowac
        bool should_cook = (rand() % 2) == 0;
        
        //przygotowywanie porcji
        if (should_cook) {
            
            pickup_forks(cook_id);
            prepare_portion(cook_id);
            int portion_weight = rand() % MAX_PORTION_WEIGHT + 1; //po podniesieniu widelcy i przygotowaniu porcji losowanie wagi tej porcji

            pthread_mutex_lock(&table_lock); //blokujemy stół mutexem, żeby inny watek nie wtracil czegos
            if (table_weight + portion_weight <= MAX_WEIGHT) { //jesli mozna zmiescic porcje na stole
                elem.mtype = PELNY; // porcja na stole
                elem.weight = portion_weight; //waga porcji
                if (msgsnd(table_msgid, &elem, sizeof(elem.weight), 0) == -1) { //wysylamy do reprezentacji naszego stolu - kolejki komunikatow, komunikat z porcją
                    perror("msgsnd");
                    exit(1);
                }
                table_weight += portion_weight;
                printf("Cook %d placed a portion of weight %d on the table. Current weight: %d\n",cook_id, portion_weight, table_weight);
            } else {
                printf("Cook %d couldn't place a portion due to weight limit.\n", cook_id);
            }
            pthread_mutex_unlock(&table_lock); //odblokowywujemy mutex blokujacy stol
            putdown_forks(cook_id); // odkladamy widelce


        // konsumpcja porcji    
        } else {
    pickup_forks(cook_id);  // Najpierw zajmujemy widelce
    
    pthread_mutex_lock(&table_lock);  // Blokujemy stół
    if (table_weight > 0) { // Sprawdzenie czy jakakolwiek porcja na stole
        if (msgrcv(table_msgid, &elem, sizeof(elem.weight), PELNY, IPC_NOWAIT) != -1 && elem.weight > 0) {
            table_weight -= elem.weight; 
            printf("Cook %d took a portion of weight %d. Current weight: %d\n", cook_id, elem.weight, table_weight); 
            
            pthread_mutex_unlock(&table_lock);
            consume_portion(cook_id, elem.weight);
            putdown_forks(cook_id);
            
        } else {
            pthread_mutex_unlock(&table_lock);
            putdown_forks(cook_id);  // Pamiętamy o odłożeniu widelców w przypadku błędu
            printf("Cook %d couldn't receive a portion.\n", cook_id);
        }
    } else {
        pthread_mutex_unlock(&table_lock);
        putdown_forks(cook_id);  // Pamiętamy o odłożeniu widelców gdy nie ma porcji
        printf("Cook %d found no portions to consume.\n", cook_id);
    }
}
        
        sleep(1);
    }
    return NULL;
}

int main() {
    pthread_t cooks[NUM_COOKS];
    int cook_ids[NUM_COOKS];

    //inicjalizacja mutexow dla widelcy
    pthread_mutex_init(&table_lock, NULL);
    for (int i = 0; i < NUM_COOKS; i++) {
        pthread_mutex_init(&forks[i], NULL);
    }

    //generujemy klucze do komunikatów poprzez ftok. ftok korzysta z identyfikatora i-Node tutaj pliku: "keygen" aby stworzyć klucz.
    key_t key = ftok("keygen", 65);
    //tworzenie kolejki
    table_msgid = msgget(key, IPC_CREAT | IPC_EXCL | 0600); //tworzy jesli nie istnieje inna
    if (table_msgid == -1) {
        table_msgid = msgget(key, 0600); // jesli istnieje inna to ja usuwamy
        if (table_msgid != -1) {
            msgctl(table_msgid, IPC_RMID, NULL);
        }
        table_msgid = msgget(key, IPC_CREAT | 0600); // jeszcze raz tworzymy
        if (table_msgid == -1) {
            perror("msgget");
            exit(1);
        }
    }        // takie cos jest potrzebne bo jesli wylaczamy program ctrl+c albo jakkolwiek inaczej to kolejka nadal zostaje w systemie

    //tworzymy watki
    for (int i = 0; i < NUM_COOKS; i++) {
        cook_ids[i] = i;
        pthread_create(&cooks[i], NULL, cook, &cook_ids[i]);
    }

    //w tym miejscu kazdy watek wykonuje funkcje cook(cook_ids)



    //koniec dzialania watkow
    for (int i = 0; i < NUM_COOKS; i++) {
        pthread_join(cooks[i], NULL);
    }
    //niszczenie mutexow
    pthread_mutex_destroy(&table_lock);
    for (int i = 0; i < NUM_COOKS; i++) {
        pthread_mutex_destroy(&forks[i]);
    }
    //zniszczenie kolejki komunikaotw
    msgctl(table_msgid, IPC_RMID, NULL);

    return 0;
}