#ifndef PLAYER_FFMPEG_WEBRTC_DECODER_H_
#define PLAYER_FFMPEG_WEBRTC_DECODER_H_

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#endif
//extern "C"
//{
#define __STDC_CONSTANT_MACROS
//#include "video/yangrecordthread.h"
#include "yangplayer/YangPlayerHandle.h"
#include "yangstream/YangStreamType.h"
//#include "yangplayer/YangPlayWidget.h"
#include <yangutil/yangavinfotype.h>
#include <yangutil/sys/YangSysMessageI.h>
#include <yangutil/sys/YangSocket.h>
#include <yangutil/sys/YangLog.h>
#include <yangutil/sys/YangMath.h>

//}
#include <future>
#include <functional>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include <boost/asio.hpp>
class WebRTCDecoder :  public YangSysMessageI
{
    enum Status {
    STOPPED = 1,
    CONNECTTING = 2,
    CONNECTED = 3
    };
public:
    static WebRTCDecoder* GetInstance();
    void stopPlay();
    void startPlay(const std::string& strUrl);
    WebRTCDecoder();
    ~WebRTCDecoder();
    void success();
    void failure(int32_t errcode);
    bool isStop() { return m_isStop; }
    int width();
    int height();
    void receiveFrame(); 
    std::vector<unsigned char> getFrameData();

private:
    // caller must hold m_control_mutex
    void startPlayLocked(const std::string& strUrl);
    void stopPlayLocked();
    bool isStreamStale() const;
    void touchFrameTime();
    void watchdogLoop();

    std::atomic<bool> m_isStop{false};
    Status m_status = STOPPED;
    YangPlayerHandle* m_player;
    YangFrame m_frame;
    std::mutex frame_mutex_;
    static WebRTCDecoder *g_pSingleton;
    boost::asio::ip::tcp::socket* m_psocket=nullptr;
    std::vector<unsigned char> m_frame_data;
    // serialises startPlay()/stopPlay() so the control path never blocks on frame_mutex_
    std::mutex m_control_mutex;
    // milliseconds since epoch of the last decoded frame, 0 while not playing
    std::atomic<long long> m_last_frame_ms{0};
    // true between startPlay() and an explicit stopPlay(), drives the reconnect watchdog
    std::atomic<bool> m_play_requested{false};
    std::atomic<bool> m_watchdog_stop{false};
    std::thread m_watchdog;
protected:
    YangContext* m_context;
    std::string m_url;
    std::future<void> m_playFutrue;
    int m_width=1920;
    int m_height=1080;
};

#endif // ! PLAYER_FFMPEG_WEBRTC_DECODER_H_