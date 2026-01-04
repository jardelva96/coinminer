#include "job.h"

#include <string.h>

void bitcoin_job_clear(bitcoin_job *job) {
    if (!job) return;
    memset(job, 0, sizeof(*job));
}

void bitcoin_job_note_notify(bitcoin_job *job) {
    if (!job) return;
    job->notify_count++;
}
