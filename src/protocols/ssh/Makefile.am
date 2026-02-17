AUTOMAKE_OPTIONS = foreign 
ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libguac-client-ssh.la

libguac_client_ssh_la_SOURCES = \
    argv.c                      \
    client.c                    \
    clipboard.c                 \
    command-acl.c               \
    input.c                     \
    pipe.c                      \
    settings.c                  \
    sftp.c                      \
    ssh.c                       \
    ttymode.c                   \
    user.c                      \
    command_logger.c

noinst_HEADERS = \
    argv.h                      \
    client.h                    \
    clipboard.h                 \
    command-acl.h               \
    input.h                     \
    pipe.h                      \
    settings.h                  \
    sftp.h                      \
    ssh.h                       \
    ttymode.h                   \
    user.h                      \
    command_logger.h

# Add agent sources if enabled
if ENABLE_SSH_AGENT
libguac_client_ssh_la_SOURCES += ssh_agent.c
noinst_HEADERS += ssh_agent.h
endif

libguac_client_ssh_la_CFLAGS = \
    -Werror -Wall -Iinclude    \
    @COMMON_SSH_INCLUDE@       \
    @LIBGUAC_INCLUDE@          \
    @TERMINAL_INCLUDE@

libguac_client_ssh_la_LIBADD = \
    @COMMON_LTLIB@             \
    @COMMON_SSH_LTLIB@         \
    @LIBGUAC_LTLIB@            \
    @TERMINAL_LTLIB@

libguac_client_ssh_la_LDFLAGS = \
    -version-info 0:0:0         \
    @PTHREAD_LIBS@              \
    @SSH_LIBS@                  \
    @SSL_LIBS@
