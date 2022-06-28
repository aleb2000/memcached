#ifndef BENCH_UTILS_H
#define BENCH_UTILS_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_PAYLOAD_SIZE 1000000
#define SAFE_BUF_SIZE MAX_PAYLOAD_SIZE * 2

int fill_set_key_value(char* buf, int tid, int iter);
int fill_get_key_value(char* buf, int tid, int iter);
int fill_expected_get_resp(char* buf, int tid, int iter);

void init_payload(size_t len);

size_t get_payload_len(void);


#endif // BENCH_UTILS_H