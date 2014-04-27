/* Copyright (C) 2012, Chris Morrison <chris-morrison@cy.com>
 *
 * libolmec.c
 *
 * This file is part of libolm.
 *
 * libolmec is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libolmec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libolmec.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>
#include <time.h>
#include <libxml/xmlreader.h>
#include <simclist.h>
#include "private.h"
#include "libolmec.h"

int get_eocd_record(olm_file_t *zipfile, eocd_record32 *eocd_record);
int get_eocdr64_locator(olm_file_t *zipfile, eocdr_locator64 *eocdr_locator, size_t comment_length);
int get_eocd64_record(olm_file_t *zipfile, eocd_record64 *eocd_record, off_t search_offset);
internal_archive_entry_data *read_next_entry_from_central_dir(olm_file_t *file, int *error);
int is_message(internal_archive_entry_data *entry);
int is_attachment(internal_archive_entry_data *entry);
int ends_with_attachment_suffix(const char *filename);
void free_entry_from_central_dir(internal_archive_entry_data *entry);
ssize_t read_out_extra_field(olm_file_t *zf, extra_field_header *buffer);
int parse_element_names(xmlNode * a_node, olm_mail_message_t *message);
ssize_t index_of_last(const char *str, char c);
char *allocate_block_for_buffer(size_t buff_size, size_t *block_size, size_t *block_count);

/******************************************************************************************************************************
 * Opens an OLM file for reading.
 *-----------------------------------------------------------------------------------------------------------------------------
 * Parameters:
 *
 *   olm_filename   The full pathname of the OLM file to be opened. Cannot be NULL.
 *   opts           The options to be applied when opening this file. See libolmec.h
 *   error_code     Pointer to a variable to hold the error code in the event that the call fails. Cannot be NULL.
 *
 * Returns:
 *
 *   An olm_file_t object or NULL if the call failed. The OLM file must be closed using olm_close_file() when you have
 *   finished with it.
 ******************************************************************************************************************************/
olm_file_t *olm_open_file(const char *olm_filename, int opts, int *error_code)
{
    off_t file_size = 0;
    struct stat stat_buff;
    uint32_t signature = 0;
    olm_file_t *file = NULL;
    internal_archive_entry_data *entry_buff = NULL;
    int magic_entries_found = 0;
    
    // The most likley cause of failure.
    *error_code = OLM_ERROR_FILE_CORRUPTED;
    
    /* Do a size test on the file. */
    if (stat(olm_filename, &stat_buff) == -1)
    {
        *error_code = OLM_ERROR_FILE_IO_ERROR;
        return INVALID_OLM_FILE;
    }
    file_size = stat_buff.st_size;
    if (file_size == 145)
    {
        *error_code = OLM_ERROR_FILE_CORRUPTED;
        return INVALID_OLM_FILE;
    }
    
    /* === First allocate some memory for the OLM_FILE descriptor that this fuction will return. === */
    file = (olm_file_t *)malloc(sizeof (olm_file_t));
    if (file == NULL)
    {
        *error_code = OLM_ERROR_NO_MEMORY;
        return INVALID_OLM_FILE;
    }
    memset(file, 0, sizeof (olm_file_t));
    
    /* Store the filename/char object. */
    file->filename = (char *) malloc(strlen(olm_filename) + 1);
    if (file->filename == NULL)
    {
        *error_code = OLM_ERROR_NO_MEMORY;
        goto bail_and_die;
    }
    memset(file->filename, 0, strlen(olm_filename) + 1);
    strncpy(file->filename, olm_filename, strlen(olm_filename));
    
    /* Now try to open the file. */
    file->file_seg = open(olm_filename, O_RDONLY);
    if (file->file_seg == -1)
    {
        *error_code = OLM_ERROR_FILE_IO_ERROR;
        goto bail_and_die;
    }
    
    /* Look for the magic number in the first four bytes of the file. */
    if (read(file->file_seg, &signature, 4) != 4)
    {
        *error_code = OLM_ERROR_FILE_IO_ERROR;
        goto bail_and_die;
    }
    if (signature != SIG_LOCAL_FILE_HEADER)
    {
        *error_code = OLM_ERROR_NOT_OLM_FILE;
        goto bail_and_die;
    }
    
    /* Now sniff for the end of central directory record that should be found 22 bytes from the end of the file (unless a comment is present). */
    if (get_eocd_record(file, &file->eocd_rec32) == false)
    {
        *error_code = OLM_ERROR_NOT_OLM_FILE;
        goto bail_and_die;
    }
    
    /* Get the data we need. */
    file->central_dir_size = file->eocd_rec32.central_dir_size;
    file->central_dir_offset = file->eocd_rec32.central_dir_offset;
    
    /* Now look for a ZIP64 EOCDR locator record. */
    memset(&file->eocd_loc64, 0, sizeof (file->eocd_loc64));
    /* Check if we have a zip64 archive. */
    if (get_eocdr64_locator(file, &file->eocd_loc64, file->eocd_rec32.comment_length) == true)
    {
        file->zip64 = true;
        file->total_segments = file->eocd_loc64.total_segments;
        file->current_segment = file->eocd_loc64.total_segments;
    }
    else
    {
        file->zip64 = false;
        file->total_segments = file->eocd_rec32.this_segment + 1;
        file->current_segment = file->eocd_rec32.this_segment + 1;
        file->total_entries = file->eocd_rec32.total_entries;
    }
    
    /* Ontain the ZIP64 end of central directory if present. */
    if (file->zip64)
    {    
        memset(&file->eocd_rec64, 0, sizeof(file->eocd_rec64));
        if (get_eocd64_record(file, &file->eocd_rec64, (off_t)file->eocd_loc64.zip64_EOCDR_offset) == true)
        {
            file->total_entries = file->eocd_rec64.total_entries;
            file->central_dir_size = file->eocd_rec64.central_directory_size;
            file->central_dir_offset = (off_t)file->eocd_rec64.central_directory_offset;
        }
        else
        {
            /* If we could not get the ZIP64 end of central directory then we will not be able to continue. */
            *error_code = OLM_ERROR_FILE_CORRUPTED;
            goto bail_and_die;
        }
    }

    /* Now we must seek to, read out and parse the central directory. */
    if (lseek(file->file_seg, file->central_dir_offset, SEEK_SET) == -1)
    {
        *error_code = OLM_ERROR_FILE_IO_ERROR;
        goto bail_and_die;
    }
    
    /* Initialise the vectors. */
    list_init(&file->message_entries);
    list_init(&file->attachment_entries);
    list_init(&file->contact_entries);
    
    for (uint64_t idx = 0; idx < file->total_entries; idx++)
    {
        entry_buff = read_next_entry_from_central_dir(file, error_code);
        if (entry_buff == NULL) goto bail_and_die;
        
        if (strcmp(entry_buff->raw_entry_path, "Categories.xml") == 0)
        {
            magic_entries_found |= 4;
            free_entry_from_central_dir(entry_buff);
            continue;
        }
        
        if (strcmp(entry_buff->raw_entry_path, "Local/Address Book/Contacts.xml") == 0)
        {
            /* TODO: Process contacts. */
            free_entry_from_central_dir(entry_buff);
            continue;
        }
        
        /* Discard directories and invalid files.*/
        if (entry_buff->is_directory == true)
        {
            if (strncmp(entry_buff->raw_entry_path, "Accounts", 8) == 0) magic_entries_found |= 1;
            if (strncmp(entry_buff->raw_entry_path, "Local", 5) == 0) magic_entries_found |= 2;
            free_entry_from_central_dir(entry_buff);
            continue;
        }
        
        /* Look for and store messages. */
        if (is_message(entry_buff) == true)
        {
            list_append(&file->message_entries, entry_buff);
            continue;
        }
        if (is_attachment(entry_buff) == true)
        {
            list_append(&file->attachment_entries, entry_buff);
            continue;
        }
        
        /* Make sure anything we missed gets freed. */
        free_entry_from_central_dir(entry_buff);
    }
    
    if (magic_entries_found != 7)
    {
        *error_code = OLM_ERROR_NOT_OLM_FILE;
        goto bail_and_die;
    }
    
    /* Store the options. */
    file->options = opts;
        
    *error_code = OLM_ERROR_SUCCESS;
    return file;
    
bail_and_die:
    
    olm_close_file(file);
        
    return INVALID_OLM_FILE;
}

int is_message(internal_archive_entry_data *entry)
{
    size_t len = 0;
    char *last_four = NULL;
    
    if (entry == NULL) return false;
    if (entry->is_directory == true) return false;
    if (entry->filename == NULL) return false;
    if (entry->directory == NULL) return false;
    if (strncmp(entry->directory, "Local/com.microsoft.__Messages", 30) != 0) return false;
    if (strstr(entry->filename, "__message_attachment__") == NULL) return false;
    if (strstr(entry->directory, "com.microsoft.__Attachments") != NULL) return false;
    
    len = strlen(entry->filename) - 4;
    last_four = entry->filename + len;
    if (strcasecmp(last_four, ".xml") != 0) return false;
    
    return true;
}

int is_attachment(internal_archive_entry_data *entry)
{
    if (entry == NULL) return false;
    if (entry->is_directory == true) return false;
    if (entry->filename == NULL) return false;
    if (entry->directory == NULL) return false;
    if (strncmp(entry->directory, "Local/com.microsoft.__Messages", 30) != 0) return false;
    if (strstr(entry->filename, "__message_attachment__") != NULL) return false;
    if (strstr(entry->directory, "com.microsoft.__Attachments") == NULL) return false;
    if (ends_with_attachment_suffix(entry->filename) == false) return false;
    
    return true;
}

int ends_with_attachment_suffix(const char *filename)
{
    char *end = NULL;
    
    if (filename == NULL) return false;
    end = strrchr(filename, (int)'_');
    if (end == NULL) return false;
    
    /* Lose the underscore. */
    ++end;
    while (end[0] != '\0')
    {
        if ((end[0] >= '0') && (end[0] <= '9'))
        {
            ++end;
            continue;
        }
        if (end[0] == '.')
        {
            ++end;
            continue;
        }
        
        return false;
    }
    
    return true;
}

olm_mail_message_t *olm_get_message_at(olm_file_t *file, uint64_t index, int *error_code)
{
    olm_mail_message_t *message = NULL;
    char *message_data = NULL;
    uint32_t local_header_sig = 0;
    uint16_t filename_len = 0;
    uint16_t extra_len = 0;
    char *data_buffer = NULL;
    xmlDoc *doc = NULL;
    xmlNode *root_node = NULL;
    size_t data_len = 0;
    
    message = (olm_mail_message_t *)malloc(sizeof(olm_mail_message_t));
    if (message == NULL)
    {
        *error_code = OLM_ERROR_NO_MEMORY;
        return INVALID_OLM_MESSAGE;
    }
    
    /* Set up the message block with plain vanilla values. */
    memset(message, 0, sizeof(olm_mail_message_t));
    
    /* Get the entry and process it. */
    internal_archive_entry_data *entry = (internal_archive_entry_data *)list_get_at(&file->message_entries, index);
    if (entry == NULL)
    {
        *error_code = OLM_ERROR_NO_MEMORY;
        goto bail_and_die;
    }
    printf("+ Getting data from file %s\n", entry->raw_entry_path);
    if (entry->compression_method != ZIP_CA_STORED)
    {
        *error_code = OLM_ERROR_MESSAGE_CORRUPTED;
        goto bail_and_die; /* OLM files should not use compression for messages. */
    }
    /* Allocate a buffer for the message data. */
    message_data = (char *)malloc(entry->entry_size);
    if (message_data == NULL)
    {
        *error_code = OLM_ERROR_NO_MEMORY;
        goto bail_and_die;
    }
    /* Now read it from the olm file. */
    if (lseek(file->file_seg, (off_t)entry->file_offset, SEEK_SET) == -1)
    {
        *error_code = OLM_ERROR_FILE_IO_ERROR;
        goto bail_and_die;
    }
    /* We must now skip the (redundant) local header. */
    /* all the following error (bar two) will due to an IO error. */
    *error_code = OLM_ERROR_FILE_IO_ERROR;
    if (read(file->file_seg, &local_header_sig, 4) != 4) goto bail_and_die;
    if (local_header_sig != SIG_LOCAL_FILE_HEADER)  /* Just to make sure. */
    {
        *error_code = OLM_ERROR_FILE_CORRUPTED;
        goto bail_and_die;
    }
    if (lseek(file->file_seg, 22, SEEK_CUR) == -1) goto bail_and_die;
    if ((read(file->file_seg, &filename_len, 2) != 2) || (read(file->file_seg, &extra_len, 2) != 2)) goto bail_and_die;
    if (lseek(file->file_seg, (filename_len + extra_len), SEEK_CUR) == -1) goto bail_and_die;
    /* Now get the actual data out. */
    data_buffer = (char *)malloc(entry->entry_size);
    if (data_buffer == NULL)
    {
        *error_code = OLM_ERROR_NO_MEMORY;
        goto bail_and_die;
    }
    if (read(file->file_seg, data_buffer, entry->entry_size) != entry->entry_size) goto  bail_and_die;
    /* Check the data. */
    if (crc32(0, data_buffer, entry->entry_size) != entry->crc32)
    {
        *error_code = OLM_ERROR_MESSAGE_CORRUPTED;
        goto bail_and_die;
    }
    /* Now read the XML. */
    if ((file->options & OLM_OPT_IGNORE_ERRORS) == OLM_OPT_IGNORE_ERRORS)
    {
        doc = xmlReadMemory(data_buffer, entry->entry_size, NULL, NULL, XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    }
    else
    {
        doc = xmlParseMemory(data_buffer, entry->entry_size);
    }
    *error_code = OLM_ERROR_MESSAGE_CORRUPTED;
    if (doc == NULL) goto bail_and_die;
    root_node = xmlDocGetRootElement(doc);
    if (root_node == NULL) goto bail_and_die;
    *error_code = parse_element_names(root_node, message);
    if (*error_code != OLM_ERROR_SUCCESS) goto bail_and_die;
    /*free the document */
    xmlFreeDoc(doc);
    xmlCleanupParser();
    
    /* Make sure all the fields are allocated. */
    *error_code = OLM_ERROR_NO_MEMORY;
    if (message->to == NULL)
    {
        data_len = strlen(NO_ADDRESS) + 2;
        message->to = (char *)malloc(data_len);
        if (message->to == NULL) goto bail_and_die;
        memset(message->to, 0, data_len);
        strncpy(message->to, NO_ADDRESS, data_len);
    }
    if (message->from == NULL)
    {
        data_len = strlen(NO_ADDRESS) + 2;
        message->from = (char *)malloc(data_len);
        if (message->from == NULL) goto bail_and_die;
        memset(message->from, 0, data_len);
        strncpy(message->from, NO_ADDRESS, data_len);
    }
    if (message->reply_to == NULL)
    {
        data_len = strlen(NO_ADDRESS) + 2;
        message->reply_to = (char *)malloc(data_len);
        if (message->reply_to == NULL) goto bail_and_die;
        memset(message->reply_to, 0, data_len);
        strncpy(message->reply_to, NO_ADDRESS, data_len);
    }
    if (message->subject == NULL)
    {
        data_len = strlen(NO_SUBJECT) + 2;
        message->subject = (char *)malloc(data_len);
        if (message->subject == NULL) goto bail_and_die;
        memset(message->subject, 0, data_len);
        strncpy(message->subject, NO_SUBJECT, data_len);
    }
    if (message->message_id == NULL)
    {
        data_len = strlen(NO_MID) + 2;
        message->message_id = (char *)malloc(data_len);
        if (message->message_id == NULL) goto bail_and_die;
        memset(message->message_id, 0, data_len);
        strncpy(message->message_id, NO_MID, data_len);
    }
    if (message->body == NULL)
    {
        data_len = strlen(NO_MESSAGE_BODY) + 2;
        message->body = (char *)malloc(data_len);
        if (message->body == NULL) goto bail_and_die;
        memset(message->body, 0, data_len);
        strncpy(message->body, NO_MESSAGE_BODY, data_len);
    }
    
    free(data_buffer);
    free(message_data);
    *error_code = OLM_ERROR_SUCCESS;
    return message;
    
bail_and_die:
    
    if (message_data != NULL) free(message_data);
    if (data_buffer != NULL) free(data_buffer);
    if (doc != NULL)
    {
        xmlFreeDoc(doc);
        xmlCleanupParser();
    }
    
    olm_message_free(message);
    
    return INVALID_OLM_MESSAGE;
}

int parse_element_names(xmlNode * a_node, olm_mail_message_t *message)
{
    xmlNode *cur_node = NULL;
    xmlAttr *attribute = NULL;
    int found = false;
    xmlChar *char_data = NULL;
    unsigned long child_count = 0;
    int found_to = false;
    int found_reply_to = false;
    olm_attachment_t *curr_att = NULL;
    size_t data_len = 0;
    struct tm time_str;
    
    for (cur_node = a_node; cur_node; cur_node = cur_node->next)
    {
        if (cur_node->type == XML_ELEMENT_NODE)
        {
            /* e-mail addresses. */
            if (strcmp((const char *)cur_node->name, "emailAddress") == 0) /* email address node */
            {
                /* To */
                if (strcmp((const char *)cur_node->parent->name, "OPFMessageCopyToAddresses") == 0)
                {
                    attribute = cur_node->properties;
                    found = false;
                    while ((attribute != NULL) && (found == false))
                    {
                        if (strcmp((const char *)attribute->name, "OPFContactEmailAddressAddress") == 0)
                        {
                            char_data = xmlNodeGetContent(attribute->children);
                            if (char_data != NULL)
                            {
                                data_len = strlen((const char *)char_data) + 2;
                                message->to = (char *)malloc(data_len);
                                if (message->to == NULL) return OLM_ERROR_NO_MEMORY;
                                memset(message->to, 0, data_len);
                                if (found_to == false)
                                {
                                    strncpy(message->to, (const char *)char_data, data_len);
                                }
                                else
                                {
                                    strncat(message->to, ",", data_len);
                                    strncat(message->to, (const char *)char_data, data_len);
                                }
                                xmlFree(char_data);
                                found_to = true;
                            }

                            found = true;
                        }
                        attribute = attribute->next;
                    }
                }
                /* Reply to */
                if (strcmp((const char *)cur_node->parent->name, "OPFMessageCopyReplyToAddresses") == 0)
                {
                    attribute = cur_node->properties;
                    found = false;
                    while ((attribute != NULL) && (found == false))
                    {
                        if (strcmp((const char *)attribute->name, "OPFContactEmailAddressAddress") == 0)
                        {
                            char_data = xmlNodeGetContent(attribute->children);
                            if (char_data != NULL)
                            {
                                data_len = strlen((const char *)char_data) + 2;
                                message->reply_to = (char *)malloc(data_len);
                                if (message->reply_to == NULL) return OLM_ERROR_NO_MEMORY;
                                memset(message->reply_to, 0, data_len);
                                if (found_reply_to == false)
                                {
                                    strncpy(message->reply_to, (const char *)char_data, data_len);
                                }
                                else
                                {
                                    strncat(message->reply_to, ",", data_len);
                                    strncat(message->reply_to, (const char *)char_data, data_len);
                                }
                                xmlFree(char_data);
                                found_reply_to = true;
                            }
                            
                            found = true;
                        }
                        attribute = attribute->next;
                    }
                }
                /* From */
                if (strcmp((const char *)cur_node->parent->name, "OPFMessageCopySenderAddress") == 0)
                {
                    attribute = cur_node->properties;
                    found = false;
                    while ((attribute != NULL) && (found == false))
                    {
                        if (strcmp((const char *)attribute->name, "OPFContactEmailAddressAddress") == 0)
                        {
                            char_data = xmlNodeGetContent(attribute->children);
                            if (char_data != NULL)
                            {
                                data_len = strlen((const char *)char_data) + 2;
                                message->from = (char *)malloc(data_len);
                                if (message->from == NULL) return OLM_ERROR_NO_MEMORY;
                                memset(message->from, 0, data_len);
                                strncpy(message->from, (const char *)char_data, data_len);
                                xmlFree(char_data);
                            }
                            
                            found = true;
                        }
                        attribute = attribute->next;
                    }
                }
            }
        }
        /* Subject. */
        if (strcmp((const char *)cur_node->name, "OPFMessageCopySubject") == 0)
        {
            char_data = xmlNodeGetContent(cur_node);
            if (char_data != NULL)
            {
                data_len = strlen((const char *)char_data) + 2;
                message->subject = (char *)malloc(data_len);
                if (message->subject == NULL) return OLM_ERROR_NO_MEMORY;
                memset(message->subject, 0, data_len);
                strncpy(message->subject, (const char *)char_data, data_len);
                xmlFree(char_data);
            }
        }
        /* Message body. */
        if (strcmp((const char *)cur_node->name, "OPFMessageCopyBody") == 0)
        {
            char_data = xmlNodeGetContent(cur_node);
            if (char_data != NULL)
            {
                message->body = (char *)malloc(strlen((char *)char_data) + 1);
                if (message->body != NULL)
                {
                    memset(message->body, 0, strlen((char *)char_data) + 1);
                    strncpy(message->body, (const char *)char_data, strlen((char *)char_data) + 1);
                    xmlFree(char_data);
                }
                else
                {
                    xmlFree(char_data);
                    return OLM_ERROR_NO_MEMORY;
                }
            }
        }
        /* Sent time/date */
        if (strcmp((const char *)cur_node->name, "OPFMessageCopySentTime") == 0)
        {
            char_data = xmlNodeGetContent(cur_node);
            if (char_data != NULL)
            {
                sscanf ((const char *)char_data, "%d%*c%d%*c%d%*c%d%*c%d%*c%d", &time_str.tm_year, &time_str.tm_mon, &time_str.tm_mday, &time_str.tm_hour, &time_str.tm_min, &time_str.tm_sec);
                time_str.tm_year -= 1900;
                time_str.tm_mon -= 1;
                time_str.tm_isdst = -1;
                message->sent_time = mktime(&time_str);
            }
        }
        /* Received time/date */
        if (strcmp((const char *)cur_node->name, "OPFMessageCopyReceivedTime") == 0)
        {
            char_data = xmlNodeGetContent(cur_node);
            if (char_data != NULL)
            {
                sscanf ((const char *)char_data, "%d%*c%d%*c%d%*c%d%*c%d%*c%d", &time_str.tm_year, &time_str.tm_mon, &time_str.tm_mday, &time_str.tm_hour, &time_str.tm_min, &time_str.tm_sec);
                time_str.tm_year -= 1900;
                time_str.tm_mon -= 1;
                time_str.tm_isdst = -1;
                message->received_time = mktime(&time_str);
            }
        }
        /* Modified time/date */
        if (strcmp((const char *)cur_node->name, "OPFMessageCopyModDate") == 0)
        {
            char_data = xmlNodeGetContent(cur_node);
            if (char_data != NULL)
            {
                sscanf ((const char *)char_data, "%d%*c%d%*c%d%*c%d%*c%d%*c%d", &time_str.tm_year, &time_str.tm_mon, &time_str.tm_mday, &time_str.tm_hour, &time_str.tm_min, &time_str.tm_sec);
                time_str.tm_year -= 1900;
                time_str.tm_mon -= 1;
                time_str.tm_isdst = -1;
                message->modified_time = mktime(&time_str);
            }
        }
        /* Message ID. */
        if (strcmp((const char *)cur_node->name, "OPFMessageCopyMessageID") == 0)
        {
            char_data = xmlNodeGetContent(cur_node);
            if (char_data != NULL)
            {
                data_len = strlen((const char *)char_data) + 2;
                message->message_id = (char *)malloc(data_len);
                if (message->message_id == NULL) return OLM_ERROR_NO_MEMORY;
                memset(message->message_id, 0, data_len);
                strncpy(message->message_id, (const char *)char_data, data_len);
            }
        }
        /* HTML status */
        if (strcmp((const char *)cur_node->name, "OPFMessageGetHasHTML") == 0)
        {
            char_data = xmlNodeGetContent(cur_node);
            if (char_data != NULL)
            {
                if (char_data[0] == '0')
                    message->has_html = 0;
                else
                    message->has_html = 1;
            }
        }
        /* Rich text status */
        if (strcmp((const char *)cur_node->name, "OPFMessageGetHasRichText") == 0)
        {
            char_data = xmlNodeGetContent(cur_node);
            if (char_data != NULL)
            {
                if (char_data[0] == '0')
                    message->has_rich_text = 0;
                else
                    message->has_rich_text = 1;
            }
        }
        /* Message priority */
        if (strcmp((const char *)cur_node->name, "OPFMessageGetPriority") == 0)
        {
            char_data = xmlNodeGetContent(cur_node);
            if (char_data != NULL)
            {
                message->message_priority = (int)char_data[0];
                message->message_priority -=30;
                if ((message->message_priority < MESSAGE_PRIORITY_LOWEST) || (message->message_priority > MESSAGE_PRIORITY_HIGHEST)) message->message_priority = MESSAGE_PRIORITY_NORMAL;
            }
        }
        
        /* Attachments. */
        if (strcmp((const char *)cur_node->name, "OPFMessageCopyAttachmentList") == 0)
        {
            message->attachment_list = NULL;
            message->attachment_count = 0;
            child_count = xmlChildElementCount(cur_node);
            if (child_count > 0)
            {
                /* Allocate some memory. */
                message->attachment_list = (olm_attachment_t **)malloc(sizeof(olm_attachment_t *) * child_count);
                if (message->attachment_list == NULL) return OLM_ERROR_NO_MEMORY;
                memset(message->attachment_list, 0, (sizeof(olm_attachment_t *) * child_count));
            }
        }
        if (strcmp((const char *)cur_node->name, "messageAttachment") == 0)
        {
            curr_att = malloc(sizeof(olm_attachment_t));
            if (curr_att == NULL) return OLM_ERROR_NO_MEMORY;
            memset(curr_att, 0, sizeof(olm_attachment_t));
            attribute = cur_node->properties;
            while (attribute != NULL)
            {
                if (strcmp((const char *)attribute->name, "OPFAttachmentContentExtension") == 0)
                {
                    char_data = xmlNodeGetContent(attribute->children);
                    if (char_data != NULL)
                    {
                        data_len = strlen((const char *)char_data) + 2;
                        curr_att->extension = (char *)malloc(data_len);
                        if (curr_att->extension == NULL) return OLM_ERROR_NO_MEMORY;
                        memset(curr_att->extension, 0, data_len);
                        strncpy(curr_att->extension, (const char *)char_data, data_len);
                        xmlFree(char_data);
                    }
                }
                if (strcmp((const char *)attribute->name, "OPFAttachmentContentType") == 0)
                {
                    char_data = xmlNodeGetContent(attribute->children);
                    if (char_data != NULL)
                    {
                        data_len = strlen((const char *)char_data) + 2;
                        curr_att->content_type = (char *)malloc(data_len);
                        if (curr_att->content_type == NULL) return OLM_ERROR_NO_MEMORY;
                        memset(curr_att->content_type, 0, data_len);
                        strncpy(curr_att->content_type, (const char *)char_data, data_len);
                        xmlFree(char_data);
                    }
                }
                if (strcmp((const char *)attribute->name, "OPFAttachmentName") == 0)
                {
                    char_data = xmlNodeGetContent(attribute->children);
                    if (char_data != NULL)
                    {
                        data_len = strlen((const char *)char_data) + 2;
                        curr_att->filename = (char *)malloc(data_len);
                        if (curr_att->filename == NULL) return OLM_ERROR_NO_MEMORY;
                        memset(curr_att->filename, 0, data_len);
                        strncpy(curr_att->filename, (const char *)char_data, data_len);
                        xmlFree(char_data);
                    }
                }
                if (strcmp((const char *)attribute->name, "OPFAttachmentContentFileSize") == 0)
                {
                    char_data = xmlNodeGetContent(attribute->children);
                    if (char_data != NULL)
                    {
                        curr_att->file_size = atoll((const char *)char_data);
                        xmlFree(char_data);
                    }
                    else
                    {
                        curr_att->file_size = 0;
                    }
                }
                if (strcmp((const char *)attribute->name, "OPFAttachmentURL") == 0)
                {
                    char_data = xmlNodeGetContent(attribute->children);
                    if (char_data != NULL)
                    {
                        data_len = strlen((const char *)char_data) + 2;
                        curr_att->__private = (char *)malloc(data_len);
                        if (curr_att->__private == NULL) return OLM_ERROR_NO_MEMORY;
                        memset(curr_att->__private, 0, data_len);
                        strncpy(curr_att->__private, (const char *)char_data, data_len);
                        xmlFree(char_data);
                    }
                }
                
                attribute = attribute->next;
            }
            message->attachment_list[message->attachment_count] = curr_att;
            message->attachment_count++;
        }
        parse_element_names(cur_node->children, message);
    }
    
    return OLM_ERROR_SUCCESS;
}

void olm_message_free(olm_mail_message_t *message)
{
    if (message == NULL) return;
    if (message->to != NULL) free(message->to);
    if (message->from != NULL) free(message->from);
    if (message->reply_to != NULL) free(message->reply_to);
    if (message->subject != NULL) free(message->subject);
    if (message->message_id != NULL) free(message->message_id);
    if (message->body != NULL) free(message->body);
    if (message->attachment_list != NULL)
    {
        for (uint64_t i = 0; i < message->attachment_count; i++)
        {
            if (message->attachment_list[i]->__private != NULL) free(message->attachment_list[i]->__private);
            if (message->attachment_list[i]->content_type != NULL) free(message->attachment_list[i]->content_type);
            if (message->attachment_list[i]->extension != NULL) free(message->attachment_list[i]->extension);
            if (message->attachment_list[i]->filename != NULL) free(message->attachment_list[i]->filename);
            free(message->attachment_list[i]);
        }
        free(message->attachment_list);
    }
    free(message);
}

int olm_extract_and_save_attachment(olm_file_t *file, olm_attachment_t* attachment, const char *dest_path)
{
    internal_archive_entry_data *attachment_entry = NULL;
    int dest_fd = -1;
    char *copy_buff = NULL;
    size_t block_size = 0;
    size_t block_count = 0;
    uint32_t local_header_sig = 0;
    uint16_t filename_len = 0;
    uint16_t extra_len = 0;
    ssize_t bytes_xfer = 0;
    uint32_t crc = 0;
    
    /* Look for the attachment in the list of archive entries. */
    list_iterator_start(&file->attachment_entries);
    while (list_iterator_hasnext(&file->attachment_entries) != 0)
    {
        attachment_entry = (internal_archive_entry_data *)list_iterator_next(&file->attachment_entries);
        if (strcmp(attachment_entry->raw_entry_path, attachment->__private) == 0) break;
    }
    list_iterator_stop(&file->attachment_entries);
    
    /* Make sure we have an attachment. */
    if (attachment_entry == NULL) return OLM_ERROR_ATTACHMENT_NOT_FOUND;
    
    /* Seek to the appropriate place in the source archive. */
    if (lseek(file->file_seg, attachment_entry->file_offset, SEEK_SET) == -1) return OLM_ERROR_FILE_IO_ERROR;
    /* Skip the (redundant) local header. */
    if (read(file->file_seg, &local_header_sig, 4) != 4) return OLM_ERROR_FILE_IO_ERROR;
    if (local_header_sig != SIG_LOCAL_FILE_HEADER)  return OLM_ERROR_FILE_CORRUPTED; /* Just to make sure. */
    if (lseek(file->file_seg, 22, SEEK_CUR) == -1) return OLM_ERROR_FILE_IO_ERROR;
    if ((read(file->file_seg, &filename_len, 2) != 2) || (read(file->file_seg, &extra_len, 2) != 2)) return OLM_ERROR_FILE_IO_ERROR;
    if (lseek(file->file_seg, (filename_len + extra_len), SEEK_CUR) == -1) return OLM_ERROR_FILE_CORRUPTED;
    
    /* Check if the attachment is compressed (it shouldn't be). */
    if (attachment_entry->compression_method != ZIP_CA_STORED)
    {
        return OLM_ERROR_ATTACHMENT_CORRUPTED;
    }
    
    /* Allocate the copy buffer. */
    copy_buff = allocate_block_for_buffer(attachment_entry->entry_size, &block_size, &block_count);
    if (copy_buff == NULL) return OLM_ERROR_NO_MEMORY;
    
    /* Now try to create the destination file. This will be overwritten if it already exists. */
    dest_fd = open(dest_path, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP);
    if (dest_fd == -1)
    {
        free(copy_buff);
        return OLM_ERROR_FILE_IO_ERROR;
    }
    
    /* Now copy the data from the archive to the dest files. */
    for (size_t block = 0; block < block_count; block++)
    {
        bytes_xfer = read(file->file_seg, copy_buff, block_size);
        if (bytes_xfer != block_size)
        {
            free(copy_buff);
            close(dest_fd);
            return OLM_ERROR_FILE_IO_ERROR;
        }
        crc = crc32(crc, copy_buff, block_size);
        bytes_xfer = write(dest_fd, copy_buff, block_size);
        if (bytes_xfer != block_size)
        {
            free(copy_buff);
            close(dest_fd);
            return OLM_ERROR_FILE_IO_ERROR;
        }
    }
    
    free(copy_buff);
    close(dest_fd);
    
    /* Check the extracted file. */
    if (crc != attachment_entry->crc32)
    {
        return  OLM_ERROR_ATTACHMENT_CORRUPTED;
        unlink(dest_path);
    }

    return OLM_ERROR_SUCCESS;
}

void olm_close_file(olm_file_t *file)
{
    if (file != NULL)
    {
        /* Free the entries */
        list_iterator_start(&file->message_entries);
        while (list_iterator_hasnext(&file->message_entries) != 0)
        {
            free_entry_from_central_dir((internal_archive_entry_data *)list_iterator_next(&file->message_entries));
        }
        list_iterator_stop(&file->message_entries);
        list_destroy(&file->message_entries);
        
        list_iterator_start(&file->attachment_entries);
        while (list_iterator_hasnext(&file->attachment_entries) != 0)
        {
            free_entry_from_central_dir((internal_archive_entry_data *)list_iterator_next(&file->attachment_entries));
        }
        list_iterator_stop(&file->attachment_entries);
        list_destroy(&file->attachment_entries);
        
        free(file->filename);
        close(file->file_seg);
        
        free(file);
    }
}

/**************************************************************************************************
 * Returns the number of messages contained in this olm archive.
 **************************************************************************************************/
uint64_t olm_mail_message_count(olm_file_t *file)
{    
    if (file != NULL) return (uint64_t)list_size(&file->message_entries);
    
    return 0;
}

/***************************************************************************************************************************************************
 * Utility functions.
 ***************************************************************************************************************************************************/

/**************************************************************************************************
 * This function will attempt to fetch the end of central directory record from the end of the
 * ZIP file, or the current segment. All ZIP files have this record at the end.
 *
 * If the ZIP file has a comment on the end, the location of this record will be further back in
 * the file. The function will search back until a record is found or an error occurs. The function
 * will not search back more then 64k + the size of the record, as this is the maximum size of the
 * comment, and the function could block for a long time if searching a big ZIP file.
 *
 * If the record is found the function will return TRUE. If the record is not found; FALSE will
 * be returned. If an I/O error occurs errno should hold a value that indicates what went wrong.
 **************************************************************************************************/
int get_eocd_record(olm_file_t *file, eocd_record32 *eocd_record)
{
    off_t seek_offset = -22;
    int rec_found = false;
    int give_up = false;
    ssize_t bytes_read = 0;
    ssize_t record_size = sizeof(eocd_record32);
    
    /* Look for the end of central directory record.
     * If there is no ZIP file comment, the record will be 22 bytes from the end of the file,
     * if there is a comment we will have to search back.
     */
    
    rec_found = false;
    give_up = false;
    while ((rec_found == false) && (give_up == false))
    {
        /* Position the stream. */
        if (lseek(file->file_seg, seek_offset, SEEK_END) == -1) return false;
        
        /* Look for the end of central directory record signature. */
        bytes_read = read(file->file_seg, eocd_record, (size_t)record_size);
        if (bytes_read != record_size) return false;
        
        /* Check that we have in fact read out a valid end of central directory record from the specified location. */
        if (eocd_record->signature == SIG_END_OF_CENTRAL_DIR)
        {
            rec_found = true;
        }
        else
        {
            if (seek_offset <= -65558)
            {
                give_up = true;
                rec_found = false;
            }
            --seek_offset;
        }
    }
    
    /* Get the comment if there is one. */
    if ((rec_found) && (eocd_record->comment_length > 0))
    {
        /* Allocate some memory and make room for a zero terminator byte. */
        file->comment = (char *) malloc(eocd_record->comment_length + 1);
        /* Make sure the memory was allocated. */
        if (file->comment != NULL)
        {
            memset(file->comment, 0, eocd_record->comment_length);
            bytes_read = read(file->file_seg, file->comment, eocd_record->comment_length);
            if (bytes_read != eocd_record->comment_length) return false;
            /* make sure the string is NULL terminated. */
            file->comment[eocd_record->comment_length] = '\0';
        }
    }
    
    return rec_found;
}

/**************************************************************************************************
 * This function will fetch the ZIP64 end of central directory locator if it is present.
 *
 * The function will return TRUE if the record was found and FALSE in any other case.
 *
 * If the record was not found due to an error, errno should hold a value that indicates what went wrong.
 **************************************************************************************************/
int get_eocdr64_locator(olm_file_t *file, eocdr_locator64 *eocdr_locator, size_t comment_length)
{
    off_t seek_offset = -42;
    ssize_t bytes_read = 0;
    ssize_t record_size = sizeof(eocdr_locator64);
    
    seek_offset -= comment_length; /* subtract then length of the comment from the seek offset. */
    
    /* The zip64 locator record will (if present) be located immediatley before the end of central directory record. */
    /* It will therefore be 42 bytes + the length of the comment from the end of the file. */
    
    /* Position the file pointer. */
    if (lseek(file->file_seg, seek_offset, SEEK_END) == -1) return false;
    
    /* Read out the the record. */
    bytes_read = read(file->file_seg, eocdr_locator, (size_t)record_size);
    if (bytes_read != record_size) return false;
    
    if (eocdr_locator->signature != SIG_ZIP64_EOCDR_LOCATOR) return false;
    
    return true;
}

/**************************************************************************************************
 * This function will fetch the ZIP64 end of central directory record if it is present.
 *
 * The function will return TRUE if the record was found and FALSE in any other case.
 *
 * If the record was not found due to an error, errno should hold a value that indicates what went wrong.
 **************************************************************************************************/
int get_eocd64_record(olm_file_t *file, eocd_record64 *eocd_record, off_t search_offset)
{
    ssize_t record_size = sizeof(eocd_record64);
    ssize_t bytes_read = 0;
    
    /* Position the file pointer. */
    if (lseek(file->file_seg, search_offset, SEEK_SET) == -1) return false;
    
    /* Read out the record. */
    bytes_read = read(file->file_seg, eocd_record, (size_t)record_size);
    if (bytes_read != record_size) return false;
    
    /* check the magic number. */
    if (eocd_record->signature != SIG_ZIP64_END_OF_CENTRAL_DIR) return false;
    
    return true;
}

/**************************************************************************************************
 * This function read the next entry from the central directory and populate the given internal
 * archive entry data structure.
 *
 * The function will return the number of bytes read from the underlying stream. or -1 if there was
 * an error.
 *
 * If the record was not found due to an error, errno should hold a value that indicates what went
 * wrong.
 **************************************************************************************************/
internal_archive_entry_data *read_next_entry_from_central_dir(olm_file_t *file, int *error_code)
{
    central_dir_entry_header header_buff = { 0, 0, 0, 0, 0, {0}, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    char *last_slash = NULL;
    internal_archive_entry_data *entry = (internal_archive_entry_data *)malloc(sizeof(internal_archive_entry_data));
    if (entry == NULL) return NULL;
    memset(entry, 0, sizeof(internal_archive_entry_data));
    ssize_t bytes_read = 0;
    ssize_t total_bytes_read = 0;
    extra_field_header efh = { 0, 0, NULL };
    size_t str_len = 0;
    
    /* Read out the header. */
    if (read(file->file_seg, &header_buff, sizeof(central_dir_entry_header)) == -1)
    {
        *error_code = OLM_ERROR_FILE_IO_ERROR;
        goto bail_and_die;
    }
    
    /*******************************************************************
     * Now we must read out the variable data that follows the header. *
     *******************************************************************/
    
    /* Get the filename. */
    if (header_buff.filename_length == 0)
    {
        *error_code = OLM_ERROR_FILE_CORRUPTED;
        goto bail_and_die;
    }
    
    entry->raw_entry_path = (char *)malloc(header_buff.filename_length + 1);
    if (entry->raw_entry_path == NULL)
    {
        *error_code = OLM_ERROR_NO_MEMORY;
        goto bail_and_die;
    }
    memset(entry->raw_entry_path, 0, (header_buff.filename_length + 1));
    if (read(file->file_seg, entry->raw_entry_path, header_buff.filename_length) == -1)
    {
        *error_code = OLM_ERROR_FILE_IO_ERROR;
        goto bail_and_die;
    }
    
    /* First get the values we need from the header. */
    entry->entry_size = header_buff.uncompressed_size;
    entry->entry_compressed_size = header_buff.compressed_size;
    entry->attributes = header_buff.internal_file_attributes;
    entry->compression_method = header_buff.compression_method;
    entry->crc32 = header_buff.crc32;
    entry->flags = header_buff.bit_flag;
    entry->file_offset = header_buff.local_header_offset;
    
    /* Get (skip) the file comment. */
    if (header_buff.file_comment_length != 0)
    {
        /* We are not interested in the file comment (for now ?) */
        if (lseek(file->file_seg, header_buff.file_comment_length, SEEK_CUR) == -1) goto bail_and_die;
    }
    
    /* Get (skip) the extra fields. */
    if (header_buff.extra_field_length != 0)
    {
        total_bytes_read = 0;
        while (total_bytes_read < header_buff.extra_field_length)
        {
            bytes_read = read_out_extra_field(file, &efh);
            if (bytes_read == -1) goto bail_and_die;
            switch (efh.header_id)
            {
                case 0x0001: /* ZIP64 extra field. */
                    if (header_buff.uncompressed_size == 0xFFFFFFFF) memcpy(&entry->entry_size, efh.data, 8);
                    if (header_buff.compressed_size == 0xFFFFFFFF) memcpy(&entry->entry_compressed_size, (efh.data + 8), 8);
                    if (header_buff.local_header_offset == 0xFFFFFFFF) memcpy(&entry->file_offset, (efh.data + 16), 8);
                    break;
            }
            if (efh.data != NULL) free(efh.data);
            total_bytes_read += bytes_read;
        }
    }
    
    /* Check if this is a directory. */
    if ((entry->raw_entry_path[header_buff.filename_length - 1] == '/') || ((entry->attributes & FAT_ATTRIB_DIR) == FAT_ATTRIB_DIR))
    {
        if (entry->raw_entry_path[header_buff.filename_length - 1] == '/') entry->raw_entry_path[header_buff.filename_length - 1] = '\0';
        entry->is_directory = true;
        str_len = strlen(entry->raw_entry_path) + 1;
        entry->directory = (char *)malloc(str_len);
        if (entry->directory == NULL)
        {
            *error_code = OLM_ERROR_NO_MEMORY;
            goto bail_and_die;
        }
        memset(entry->directory, 0, str_len);
        strncpy(entry->directory, entry->raw_entry_path, str_len);
        entry->filename = NULL;
    }
    else
    {
        entry->is_directory = false;
        if (strchr(entry->raw_entry_path, '/') == NULL)
        {
            str_len = strlen(entry->raw_entry_path) + 1;
            entry->filename = (char *)malloc(str_len);
            if (entry->filename == NULL)
            {
                *error_code = OLM_ERROR_NO_MEMORY;
                goto bail_and_die;
            }
            memset(entry->filename, 0, str_len);
            strncpy(entry->filename, entry->raw_entry_path, str_len);
            entry->directory = NULL;
        }
        else
        {
            str_len = strlen(entry->raw_entry_path) + 1;
            entry->filename = (char *)malloc(str_len);
            entry->directory = (char *)malloc(str_len);
            if ((entry->filename == NULL) || (entry->directory == NULL))
            {
                *error_code= OLM_ERROR_NO_MEMORY;
                goto bail_and_die;
            }
            memset(entry->filename, 0, str_len);
            memset(entry->directory, 0, str_len);
            last_slash = strrchr(entry->raw_entry_path, '/');
            strncpy(entry->filename, (last_slash + 1), str_len);
            strncpy(entry->directory, entry->raw_entry_path, index_of_last(entry->raw_entry_path, '/'));
        }
    }
    
    return entry;
    
bail_and_die:
    
    free_entry_from_central_dir(entry);
    
    return NULL;
}

ssize_t index_of_last(const char *str, char c)
{
    char ch = 1;
    if (str == NULL) return 0;
    size_t cur_idx = 0;
    ssize_t idx = -1;
    
    while (ch != '\0')
    {
        ch = str[cur_idx];
        if (ch == c) idx = cur_idx;
        ++cur_idx;
    }
    
    return idx;
}

void free_entry_from_central_dir(internal_archive_entry_data *entry)
{
    if (entry != NULL)
    {
        if (entry->filename != NULL)
        {
            free(entry->filename);
            entry->filename = NULL;
        }
        if (entry->directory != NULL)
        {
            free(entry->directory);
            entry->directory = NULL;
        }
        if (entry->raw_entry_path == NULL)
        {
            free(entry->raw_entry_path);
            entry->raw_entry_path = NULL;
        }
        
        free(entry);
    }
}

ssize_t read_out_extra_field(olm_file_t *file, extra_field_header *buffer)
{
    if (read(file->file_seg, buffer, 4) == -1) return -1;
    
    /* Get some memory. */
    if (buffer->data_size > 0)
    {
        buffer->data = (unsigned char *)malloc(buffer->data_size);
        if (buffer->data == NULL) return -1;
        
        if (read(file->file_seg, buffer->data, buffer->data_size) == -1)
        {
            free(buffer->data);
            return -1;
        }
    }
    else
    {
        buffer->data = NULL;
    }
    
    return (buffer->data_size + 4);
}

char *allocate_block_for_buffer(size_t buff_size, size_t *block_size, size_t *block_count)
{
    char *buff = NULL;
    size_t bs = 0;
    
    if ((buff_size == 0) || (block_size == NULL) || (block_count == NULL)) return NULL;

    for (size_t bc = 1; bc <= buff_size; bc++)
    {
        if ((buff_size % bc) != 0) continue;
        bs = buff_size / bc;
        buff = (char *)malloc(bs);
        if (buff == NULL) continue;
        *block_count = bc;
        *block_size = bs;
        break;
    }
    
    return buff;
}

