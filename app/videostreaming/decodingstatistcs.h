#ifndef DECODINGSTATISTCS_H
#define DECODINGSTATISTCS_H

#include <QObject>

//#include <lqtutils_prop.h>
#include "../../lib/lqtutils_master/lqtutils_prop.h"

/**
 * @brief Simple QT model (signals) to expose QOpenHD decoding statistics to the UI.
 * singleton, corresponding qt name is "_decodingStatistics" (see main)
 * NOTE: r.n unused, and even if we use it, only data that is created by QOpenHD shuld land here -
 * .e.g the Bitrate the HW decoder reports, or similar.
 * Data generated by OpenHD itself should (e.g. the encoder stats from the air pi) should be handled by ../openhd_systems
 */
class DecodingStatistcs : public QObject
{
    Q_OBJECT
    L_RW_PROP(QString, decode_time, set_decode_time, "?")
    L_RW_PROP(QString, decode_and_render_time, set_decode_and_render_time, "?")
    L_RW_PROP(int, n_dropped_frames, set_n_dropped_frames, -1)
    L_RW_PROP(int, n_rendered_frames, set_n_rendered_frames, -1)
public:
    explicit DecodingStatistcs(QObject *parent = nullptr);
    static DecodingStatistcs& instance();
public:
    Q_PROPERTY(int bitrate MEMBER m_bitrate WRITE set_bitrate NOTIFY bitrate_changed)
    void set_bitrate(int bitrate);
signals:
    void bitrate_changed(int bitrate);
public:
    int m_bitrate=100;
};

#endif // DECODINGSTATISTCS_H
