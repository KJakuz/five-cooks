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

pthread_mutex_t forks[NUM_COOKS];
pthread_mutex_t table_lock;
int table_weight = 0;
int table_msgid;

struct portion {
    long mtype;
    int weight;
};

#define PUSTY 1
#define PELNY 2

void pickup_forks(int cook_id) {
    int first_fork = cook_id;
    int second_fork = (cook_id + 1) % NUM_COOKS;
    
    if (first_fork > second_fork) {
        int temp = first_fork;
        first_fork = second_fork;
        second_fork = temp;
    }
    
    pthread_mutex_lock(&forks[first_fork]);
    pthread_mutex_lock(&forks[second_fork]);
    printf("Cook %d picked up forks %d and %d\n", cook_id, first_fork, second_fork);
}

void putdown_forks(int cook_id) {
    int first_fork = cook_id;
    int second_fork = (cook_id + 1) % NUM_COOKS;
    
    if (first_fork > second_fork) {
        int temp = first_fork;
        first_fork = second_fork;
        second_fork = temp;
    }
    
    pthread_mutex_unlock(&forks[second_fork]);
    pthread_mutex_unlock(&forks[first_fork]);
    printf("Cook %d put down forks %d and %d\n", cook_id, first_fork, second_fork);
}

void prepare_portion(int cook_id) {
    printf("Cook %d is preparing a portion...\n", cook_id);
    sleep(rand() % 2 + 1);
}

void consume_portion(int cook_id, int weight) {
    printf("Cook %d is consuming a portion of weight %d...\n", cook_id, weight);
    sleep(rand() % 2 + 1);
}

void *cook(void *arg) {
    int cook_id = *(int *)arg;
    struct portion elem;
    int portions_prepared = 0;
    int portions_consumed = 0;
    
    while (1) {
        bool should_cook = (rand() % 2) == 0;
        
        if (should_cook) {
            pickup_forks(cook_id);
            prepare_portion(cook_id);
            int portion_weight = rand() % MAX_PORTION_WEIGHT + 1;
            
            pthread_mutex_lock(&table_lock);
            if (table_weight + portion_weight <= MAX_WEIGHT) {
                elem.mtype = PELNY;
                elem.weight = portion_weight;
                if (msgsnd(table_msgid, &elem, sizeof(elem.weight), 0) == -1) {
                    perror("msgsnd");
                    exit(1);
                }
                table_weight += portion_weight;
                portions_prepared++;
                printf("Cook %d placed a portion of weight %d on the table. Current weight: %d\n", 
                       cook_id, portion_weight, table_weight);
            } else {
                printf("Cook %d couldn't place a portion due to weight limit.\n", cook_id);
            }
            pthread_mutex_unlock(&table_lock);
            putdown_forks(cook_id);
        } else {
            pthread_mutex_lock(&table_lock);
            if (table_weight > 0) {
                if (msgrcv(table_msgid, &elem, sizeof(elem.weight), PELNY, IPC_NOWAIT) != -1 && elem.weight > 0) {
                    table_weight -= elem.weight;
                    printf("Cook %d took a portion of weight %d. Current weight: %d\n", 
                           cook_id, elem.weight, table_weight);
                    pthread_mutex_unlock(&table_lock);
                    
                    pickup_forks(cook_id);
                    consume_portion(cook_id, elem.weight);
                    putdown_forks(cook_id);
                    
                    portions_consumed++;
                } else {
                    pthread_mutex_unlock(&table_lock);
                    printf("Cook %d couldn't receive a portion.\n", cook_id);
                }
            } else {
                pthread_mutex_unlock(&table_lock);
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

    pthread_mutex_init(&table_lock, NULL);
    for (int i = 0; i < NUM_COOKS; i++) {
        pthread_mutex_init(&forks[i], NULL);
    }

    key_t key = ftok("table", 65);
    table_msgid = msgget(key, IPC_CREAT | IPC_EXCL | 0600);
    if (table_msgid == -1) {
        table_msgid = msgget(key, 0600);
        if (table_msgid != -1) {
            msgctl(table_msgid, IPC_RMID, NULL);
        }
        table_msgid = msgget(key, IPC_CREAT | 0600);
        if (table_msgid == -1) {
            perror("msgget");
            exit(1);
        }
    }

    for (int i = 0; i < NUM_COOKS; i++) {
        cook_ids[i] = i;
        pthread_create(&cooks[i], NULL, cook, &cook_ids[i]);
    }

    for (int i = 0; i < NUM_COOKS; i++) {
        pthread_join(cooks[i], NULL);
    }

    pthread_mutex_destroy(&table_lock);
    for (int i = 0; i < NUM_COOKS; i++) {
        pthread_mutex_destroy(&forks[i]);
    }
    msgctl(table_msgid, IPC_RMID, NULL);

    return 0;
}