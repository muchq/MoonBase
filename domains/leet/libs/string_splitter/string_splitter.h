#ifndef C_STRING_SPLITTER_STRING_SPLITTER_H
#define C_STRING_SPLITTER_STRING_SPLITTER_H

typedef struct {
  char* sep;
  int part_count;
  char** parts;
} split_string_holder_t;

void free_split_string_holder(split_string_holder_t* holder);
split_string_holder_t* new_split_string_holder(const char* input, const char* sep);

#endif
