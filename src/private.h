/* Copyright (C) 2012, Chris Morrison <chris-morrison@cyberservices.com>
 *
 * private.h
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


#ifndef libolm_private_h
#define libolm_private_h

#define _FILE_OFFSET_BITS 64
#define _DARWIN_USE_64_BIT_INODE

#define NO_ADDRESS      "NO_ADDRESS"
#define NO_SUBJECT      "NO_SUBJECT"
#define NO_MID          "NO_MESSAGE_ID"
#define NO_MESSAGE_BODY "NO_BODY"
#define NO_MEM_FOR_DATA "ERROR"

/* ZIP file record signatures */
#define SIG_LOCAL_FILE_HEADER                    0x04034b50         /* Signature for a local file header block (should be the first 4 bytes of a normal ZIP file). */
#define SIG_LOCAL_FILE_HEADER_SPANNED            0x08074b50         /* Signature that will appear in the first 4 bytes of the first segment of a spanned zip file. */
#define SIG_SPECIAL_MARKER                       0x30304b50         /* Signature that will appear in the first 4 bytes of a normal ZIP file for which spanning was started but did not need to be implemented. */
#define SIG_END_OF_CENTRAL_DIR                   0x06054b50         /* Signature for the end of central directory record that should appear at the very end of the ZIP file. */
#define SIG_ZIP64_EOCDR_LOCATOR                  0x07064b50         /* Signature for the ZIP64 end of central directory record locator, for a ZIP file with ZIP64 extensions. */
#define SIG_ZIP64_END_OF_CENTRAL_DIR		     0x06064b50         /* Signature for the ZIP64 end of central directory record, for a ZIP file with ZIP64 extensions. */
#define SIG_CENTRAL_FILE_HEADER                  0x02014b50         /* Signature for an entry in the central directory. */
#define SIG_DIGITAL_SIGNATURE                    0x05054b50         /* Signature for the digital signature block located at the end of the central directory data. */
#define SIG_ARCHIVE_EXTRA_DATA                   0x08064b50         /* Signature for the archive extra data record, for a ZIP file with an encrytpted central directory record. */

/* Compression algorithms */
#define ZIP_CA_STORED                            0x0000             /* The ZIP file entry is stored with no compression or the archive format does not support compression. */
#define ZIP_CA_SHRUNK                            0x0001             /* The ZIP file entry is Shrunk. */
#define ZIP_CA_REDUCEDFACTOR1                    0x0002             /* The ZIP file entry is Reduced with compression factor 1. */
#define ZIP_CA_REDUCEDFACTOR2                    0x0003             /* The ZIP file entry is Reduced with compression factor 2. */
#define ZIP_CA_REDUCEDFACTOR3                    0x0004             /* The ZIP file entry is Reduced with compression factor 3. */
#define ZIP_CA_REDUCEDFACTOR4                    0x0005             /* The ZIP file entry is Reduced with compression factor 4. */
#define ZIP_CA_IMPLODE                           0x0006             /* The ZIP file entry is Imploded. */
#define ZIP_CA_TOKENISED                         0x0007             /* The ZIP file entry is compressed using the Tokenizing compression algorithm. */
#define ZIP_CA_DEFLATE                           0x0008             /* The ZIP file entry is Deflated. */
#define ZIP_CA_DEFLATE64                         0x0009             /* The ZIP file entry is compressed using the Enhanced Deflating using Deflate64(tm). */
#define ZIP_CA_PDCLIMPLODE                       0x000a             /* The ZIP file entry is compressed using the PKWARE Date Compression Library Imploding. */
#define ZIP_CA_RESERVED                          0x000b             /* Reserved by PKWARE. */
#define ZIP_CA_BZIP2                             0x000c             /* The ZIP file entry is compressed using the BZIP2 compression algorithm. */

/* FAT/FAT32 attribute masks. */
#define FAT_ATTRIB_READ_ONLY                     0x0001             /* Indicates that the file is read only. */
#define FAT_ATTRIB_HIDDEN                        0x0002             /* Indicates a hidden file. Such files can be displayed if it is really required. */
#define FAT_ATTRIB_SYSTEM                        0x0004             /* Indicates a system file. These are hidden as well. */
#define FAT_ATTRIB_LABEL                         0x0008             /* Indicates a special entry containing the disk's volume label, instead of describing a file. This kind of entry appears only in the root directory. */
#define FAT_ATTRIB_DIR                           0x0010             /* The entry describes a subdirectory. */
#define FAT_ATTRIB_ARCHIVE                       0x0020             /* This is the archive flag. */

/* ZIP file end of central directory record structure */
typedef struct eocd_record32
{
    uint32_t signature;                                             /* Holds the magic number. */
    uint16_t this_segment;                                          /* Number of this segment 2 bytes. */
    uint16_t central_dir_start_segment;                             /* Number of the segment with the start of the central directory 2 bytes. */
    uint16_t total_entries_on_segment;                              /* Total number of entries in the central directory on this segment 2 bytes. */
    uint16_t total_entries;                                         /* Total number of entries in the central directory 2 bytes. */
    uint32_t central_dir_size;                                      /* Size of the central directory 4 bytes. */
    uint32_t central_dir_offset;                                    /* Offset of start of central directory with respect to the starting segment number 4 bytes. */
    uint16_t comment_length;                                        /* ZIP file comment length 2 bytes. */
} __attribute__((__packed__)) eocd_record32;

/* ZIP file ZIP64 end of central directory record locator structure. */
typedef struct eocdr_locator64
{
    uint32_t signature;                                             /* Holds the magic number. */
    uint32_t zip64_EOCDR_start_segment;                             /* Segment on which ZIP64 end of central directory record begins 4 bytes. */
    uint64_t zip64_EOCDR_offset;                                    /* Offset of ZIP64 end of central directory record on it's start segment 8 bytes. */
    uint32_t total_segments;                                        /* Total and current segment 4 bytes. */
} __attribute__((__packed__)) eocdr_locator64;

/* ZIP file ZIP64 end of central directory record structure. */
typedef struct eocd_record64
{
    uint32_t signature;                                             /* Zip64 end of central dir signature 4 bytes (0x06064b50). */
    uint64_t EOCDRSize;                                             /* Size of zip64 end of central directory record 8 bytes. */
    uint16_t version_made_by;                                       /* Version made by 2 bytes. */
    uint16_t extract_version;                                       /* Version needed to extract 2 bytes. */
    uint32_t this_segment;                                          /* Number of this disk 4 bytes. */
    uint32_t CDR_start_segment;                                     /* Number of the disk with the start of the central directory 4 bytes. */
    uint64_t total_entries_on_segment;                              /* Total number of entries in the central directory on this disk 8 bytes. */
    uint64_t total_entries;                                         /* Total number of entries in the central directory 8 bytes. */
    uint64_t central_directory_size;                                /* Size of the central directory 8 bytes. */
    uint64_t central_directory_offset;                              /* Offset of start of central directory with respect to the starting disk number 8 bytes. */
    /* Version 2 of this structure provides some extra fields to support the compression and encryption of the central directory record from PKZIP 6.2.0 onwards. */
    uint16_t compression_method;                                    /* Method used to compress the Central Directory. */
    uint64_t compressed_size;                                       /* Size of the compressed data. */
    uint64_t original_size;                                         /* Original uncompressed size. */
    uint16_t algorithm_ID;                                          /* Encryption algorithm ID. */
    uint16_t bit_length;                                            /* Encryption key length. */
    uint16_t encryption_flags;                                      /* Encryption flags. */
    uint16_t hash_ID;                                               /* Hash algorithm identifier. */
    uint16_t hash_data_length;                                      /* Length of hash data. */
} __attribute__((__packed__)) eocd_record64;

typedef struct _central_dir_entry_header
{
    uint32_t signature;
    uint16_t version_made_by;
    uint16_t extract_version;
    uint16_t bit_flag;
    uint16_t compression_method;
    union _mdt
    {
        uint16_t file_time;
        uint16_t file_date;
        uint32_t file_date_time;
    } __attribute__((__packed__)) modified_date_time;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_length;
    uint16_t extra_field_length;
    uint16_t file_comment_length;
    uint16_t start_segment_number;
    uint16_t internal_file_attributes;
    uint32_t external_file_attributes;
    uint32_t local_header_offset;
} __attribute__((__packed__)) central_dir_entry_header;

/* Internal archive entry decriptor. */
typedef struct _internal_archive_entry_data
{
    char *raw_entry_path;
    char *filename;
    char *directory;
    char *comment;
    int is_directory;
    uint64_t entry_size;
    uint64_t entry_compressed_size;
    uint16_t attributes;
    int compression_method;                                         /* Indicates the method of compression if any used to compress this entry. */
    uint32_t crc32;
    uint16_t  flags;                                                /* The general purpose flags. */
    uint64_t file_offset;                                           /* The offset of the start of the local file header for this entry. */
} internal_archive_entry_data;

/* Internal ZIP file descriptor. */
struct olm_file_t
{
    char *filename;                                                 /* The full path of the ZIP file we are working on. */
    int file_seg;                                                   /* The ZIP file or segment of the ZIP file that we are working on. */
    int options;                                                    /* The type of ZIP file this is. */
    uint32_t total_segments;                                        /* The total number of segments that make up this ZIP file. */
    uint32_t current_segment;                                       /* The segment of this ZIP file that is loaded and we are working on. */
    uint64_t total_entries;                                         /* The total number of entries (files and directories) in this ZIP file. */
    int zip64;                                                      /* Set to TRUE if this ZIP file uses ZIP64 extensions (i.e. it is larger that 2GB and/or it has a compressed/encrypted central directory. */
    char *comment;                                                  /* Points to the comment of the ZIP file (if present). */
    unsigned char *cdr_buffer;                                      /* The buffer in which the central directory will be read. */
    unsigned char *cdr_sig_data;                                    /* Holds the digital signature data for the central directory. */
    int cdr_compressed;                                             /* Set to TRUE if the central directory is compressed. */
    int cdr_compression_algo;                                       /* The compression algorithm used to compress the central directory. */
    int cdr_encrypted;                                              /* Set to TRUE if the central directory is encrypted. */
    int cdr_encryption_algo;                                        /* The encryption algorithm used to encrypt the central directory (weak encryption not supported). */
    unsigned int  digital_signing_method;                           /* The method of digital signing used in the ZIP file. */
    eocd_record32 eocd_rec32;
    eocdr_locator64 eocd_loc64;
    eocd_record64 eocd_rec64;
    uint64_t central_dir_size;
    off_t central_dir_offset;
    list_t message_entries;
    list_t attachment_entries;
    list_t contact_entries;
};

typedef struct _extra_field_header
{
    uint16_t header_id;
    uint16_t data_size;
    unsigned char *data; /* Free when done. */
} __attribute__((__packed__)) extra_field_header;

#endif
