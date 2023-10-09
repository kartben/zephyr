#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

// Shared counter and spin lock
int counter = 0;
pthread_spinlock_t spinlock;

// Thread function to increment counter
void* increment_counter(void* arg) {
    char thread_name[16];
    strncpy(thread_name, (char*) arg, sizeof(thread_name) - 1);
    thread_name[sizeof(thread_name) - 1] = '\0';  // Null-terminate
    pthread_setname_np(pthread_self(), thread_name);

    int rounds = 100000;  // Hard-coded for demonstration

    for (int i = 0; i < rounds; i++) {
        pthread_spin_lock(&spinlock);  // Lock the counter
        counter++;

        // Print a message every 1000 increments
        if (counter % 1000 == 0) {
            printf("%s: Counter value %d\n", thread_name, counter);
        }

        pthread_spin_unlock(&spinlock);  // Unlock the counter
    }

    return NULL;
}

int main() {
    pthread_t t1, t2;

    // Initialize the spin lock
    pthread_spin_init(&spinlock, 0);

    // Create threads
    pthread_create(&t1, NULL, increment_counter, (void*)"Thread 1");
    pthread_create(&t2, NULL, increment_counter, (void*)"Thread 2");

    // Wait for both threads to finish
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    // Destroy the spin lock
    pthread_spin_destroy(&spinlock);

    printf("Final counter value: %d\n", counter);

    return 0;
}
