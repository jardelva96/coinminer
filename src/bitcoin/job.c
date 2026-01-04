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

void bitcoin_job_set_last_notify(bitcoin_job *job, const char *line, size_t len) {
    if (!job || !line) return;
    if (len >= sizeof(job->last_notify)) len = sizeof(job->last_notify) - 1;
    memcpy(job->last_notify, line, len);
    job->last_notify[len] = '\0';
}
