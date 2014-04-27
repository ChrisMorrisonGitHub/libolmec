/* Copyright (C) 2012, Chris Morrison <chris-morrison@cyberservices.com>
 *
 * libolmec.h
 *
 * This file is part of libolmec.
 *
 * libolmec is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libolm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libolmec.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef libolmec_h
#define libolmec_h

#define _FILE_OFFSET_BITS 64
#define _DARWIN_USE_64_BIT_INODE

#define INVALID_OLM_FILE                         NULL
#define INVALID_OLM_MESSAGE                      0

/* Errors. */
#define OLM_ERROR_SUCCESS                        0x00
#define OLM_ERROR_INVALID_PARAMETER              0x01
#define OLM_ERROR_NOT_OLM_FILE                   0x02
#define OLM_ERROR_FILE_IO_ERROR                  0x03
#define OLM_ERROR_FILE_CORRUPTED                 0x04
#define OLM_ERROR_NO_MEMORY                      0x05
#define OLM_ERROR_INVALID_FILE_HANDLE            0x06
#define OLM_ERROR_MESSAGE_CORRUPTED              0x07
#define OLM_ERROR_ATTACHMENT_CORRUPTED           0X08
#define OLM_ERROR_ATTACHMENT_NOT_FOUND           0x09

#define MESSAGE_PRIORITY_HIGHEST                 1
#define MESSAGE_PRIORITY_HIGH                    2
#define MESSAGE_PRIORITY_NORMAL                  3
#define MESSAGE_PRIORITY_LOW                     4
#define MESSAGE_PRIORITY_LOWEST                  5

#define OLM_OPT_IGNORE_ERRORS                    0x01

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque type for the olm file descriptor. */
typedef struct olm_file_t olm_file_t;

typedef struct _attch
{
    char *__private;
    char *filename;
    char *extension;
    char *content_type;
    uint64_t file_size;
} olm_attachment_t;

typedef struct _mail_message
{
    char *to;
    char *from;
    char *reply_to;
    char *subject;
    char *message_id;
    char *body;
    time_t sent_time;
    time_t received_time;
    time_t modified_time;
    int has_html;
    int has_rich_text;
    int message_priority;
    unsigned long attachment_count;
    olm_attachment_t **attachment_list;
} olm_mail_message_t;

olm_file_t          *olm_open_file(const char *olm_filename, int opts, int *error_code);
olm_mail_message_t  *olm_get_message_at(olm_file_t *file, uint64_t index, int *error_code);
uint64_t             olm_mail_message_count(olm_file_t *file);
int                  olm_extract_and_save_attachment(olm_file_t *file, olm_attachment_t* attachment, const char *dest_path);
void                 olm_message_free(olm_mail_message_t *message);
void                 olm_close_file(olm_file_t *file);
    
#ifdef __cplusplus
}
#endif

#endif
