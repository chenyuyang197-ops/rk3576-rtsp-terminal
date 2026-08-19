#ifndef RK3576_MPP_CODEC_H
#define RK3576_MPP_CODEC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MppEncoder MppEncoder;
typedef struct MppDecoder MppDecoder;

MppEncoder *mpp_encoder_create_hevc(int width, int height, int fps, int bitrate);
int mpp_encoder_encode(MppEncoder *enc, const uint8_t *nv12, int64_t pts_us,
                       uint8_t *out, int out_cap, int *out_len, int *is_key);
void mpp_encoder_destroy(MppEncoder *enc);

MppDecoder *mpp_decoder_create_hevc(int width, int height);
int mpp_decoder_decode(MppDecoder *dec, const uint8_t *data, int len,
                       uint8_t *nv12_out, int nv12_cap);
/* 仅 drain 已缓冲帧，不送新 packet；0=有帧，1=暂无 */
int mpp_decoder_drain(MppDecoder *dec, uint8_t *nv12_out, int nv12_cap);
void mpp_decoder_destroy(MppDecoder *dec);

#ifdef __cplusplus
}
#endif

#endif
