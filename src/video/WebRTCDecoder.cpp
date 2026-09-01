#include "WebRTCDecoder.h"
#include <cstring>
#include <thread>
#include <yangstream/YangSynBuffer.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>
#include <jpeglib.h>
WebRTCDecoder* WebRTCDecoder::g_pSingleton = new (std::nothrow) WebRTCDecoder();
WebRTCDecoder::WebRTCDecoder()
{
    yang_setCLogLevel(1);
    m_context = new YangContext();
    m_context->init();

    m_context->synMgr.session->playBuffer = (YangSynBuffer*)yang_calloc(sizeof(YangSynBuffer), 1);//new YangSynBuffer();
    yang_create_synBuffer(m_context->synMgr.session->playBuffer);

    m_context->avinfo.sys.mediaServer = Yang_Server_P2p;//Yang_Server_Srs/Yang_Server_Zlm
    m_context->avinfo.rtc.rtcSocketProtocol = Yang_Socket_Protocol_Udp;//

    m_context->avinfo.rtc.rtcLocalPort = 10000 + yang_random() % 15000;
    memset(m_context->avinfo.rtc.localIp, 0, sizeof(m_context->avinfo.rtc.localIp));
    yang_getLocalInfo(m_context->avinfo.sys.familyType, m_context->avinfo.rtc.localIp);
    m_context->avinfo.rtc.enableDatachannel = yangfalse;
    m_context->avinfo.rtc.iceCandidateType = YangIceHost;
    m_context->avinfo.rtc.turnSocketProtocol = Yang_Socket_Protocol_Udp;

    m_context->avinfo.rtc.enableAudioBuffer = yangtrue; //use audio buffer
    m_context->avinfo.audio.enableAudioFec = yangfalse; //srs not use audio fec
    m_player = YangPlayerHandle::createPlayerHandle(m_context, this);
}
WebRTCDecoder::~WebRTCDecoder()
{
    m_watchdog_stop = true;
    if (m_watchdog.joinable())
        m_watchdog.join();
}
WebRTCDecoder* WebRTCDecoder::GetInstance()
{
    return g_pSingleton;
}
namespace {
// a stream that has not delivered a frame for this long is treated as dead
const long long kStaleFrameMs = 8000;

long long now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

bool WebRTCDecoder::isStreamStale() const
{
    long long last = m_last_frame_ms.load();
    return last != 0 && now_ms() - last > kStaleFrameMs;
}

void WebRTCDecoder::touchFrameTime()
{
    m_last_frame_ms = now_ms();
}

void WebRTCDecoder::stopPlay()
{
    std::lock_guard<std::mutex> guard(m_control_mutex);
    m_play_requested = false;
    stopPlayLocked();
}

void WebRTCDecoder::stopPlayLocked()
{
    m_isStop = true;
    // the receive thread checks m_isStop every 2ms and no longer holds frame_mutex_
    // while blocking, so waiting for it to finish is safe and keeps the player state clean
    if (m_playFutrue.valid())
        m_playFutrue.wait();

    if (m_player) m_player->stopPlay();

    m_status = STOPPED;
    m_last_frame_ms = 0;
    {
        std::lock_guard<std::mutex> guard(this->frame_mutex_);
        m_frame_data.clear();
    }
}

void WebRTCDecoder::startPlay(const std::string& strUrl)
{
    std::lock_guard<std::mutex> guard(m_control_mutex);
    m_play_requested = true;
    startPlayLocked(strUrl);

    if (!m_watchdog.joinable()) {
        m_watchdog_stop = false;
        m_watchdog = std::thread([this]() { this->watchdogLoop(); });
    }
}

void WebRTCDecoder::startPlayLocked(const std::string& strUrl)
{
    // an existing session on the same url is only reused while it still delivers frames,
    // otherwise a reload of the video page would silently keep the dead connection
    if (m_status == CONNECTED && strUrl == m_url && !isStreamStale())
        return;

    if (m_status != STOPPED || m_playFutrue.valid())
        stopPlayLocked();

    std::cout << "start webrtc play: " << strUrl << "\r\n";
    m_status = CONNECTTING;
    m_url = strUrl;
    m_isStop = false;
    // give the handshake the full stale timeout before the watchdog may retry
    touchFrameTime();

    m_context->synMgr.session->playBuffer->resetVideoClock(m_context->synMgr.session->playBuffer->session);
    int32_t err = m_player->playRtc(0, const_cast<char*>(m_url.c_str()));
    if (err)
    {
        std::cout << "webrtc playRtc failed: " << err << "\r\n";
        m_status = STOPPED;
        m_last_frame_ms = 0;
        return;
    }

    m_playFutrue = std::async(std::launch::async, [this](){
        while(!this->m_isStop)
        {
            this->receiveFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
}

void WebRTCDecoder::watchdogLoop()
{
    while (!m_watchdog_stop)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (m_watchdog_stop)
            break;

        std::unique_lock<std::mutex> guard(m_control_mutex, std::try_to_lock);
        if (!guard.owns_lock())
            continue;
        // covers both a silent stall and a connection the library already reported as failed
        if (!m_play_requested || m_url.empty() || !isStreamStale())
            continue;

        std::cout << "webrtc stream stalled, reconnecting: " << m_url << "\r\n";
        const std::string url = m_url;
        stopPlayLocked();
        startPlayLocked(url);
    }
}

int WebRTCDecoder::width() 
{
    return m_width;
}
int WebRTCDecoder::height(){
    return m_height;
    }
void YUV420P_to_RGB24(const unsigned char* yuv, unsigned char* rgb, int width, int height) {
    const unsigned char* y_plane = yuv;
    const unsigned char* u_plane = yuv + width * height;
    const unsigned char* v_plane = yuv + width * height + (width / 2) * (height / 2);
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int yy = y_plane[y * width + x];
            int uu = u_plane[(y / 2) * (width / 2) + (x / 2)];
            int vv = v_plane[(y / 2) * (width / 2) + (x / 2)];
            
            // YUV to RGB conversion formulas
            int r = yy + 1.402 * (vv - 128);
            int g = yy - 0.34414 * (uu - 128) - 0.71414 * (vv - 128);
            int b = yy + 1.772 * (uu - 128);
            
            // Clamp values to [0, 255]
            r = std::max(0, std::min(255, r));
            g = std::max(0, std::min(255, g));
            b = std::max(0, std::min(255, b));
            
            rgb[(y * width + x) * 3 + 0] = r;
            rgb[(y * width + x) * 3 + 1] = g;
            rgb[(y * width + x) * 3 + 2] = b;
        }
    }
}
    /**
 * 将 RGB 数据压缩为 JPEG 并存储在内存中
 * 
 * @param rgb_data 输入的 RGB 数据 (格式为 R,G,B,R,G,B,...)
 * @param width 图像宽度
 * @param height 图像高度
 * @param quality JPEG 质量 (1-100)
 * @param[out] out_buffer 输出的 JPEG 数据
 * @param[out] out_size 输出的 JPEG 数据大小
 * 
 * @return 成功返回 true，失败返回 false
 */
bool rgb_to_jpeg(const unsigned char* rgb_data, int width, int height, int quality,
    std::vector<unsigned char>& out_buffer) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    // 初始化 JPEG 压缩对象
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    // 设置内存目标
    unsigned char* buffer = nullptr;
    unsigned long buffer_size = 0;
    jpeg_mem_dest(&cinfo, &buffer, &buffer_size);

    // 设置图像参数
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;  // RGB 有 3 个分量
    cinfo.in_color_space = JCS_RGB;

    // 设置默认参数
    jpeg_set_defaults(&cinfo);

    // 设置质量
    jpeg_set_quality(&cinfo, quality, TRUE);

    // 开始压缩
    jpeg_start_compress(&cinfo, TRUE);

    // 逐行写入数据
    JSAMPROW row_pointer[1];
    int row_stride = width * 3;  // RGB 每行有 width*3 字节

    while (cinfo.next_scanline < cinfo.image_height) {
    row_pointer[0] = (JSAMPROW)&rgb_data[cinfo.next_scanline * row_stride];
    jpeg_write_scanlines(&cinfo, row_pointer, 1);
}

// 完成压缩
jpeg_finish_compress(&cinfo);

// 将数据拷贝到输出缓冲区
out_buffer.assign(buffer, buffer + buffer_size);

// 清理
jpeg_destroy_compress(&cinfo);
free(buffer);

return true;
}
std::vector<unsigned char> WebRTCDecoder::getFrameData()
{
    std::lock_guard<std::mutex> guard(this->frame_mutex_);
    if(m_status!=CONNECTED)
        return std::vector<unsigned char>();
    return m_frame_data;
}
void WebRTCDecoder::receiveFrame(){
    YangVideoBuffer*  vb = m_player->getVideoBuffer();
    if(vb == nullptr)
        return;
    uint8_t* t_vb = vb->getVideoRef(&m_frame);
    if (!t_vb)
        return;

    const int width  = vb->m_width;
    const int height = vb->m_height;
    if(width <= 0 || height <= 0)
        return;

    m_width  = width;
    m_height = height;

    std::vector<unsigned char> rgbData(width * height * 3);
    std::vector<unsigned char> yuvData(width * height * 3 / 2);
    std::copy(t_vb, t_vb + yuvData.size(), yuvData.begin());
    YUV420P_to_RGB24(yuvData.data(), rgbData.data(), width, height);

    std::vector<unsigned char> frame_data;
    const int quality = 90;
    if (!rgb_to_jpeg(rgbData.data(), width, height, quality, frame_data))
        return;

    {
        std::lock_guard<std::mutex> guard(this->frame_mutex_);
        m_frame_data.swap(frame_data);
    }
    touchFrameTime();
}
void WebRTCDecoder::success()
{
    m_status = CONNECTED;
    touchFrameTime();
}
void WebRTCDecoder::failure(int32_t errcode)
{
    std::cout << "webrtc connect failure: " << errcode << "\r\n";
    // m_last_frame_ms is kept so the watchdog retries once the stale timeout has passed
    m_status = STOPPED;
    //emit RtcConnectFailure(errcode);
}
