#pragma once

#include <QByteArrayView>
#include <QList>

// IMA ADPCM decoder for the remote's voice audio: 4-bit codes, high nibble first, 16-bit mono.
// One decoder per recording; the remote sends no sync messages, so each recording starts from
// predictor 0 / step index 0 and is decoded as one continuous stream.
class ImaAdpcmDecoder
{
public:
    void reset();
    // Appends the decoded samples of `data` to `out`.
    void decode(QByteArrayView data, QList<qint16> &out);

private:
    int decodeCode(int code);

    int m_predictor = 0;
    int m_stepIndex = 0;
};
