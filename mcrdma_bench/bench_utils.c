#include "bench_utils.h"
#include <stdio.h>
#include <stdlib.h>

char* payload = NULL;
size_t payload_len = 0;

// Taken from https://stackoverflow.com/a/15768317
static void rand_str(char *dest, size_t length) {
    char charset[] = "0123456789"
                     "abcdefghijklmnopqrstuvwxyz"
                     "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    while (length-- > 0) {
        size_t index = (double) rand() / RAND_MAX * (sizeof charset - 1);
        *dest++ = charset[index];
    }
    *dest = '\0';
}


void init_payload(size_t len) {
    payload = malloc(len + 1);
    payload_len = len;

    rand_str(payload, payload_len);

    //printf("string: %s\n", payload);
}

int fill_set_key_value(char* buf, int tid, int iter) {
    return sprintf(buf, "set thread%02d_key%010d 0 100 %lu\r\n%s\r\n", tid, iter, payload_len, payload);
}

int fill_get_key_value(char* buf, int tid, int iter) {
    return sprintf(buf, "get thread%02d_key%010d\r\n", tid, iter);
}

int fill_expected_get_resp(char* buf, int tid, int iter) {
    return sprintf(buf, "VALUE thread%02d_key%010d 0 %lu\r\n%s\r\nEND\r\n", tid, iter, payload_len, payload);
}

size_t get_payload_len(void) {
    return payload_len;
}