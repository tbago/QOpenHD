#ifndef TEXTURERENDERER_H
#define TEXTURERENDERER_H


#include <QObject>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QOpenGLTexture>
#include <memory>
#include <chrono>
#include <mutex>

#include "gl/gl_videorenderer.h"

#include "../common_consti/TimeHelper.hpp"

class TextureRenderer : public QObject
{
    Q_OBJECT
public:
    // DIRTY, FIXME
    static TextureRenderer& instance();

    void setViewportSize(const QSize &size) { m_viewportSize = size; }
    void setWindow(QQuickWindow *window) { m_window = window; }
    // create and link the shaders
    void initGL();
    // draw function
    void paint();
    //
    int queue_new_frame_for_display(AVFrame * src_frame);
private:
    QSize m_viewportSize;
    int m_index=0;
    QQuickWindow *m_window = nullptr;
    std::chrono::steady_clock::time_point last_frame=std::chrono::steady_clock::now();
    //
    std::unique_ptr<GL_VideoRenderer> gl_video_renderer=nullptr;
    //
    bool initialized=false;
    int renderCount=0;
private:
    std::mutex latest_frame_mutex;
    AVFrame* m_latest_frame=nullptr;
    AVFrame* fetch_latest_decoded_frame();
private:
    struct DisplayStats{
        int n_frames_rendered=0;
        int n_frames_dropped=0;
        // Delay between frame was given to the egl render <-> we uploaded it to the texture (if not dropped)
        //AvgCalculator delay_until_uploaded{"Delay until uploaded"};
        // Delay between frame was given to the egl renderer <-> swap operation returned (it is handed over to the hw composer)
        //AvgCalculator delay_until_swapped{"Delay until swapped"};
        AvgCalculator decode_and_render{"Decode and render"}; //Time picked up by GL Thread
      };
    DisplayStats m_display_stats;
    bool dev_draw_alternating_rgb_dummy_frames=false;
};

#endif // TEXTURERENDERER_H
