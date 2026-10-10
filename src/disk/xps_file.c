#include "../xps.h"

void file_source_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_pipe_source_t *source = ptr;

    xps_file_t *file = source->ptr;

    /*create buffer and handle any error*/
    xps_buffer_t *buff = xps_buffer_create(DEFAULT_BUFFER_SIZE, DEFAULT_BUFFER_SIZE, NULL);
    if (buff == NULL)
    {
        logger(LOG_DEBUG, "file_source_handler()", "xps_buffer_create() failed");
        return;
    }

    // Read from file
    size_t read_n = fread(buff->data, 1, buff->size, file->file_struct);
    buff->len = read_n;

    // Checking for read errors
    if (ferror(file->file_struct))
    {
        logger(LOG_ERROR, "file_source_handler()", "read errors");

        xps_buffer_destroy(buff);
        fclose(file->file_struct);

        return;
    }

    // If end of file reached
    if (read_n == 0 && feof(file->file_struct))
    {
        xps_buffer_destroy(buff);
        fclose(file->file_struct);

        return;
    }

    /*Write to pipe from buff*/
    int pipe_threshold = DEFAULT_BUFFER_SIZE;
    int size = source->pipe->buff_thresh - source->pipe->buff_list->len;
    if (pipe_threshold > size)
    {
        pipe_threshold = size;
    }

    if (buff->len > pipe_threshold)
    {
        xps_buffer_destroy(buff);
        source->ready = false;
        return;
    }

    if (xps_pipe_source_write(source, buff) != OK)
    {
        logger(LOG_ERROR, "file_source_handler()", "xps_pipe_source_write() failed");

        xps_buffer_destroy(buff);
        fclose(file->file_struct);

        return;
    }

    xps_buffer_destroy(buff);
}

void file_source_fclose_handler(void *ptr)
{
    assert(ptr != NULL);

    xps_pipe_source_t *source = ptr;

    xps_file_t *file = source->ptr;

    xps_file_destroy(file);
}

xps_file_t *xps_file_create(xps_core_t *core, const char *file_path, int *error)
{
    assert(core != NULL);
    assert(file_path != NULL);

    *error = E_FAIL;

    // check if the file is in the public directory
    char *resolved_path = realpath(file_path, NULL);
    char *resolved_public = realpath("../public", NULL);

    if (!resolved_path || !resolved_public)
    {
        logger(LOG_ERROR, "xps_file_create()", "realpath() failed");

        free(resolved_path);
        free(resolved_public);

        return NULL;
    }

    size_t public_len = strlen(resolved_public);
    if (strncmp(resolved_path, resolved_public, public_len) != 0)
    {
        logger(LOG_WARNING, "xps_file_create()", "file requested is outside of public directory");
        *error = E_PERMISSION;

        free(resolved_path);
        free(resolved_public);

        return NULL;
    }

    free(resolved_path);
    free(resolved_public);

    /*check if others have read permission*/
    struct stat file_stat;
    if (stat(file_path, &file_stat) != 0)
    {
        logger(LOG_ERROR, "xps_file_create()", "stat() failed");
        perror("Error message");

        return NULL;
    }

    if (!(file_stat.st_mode & S_IROTH))
    {
        logger(LOG_WARNING, "xps_file_create()", "others do not have read permission");
        *error = E_PERMISSION;

        return NULL;
    }

    long temp_size = file_stat.st_size;

    // Opening file
    FILE *file_struct = fopen(file_path, "rb");
    if (!file_struct)
    {
        switch (errno)
        {
        case EACCES:
            logger(LOG_ERROR, "xps_file_create()", "Permission denied");
            break;
        case ENOENT:
            logger(LOG_ERROR, "xps_file_create()", "File does not exist");
            break;
        default:
            logger(LOG_ERROR, "xps_file_create()", strerror(errno));
            break;
        }

        return NULL;
    }

    const char *mime_type = xps_get_mime(file_path);

    xps_file_t *file = malloc(sizeof(xps_file_t));

    xps_pipe_source_t *source = xps_pipe_source_create((void *)file, file_source_handler, file_source_fclose_handler);
    if (!source)
    {
        fclose(file_struct);

        return NULL;
    }

    source->ready = true;

    file->core = core;
    file->file_path = file_path;
    file->file_struct = file->file_struct;
    file->mime_type = mime_type;
    file->size = temp_size;
    file->source = source;

    *error = OK;

    logger(LOG_DEBUG, "xps_file_create()", "created file");

    return file;
}

void xps_file_destroy(xps_file_t *file)
{
    assert(file != NULL);

    fclose(file->file_struct);
    xps_pipe_source_destroy(file->source);

    free(file);

    logger(LOG_DEBUG, "xps_file_destroy()", "destroyed file struct");
}