#include "job.h"

#include <string.h>

static int parse_json_string(const char **p, char *out, size_t out_size) {
    const char *s = *p;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s != '\"') return 0;
    s++;  // skip opening quote

    size_t len = 0;
    while (*s && *s != '\"') {
        if (len + 1 < out_size) {
            out[len] = *s;
            len++;
        }
        s++;
    }
    if (*s != '\"') return 0;
    out[len] = '\0';
    s++;  // skip closing quote
    *p = s;
    return 1;
}

static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
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

    // Expect Stratum notify params array:
    // ["job_id","prevhash","coinb1","coinb2",[merkle...],"version","nbits","ntime",clean_jobs]
    const char *p = strstr(line, "\"params\"");
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;  // move past '['

    if (!parse_json_string(&p, job->job_id, sizeof(job->job_id))) return 0;
    skip_ws(&p);
    if (*p == ',') p++;
    if (!parse_json_string(&p, job->prev_hash, sizeof(job->prev_hash))) return 0;
    skip_ws(&p);
    if (*p == ',') p++;
    parse_json_string(&p, job->coinb1, sizeof(job->coinb1));
    skip_ws(&p);
    if (*p == ',') p++;
    parse_json_string(&p, job->coinb2, sizeof(job->coinb2));
    skip_ws(&p);
    if (*p == ',') p++;

    // Merkle array
    skip_ws(&p);
    if (*p != '[') return 0;
    p++;
    job->merkle_count = 0;
    while (*p && *p != ']') {
        skip_ws(&p);
        if (*p == ']') break;
        if (job->merkle_count < 16) {
            if (!parse_json_string(&p, job->merkle_branch[job->merkle_count], sizeof(job->merkle_branch[0]))) break;
            job->merkle_count++;
        } else {
            // skip string
            char dummy[2];
            if (!parse_json_string(&p, dummy, sizeof(dummy))) break;
        }
        skip_ws(&p);
        if (*p == ',') p++;
    }
    if (*p == ']') p++;
    skip_ws(&p);
    if (*p == ',') p++;

    parse_json_string(&p, job->version, sizeof(job->version));
    skip_ws(&p);
    if (*p == ',') p++;
    parse_json_string(&p, job->nbits, sizeof(job->nbits));
    skip_ws(&p);
    if (*p == ',') p++;
    parse_json_string(&p, job->ntime, sizeof(job->ntime));
    skip_ws(&p);
    if (*p == ',') p++;

    // clean_jobs boolean
    skip_ws(&p);
    if (strncmp(p, "true", 4) == 0) {
        job->clean_jobs = 1;
    } else {
        job->clean_jobs = 0;
    }

    job->parsed = 1;
    return 1;
}
