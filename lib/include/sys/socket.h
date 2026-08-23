#ifndef _EFMLIBC_SYS_SOCKET_H
#define _EFMLIBC_SYS_SOCKET_H
#ifdef __cplusplus
#include_next <sys/socket.h>
#else
#include <sys/types.h>

typedef unsigned int socklen_t;
typedef unsigned int sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[108];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char        __data[128 - sizeof(sa_family_t)];
};

struct msghdr {
    void         *msg_name;
    socklen_t     msg_namelen;
    struct iovec *msg_iov;
    size_t        msg_iovlen;
    void         *msg_control;
    size_t        msg_controllen;
    int           msg_flags;
};

struct cmsghdr {
    socklen_t cmsg_len;
    int       cmsg_level;
    int       cmsg_type;
};

#define CMSG_DATA(cmsg)     ((unsigned char *)((struct cmsghdr *)(cmsg) + 1))
#define CMSG_NXTHDR(mhdr, cmsg) \
    (((cmsg) == NULL) \
        ? (struct cmsghdr *)((mhdr)->msg_control) \
        : ((((unsigned char *)(cmsg) + ((struct cmsghdr *)(cmsg))->cmsg_len + \
             sizeof(long) - 1) & ~(sizeof(long)-1)) + sizeof(struct cmsghdr) > \
           (unsigned char *)((mhdr)->msg_control) + (mhdr)->msg_controllen) \
          ? NULL \
          : (struct cmsghdr *)(((unsigned char *)(cmsg) + \
                                ((struct cmsghdr *)(cmsg))->cmsg_len + \
                                sizeof(long) - 1) & ~(sizeof(long)-1))))
#define CMSG_FIRSTHDR(mhdr) \
    ((size_t)(mhdr)->msg_controllen >= sizeof(struct cmsghdr) \
     ? (struct cmsghdr *)(mhdr)->msg_control : (struct cmsghdr *)NULL)
#define CMSG_ALIGN(len)    (((len) + sizeof(long) - 1) & ~(sizeof(long) - 1))
#define CMSG_SPACE(len)    (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#define CMSG_LEN(len)      (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))

struct linger {
    int l_onoff;
    int l_linger;
};

/* socket families */
#define AF_UNSPEC   0
#define AF_UNIX     1
#define AF_LOCAL    1
#define AF_INET     2
#define AF_AX25     3
#define AF_IPX      4
#define AF_APPLETALK 5
#define AF_NETROM   6
#define AF_BRIDGE   7
#define AF_ATMPVC   8
#define AF_X25      9
#define AF_INET6    10
#define AF_ROSE     11
#define AF_DECnet   12
#define AF_NETBEUI  13
#define AF_SECURITY 14
#define AF_KEY      15
#define AF_NETLINK  16
#define AF_ROUTE    16
#define AF_PACKET   17
#define AF_ASH      18
#define AF_ECONET   19
#define AF_ATMSVC   20
#define AF_RDS      21
#define AF_SNA      22
#define AF_IRDA     23
#define AF_PPPOX    24
#define AF_WANPIPE  25
#define AF_LLC      26
#define AF_IB       27
#define AF_MPLS     28
#define AF_CAN      29
#define AF_TIPC     30
#define AF_BLUETOOTH 31
#define AF_IUCV     32
#define AF_RXRPC    33
#define AF_ISDN     34
#define AF_PHONET   35
#define AF_IEEE802154 36
#define AF_CAIF     37
#define AF_ALG      38
#define AF_NFC      39
#define AF_VSOCK    40
#define AF_KCM      41
#define AF_QIPCRTR  42
#define AF_SMC      43
#define AF_XDP      44
#define AF_MAX      45
#define PF_UNSPEC   AF_UNSPEC
#define PF_UNIX     AF_UNIX
#define PF_LOCAL    AF_LOCAL
#define PF_INET     AF_INET
#define PF_INET6    AF_INET6
#define PF_NETLINK  AF_NETLINK
#define PF_ROUTE    AF_ROUTE
#define PF_PACKET   AF_PACKET
#define PF_MAX      AF_MAX

/* socket types */
#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3
#define SOCK_RDM        4
#define SOCK_SEQPACKET  5
#define SOCK_DCCP       6
#define SOCK_PACKET     10
#define SOCK_CLOEXEC    02000000
#define SOCK_NONBLOCK   04000

/* protocol levels */
#define SOL_SOCKET      1
#define SOL_IP          0
#define SOL_IPV6        41
#define SOL_TCP         6
#define SOL_UDP         17

/* socket options */
#define SO_DEBUG        1
#define SO_REUSEADDR    2
#define SO_TYPE         3
#define SO_ERROR        4
#define SO_DONTROUTE    5
#define SO_BROADCAST    6
#define SO_SNDBUF       7
#define SO_RCVBUF       8
#define SO_SNDBUFFORCE  32
#define SO_RCVBUFFORCE  33
#define SO_KEEPALIVE    9
#define SO_OOBINLINE    10
#define SO_NO_CHECK     11
#define SO_PRIORITY     12
#define SO_LINGER       13
#define SO_BSDCOMPAT    14
#define SO_REUSEPORT    15
#define SO_PASSCRED     16
#define SO_PEERCRED     17
#define SO_RCVLOWAT     18
#define SO_SNDLOWAT     19
#define SO_RCVTIMEO     20
#define SO_SNDTIMEO     21
#define SO_ACCEPTCONN   30
#define SO_PEERSEC      31
#define SO_SNDBUFFORCE  32
#define SO_RCVBUFFORCE  33
#define SO_PROTOCOL     38
#define SO_DOMAIN       39

/* msg flags */
#define MSG_OOB         1
#define MSG_PEEK        2
#define MSG_DONTROUTE   4
#define MSG_TRYHARD     4
#define MSG_CTRUNC      8
#define MSG_PROBE       0x10
#define MSG_TRUNC       0x20
#define MSG_DONTWAIT    0x40
#define MSG_EOR         0x80
#define MSG_WAITALL     0x100
#define MSG_FIN         0x200
#define MSG_SYN         0x400
#define MSG_CONFIRM     0x800
#define MSG_RST         0x1000
#define MSG_ERRQUEUE    0x2000
#define MSG_NOSIGNAL    0x4000
#define MSG_MORE        0x8000
#define MSG_WAITFORONE  0x10000
#define MSG_BATCH       0x40000
#define MSG_FASTOPEN    0x20000000
#define MSG_CMSG_CLOEXEC 0x40000000

/* shutdown */
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

/* max backlog */
#define SOMAXCONN  128

/* send/recv msg */
#ifndef _EFMLIBC_IOVEC_DEFINED
#define _EFMLIBC_IOVEC_DEFINED
struct iovec { void *iov_base; size_t iov_len; };
#endif

/* socket level control messages */
#define SCM_RIGHTS     1
#define SCM_CREDENTIALS 2

struct ucred {
    pid_t pid; uid_t uid; gid_t gid;
};

int socket(int domain, int type, int protocol);
int socketpair(int domain, int type, int protocol, int sv[2]);
int shutdown(int sockfd, int how);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);
#endif
#endif
