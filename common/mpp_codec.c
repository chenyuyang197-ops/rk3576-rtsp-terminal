#include "mpp_codec.h"

#include "log.h"

#include <stdlib.h>
#include <string.h>

#include <rockchip/rk_mpi.h>
#include <rockchip/rk_mpi_cmd.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>

struct MppEncoder {
    MppCtx ctx;
    MppApi *mpi;
    MppBuffer frm_buf;
    int width;
    int height;
    int fps;
    int frame_idx;
};

struct MppDecoder {
    MppCtx ctx;
    MppApi *mpi;
    MppBuffer frm_buf;
    int width;
    int height;
};

static void set_enc_cfg(MppEncoder *enc) {
    MppEncCfg cfg;
    mpp_enc_cfg_init(&cfg);
    mpp_enc_cfg_set_s32(cfg, "prep:width", enc->width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", enc->height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", enc->width);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", enc->height);
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP);
    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", 2000000);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", 2500000);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", 1500000);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", enc->fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denom", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", enc->fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denom", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", enc->fps);
    mpp_enc_cfg_set_s32(cfg, "codec:type", MPP_VIDEO_CodingHEVC);
    enc->mpi->control(enc->ctx, MPP_ENC_SET_CFG, cfg);
    mpp_enc_cfg_deinit(cfg);
}

MppEncoder *mpp_encoder_create_hevc(int width, int height, int fps, int bitrate) {
    (void)bitrate;
    MppEncoder *enc = (MppEncoder *)calloc(1, sizeof(MppEncoder));
    if (!enc) {
        return NULL;
    }
    enc->width = width;
    enc->height = height;
    enc->fps = fps;
    if (mpp_create(&enc->ctx, &enc->mpi) != MPP_OK) {
        free(enc);
        return NULL;
    }
    if (mpp_init(enc->ctx, MPP_CTX_ENC, MPP_VIDEO_CodingHEVC) != MPP_OK) {
        mpp_destroy(enc->ctx);
        free(enc);
        return NULL;
    }
    set_enc_cfg(enc);
    size_t frm_size = (size_t)width * height * 3 / 2;
    if (mpp_buffer_get(NULL, &enc->frm_buf, frm_size) != MPP_OK) {
        mpp_destroy(enc->ctx);
        free(enc);
        return NULL;
    }
    return enc;
}

int mpp_encoder_encode(MppEncoder *enc, const uint8_t *nv12, int64_t pts_us,
                       uint8_t *out, int out_cap, int *out_len, int *is_key) {
    size_t frm_size = (size_t)enc->width * enc->height * 3 / 2;
    void *ptr = mpp_buffer_get_ptr(enc->frm_buf);
    memcpy(ptr, nv12, frm_size);

    MppFrame frame = NULL;
    mpp_frame_init(&frame);
    mpp_frame_set_width(frame, enc->width);
    mpp_frame_set_height(frame, enc->height);
    mpp_frame_set_hor_stride(frame, enc->width);
    mpp_frame_set_ver_stride(frame, enc->height);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    mpp_frame_set_buffer(frame, enc->frm_buf);
    mpp_frame_set_pts(frame, pts_us);

    if (enc->mpi->encode_put_frame(enc->ctx, frame) != MPP_OK) {
        mpp_frame_deinit(&frame);
        return -1;
    }
    mpp_frame_deinit(&frame);

    MppPacket packet = NULL;
    if (enc->mpi->encode_get_packet(enc->ctx, &packet) != MPP_OK || !packet) {
        return -1;
    }
    size_t len = mpp_packet_get_length(packet);
    if ((int)len > out_cap) {
        mpp_packet_deinit(&packet);
        return -1;
    }
    memcpy(out, mpp_packet_get_data(packet), len);
    *out_len = (int)len;
    *is_key = (enc->frame_idx == 0 || (enc->frame_idx % (enc->fps * 2) == 0)) ? 1 : 0;
    mpp_packet_deinit(&packet);
    enc->frame_idx++;
    return 0;
}

void mpp_encoder_destroy(MppEncoder *enc) {
    if (!enc) {
        return;
    }
    if (enc->frm_buf) {
        mpp_buffer_put(enc->frm_buf);
    }
    if (enc->ctx) {
        mpp_destroy(enc->ctx);
    }
    free(enc);
}

MppDecoder *mpp_decoder_create_hevc(int width, int height) {
    MppDecoder *dec = (MppDecoder *)calloc(1, sizeof(MppDecoder));
    if (!dec) {
        return NULL;
    }
    dec->width = width;
    dec->height = height;
    if (mpp_create(&dec->ctx, &dec->mpi) != MPP_OK) {
        free(dec);
        return NULL;
    }
    RK_U32 split = 1;
    if (dec->mpi->control(dec->ctx, MPP_DEC_SET_PARSER_SPLIT_MODE, &split) != MPP_OK) {
        mpp_destroy(dec->ctx);
        free(dec);
        return NULL;
    }
    if (mpp_init(dec->ctx, MPP_CTX_DEC, MPP_VIDEO_CodingHEVC) != MPP_OK) {
        mpp_destroy(dec->ctx);
        free(dec);
        return NULL;
    }
    size_t frm_size = (size_t)width * height * 3 / 2;
    if (mpp_buffer_get(NULL, &dec->frm_buf, frm_size) != MPP_OK) {
        mpp_destroy(dec->ctx);
        free(dec);
        return NULL;
    }
    return dec;
}

static int copy_mpp_frame(MppDecoder *dec, MppFrame frame, uint8_t *nv12_out, int nv12_cap) {
    if (mpp_frame_get_errinfo(frame) || mpp_frame_get_discard(frame)) {
        return -1;
    }
    MppBuffer buf = mpp_frame_get_buffer(frame);
    if (!buf) {
        return -1;
    }
    int width = mpp_frame_get_width(frame);
    int height = mpp_frame_get_height(frame);
    int hor_stride = mpp_frame_get_hor_stride(frame);
    int ver_stride = mpp_frame_get_ver_stride(frame);
    if (width <= 0) {
        width = dec->width;
    }
    if (height <= 0) {
        height = dec->height;
    }
    if (hor_stride <= 0) {
        hor_stride = width;
    }
    if (ver_stride <= 0) {
        ver_stride = height;
    }
    size_t need = (size_t)width * height * 3 / 2;
    if ((int)need > nv12_cap) {
        return -1;
    }
    const uint8_t *src = (const uint8_t *)mpp_buffer_get_ptr(buf);
    uint8_t *dst = nv12_out;
    for (int y = 0; y < height; ++y) {
        memcpy(dst + (size_t)y * width, src + (size_t)y * hor_stride, (size_t)width);
    }
    src += (size_t)hor_stride * ver_stride;
    dst += (size_t)width * height;
    for (int y = 0; y < height / 2; ++y) {
        memcpy(dst + (size_t)y * width, src + (size_t)y * hor_stride, (size_t)width);
    }
    return 0;
}

static int decoder_get_one(MppDecoder *dec, uint8_t *nv12_out, int nv12_cap) {
    for (int i = 0; i < 16; ++i) {
        MppFrame frame = NULL;
        MPP_RET ret = dec->mpi->decode_get_frame(dec->ctx, &frame);
        if (ret != MPP_OK || !frame) {
            return 1;
        }
        if (mpp_frame_get_info_change(frame)) {
            dec->mpi->control(dec->ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
            mpp_frame_deinit(&frame);
            continue;
        }
        int rc = copy_mpp_frame(dec, frame, nv12_out, nv12_cap);
        mpp_frame_deinit(&frame);
        if (rc == 0) {
            return 0;
        }
    }
    return 1;
}

int mpp_decoder_decode(MppDecoder *dec, const uint8_t *data, int len,
                       uint8_t *nv12_out, int nv12_cap) {
    MppPacket packet = NULL;
    mpp_packet_init(&packet, (void *)data, (size_t)len);
    MPP_RET put_ret = dec->mpi->decode_put_packet(dec->ctx, packet);
    mpp_packet_deinit(&packet);
    if (put_ret != MPP_OK) {
        return -1;
    }
    return decoder_get_one(dec, nv12_out, nv12_cap);
}

int mpp_decoder_drain(MppDecoder *dec, uint8_t *nv12_out, int nv12_cap) {
    return decoder_get_one(dec, nv12_out, nv12_cap);
}

void mpp_decoder_destroy(MppDecoder *dec) {
    if (!dec) {
        return;
    }
    if (dec->frm_buf) {
        mpp_buffer_put(dec->frm_buf);
    }
    if (dec->ctx) {
        mpp_destroy(dec->ctx);
    }
    free(dec);
}
