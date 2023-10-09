#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

pthread_mutex_t guess_mutex;
pthread_mutexattr_t guess_mutex_attr;
pthread_cond_t guess_cond;
pthread_condattr_t guess_cond_attr;

int correct_guess = 0;

void *guessing_game(void *arg) {
    int target = *(int *)arg;
    int guess;

    pthread_mutex_lock(&guess_mutex);
    while (!correct_guess) {
        printf("Enter your guess: ");
        scanf("%d", &guess);

        if (guess > target) {
            printf("Too high!\n");
        } else if (guess < target) {
            printf("Too low!\n");
        } else {
            printf("Congratulations! You've guessed the number.\n");
            correct_guess = 1;
            pthread_cond_signal(&guess_cond);
        }
    }
    pthread_mutex_unlock(&guess_mutex);
    pthread_exit(NULL);
}

int main() {
    pthread_t game_thread;

    srand(time(NULL));
    int random_number = rand() % 100 + 1;  // A random number between 1 and 100

    pthread_mutexattr_init(&guess_mutex_attr);
    pthread_mutex_init(&guess_mutex, &guess_mutex_attr);

    pthread_condattr_init(&guess_cond_attr);
    pthread_cond_init(&guess_cond, &guess_cond_attr);

    // Create a new thread to run the guessing game
    if (pthread_create(&game_thread, NULL, guessing_game, &random_number)) {
        printf("Error creating thread.\n");
        return 1;
    }

    pthread_mutex_lock(&guess_mutex);
    while (!correct_guess) {
        pthread_cond_wait(&guess_cond, &guess_mutex);
    }
    pthread_mutex_unlock(&guess_mutex);

    // Cleanup
    pthread_mutex_destroy(&guess_mutex);
    pthread_mutexattr_destroy(&guess_mutex_attr);
    pthread_cond_destroy(&guess_cond);
    pthread_condattr_destroy(&guess_cond_attr);

    // Wait for the game to finish
    if (pthread_join(game_thread, NULL)) {
        printf("Error joining thread.\n");
        return 2;
    }

    printf("Bye!\n");

    return 0;
}
