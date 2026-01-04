#ifndef BITCOIN_JOB_H
#define BITCOIN_JOB_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char job_id[128];
    char prev_hash[80];
    char coinb1[256];
    char coinb2[256];
    char merkle_branch[2048];
    char version[16];
    char nbits[16];
    char ntime[16];
    int clean_jobs;
    uint64_t notify_count;
} bitcoin_job;

void bitcoin_job_clear(bitcoin_job *job);
void bitcoin_job_note_notify(bitcoin_job *job);

#endif
