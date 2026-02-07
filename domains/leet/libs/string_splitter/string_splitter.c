#include "string_splitter.h"

#include <stdlib.h>
#include <string.h>

void free_split_string_holder(split_string_holder_t *holder) {
    if (holder == NULL) {
        return;
    }

    if (holder->parts != NULL) {
        for (int i=0; i<holder->part_count; i++) {
            free(holder->parts[i]);
        }

        free(holder->parts);
    }

    free(holder->sep);
    free(holder);
}

split_string_holder_t * new_split_string_holder(const char *input, const char *sep) {
    if (input == NULL || sep == NULL) {
        return NULL;
    }

    split_string_holder_t * holder = malloc(sizeof(split_string_holder_t));
    if (holder == NULL) {
        return NULL;
    }

    // initialize holder fields
    holder->sep = NULL;
    holder->parts = NULL;
    holder->part_count = 0;

    char * owned_copy_for_token_count = strdup(input);
    if (owned_copy_for_token_count == NULL) {
        free_split_string_holder(holder);
        return NULL;
    }

    // count tokens
    int token_count = 0;
    char *token = strtok(owned_copy_for_token_count, sep);
    while (token != NULL) {
        token_count++;
        token = strtok(NULL, sep);
    }
    free(owned_copy_for_token_count)                                                                                                                                                                                                                                                                                ;

    holder->sep = strdup(sep);
    if (holder->sep == NULL) {
        free_split_string_holder(holder);
        return NULL;
    }

    holder->part_count = token_count;
    holder->parts = calloc(token_count, sizeof(char *));
    if (holder->parts == NULL) {
        free_split_string_holder(holder);
        return NULL;
    }

    char * owned_copy_for_split = strdup(input);
    if (owned_copy_for_split == NULL) {
        free_split_string_holder(holder);
        return NULL;
    }

    token = strtok(owned_copy_for_split, sep);
    int i = 0;
    while (token != NULL) {
        holder->parts[i] = strdup(token);
        if (holder->parts[i] == NULL) {
            free_split_string_holder(holder);
            free(owned_copy_for_split);
            return NULL;
        }
        i++;
        token = strtok(NULL, sep);
    }

    free(owned_copy_for_split);

    return holder;
}
