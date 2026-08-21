#include "frame_queue.h"
#include "log.h"

#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include <liveMedia.hh>
#include <MPEG4GenericRTPSink.hh>

#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_run = 1;
static EventLoopWatchVariable *g_watch = NULL;
static uint32_t g_pub_channels = 1;

static uint32_t read_pub_channels(void) {
    const char *v = getenv("RTSP_PUB_CHANNELS");
    if (!v || !v[0]) {
        return 1;
    }
    int n = atoi(v);
    if (n < 1) {
        n = 1;
    }
    if (n > (int)FQ_MAX_CHANNELS) {
        n = FQ_MAX_CHANNELS;
    }
    return (uint32_t)n;
}

static void on_sig(int sig) {
    (void)sig;
    g_run = 0;
    if (g_watch) {
        *g_watch = 1;
    }
}

class ShmAuFramedSource : public FramedSource {
public:
    static ShmAuFramedSource *createNew(UsageEnvironment &env, uint32_t channel) {
        return new ShmAuFramedSource(env, channel);
    }

protected:
    ShmAuFramedSource(UsageEnvironment &env, uint32_t channel)
        : FramedSource(env), channel_(channel), pending_len_(0), pending_wait_(0) {
        buf_ = new uint8_t[FQ_VIDEO_SLOT_SIZE];
    }
    ~ShmAuFramedSource() override { delete[] buf_; }

    void doGetNextFrame() override {
        OutPacketBuffer::increaseMaxSizeTo(FQ_VIDEO_SLOT_SIZE + 65536);

        const uint8_t *payload = NULL;
        uint32_t payload_len = 0;
        if (pending_len_ > 0) {
            payload = buf_;
            payload_len = pending_len_;
        } else {
            FqFrameMeta meta;
            int rc = fq_pop(channel_, FQ_TYPE_VIDEO, &meta, buf_, FQ_VIDEO_SLOT_SIZE);
            if (rc == 1) {
                nextTask() = envir().taskScheduler().scheduleDelayedTask(
                    5000, (TaskFunc *)FramedSource::afterGetting, this);
                return;
            }
            if (rc < 0) {
                handleClosure();
                return;
            }
            if (meta.data_len == 0) {
                nextTask() = envir().taskScheduler().scheduleDelayedTask(
                    5000, (TaskFunc *)FramedSource::afterGetting, this);
                return;
            }
            payload = buf_;
            payload_len = meta.data_len;
        }

        if (payload_len > fMaxSize) {
            if (pending_len_ == 0) {
                pending_len_ = payload_len;
            }
            ++pending_wait_;
            if (pending_wait_ >= 20) {
                LOGW("RTSP ch=%u AU force-truncate len=%u fMaxSize=%u", channel_,
                     payload_len, fMaxSize);
                fFrameSize = fMaxSize;
                fNumTruncatedBytes = payload_len - fMaxSize;
                memmove(fTo, payload, fFrameSize);
                pending_len_ = 0;
                pending_wait_ = 0;
                gettimeofday(&fPresentationTime, NULL);
                FramedSource::afterGetting(this);
                return;
            }
            nextTask() = envir().taskScheduler().scheduleDelayedTask(
                5000, (TaskFunc *)FramedSource::afterGetting, this);
            return;
        }

        fFrameSize = payload_len;
        fNumTruncatedBytes = 0;
        memmove(fTo, payload, fFrameSize);
        pending_len_ = 0;
        pending_wait_ = 0;
        gettimeofday(&fPresentationTime, NULL);
        FramedSource::afterGetting(this);
    }

private:
    uint32_t channel_;
    uint8_t *buf_;
    uint32_t pending_len_;
    unsigned pending_wait_;
};

class ShmServerMediaSubsession : public OnDemandServerMediaSubsession {
public:
    static ShmServerMediaSubsession *createNew(UsageEnvironment &env, char const *suffix,
                                               uint32_t channel) {
        return new ShmServerMediaSubsession(env, suffix, channel);
    }

protected:
    ShmServerMediaSubsession(UsageEnvironment &env, char const *suffix, uint32_t channel)
        : OnDemandServerMediaSubsession(env, True), channel_(channel) {
        suffix_ = strDup(suffix);
    }
    ~ShmServerMediaSubsession() override { delete[] suffix_; }

    FramedSource *createNewStreamSource(unsigned /*clientSessionId*/,
                                        unsigned &estBitrate) override {
        OutPacketBuffer::increaseMaxSizeTo(FQ_VIDEO_SLOT_SIZE + 65536);
        fq_flush(channel_, FQ_TYPE_VIDEO);
        LOGI("RTSP ch=%u new stream: flush video depth=%u", channel_,
             fq_depth(channel_, FQ_TYPE_VIDEO));
        estBitrate = 2000;
        ShmAuFramedSource *src = ShmAuFramedSource::createNew(envir(), channel_);
        return H265VideoStreamFramer::createNew(envir(), src);
    }

    RTPSink *createNewRTPSink(Groupsock *rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic,
                              FramedSource * /*inputSource*/) override {
        return H265VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
    }

private:
    char *suffix_;
    uint32_t channel_;
};

static unsigned const kSamplingFrequencyTable[16] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
    16000, 12000, 11025, 8000,  7350,  0,     0,     0,
};

class ShmAdtsLiveSource : public FramedSource {
public:
    static ShmAdtsLiveSource *createNew(UsageEnvironment &env, uint32_t channel) {
        return new ShmAdtsLiveSource(env, channel);
    }
    unsigned samplingFrequency() const { return sample_rate_; }
    unsigned numChannels() const { return num_channels_; }
    char const *configStr() const { return config_str_; }

protected:
    ShmAdtsLiveSource(UsageEnvironment &env, uint32_t channel)
        : FramedSource(env), channel_(channel) {
        buf_ = new uint8_t[FQ_AUDIO_SLOT_SIZE];
    }
    ~ShmAdtsLiveSource() override { delete[] buf_; }

    void doGetNextFrame() override {
        FqFrameMeta meta;
        int rc = fq_pop(channel_, FQ_TYPE_AUDIO, &meta, buf_, FQ_AUDIO_SLOT_SIZE);
        if (rc == 1) {
            nextTask() = envir().taskScheduler().scheduleDelayedTask(
                5000, (TaskFunc *)FramedSource::afterGetting, this);
            return;
        }
        if (rc < 0 || meta.data_len < 7) {
            handleClosure();
            return;
        }
        if (!parsed_) {
            parseAdtsConfig(buf_, meta.data_len);
            parsed_ = 1;
        }
        Boolean protection_absent = buf_[1] & 0x01;
        unsigned header_size = protection_absent ? 7U : 9U;
        if (meta.data_len <= header_size) {
            nextTask() = envir().taskScheduler().scheduleDelayedTask(
                5000, (TaskFunc *)FramedSource::afterGetting, this);
            return;
        }
        unsigned payload = meta.data_len - header_size;
        if (payload > fMaxSize) {
            fNumTruncatedBytes = payload - fMaxSize;
            fFrameSize = fMaxSize;
        } else {
            fFrameSize = payload;
            fNumTruncatedBytes = 0;
        }
        memmove(fTo, buf_ + header_size, fFrameSize);
        if (presentation_time_.tv_sec == 0 && presentation_time_.tv_usec == 0) {
            gettimeofday(&presentation_time_, NULL);
        } else {
            unsigned uSecs = presentation_time_.tv_usec + usecs_per_frame_;
            presentation_time_.tv_sec += uSecs / 1000000;
            presentation_time_.tv_usec = uSecs % 1000000;
        }
        fPresentationTime = presentation_time_;
        fDurationInMicroseconds = usecs_per_frame_;
        FramedSource::afterGetting(this);
    }

private:
    void parseAdtsConfig(const uint8_t *adts, unsigned len) {
        if (len < 4 || adts[0] != 0xFF || (adts[1] & 0xF0) != 0xF0) {
            return;
        }
        u_int8_t profile = (adts[2] & 0xC0) >> 6;
        u_int8_t sampling_frequency_index = (adts[2] & 0x3C) >> 2;
        u_int8_t channel_configuration =
            ((adts[2] & 0x01) << 2) | ((adts[3] & 0xC0) >> 6);
        if (sampling_frequency_index < 16 &&
            kSamplingFrequencyTable[sampling_frequency_index] != 0) {
            sample_rate_ = kSamplingFrequencyTable[sampling_frequency_index];
        }
        num_channels_ = channel_configuration == 0 ? 2U : channel_configuration;
        usecs_per_frame_ = (1024U * 1000000U) / sample_rate_;
        unsigned char audioSpecificConfig[2];
        u_int8_t audioObjectType = profile + 1;
        audioSpecificConfig[0] = (audioObjectType << 3) | (sampling_frequency_index >> 1);
        audioSpecificConfig[1] =
            (sampling_frequency_index << 7) | (channel_configuration << 3);
        snprintf(config_str_, sizeof(config_str_), "%02X%02X", audioSpecificConfig[0],
                 audioSpecificConfig[1]);
    }

    uint8_t *buf_;
    uint32_t channel_;
    unsigned sample_rate_ = 44100;
    unsigned num_channels_ = 2;
    unsigned usecs_per_frame_ = 23219;
    char config_str_[8] = "1210";
    int parsed_ = 0;
    struct timeval presentation_time_ = {0, 0};
};

class ShmAdtsServerMediaSubsession : public OnDemandServerMediaSubsession {
public:
    static ShmAdtsServerMediaSubsession *createNew(UsageEnvironment &env, uint32_t channel) {
        return new ShmAdtsServerMediaSubsession(env, channel);
    }

protected:
    ShmAdtsServerMediaSubsession(UsageEnvironment &env, uint32_t channel)
        : OnDemandServerMediaSubsession(env, True), channel_(channel) {}

    FramedSource *createNewStreamSource(unsigned /*clientSessionId*/,
                                        unsigned &estBitrate) override {
        fq_flush(channel_, FQ_TYPE_AUDIO);
        LOGI("RTSP ch=%u new audio stream: flush depth=%u", channel_,
             fq_depth(channel_, FQ_TYPE_AUDIO));
        estBitrate = 64;
        return ShmAdtsLiveSource::createNew(envir(), channel_);
    }

    RTPSink *createNewRTPSink(Groupsock *rtpGroupsock,
                              unsigned char rtpPayloadTypeIfDynamic,
                              FramedSource *inputSource) override {
        ShmAdtsLiveSource *src = (ShmAdtsLiveSource *)inputSource;
        return MPEG4GenericRTPSink::createNew(envir(), rtpGroupsock,
                                              rtpPayloadTypeIfDynamic,
                                              src->samplingFrequency(), "audio", "AAC-hbr",
                                              src->configStr(), src->numChannels());
    }

private:
    uint32_t channel_;
};

class RkRTSPServer : public RTSPServer {
public:
    static RkRTSPServer *createNew(UsageEnvironment &env, Port port) {
        int v4 = setUpOurSocket(env, port, AF_INET);
        if (v4 < 0) {
            return NULL;
        }
        int v6 = setUpOurSocket(env, port, AF_INET6);
        return new RkRTSPServer(env, v4, v6, port);
    }

protected:
    RkRTSPServer(UsageEnvironment &env, int v4, int v6, Port port)
        : RTSPServer(env, v4, v6, port, NULL, 65) {}
};

static void add_live_session(RTSPServer *server, UsageEnvironment *env, char const *mount,
                             uint32_t channel, int with_audio) {
    ServerMediaSession *sms =
        ServerMediaSession::createNew(*env, mount, "rk3576", "H265/AAC live stream");
    sms->addSubsession(ShmServerMediaSubsession::createNew(*env, "video", channel));
    if (with_audio) {
        sms->addSubsession(ShmAdtsServerMediaSubsession::createNew(*env, channel));
    }
    server->addServerMediaSession(sms);
    char *url = server->rtspURL(sms);
    LOGI("RTSP URL: %s%s ch=%u", url, with_audio ? " (H265+AAC)" : "", channel);
    delete[] url;
}

int main(int argc, char **argv) {
    int port = (argc > 1) ? atoi(argv[1]) : 554;
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    g_pub_channels = read_pub_channels();
    LOGI("rtsp_server: pub_channels=%u", g_pub_channels);
    if (fq_pool_attach(FQ_TYPE_VIDEO, g_pub_channels) < 0) {
        LOGE("attach video shm failed");
        return 1;
    }
    int with_audio = 1;
    if (fq_pool_attach(FQ_TYPE_AUDIO, g_pub_channels) < 0) {
        LOGW("attach audio shm failed, video-only RTSP");
        with_audio = 0;
    }

    OutPacketBuffer::increaseMaxSizeTo(16000000);

    TaskScheduler *scheduler = BasicTaskScheduler::createNew();
    UsageEnvironment *env = BasicUsageEnvironment::createNew(*scheduler);
    const char *bind_ip = getenv("RTSP_BIND_IP");
    if (bind_ip && bind_ip[0]) {
        ReceivingInterfaceAddr = inet_addr(bind_ip);
        LOGI("RTSP bind interface %s", bind_ip);
    }
    Port rtspPort((portNumBits)port);
    RkRTSPServer *server = RkRTSPServer::createNew(*env, rtspPort);
    if (!server) {
        LOGE("RTSP server create failed port=%d (%s)", port,
             env->getResultMsg() ? env->getResultMsg() : "IPv4 bind failed");
        return 1;
    }

    add_live_session(server, env, "live", 0, with_audio);
    add_live_session(server, env, "live2", g_pub_channels >= 2 ? 1U : 0U, with_audio);

    EventLoopWatchVariable watchVar(0);
    g_watch = &watchVar;
    while (g_run) {
        watchVar = 0;
        env->taskScheduler().doEventLoop(&watchVar);
    }
    g_watch = NULL;
    Medium::close(server);
    env->reclaim();
    delete scheduler;
    fq_pool_detach(FQ_TYPE_VIDEO);
    fq_pool_detach(FQ_TYPE_AUDIO);
    return 0;
}
