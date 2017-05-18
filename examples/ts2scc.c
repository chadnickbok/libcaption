/**********************************************************************************************/
/* The MIT License                                                                            */
/*                                                                                            */
/* Copyright 2016-2017 Twitch Interactive, Inc. or its affiliates. All Rights Reserved.       */
/*                                                                                            */
/* Permission is hereby granted, free of charge, to any person obtaining a copy               */
/* of this software and associated documentation files (the "Software"), to deal              */
/* in the Software without restriction, including without limitation the rights               */
/* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell                  */
/* copies of the Software, and to permit persons to whom the Software is                      */
/* furnished to do so, subject to the following conditions:                                   */
/*                                                                                            */
/* The above copyright notice and this permission notice shall be included in                 */
/* all copies or substantial portions of the Software.                                        */
/*                                                                                            */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR                 */
/* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,                   */
/* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE                */
/* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER                     */
/* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,              */
/* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN                  */
/* THE SOFTWARE.                                                                              */
/**********************************************************************************************/
#include "avc.h"
#include "ts.h"
#include <math.h>
#include <stdio.h>

#define DEFAULT_FPS 30
#define FRAMES_PER_MS (DEFAULT_FPS / 1000.0)
#define DEFAULT_TYPE cc_type_ntsc_cc_field_1

static inline void _crack_time(double tt, int* hh, int* mm, int* ss, int* ms)
{
    (*ms) = (int)((int64_t)(tt * 1000) % 1000);
    (*ss) = (int)((int64_t)(tt) % 60);
    (*mm) = (int)((int64_t)(tt / (60)) % 60);
    (*hh) = (int)((int64_t)(tt / (60 * 60)));
}

int main(int argc, char** argv)
{
    const char* path = argv[1];

    ts_t ts;
    h26x_t h26x;
    cea708_t cea708;
    uint8_t pkt[TS_PACKET_SIZE];
    ts_init(&ts);
    h26x_init(&h26x);

    FILE* file = fopen(path, "rb+");

    fprintf(stderr, "Scenarist_SCC V1.0\n\n");

    while (TS_PACKET_SIZE == fread(&pkt[0], 1, TS_PACKET_SIZE, file)) {
        switch (ts_parse_packet(&ts, &pkt[0])) {
        case LIBCAPTION_OK:
            // fprintf (stderr,"read ts packet\n");
            break;

        case LIBCAPTION_READY: {
            // fprintf (stderr,"read ts packet DATA\n");
            while (ts.size) {
                // fprintf (stderr,"ts.size %d (%02X%02X%02X%02X)\n",ts.size, ts.data[0], ts.data[1], ts.data[2], ts.data[3]);

                switch (h26x_parse(&h26x, &ts.data, &ts.size)) {
                case LIBCAPTION_OK:
                    break;

                case LIBCAPTION_ERROR:
                    // fprintf (stderr,"LIBCAPTION_ERROR == h26x_parse_annexb()\n");
                    h26x_init(&h26x);
                    break;

                case LIBCAPTION_READY: {

                    int ccidx;
                    int cc_count = 0;
                    int hh, mm, ss, ms;
                    _crack_time(ts_dts_seconds(&ts), &hh, &mm, &ss, &ms);

                    if (STREAM_TYPE_H264 == ts.type && H264_NALU_TYPE_SEI == h264_type(&h26x)) {
                        // fprintf (stderr,"NALU %d (%d)\n", h26x_type (&h26x), h26x_size (&h26x));
                        sei_t sei;
                        sei_message_t* msg;
                        sei_init(&sei);
                        sei_parse_avcnalu(&sei, &h26x, ts_dts_seconds(&ts), ts_cts_seconds(&ts));

                        for (msg = sei_message_head(&sei); msg; msg = sei_message_next(msg)) {
                            cea708_init(&cea708);
                            sei_decode_cea708(msg, &cea708);

                            for (ccidx = 0; ccidx < cea708_cc_count(&cea708.user_data); ++ccidx) {
                                int valid;
                                cea708_cc_type_t type;
                                uint16_t cc = cea708_cc_data(&cea708.user_data, ccidx, &valid, &type);

                                if (valid && DEFAULT_TYPE == type) {
                                    if (0 == cc_count) {
                                        fprintf(stderr, "%02d:%02d:%02d:%02d", hh, mm, ss, (int)lround(FRAMES_PER_MS * ms));
                                    }

                                    ++cc_count;
                                    fprintf(stderr, " %02X%02X", (uint8_t)(cc >> 8), (uint8_t)(cc));
                                }
                            }
                        }
                    }

                    if (STREAM_TYPE_H262 == ts.type && H262_NALU_TYPE_USER_DATA == h262_type(&h26x)) {
                        cea708_parse_h262(h26x_data(&h26x), h26x_size(&h26x), &cea708);

                        for (ccidx = 0; ccidx < cea708_cc_count(&cea708.user_data); ++ccidx) {
                            int valid;
                            cea708_cc_type_t type;
                            uint16_t cc = cea708_cc_data(&cea708.user_data, ccidx, &valid, &type);

                            if (valid && DEFAULT_TYPE == type) {
                                if (0 == cc_count) {
                                    fprintf(stderr, "%02d:%02d:%02d:%02d", hh, mm, ss, (int)lround(FRAMES_PER_MS * ms));
                                }

                                ++cc_count;
                                fprintf(stderr, " %02X%02X", (uint8_t)(cc >> 8), (uint8_t)(cc));
                            }
                        }
                    }

                    if (0 < cc_count) {
                        fprintf(stderr, "\n\n");
                    }

                    h26x_init(&h26x);
                } break;
                }
            }
        } break;

        case LIBCAPTION_ERROR:
            // fprintf (stderr,"read ts packet ERROR\n");
            break;
        }
    }

    return 1;
}
