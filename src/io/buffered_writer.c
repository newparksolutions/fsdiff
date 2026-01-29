/**
 * @file buffered_writer.c
 * @brief Buffered file writer implementation
 */

#include "buffered_writer.h"
#include <stdlib.h>
#include <string.h>

fsd_error_t fsd_writer_create(fsd_buffered_writer_t **writer_out,
                              const char *path,
                              size_t buffer_size) {
    if (!writer_out || !path) {
        return FSD_ERR_INVALID_ARG;
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        return FSD_ERR_IO;
    }

    fsd_error_t err = fsd_writer_create_from_file(writer_out, file, buffer_size);
    if (err != FSD_SUCCESS) {
        fclose(file);
        return err;
    }

    (*writer_out)->owns_file = true;
    return FSD_SUCCESS;
}

fsd_error_t fsd_writer_create_from_file(fsd_buffered_writer_t **writer_out,
                                        FILE *file,
                                        size_t buffer_size) {
    if (!writer_out || !file) {
        return FSD_ERR_INVALID_ARG;
    }

    if (buffer_size == 0) {
        buffer_size = FSD_WRITER_DEFAULT_BUFFER;
    }

    fsd_buffered_writer_t *writer = calloc(1, sizeof(fsd_buffered_writer_t));
    if (!writer) {
        return FSD_ERR_OUT_OF_MEMORY;
    }

    writer->buffer = malloc(buffer_size);
    if (!writer->buffer) {
        free(writer);
        return FSD_ERR_OUT_OF_MEMORY;
    }

    writer->file = file;
    writer->buffer_size = buffer_size;
    writer->buffer_pos = 0;
    writer->total_written = 0;
    writer->owns_file = false;

    *writer_out = writer;
    return FSD_SUCCESS;
}

fsd_error_t fsd_writer_flush(fsd_buffered_writer_t *writer) {
    if (!writer || writer->buffer_pos == 0) {
        return FSD_SUCCESS;
    }

    size_t written = fwrite(writer->buffer, 1, writer->buffer_pos, writer->file);
    if (written != writer->buffer_pos) {
        return FSD_ERR_IO;
    }

    writer->total_written += writer->buffer_pos;
    writer->buffer_pos = 0;

    return FSD_SUCCESS;
}

fsd_error_t fsd_writer_write(fsd_buffered_writer_t *writer,
                             const void *data,
                             size_t len) {
    if (!writer || !data) {
        return FSD_ERR_INVALID_ARG;
    }

    const uint8_t *src = (const uint8_t *)data;

    while (len > 0) {
        size_t space = writer->buffer_size - writer->buffer_pos;

        if (len <= space) {
            /* Fits in buffer */
            memcpy(writer->buffer + writer->buffer_pos, src, len);
            writer->buffer_pos += len;
            return FSD_SUCCESS;
        }

        /* Fill buffer and flush */
        memcpy(writer->buffer + writer->buffer_pos, src, space);
        writer->buffer_pos = writer->buffer_size;
        src += space;
        len -= space;

        fsd_error_t err = fsd_writer_flush(writer);
        if (err != FSD_SUCCESS) {
            return err;
        }
    }

    return FSD_SUCCESS;
}

fsd_error_t fsd_writer_write_byte(fsd_buffered_writer_t *writer, uint8_t byte) {
    if (!writer) {
        return FSD_ERR_INVALID_ARG;
    }

    if (writer->buffer_pos >= writer->buffer_size) {
        fsd_error_t err = fsd_writer_flush(writer);
        if (err != FSD_SUCCESS) {
            return err;
        }
    }

    writer->buffer[writer->buffer_pos++] = byte;
    return FSD_SUCCESS;
}

fsd_error_t fsd_writer_write_u16_le(fsd_buffered_writer_t *writer, uint16_t value) {
    uint8_t buf[2] = {
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF)
    };
    return fsd_writer_write(writer, buf, 2);
}

fsd_error_t fsd_writer_write_u32_le(fsd_buffered_writer_t *writer, uint32_t value) {
    uint8_t buf[4] = {
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF),
        (uint8_t)((value >> 16) & 0xFF),
        (uint8_t)((value >> 24) & 0xFF)
    };
    return fsd_writer_write(writer, buf, 4);
}

fsd_error_t fsd_writer_write_u64_le(fsd_buffered_writer_t *writer, uint64_t value) {
    uint8_t buf[8] = {
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF),
        (uint8_t)((value >> 16) & 0xFF),
        (uint8_t)((value >> 24) & 0xFF),
        (uint8_t)((value >> 32) & 0xFF),
        (uint8_t)((value >> 40) & 0xFF),
        (uint8_t)((value >> 48) & 0xFF),
        (uint8_t)((value >> 56) & 0xFF)
    };
    return fsd_writer_write(writer, buf, 8);
}

size_t fsd_writer_bytes_written(const fsd_buffered_writer_t *writer) {
    if (!writer) return 0;
    return writer->total_written + writer->buffer_pos;
}

fsd_error_t fsd_writer_close(fsd_buffered_writer_t *writer) {
    if (!writer) {
        return FSD_SUCCESS;
    }

    fsd_error_t err = fsd_writer_flush(writer);

    if (writer->file) {
        /* Always flush the FILE* stream to ensure data reaches disk */
        fflush(writer->file);

        if (writer->owns_file) {
            fclose(writer->file);
        }
    }

    free(writer->buffer);
    free(writer);

    return err;
}
