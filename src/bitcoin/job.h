#ifndef BITCOIN_JOB_H
#define BITCOIN_JOB_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char job_id[128];
    char prev_hash[80];
    char coinb1[256];
    char coinb2[256];
    char merkle_branch[16][70];
    size_t merkle_count;
    char version[16];
    char nbits[16];
    char ntime[16];
    int clean_jobs;
    int parsed;
    uint64_t notify_count;
    char last_notify[512];
} bitcoin_job;

void bitcoin_job_clear(bitcoin_job *job);
void bitcoin_job_note_notify(bitcoin_job *job);
void bitcoin_job_set_last_notify(bitcoin_job *job, const char *line, size_t len);
int bitcoin_job_parse_notify(bitcoin_job *job, const char *line, size_t len);

#endif
