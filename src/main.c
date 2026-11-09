#include "xps.h"

xps_core_t *core;
xps_config_t *config;

void sigint_handler(int signum);
void core_create();
void core_destory();

int main(int argc, char *argv[])
{
    signal(SIGINT, sigint_handler);

    xps_cliargs_t *cliargs = xps_cliargs_create(argc, argv);

    config = xps_config_create(cliargs->config_path);

    core_create();

    xps_core_start(core);
}

void core_create()
{
    assert(config != NULL);

    core = xps_core_create(config);

    for (int i = 0; i < config->_all_listeners.length; i++)
    {
        xps_config_listener_t *listener_config = config->_all_listeners.data[i];
        if (listener_config)
        {
            xps_listener_t *listener = xps_listener_create(core, listener_config->host, listener_config->port);
            if (!listener)
            {
                logger(LOG_ERROR, "main()", "Failed to start on host %s and port %u", listener_config->host, listener_config->port);
                continue;
            }
            logger(LOG_INFO, "main()", "Server listening on host %s and port %u", listener_config->host, listener_config->port);

            // int dup_fd = dup(listener->sock_fd);
            // xps_listener_t *dup_listener = malloc(sizeof(xps_listener_t));
            // dup_listener->core = core;
            // dup_listener->host = listener->host;
            // dup_listener->core = listener->port;
            // dup_listener->sock_fd = dup_fd;

            // vec_push(&(core->listeners), dup_listener);
            // xps_loop_attach(core->loop, dup_fd, EPOLLIN | EPOLLET, listener, listener_connection_handler, NULL, NULL);
        }
    }
}

void core_destory()
{
    xps_core_destroy(core);
}

void sigint_handler(int signum)
{
    logger(LOG_WARNING, "sigint_handler()", "SIGINT received");

    xps_config_destroy(config);

    core_destory();

    exit(EXIT_SUCCESS);
}