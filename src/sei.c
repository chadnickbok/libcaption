/**********************************************************************************************/
/* Copyright 2016-2016 Twitch Interactive, Inc. or its affiliates. All Rights Reserved.       */
/*                                                                                            */
/* Licensed under the Apache License, Version 2.0 (the "License"). You may not use this file  */
/* except in compliance with the License. A copy of the License is located at                 */
/*                                                                                            */
/*     http://aws.amazon.com/apache2.0/                                                       */
/*                                                                                            */
/* or in the "license" file accompanying this file. This file is distributed on an "AS IS"    */
/* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the    */
/* License for the specific language governing permissions and limitations under the License. */
/**********************************************************************************************/

#include "sei.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
////////////////////////////////////////////////////////////////////////////////
// AVC RBSP Methods
//  TODO move the to a avcutils file
static size_t _find_emulation_prevention_byte (const uint8_t* data, size_t size)
{
    size_t offset = 2;

    while (offset < size) {
        if (0 == data[offset]) {
            // 0 0 X 3 //; we know X is zero
            offset += 1;
        } else if (3 != data[offset]) {
            // 0 0 X 0 0 3; we know X is not 0 and not 3
            offset += 3;
        } else if (0 != data[offset-1]) {
            // 0 X 0 0 3
            offset += 2;
        } else if (0 != data[offset-2]) {
            // X 0 0 3
            offset += 1;
        } else {
            // 0 0 3
            return offset;
        }
    }

    return size;
}

static size_t _copy_to_rbsp (uint8_t* destData, size_t destSize, const uint8_t* sorcData, size_t sorcSize)
{
    size_t toCopy, totlSize = 0;

    for (;;) {
        if (destSize >= sorcSize) {
            return 0;
        }

        // The following line IS correct! We want to look in sorcData up to destSize bytes
        // We know destSize is smaller than sorcSize because of the previous line
        toCopy = _find_emulation_prevention_byte (sorcData,destSize);
        memcpy (destData, sorcData, toCopy);
        totlSize += toCopy;
        destData += toCopy;
        destSize -= toCopy;

        if (0 == destSize) {
            return totlSize;
        }

        // skip the emulation prevention byte
        totlSize += 1;
        sorcData += toCopy + 1;
        sorcSize -= toCopy + 1;
    }

    return 0;
}
////////////////////////////////////////////////////////////////////////////////
static inline size_t _find_emulated (uint8_t* data, size_t size)
{
    size_t offset = 2;

    while (offset < size) {
        if (3 < data[offset]) {
            // 0 0 X; we know X is not 0, 1, 2 or 3
            offset += 3;
        } else if (0 != data[offset-1]) {
            // 0 X 0 0 1
            offset += 2;
        } else if (0 != data[offset-2]) {
            // X 0 0 1
            offset += 1;
        } else {
            // 0 0 0, 0 0 1
            return offset;
        }
    }

    return size;
}

size_t _copy_from_rbsp (uint8_t* data, uint8_t* payloadData, size_t payloadSize)
{
    size_t total = 0;

    while (payloadSize) {
        size_t bytes = _find_emulated (payloadData,payloadSize);

        if (bytes > payloadSize) {
            return 0;
        }

        memcpy (data, payloadData, bytes);

        if (bytes == payloadSize) {
            return total + bytes;
        }

        data[bytes] = 3; // insert emulation prevention byte
        data += bytes + 1; total += bytes + 1;
        payloadData += bytes; payloadSize -= bytes;
    }

    return total;
}
////////////////////////////////////////////////////////////////////////////////
struct _sei_message_t {
    double pts;
    size_t size;
    sei_msgtype_t type;
    struct _sei_message_t* next;
};

sei_message_t* sei_message_next (sei_message_t* msg) { return ( (struct _sei_message_t*) msg)->next; }
sei_msgtype_t  sei_message_type (sei_message_t* msg) { return ( (struct _sei_message_t*) msg)->type; }
size_t         sei_message_size (sei_message_t* msg) { return ( (struct _sei_message_t*) msg)->size; }
double         sei_message_pts (sei_message_t* msg) { return ( (struct _sei_message_t*) msg)->pts; }
uint8_t*       sei_message_data (sei_message_t* msg) { return ( (uint8_t*) msg) + sizeof (struct _sei_message_t); }
void           sei_message_free (sei_message_t* msg) { if (msg) { free (msg); } }



sei_message_t* sei_message_new (sei_msgtype_t type, uint8_t* data, size_t size, double pts)
{
    struct _sei_message_t* msg = (struct _sei_message_t*) malloc (sizeof (struct _sei_message_t) + size);
    msg->next = 0; msg->type = type; msg->size = size; msg->pts = pts;

    if (data) {
        memcpy (sei_message_data (msg), data, size);
    } else {
        memset (sei_message_data (msg), 0, size);
    }

    return (sei_message_t*) msg;
}

sei_message_t* sei_message_take_head (sei_t* sei)
{
    sei_message_t* head = sei_message_head (sei);

    if (0 != head && sei_message_pts (head) < sei->dts) {
        return 0;
    }

    // return the first message iff its pts is less that most recent dts
    sei->head = head->next;
    head->next = 0;
    return head;
}

////////////////////////////////////////////////////////////////////////////////
void sei_init (sei_t* sei)
{
    sei->dts = -1;
    sei->head = 0;
}

void sei_insert_messages (sei_t* sei, sei_message_t* msg)
{
    if (0 == sei->head || msg->pts < sei->head->pts) {
        // insert as first element
        msg->next = sei->head;
        sei->head = msg;
        return;
    } else {
        sei_message_t* next;

        // walk the list looking for the correct position
        // thi is O(n), so the list should be kept small!
        for (next = sei->head ; next ; next = next->next) {
            if (0 == next->next || msg->pts < next->next->pts) {
                msg->next = next->next;
                next->next = msg;
                return;
            }
        }

        // error if we get here
        assert (0);
    }
}

void sei_free (sei_t* sei)
{
    sei_message_t* tail;

    while (sei->head) {
        tail = sei->head->next;
        free (sei->head);
        sei->head = tail;
    }

    sei_init (sei);
}

void sei_dump (sei_t* sei)
{
    fprintf (stderr,"SEI %p\n", sei);
    sei_dump_messages (sei->head);
}

void sei_dump_messages (sei_message_t* head)
{
    sei_message_t* msg;

    for (msg = head ; msg ; msg = sei_message_next (msg)) {
        uint8_t* data = sei_message_data (msg);
        size_t size =  sei_message_size (msg);
        fprintf (stderr,"-- Message %p\n-- Message Type: %d\n-- Message Size: %d\n", data, sei_message_type (msg), (int) size);

        while (size) {
            fprintf (stderr,"%02X ", *data);
            ++data; --size;
        }

        fprintf (stderr,"\n");
    }
}

////////////////////////////////////////////////////////////////////////////////
size_t sei_render_size (sei_t* sei)
{
    size_t size = 2;
    sei_message_t* msg;

    for (msg = sei->head ; msg ; msg = sei_message_next (msg)) {
        size += 1 + (msg->type / 255);
        size += 1 + (msg->size / 255);
        size += 1 + (msg->size * 4/3);
    }

    return size;
}

// we can safely assume sei_render_size() bytes have been allocated for data
size_t sei_render (sei_t* sei, uint8_t* data)
{
    size_t escaped_size, size = 2; // nalu_type + stop bit
    sei_message_t* msg;
    (*data) = 6; ++data;

    for (msg = sei->head ; msg ; msg = sei_message_next (msg)) {
        int payloadType      = sei_message_type (msg);
        int payloadSize      = sei_message_size (msg);
        uint8_t* payloadData = sei_message_data (msg);

        while (255 < payloadType) {
            (*data) = 255;
            ++data; ++size;
            payloadType /= 255;
        }

        (*data) = payloadType;
        ++data; ++size;

        while (255 < payloadSize) {
            (*data) = 255;
            ++data; ++size;
            payloadSize /= 255;
        }

        (*data) = payloadSize;
        ++data; ++size;

        if (0 >= (escaped_size = _copy_from_rbsp (data,payloadData,payloadSize))) {
            return 0;
        }

        data += escaped_size;
        size += escaped_size;
    }

    // write stop bit and return
    (*data) = 0x80;
    return size;
}

uint8_t* sei_render_alloc (sei_t* sei, size_t* size)
{
    size_t aloc = sei_render_size (sei);
    uint8_t* data = malloc (aloc);
    (*size) = sei_render (sei, data);
    return data;
}

////////////////////////////////////////////////////////////////////////////////
int sei_parse_nalu (sei_t* sei, const uint8_t* data, size_t size, double pts, double dts)
{
    assert (pts >= dts); // cant present before decode
    assert (dts >= sei->dts); // dts MUST to monotonic
    sei->dts = dts;
    int ret = 0;

    if (0 == data || 0 == size) {
        return 0;
    }

    uint8_t nal_unit_type = (*data) & 0x1F;
    ++data; --size;

    if (6 != nal_unit_type) {
        return 0;
    }

    // SEI may contain more than one payload
    while (1<size) {
        int payloadType = 0;
        int payloadSize = 0;

        while (0 < size && 0xFF == (*data)) {
            payloadType += 255;
            ++data; --size;
        }

        if (0 == size) {
            goto error;
        }

        payloadType += (*data);
        ++data; --size;

        while (0 < size && 0xFF == (*data)) {
            payloadSize += 255;
            ++data; --size;
        }

        if (0 == size) {
            goto error;
        }

        payloadSize += (*data);
        ++data; --size;

        if (payloadSize) {
            sei_message_t* msg = sei_message_new ( (sei_msgtype_t) payloadType, 0, payloadSize, pts);
            uint8_t* payloadData = sei_message_data (msg);
            size_t bytes = _copy_to_rbsp (payloadData, payloadSize, data, size);
            sei_insert_messages (sei, msg);

            if (bytes < payloadSize) {
                goto error;
            }

            data += bytes; size -= bytes;
            ++ret;
        }
    }

    // There should be one trailing byte, 0x80. But really, we can just ignore that fact.

    return ret;
error:
    sei_free (sei);
    return 0;
}
////////////////////////////////////////////////////////////////////////////////
static int sei_message_decode (sei_message_t* msg, caption_frame_t* frame)
{
    int i;
    cea708_t cea708;
    cea708_init (&cea708);

    if (sei_type_user_data_registered_itu_t_t35 == sei_message_type (msg)) {
        cea708_parse (sei_message_data (msg), sei_message_size (msg), &cea708);
        return cea708_to_caption_frame (frame, &cea708, msg->pts);
    }

    return 0;
}

int sei_to_caption_frame (caption_frame_t* frame, sei_t* sei)
{
    sei_message_t* msg;

    for (msg = sei_message_head (sei) ; msg ; msg = sei_message_next (msg)) {
        sei_message_decode (msg,frame);
    }

    return 1;
}

////////////////////////////////////////////////////////////////////////////////
#define DEFAULT_CHANNEL 0

// This should be moved to 708.c
// This works for popon, but bad for paint on and roll up
// Please understand this function before you try to use it, setting null values have different effects than you may assume
sei_message_t* sei_encode_eia608 (sei_message_t* sei, cea708_t* cea708, uint16_t cc_data, double pts)
{
    if (!sei) {
        // Getting here with cc_data > 0 is an error
        cea708_init (cea708); // will confgure using HLS compatiable defaults
        sei = sei_message_new (sei_type_user_data_registered_itu_t_t35, 0, CEA608_MAX_SIZE, pts);
        cea708_add_cc_data (cea708, 1, cc_type_ntsc_cc_field_1, eia608_control_command (eia608_control_resume_caption_loading, DEFAULT_CHANNEL));
        cea708_add_cc_data (cea708, 1, cc_type_ntsc_cc_field_1, eia608_control_command (eia608_control_resume_caption_loading, DEFAULT_CHANNEL));
        cea708_add_cc_data (cea708, 1, cc_type_ntsc_cc_field_1, eia608_control_command (eia608_control_erase_display_memory, DEFAULT_CHANNEL));
        cea708_add_cc_data (cea708, 1, cc_type_ntsc_cc_field_1, eia608_control_command (eia608_control_end_of_caption, DEFAULT_CHANNEL));
        cea708_add_cc_data (cea708, 1, cc_type_ntsc_cc_field_1, eia608_control_command (eia608_control_resume_caption_loading, DEFAULT_CHANNEL));
        return sei;
    }

    // This one is full, flush and init a new one
    if (31 == cea708->user_data.cc_count) {
        sei->size = cea708_render (cea708, sei_message_data (sei), sei_message_size (sei));
        sei->next = sei_message_new (sei_type_user_data_registered_itu_t_t35, 0, CEA608_MAX_SIZE, pts);
        cea708_init (cea708); // will confgure using HLS compatiable defaults
        return sei_encode_eia608 (sei->next, cea708, cc_data, pts);
    }

    if (0 == cea708->user_data.cc_count) {
        cea708_add_cc_data (cea708, 1, cc_type_ntsc_cc_field_1, eia608_control_command (eia608_control_resume_caption_loading, DEFAULT_CHANNEL));
    }

    if (0 == cc_data) {
        cea708_add_cc_data (cea708, 1, cc_type_ntsc_cc_field_1, eia608_control_command (eia608_control_end_of_caption, DEFAULT_CHANNEL));
        sei->size = cea708_render (cea708, sei_message_data (sei), sei_message_size (sei));
        return sei;
    }

    cea708_add_cc_data (cea708, 1, cc_type_ntsc_cc_field_1, cc_data);
    return sei;
}
////////////////////////////////////////////////////////////////////////////////
int sei_from_caption_frame (sei_t* sei, caption_frame_t* frame)
{
    int r,c;
    const char* data;
    uint16_t prev_cc_data;

    cea708_t cea708;
    sei_message_t* msg = sei_encode_eia608 (0,&cea708,0,frame->str_pts);
    sei_message_t* head = msg;

    for (r=0; r<SCREEN_ROWS; ++r) {
        // Calculate preamble
        for (c=0; c<SCREEN_COLS && 0 == *caption_frame_read_char (frame,r,c,0,0) ; ++c) {}

        // This row is blank
        if (SCREEN_COLS == c) {
            continue;
        }

        // Write preamble
        msg = sei_encode_eia608 (msg, &cea708, eia608_row_column_pramble (r,c,DEFAULT_CHANNEL,0), frame->str_pts);
        int tab = c % 4;

        if (tab) {
            msg = sei_encode_eia608 (msg, &cea708, eia608_tab (tab,DEFAULT_CHANNEL), frame->str_pts);
        }

        // Write the row
        for (prev_cc_data = 0, data = caption_frame_read_char (frame,r,c,0,0) ;
                (*data) && c < SCREEN_COLS ; ++c, data = caption_frame_read_char (frame,r,c,0,0)) {
            uint16_t cc_data = eia608_from_utf8_1 (data,DEFAULT_CHANNEL);

            if (!cc_data) {
                // We do't want to write bad data, so just ignore it.
            } else if (eia608_is_basicna (prev_cc_data)) {
                if (eia608_is_basicna (cc_data)) {
                    // previous and current chars are both basicna, combine them into current
                    msg = sei_encode_eia608 (msg, &cea708, eia608_from_basicna (prev_cc_data,cc_data), frame->str_pts);
                } else {
                    // previous was basic na, but current isnt; write previous and current
                    msg = sei_encode_eia608 (msg, &cea708, prev_cc_data, frame->str_pts);
                    msg = sei_encode_eia608 (msg, &cea708, cc_data, frame->str_pts);
                }

                prev_cc_data = 0; // previous is handled, we can forget it now
            } else if (eia608_is_basicna (cc_data)) {
                prev_cc_data = cc_data;
            } else {
                msg = sei_encode_eia608 (msg, &cea708, cc_data, frame->str_pts);
            }
        }

        if (0 != prev_cc_data) {
            msg = sei_encode_eia608 (msg, &cea708, prev_cc_data, frame->str_pts);
        }
    }

    msg = sei_encode_eia608 (msg, &cea708, eia608_control_command (eia608_control_end_of_caption, DEFAULT_CHANNEL), frame->str_pts);
    msg = sei_encode_eia608 (msg, &cea708, 0, frame->str_pts); // flush

    // Write messages to sei
    sei_init (sei);
    sei->dts = frame->str_pts; // assumes in order frames
    sei_insert_messages (sei,head);
    return 1;
}
