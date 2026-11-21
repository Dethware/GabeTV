#include "ImaAdpcm.h"

#include <QtGlobal>

#include <array>

namespace {

constexpr std::array<int, 89> StepTable = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

constexpr std::array<int, 8> IndexTable = {-1, -1, -1, -1, 2, 4, 6, 8};

} // namespace

void ImaAdpcmDecoder::reset()
{
    m_predictor = 0;
    m_stepIndex = 0;
}

void ImaAdpcmDecoder::decode(QByteArrayView data, QList<qint16> &out)
{
    out.reserve(out.size() + data.size() * 2);
    for (char byte : data) {
        const auto value = quint8(byte);
        out.append(qint16(decodeCode(value >> 4)));
        out.append(qint16(decodeCode(value & 0x0F)));
    }
}

int ImaAdpcmDecoder::decodeCode(int code)
{
    const int step = StepTable[m_stepIndex];
    int diff = step >> 3;
    if (code & 4)
        diff += step;
    if (code & 2)
        diff += step >> 1;
    if (code & 1)
        diff += step >> 2;
    m_predictor = qBound(-32768, (code & 8) ? m_predictor - diff : m_predictor + diff, 32767);
    m_stepIndex = qBound(0, m_stepIndex + IndexTable[code & 7], 88);
    return m_predictor;
}
