/*
 * prime_counter.c
 *
 * Multi-threaded Prime Counter.
 *
 * Run:
 *     ./prime_counter <number_of_threads>
 *
 * Example:
 *     ./prime_counter 4
 *
 * The input file must be named:
 *     numbers.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <string.h>

typedef struct {
    long long *numbers;     /* shared input array */
    long start_index;       /* first array index this thread handles */
    long end_index;         /* one past the last index */
    int thread_id;
} ThreadData;


/*
 * Return 1 if n is prime, otherwise return 0.
 * Trial division up to sqrt(n), skipping multiples of 2 and 3.
 */
int is_prime(long long n)
{
    if (n < 2) {
        return 0;
    }
    if (n < 4) {
        return 1;               /* 2 and 3 */
    }
    if (n % 2 == 0 || n % 3 == 0) {
        return 0;
    }

    /* Every prime > 3 has the form 6k - 1 or 6k + 1. */
    for (long long i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0) {
            return 0;
        }
    }
    return 1;
}


/*
 * Worker function used by pthread_create().
 */
void *count_primes(void *arg)
{
    ThreadData *data = (ThreadData *)arg;

    if (data == NULL) {
        fprintf(stderr, "Worker received NULL argument\n");
        return NULL;
    }

    long long local_count = 0;

    /* Loop over this thread's assigned range. */
    for (long i = data->start_index; i < data->end_index; i++) {
        if (is_prime(data->numbers[i])) {
            local_count++;
        }
    }

    /* Exclusive memory for this thread's partial result. */
    long long *result = malloc(sizeof(long long));
    if (result == NULL) {
        fprintf(stderr, "Thread %d: could not allocate result memory\n",
                data->thread_id);
        return NULL;
    }

    *result = local_count;
    pthread_exit(result);
}


/*
 * Compute elapsed time in seconds.
 */
double elapsed_seconds(struct timespec start, struct timespec end)
{
    double seconds = (double)(end.tv_sec - start.tv_sec);
    double nanoseconds = (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;

    return seconds + nanoseconds;
}


int main(int argc, char *argv[])
{
    FILE *file = NULL;
    long long *numbers = NULL;
    pthread_t *threads = NULL;
    ThreadData *thread_data = NULL;

    long N;
    int T;

    struct timespec start_time;
    struct timespec end_time;

    /*
     * ------------------------------------------------------------
     * 1. Read and validate the thread count
     * ------------------------------------------------------------
     */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <number_of_threads>\n", argv[0]);
        return EXIT_FAILURE;
    }

    errno = 0;
    char *endptr = NULL;
    long requested_threads = strtol(argv[1], &endptr, 10);

    if (errno != 0 ||
        endptr == argv[1] ||
        *endptr != '\0' ||
        requested_threads < 1 ||
        requested_threads > 1000000) {

        fprintf(stderr, "Invalid number of threads: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    T = (int)requested_threads;


    /*
     * ------------------------------------------------------------
     * 2. Open numbers.txt
     * ------------------------------------------------------------
     */
    file = fopen("numbers.txt", "r");

    if (file == NULL) {
        fprintf(stderr, "Could not open numbers.txt: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }


    /*
     * ------------------------------------------------------------
     * 3. Read N from the first line
     * ------------------------------------------------------------
     */
    if (fscanf(file, "%ld", &N) != 1) {
        fprintf(stderr, "Error: could not read N from numbers.txt\n");
        fclose(file);
        return EXIT_FAILURE;
    }

    if (N < 0) {
        fprintf(stderr, "Error: N cannot be negative\n");
        fclose(file);
        return EXIT_FAILURE;
    }

    /*
     * ------------------------------------------------------------
     * 4. Allocate the input array
     * ------------------------------------------------------------
     */
    if (N > 0) {
        numbers = malloc((size_t)N * sizeof(long long));

        if (numbers == NULL) {
            fprintf(stderr, "Error: could not allocate input array\n");
            fclose(file);
            return EXIT_FAILURE;
        }
    }


    /*
     * ------------------------------------------------------------
     * 5. Read all N values into RAM
     * ------------------------------------------------------------
     */
    for (long i = 0; i < N; i++) {
        int rc = fscanf(file, "%lld", &numbers[i]);

        if (rc != 1) {
            if (rc == EOF && ferror(file)) {
                fprintf(stderr, "Error reading numbers.txt at value %ld: %s\n",
                        i + 1, strerror(errno));
            } else if (rc == EOF) {
                fprintf(stderr, "Error: numbers.txt ended early "
                        "(expected %ld values, found %ld)\n", N, i);
            } else {
                fprintf(stderr, "Error: invalid value on line %ld of numbers.txt\n",
                        i + 2);
            }
            fclose(file);
            free(numbers);
            return EXIT_FAILURE;
        }

        if (numbers[i] < 1) {
            fprintf(stderr, "Error: value on line %ld is not a positive integer\n",
                    i + 2);
            fclose(file);
            free(numbers);
            return EXIT_FAILURE;
        }
    }

    /*
     * fclose() can fail too, so check it.
     */
    if (fclose(file) != 0) {
        fprintf(stderr, "Warning: error closing numbers.txt: %s\n",
                strerror(errno));
        free(numbers);
        return EXIT_FAILURE;
    }

    file = NULL;


    /*
     * ------------------------------------------------------------
     * 6. Allocate pthread and argument arrays
     * ------------------------------------------------------------
     */
    threads = malloc((size_t)T * sizeof(pthread_t));
    if (threads == NULL) {
        fprintf(stderr, "Error: could not allocate thread array\n");
        free(numbers);
        return EXIT_FAILURE;
    }

    thread_data = malloc((size_t)T * sizeof(ThreadData));
    if (thread_data == NULL) {
        fprintf(stderr, "Error: could not allocate thread data array\n");
        free(threads);
        free(numbers);
        return EXIT_FAILURE;
    }

    /*
     * ------------------------------------------------------------
     * 7. Calculate the static work allocation
     * ------------------------------------------------------------
     *   C = N / T
     *   Thread i (i < T-1): indexes i*C .. (i+1)*C - 1
     *   Thread T-1:         indexes (T-1)*C .. N - 1  (gets the remainder)
     *
     * Array index k corresponds to line k+2 of numbers.txt.
     */
    long chunk_size = N / T;

    for (int i = 0; i < T; i++) {
        thread_data[i].numbers = numbers;
        thread_data[i].thread_id = i;
        thread_data[i].start_index = (long)i * chunk_size;

        if (i < T - 1) {
            thread_data[i].end_index = (long)(i + 1) * chunk_size;
        } else {
            thread_data[i].end_index = N;
        }
    }


    /*
     * ------------------------------------------------------------
     * 8. Start timing
     * ------------------------------------------------------------
     */
    if (clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
        fprintf(stderr, "clock_gettime failed: %s\n", strerror(errno));
        free(threads);
        free(thread_data);
        free(numbers);
        return EXIT_FAILURE;
    }


    /*
     * ------------------------------------------------------------
     * 9. Create the child threads
     * ------------------------------------------------------------
     */
    int created_threads = 0;
    int had_error = 0;

    for (int i = 0; i < T; i++) {
        int rc = pthread_create(&threads[i], NULL, count_primes, &thread_data[i]);

        if (rc != 0) {
            /* pthread functions return the error code instead of setting errno */
            fprintf(stderr, "Error: pthread_create failed for thread %d: %s\n",
                    i, strerror(rc));
            had_error = 1;
            break;
        }
        created_threads++;
    }


    /*
     * ------------------------------------------------------------
     * 10. Join all threads and combine their results
     * ------------------------------------------------------------
     * Join every thread that was actually created, even after an error,
     * so none are left running and no result memory leaks.
     */
    long long total_primes = 0;

    for (int i = 0; i < created_threads; i++) {
        void *result = NULL;

        int rc = pthread_join(threads[i], &result);
        if (rc != 0) {
            fprintf(stderr, "Error: pthread_join failed for thread %d: %s\n",
                    i, strerror(rc));
            had_error = 1;
            continue;
        }

        if (result == NULL) {
            fprintf(stderr, "Error: thread %d returned no result\n", i);
            had_error = 1;
            continue;
        }

        long long *count = (long long *)result;
        total_primes += *count;
        free(count);
    }

    if (had_error) {
        free(threads);
        free(thread_data);
        free(numbers);
        return EXIT_FAILURE;
    }


    /*
     * ------------------------------------------------------------
     * 11. Stop timing
     * ------------------------------------------------------------
     */
    if (clock_gettime(CLOCK_MONOTONIC, &end_time) != 0) {
        fprintf(stderr, "clock_gettime failed: %s\n", strerror(errno));
        free(threads);
        free(thread_data);
        free(numbers);
        return EXIT_FAILURE;
    }


    /*
     * ------------------------------------------------------------
     * 12. Display the result
     * ------------------------------------------------------------
     */
    double elapsed = elapsed_seconds(start_time, end_time);

    printf("Total prime found: %lld\n", total_primes);
    printf("Time taken: %.3f seconds\n", elapsed);


    /*
     * ------------------------------------------------------------
     * 13. Clean up
     * ------------------------------------------------------------
     */
    free(threads);
    free(thread_data);
    free(numbers);

    return EXIT_SUCCESS;
}
