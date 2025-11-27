# string_splitter

Simple C library for splitting strings by delimiter(s).

## API

```c
split_string_holder_t * new_split_string_holder(const char *input, const char *sep);
void free_split_string_holder(split_string_holder_t *holder);
```

## Usage

```c
split_string_holder_t *holder = new_split_string_holder("foo,bar,baz", ",");
// holder->part_count == 3
// holder->parts[0] == "foo"
// holder->parts[1] == "bar"
// holder->parts[2] == "baz"
free_split_string_holder(holder);
```

Uses `strtok` internally - any character in `sep` is treated as a delimiter.
