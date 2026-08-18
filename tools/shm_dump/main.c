#include "frame_queue.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <video|audio> <channel> <out_file> [count]\n",
                argv[0]);
        return 1;
    }
    int is_video = strcmp(argv[1], "video") == 0;
    uint32_t ch = (uint32_t)atoi(argv[2]);
    const char *out = argv[3];
    int max = (argc > 4) ? atoi(argv[4]) : 10;

    if (fq_pool_attach(is_video ? FQ_TYPE_VIDEO : FQ_TYPE_AUDIO, ch + 1) < 0) {
        LOGE("attach failed");
        return 1;
    }
    FILE *fp = fopen(out, "wb");
    if (!fp) {
        return 1;
    }
    uint8_t *buf = (uint8_t *)malloc(FQ_VIDEO_SLOT_SIZE);
    int got = 0;
    while (got < max) {
        FqFrameMeta meta;
        int rc = fq_pop(ch, is_video ? FQ_TYPE_VIDEO : FQ_TYPE_AUDIO, &meta, buf,
                        is_video ? FQ_VIDEO_SLOT_SIZE : FQ_AUDIO_SLOT_SIZE);
        if (rc == 1) {
            usleep(10000);
            continue;
        }
        if (rc < 0) {
            break;
        }
        fwrite(buf, 1, meta.data_len, fp);
        ++got;
    }
    fclose(fp);
    free(buf);
    fq_pool_detach(is_video ? FQ_TYPE_VIDEO : FQ_TYPE_AUDIO);
    LOGI("dumped %d frames to %s", got, out);
    return got > 0 ? 0 : 1;
}
