#include "frame_queue.h"
#include "log.h"

#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include <liveMedia.hh>

#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_run = 1;
static EventLoopWatchVariable *g_watch = NULL;

static void on_sig(int sig) {
    (void)sig;
    g_run = 0;
    if (g_watch) {
        *g_watch = 1;
    }
}

class ShmAuFramedSource : public FramedSource {
public:
    static ShmAuFramedSource *createNew(UsageEnvironment &env) {
        return new ShmAuFramedSource(env);
    }

protected:
    ShmAuFramedSource(UsageEnvironment &env) : FramedSource(env) {
        buf_ = new uint8_t[FQ_VIDEO_SLOT_SIZE];
    }
    ~ShmAuFramedSource() override { delete[] buf_; }

    void doGetNextFrame() override {
        FqFrameMeta meta;
        int rc = fq_pop(0, FQ_TYPE_VIDEO, &meta, buf_, FQ_VIDEO_SLOT_SIZE);
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
        if (meta.data_len > fMaxSize) {
            LOGW("RTSP AU truncated len=%u fMaxSize=%u", meta.data_len, fMaxSize);
            fNumTruncatedBytes = meta.data_len - fMaxSize;
            fFrameSize = fMaxSize;
        } else {
            fFrameSize = meta.data_len;
            fNumTruncatedBytes = 0;
        }
        memmove(fTo, buf_, fFrameSize);
        gettimeofday(&fPresentationTime, NULL);
        FramedSource::afterGetting(this);
    }

private:
    uint8_t *buf_;
};

class ShmServerMediaSubsession : public OnDemandServerMediaSubsession {
public:
    static ShmServerMediaSubsession *createNew(UsageEnvironment &env, char const *suffix) {
        return new ShmServerMediaSubsession(env, suffix);
    }

protected:
    ShmServerMediaSubsession(UsageEnvironment &env, char const *suffix)
        : OnDemandServerMediaSubsession(env, True) {
        suffix_ = strDup(suffix);
    }
    ~ShmServerMediaSubsession() override { delete[] suffix_; }

    FramedSource *createNewStreamSource(unsigned /*clientSessionId*/,
                                        unsigned &estBitrate) override {
        fq_flush(0, FQ_TYPE_VIDEO);
        LOGI("RTSP new stream: flush video queue depth=%u", fq_depth(0, FQ_TYPE_VIDEO));
        estBitrate = 2000;
        ShmAuFramedSource *src = ShmAuFramedSource::createNew(envir());
        return H265VideoStreamFramer::createNew(envir(), src);
    }

    RTPSink *createNewRTPSink(Groupsock *rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic,
                              FramedSource * /*inputSource*/) override {
        return H265VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
    }

private:
    char *suffix_;
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

static void add_live_session(RTSPServer *server, UsageEnvironment *env, char const *mount) {
    ServerMediaSession *sms =
        ServerMediaSession::createNew(*env, mount, "rk3576", "H265 live stream");
    sms->addSubsession(ShmServerMediaSubsession::createNew(*env, "video"));
    server->addServerMediaSession(sms);
    char *url = server->rtspURL(sms);
    LOGI("RTSP URL: %s", url);
    delete[] url;
}

int main(int argc, char **argv) {
    int port = (argc > 1) ? atoi(argv[1]) : 554;
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    if (fq_pool_attach(FQ_TYPE_VIDEO, 1) < 0) {
        LOGE("attach video shm failed");
        return 1;
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

    add_live_session(server, env, "live");
    add_live_session(server, env, "live2");

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
    return 0;
}
