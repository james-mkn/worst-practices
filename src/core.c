#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define ARRAY_MB 12
#define CACHE_BUSTER_SIZE (ARRAY_MB * 1024 * 1024 / sizeof(int))

volatile unsigned int* cross_thread_buffer;
int threads = 0;
int cross_thread_index = 0;
short *cross_thread_mutex;

unsigned int qrand(unsigned int *seed) {
  unsigned int x = *seed;
  x ^= x << 12;
  x ^= x >> 17;
  x ^= x << 5;
  *seed = x;
  return x;
}

struct linked_list {
  unsigned int num;
  struct linked_list* next;
};

void *cachethrasher_process(void *arg) {

  // Ensure one process per thread;
  int cpu_core = (int)(intptr_t)arg;
  unsigned int random_seed = 112312314 + cpu_core;
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu_core, &cpuset);
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  

  volatile unsigned int* buffer = aligned_alloc(64, CACHE_BUSTER_SIZE * sizeof(int));
  volatile unsigned int* buffer2 = aligned_alloc(64, CACHE_BUSTER_SIZE * sizeof(int));
  memset((void*)buffer, 20, CACHE_BUSTER_SIZE * sizeof(int));
  memset((void*)buffer2, 20, CACHE_BUSTER_SIZE * sizeof(int));
  
  struct timespec sleep_time = {0, 5};

  struct linked_list* center = malloc(sizeof(struct linked_list));
  struct linked_list* current_linked_list = malloc(sizeof(struct linked_list));
  center->next = current_linked_list;
  current_linked_list->next = center;

  // every 5 times create a new linked list to fragment memory
  volatile unsigned int times_changed = 0;
  unsigned int spike_counter = 0;
  while (1) {
    int stride = ((qrand(&random_seed) % 8) + 4) * 16;
    for (int i = 0; i < CACHE_BUSTER_SIZE; i += stride) {

      // Randomize order to reduce predictability
      int val = qrand(&random_seed) & 1;
      if (val) {
        // step through, evicting cache lines
        buffer[i] += 1;
        // doubled cachethrash
        buffer2[i] += 1;
      } else {
        // doubled cachethrash
        buffer2[i] += 1;
        // step through, evicting cache lines
        buffer[i] += 1;
      }

      // Cross Thread Buffer
      if (qrand(&random_seed) & 1) {
        cross_thread_buffer[cross_thread_index]++;
	cross_thread_mutex[cpu_core] = 1;
      } else if (qrand(&random_seed) % 20 == 0) {
	// Write somewhere completely random every so often to break cpu
	// prediction
	cross_thread_buffer[qrand(&random_seed) % (CACHE_BUSTER_SIZE - 1)]++;
      } else {
	cross_thread_buffer[cross_thread_index + cpu_core]++;
	cross_thread_mutex[cpu_core] = 1;
      }

      int proceed = 1;
      for (int i = 0; i < threads; i++) {
	if (cross_thread_mutex[i] == 0) {
	  proceed = 0;
	}
      }
      if (proceed) {
	for (int i = 0; i < threads; i++) {
	  cross_thread_mutex[i] = 0;
	}
	if (cross_thread_index >= CACHE_BUSTER_SIZE - threads - 1) {
	  cross_thread_index = 0;
	} else {
	  cross_thread_index += stride;
	}
      }
      // Cause branch misprediction via 50/50 chance
      val = qrand(&random_seed) & 1;
      if (val) {
	buffer[i] += 1;
      } else if (qrand(&random_seed) == buffer[i]) {
	buffer[i] -= 1;
      } else {
	buffer[i] -= 2;
      }
      // Go pointer chasing
      current_linked_list->num += buffer[i] + buffer2[i];
      current_linked_list = current_linked_list->next;
      // Randomize stride to minimuze prefetching
      stride = ((qrand(&random_seed) % 8) + 4) * 16;

      // Final dummy calculation to prevent optimization 
      unsigned int dummy = buffer[i] ^ buffer2[i];
      dummy ^= cross_thread_buffer[cross_thread_index];
      dummy ^= current_linked_list->num;
      current_linked_list->num = dummy;

      if (spike_counter % 100 == 0) {
        nanosleep(&sleep_time, NULL);
      }
    }
    // Cause memory fragmentation and bad memory access patterns
    times_changed += 1;
    if (times_changed > 10) {
      times_changed = 0;
      spike_counter++;
      struct linked_list *next = current_linked_list->next;
      current_linked_list->next = malloc(sizeof(struct linked_list));
      current_linked_list->next->next = next;
    }

    // Sleep to avoid moving to the top of process managers
    if (spike_counter % 100 == 0) {
      nanosleep(&sleep_time, NULL);
    }
  }
}

void cachethrasher() {
  threads = sysconf(_SC_NPROCESSORS_ONLN);
  pthread_t *thread_ids = (pthread_t *)malloc(sizeof(pthread_t) * threads);
  cross_thread_buffer = (volatile unsigned int*)aligned_alloc(64, sizeof(volatile unsigned int) * CACHE_BUSTER_SIZE);
  cross_thread_mutex = (short*)aligned_alloc(64, sizeof(short) * threads);

  for (int i = 0; i < threads; i++) {
    pthread_create(&thread_ids[i], NULL, *cachethrasher_process,
                   (void *)(intptr_t)i);
  }

  for (int i = 0; i < threads; i++) {
    pthread_join(thread_ids[i], NULL);
  }
}

int main() {
  cachethrasher();
  return 0;
}
