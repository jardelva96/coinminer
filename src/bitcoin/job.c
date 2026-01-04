#include "job.h"

#include <stdio.h>
#include <string.h>

static int parse_string_field(const char *json, const char *key, char *out, size_t out_size) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *start = strstr(json, pattern);
    if (!start) return 0;
    start += strlen(pattern);
    const char *end = strchr(start, '"');
    if (!end) return 0;
    size_t len = (size_t)(end - start);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return 1;
}

static int parse_merkle(const char *json, bitcoin_job *job) {
    const char *key = "\"merkle_branch\":[";
    const char *start = strstr(json, key);
    if (!start) return 0;
    start += strlen(key);
    job->merkle_count = 0;
    while (*start && *start != ']') {
        while (*start == ' ' || *start == '\"' || *start == ',') start++;
        if (*start == ']') break;
        const char *end = strchr(start, '"');
        if (!end) break;
        size_t len = (size_t)(end - start);
        if (job->merkle_count < 16) {
            if (len >= sizeof(job->merkle_branch[0])) len = sizeof(job->merkle_branch[0]) - 1;
            memcpy(job->merkle_branch[job->merkle_count], start, len);
            job->merkle_branch[job->merkle_count][len] = '\0';
            job->merkle_count++;
        }
        start = end + 1;
        const char *comma = strchr(start, ',');
        if (!comma) break;
        start = comma + 1;
    }
    return 1;
}

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

int bitcoin_job_parse_notify(bitcoin_job *job, const char *line, size_t len) {
    if (!job || !line) return 0;
    bitcoin_job_clear(job);
    bitcoin_job_note_notify(job);
    bitcoin_job_set_last_notify(job, line, len);

    // Simplistic JSON field scraping (not a full parser, but enough to capture common Stratum fields).
    if (!parse_string_field(line, "job_id", job->job_id, sizeof(job->job_id))) return 0;
    parse_string_field(line, "prevhash", job->prev_hash, sizeof(job->prev_hash));
    parse_string_field(line, "coinb1", job->coinb1, sizeof(job->coinb1));
    parse_string_field(line, "coinb2", job->coinb2, sizeof(job->coinb2));
    parse_string_field(line, "version", job->version, sizeof(job->version));
    parse_string_field(line, "nbits", job->nbits, sizeof(job->nbits));
    parse_string_field(line, "ntime", job->ntime, sizeof(job->ntime));
    parse_merkle(line, job);

    const char *clean_ptr = strstr(line, "\"clean_jobs\":");
    if (clean_ptr) {
        job->clean_jobs = (strstr(clean_ptr, "true") != NULL);
    }

    job->parsed = 1;
    return 1;
}
