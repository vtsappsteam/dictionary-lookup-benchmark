/*
 * sorted_array.c - Sorted array with binary search.
 */

#include <stdlib.h>
#include <string.h>
#include "structures.h"

void sorted_array_init(sorted_array_t *sa) {
    sa->data = NULL;
    sa->count = 0;
    sa->mem_bytes = 0;
}

static int cmp_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

void sorted_array_build(sorted_array_t *sa, char **words, size_t n) {
    sa->count = n;
    sa->data = (char **)malloc(n * sizeof(char *));
    sa->mem_bytes = n * sizeof(char *);

    for (size_t i = 0; i < n; i++) {
        size_t len = strlen(words[i]);
        sa->data[i] = (char *)malloc(len + 1);
        memcpy(sa->data[i], words[i], len + 1);
        sa->mem_bytes += len + 1;
    }

    qsort(sa->data, sa->count, sizeof(char *), cmp_strings);
}

bool sorted_array_lookup(const sorted_array_t *sa, const char *word) {
    size_t lo = 0, hi = sa->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = strcmp(sa->data[mid], word);
        if (c == 0) return true;
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

size_t sorted_array_memory(const sorted_array_t *sa) {
    return sa->mem_bytes;
}

void sorted_array_free(sorted_array_t *sa) {
    for (size_t i = 0; i < sa->count; i++)
        free(sa->data[i]);
    free(sa->data);
    sa->data = NULL;
    sa->count = 0;
}
