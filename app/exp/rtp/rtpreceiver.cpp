#include "rtpreceiver.h"

#include <qdebug.h>

#include "../common_consti/StringHelper.hpp"
#include "../common_consti/openhd-util.hpp"

#include "../../videostreaming/decodingstatistcs.h"
#include "common_consti/openhd-util.hpp"

RTPReceiver::RTPReceiver(int port,bool is_h265,bool feed_incomplete_frames):
    is_h265(is_h265)
{
    if(false){
        // Debug write raw data to file
        std::stringstream ss;
        ss<<"/tmp/received_rtp.";
        if(is_h265){
            ss<<"h265";
        }else{
            ss<<"h264";
        }
        m_out_file=std::make_unique<std::ofstream>(ss.str());
    }
    m_keyframe_finder=std::make_unique<KeyFrameFinder>();
    m_rtp_decoder=std::make_unique<RTPDecoder>([this](const std::chrono::steady_clock::time_point creation_time,const uint8_t *nalu_data, const int nalu_data_size){
        this->nalu_data_callback(creation_time,nalu_data,nalu_data_size);
    },feed_incomplete_frames);
    // Increase the OS max UDP buffer size (only works as root) such that the UDP receiver
    // doesn't fail when requesting a bigger UDP buffer size
    OHDUtil::run_command("sysctl ",{"-w","net.core.rmem_max=26214400"});
    m_udp_receiver=std::make_unique<UDPReceiver>(port,"V_REC",[this](const uint8_t *payload, const std::size_t payloadSize){
        this->udp_raw_data_callback(payload,payloadSize);
        DecodingStatistcs::instance().set_n_missing_rtp_video_packets(m_rtp_decoder->m_n_gaps);
    },UDPReceiver::BIG_UDP_RECEIVE_BUFFER_SIZE);
    m_udp_receiver->startReceiving();
}

RTPReceiver::~RTPReceiver()
{
    m_udp_receiver->stopReceiving();
}


 std::shared_ptr<NALU> RTPReceiver::get_data()
{
    std::lock_guard<std::mutex> lock(m_data_mutex);
    if(m_data.size()<=0)return nullptr;
    auto ret=m_data.front();
    m_data.pop();
    return ret;
}

std::shared_ptr<std::vector<uint8_t>> RTPReceiver::get_config_data()
{
     std::lock_guard<std::mutex> lock(m_data_mutex);
     if(m_keyframe_finder->allKeyFramesAvailable(is_h265)){
         return m_keyframe_finder->get_keyframe_data(is_h265);
     }
     return nullptr;
}

std::array<int, 2> RTPReceiver::sps_get_width_height(){
    std::lock_guard<std::mutex> lock(m_data_mutex);
    return m_keyframe_finder->sps_get_width_height();
}

void RTPReceiver::queue_data(const uint8_t* nalu_data,const std::size_t nalu_data_len)
{
    std::lock_guard<std::mutex> lock(m_data_mutex);
    NALU nalu(nalu_data,nalu_data_len,is_h265);
    if(m_keyframe_finder->allKeyFramesAvailable(is_h265)){
        if(!m_keyframe_finder->check_is_still_same_config_data(nalu)){
            config_has_changed_during_decode=true;
            return;
        }
        // If we have all config data, start storing video frames
        // We can drop things we don't need for decoding frames though
        if(nalu.is_config())return;
        if(nalu.is_aud())return;
        if(nalu.is_sei())return;
        if(nalu.is_dps())return;
        // recalculate rough fps in X seconds intervalls:
        m_estimate_fps_calculator.on_new_frame();
        if(m_estimate_fps_calculator.time_since_last_recalculation()>std::chrono::seconds(2)){
            const auto fps=m_estimate_fps_calculator.recalculate_fps_and_clear();
            const auto fps_as_string=StringHelper::to_string_with_precision(fps,2)+"fps";
            DecodingStatistcs::instance().set_estimate_rtp_fps({fps_as_string.c_str()});
        }
        if(m_data.size()>MAX_DATA_QUEUE_SIZE){
            // The decoder cannot keep up with the incoming stream, this should never happen - if it happens,
            // The easiest thing to do is to just drop this frame (we cannot just remove the oldest, not yet fed frame from the queue,
            // since h264 relies on previus frames.
            n_dropped_frames++;
            qDebug()<<"Dropping incoming frame, total:"<<n_dropped_frames;
            return;
        }
        m_data.push(std::make_shared<NALU>(nalu));
        return;
    }
    // We don't have all config data yet, drop anything that is not config data.
    m_keyframe_finder->saveIfKeyFrame(nalu);
}

void RTPReceiver::udp_raw_data_callback(const uint8_t *payload, const std::size_t payloadSize)
{
    //qDebug()<<"Got UDP data "<<payloadSize;
    /*if(m_packet_drop_emulator.drop_packet()){
        qDebug()<<"Emulate - Dropping packet";
        return;
    }*/
    m_rtp_bitrate.addBytes(payloadSize,[](std::string bitrate){
        DecodingStatistcs::instance().set_rtp_measured_bitrate(bitrate.c_str());
    });
    if(is_h265){
        m_rtp_decoder->parseRTPH265toNALU(payload,payloadSize);
        //m_rtp_decoder->parse_rtp_mjpeg(payload,payloadSize);
    }else{
        m_rtp_decoder->parseRTPH264toNALU(payload,payloadSize);
    }
}

void RTPReceiver::nalu_data_callback(const std::chrono::steady_clock::time_point creation_time,const uint8_t *nalu_data, const int nalu_data_size)
{
    //qDebug()<<"Got NALU "<<nalu_data_size;
    {
        //std::vector<uint8_t> tmp(nalu_data,nalu_data+nalu_data_size);
        //qDebug()<<StringHelper::vectorAsString(tmp).c_str()<<"\n";
    }
    if(m_out_file!=nullptr){
        m_out_file->write((const char*)nalu_data,nalu_data_size);
        m_out_file->flush();
    }
    queue_data(nalu_data,nalu_data_size);
}

