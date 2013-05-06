/*
 * nss_mcdb_netdb - query mcdb of hosts, protocols, networks, services, rpc
 *
 * Copyright (c) 2010, Glue Logic LLC. All rights reserved. code()gluelogic.com
 *
 *  This file is part of mcdb.
 *
 *  mcdb is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  mcdb is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with mcdb.  If not, see <http://www.gnu.org/licenses/>.
 */

/* _BSD_SOURCE or _SVID_SOURCE for struct rpcent on Linux */
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif

#include "nss_mcdb_netdb.h"
#include "nss_mcdb.h"

#include <errno.h>
#include <string.h>

#include <netdb.h>
#include <sys/socket.h>    /* AF_INET */
#include <arpa/inet.h>     /* inet_pton() ntohl() ntohs() */

/*
 * man hosts         (Internet RFC 952)
 *     hostname(1) hostname(7) resolver(3) resolver(5) host.conf resolv.conf
 *     gethostbyname gethostbyaddr sethostent gethostent endhostent
 *     gethostname getdomainname
 *     /etc/hosts
 *
 * man protocols(5)  (POSIX.1-2001)
 *     getprotobyname getprotobynumber setprotoent getprotoent endprotoent
 *     /etc/protocols
 * man networks      (POSIX.1-2001)
 *     getnetbyname getnetbyaddr setnetent getnetent endnetent
 *     /etc/networks
 * man services(5)   (POSIX.1-2001)
 *     getservbyname getservbyport setservent getservent endservent
 *     /etc/services
 *
 * man rpc(5)        (not standard)
 *     getrpcbyname getrpcbynumber setrpcent getrpcent endrpcent rpcinfo rpcbind
 *     /etc/rpc
 *
 * man netgroup      (not standard)
 *     setnetgrent getnetgrent endnetgrent innetgr
 *     /etc/netgroup
 */

/*
 * Notes:
 *
 * Preserve behavior of framework reading one line at a time from flat file.
 * For example, while DNS resolver might return hostent with multiple addresses
 * in char **h_addr_list, queries to flat file /etc/hosts will always return a
 * single address.  Also, if addresses are repeated, the list of aliases
 * contains only those that were on the same line read from the flat file.
 * Similar constraints apply to all netdb mcdb databases.  (Additional parsing
 * of netdb database could encode more intelligent aggregation, but then would
 * not be a transparent, drop-in for flat file databases.)
 *
 * For each *_name element, an entry with '=' tag char is created for *ent().
 * Then, the data is duplicated with '~' tag char for *_name element and for
 * each alias so that single query for a label finds the matching name or alias
 * and returns same entry that would be returned reading flat file line by line
 * (first match).
 *
 * gethostent supports IPv6 in /etc/hosts, unlike glibc, which documents that
 * IPv6 entries are ignored, but glibc gethostent() provides a struct hostent *
 * for IPv6 entries, but h_addrtype is AF_INET (not AF_INET6) and h_addr_list
 * is invalid (at least on RedHat Fedora release 8).
 */

static nss_status_t
nss_mcdb_netdb_hostent_decode(struct mcdb * restrict,
                              const struct nss_mcdb_vinfo * restrict)
  __attribute_nonnull__  __attribute_warn_unused_result__;

static nss_status_t
nss_mcdb_netdb_netent_decode(struct mcdb * restrict,
                             const struct nss_mcdb_vinfo * restrict)
  __attribute_nonnull__  __attribute_warn_unused_result__;

static nss_status_t
nss_mcdb_netdb_protoent_decode(struct mcdb * restrict,
                               const struct nss_mcdb_vinfo * restrict)
  __attribute_nonnull__  __attribute_warn_unused_result__;

static nss_status_t
nss_mcdb_netdb_rpcent_decode(struct mcdb * restrict,
                             const struct nss_mcdb_vinfo * restrict)
  __attribute_nonnull__  __attribute_warn_unused_result__;

static nss_status_t
nss_mcdb_netdb_servent_decode(struct mcdb * restrict,
                              const struct nss_mcdb_vinfo * restrict)
  __attribute_nonnull__  __attribute_warn_unused_result__;


static nss_status_t  __attribute_noinline__
nss_mcdb_netdb_gethost_fill_h_errnop(const nss_status_t status,
                                     int * const restrict h_errnop)
  __attribute_cold__  __attribute_nonnull__;

static nss_status_t
nss_mcdb_netdb_gethost_query(const uint32_t type,
                             const struct nss_mcdb_vinfo * restrict v,
                             int * const restrict h_errnop)
  __attribute_nonnull__;

static nss_status_t
nss_mcdb_netdb_gethost_filladdr(const void * const restrict addr,
                                const int type,
                                const struct nss_mcdb_vinfo * restrict v,
                                int * const restrict h_errnop)
  __attribute_nonnull__;


void _nss_mcdb_sethostent(const int op)  
                                 { nss_mcdb_setent(NSS_DBTYPE_HOSTS,op);     }
void _nss_mcdb_endhostent(void)  { nss_mcdb_endent(NSS_DBTYPE_HOSTS);        }

void _nss_mcdb_setnetent(const int op)   
                                 { nss_mcdb_setent(NSS_DBTYPE_NETWORKS,op);  }
void _nss_mcdb_endnetent(void)   { nss_mcdb_endent(NSS_DBTYPE_NETWORKS);     }

void _nss_mcdb_setprotoent(const int op) 
                                 { nss_mcdb_setent(NSS_DBTYPE_PROTOCOLS,op); }
void _nss_mcdb_endprotoent(void) { nss_mcdb_endent(NSS_DBTYPE_PROTOCOLS);    }

void _nss_mcdb_setrpcent(const int op)   
                                 { nss_mcdb_setent(NSS_DBTYPE_RPC,op);       }
void _nss_mcdb_endrpcent(void)   { nss_mcdb_endent(NSS_DBTYPE_RPC);          }

void _nss_mcdb_setservent(const int op)  
                                 { nss_mcdb_setent(NSS_DBTYPE_SERVICES,op);  }
void _nss_mcdb_endservent(void)  { nss_mcdb_endent(NSS_DBTYPE_SERVICES);     }


/* POSIX.1-2001 marks gethostbyaddr() and gethostbyname() obsolescent.
 * See man getnameinfo(), getaddrinfo(), freeaddrinfo(), gai_strerror()
 * However, note that getaddrinfo() allocates memory for its results 
 * (linked list of struct addrinfo) that must be free()d with freeaddrinfo(). */

nss_status_t
_nss_mcdb_gethostent_r(struct hostent * const restrict hostbuf,
                       char * const restrict buf, const size_t bufsz,
                       int * const restrict errnop,
                       int * const restrict h_errnop)
{
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_netdb_hostent_decode,
                                      .vstruct = hostbuf,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop };
    nss_status_t status;
    if (bufsz > 3) {
        buf[0] = buf[1] = buf[2] = buf[3] = '\0'; /* addr type AF_UNSPEC == 0 */
        status = nss_mcdb_getent(NSS_DBTYPE_HOSTS, &v);
    }
    else {
        *errnop = errno = ERANGE;
        status = NSS_STATUS_TRYAGAIN;
    }
    return (status == NSS_STATUS_SUCCESS)
      ? NSS_STATUS_SUCCESS
      : nss_mcdb_netdb_gethost_fill_h_errnop(status, h_errnop);
}

nss_status_t
_nss_mcdb_gethostbyname2_r(const char * const restrict name, const int type,
                           struct hostent * const restrict hostbuf,
                           char * const restrict buf, const size_t bufsz,
                           int * const restrict errnop,
                           int * const restrict h_errnop)
{
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_netdb_hostent_decode,
                                      .vstruct = hostbuf,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop,
                                      .key     = name,
                                      .klen    = strlen(name),
                                      .tagc    = (unsigned char)'~' };
    uint64_t addr[2] = {0,0};/* support addr sizes up to 128-bits (e.g. IPv6) */
    const int is_addr = inet_pton(type, name, &addr);
    if (is_addr == 0) /* name is not valid address for specified addr family */
        return nss_mcdb_netdb_gethost_query((uint32_t)type, &v, h_errnop);
    else if (is_addr > 0) /* name is valid address for specified addr family */
        return nss_mcdb_netdb_gethost_filladdr(&addr, type, &v, h_errnop);
    else /* invalid address family EAFNOSUPPORT => NSS_STATUS_RETURN */
        return nss_mcdb_netdb_gethost_fill_h_errnop(NSS_STATUS_RETURN,h_errnop);
}

nss_status_t
_nss_mcdb_gethostbyname_r(const char * const restrict name,
                          struct hostent * const restrict hostbuf,
                          char * const restrict buf, const size_t bufsz,
                          int * const restrict errnop,
                          int * const restrict h_errnop)
{
    return _nss_mcdb_gethostbyname2_r(name, AF_INET, hostbuf,
                                      buf, bufsz, errnop, h_errnop);
}

nss_status_t
_nss_mcdb_gethostbyaddr_r(const void * const restrict addr,
                          const socklen_t len, const int type,
                          struct hostent * const restrict hostbuf,
                          char * const restrict buf, const size_t bufsz,
                          int * const restrict errnop,
                          int * const restrict h_errnop)
{
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_netdb_hostent_decode,
                                      .vstruct = hostbuf,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop,
                                      .key     = addr,
                                      .klen    = len,
                                      .tagc    = (unsigned char)'b' };
    return nss_mcdb_netdb_gethost_query((uint32_t)type, &v, h_errnop);
}


#if 0  /* implemented, but not enabling by default; often used only with NIS+ */

/* netgroup setent, getent, endent differ from other netdb *ent routines:
 * setnetgrent() requires netgroup parameter which limits getnetgrent() results
 * instead of getnetgrent() iterating over entries in entire netgroup database*/

/* Note: netgroup creation in nss_mcdb_netdb_make not implemented yet,
 * so implementation below might change, if needed */

int
_nss_mcdb_setnetgrent(const char * const restrict netgroup)
{
    int errnum;
    const struct nss_mcdb_vinfo v = { .errnop  = &errnum,
                                      .key     = netgroup,
                                      .klen    = strlen(netgroup),
                                      .tagc    = 0 };
    return nss_mcdb_getentstart(NSS_DBTYPE_NETGROUP, &v);
}

void _nss_mcdb_endnetgrent(void) { nss_mcdb_endent(NSS_DBTYPE_NETGROUP);  }

nss_status_t
_nss_mcdb_getnetgrent_r(char ** const restrict host,
                        char ** const restrict user,
                        char ** const restrict domain,
                        char * const restrict buf, const size_t bufsz,
                        int * const restrict errnop)
{
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_buf_decode,
                                      .vstruct = NULL,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop,
                                      .tagc    = 0 };
    const nss_status_t status = nss_mcdb_getentnext(NSS_DBTYPE_NETGROUP, &v);
    if (status == NSS_STATUS_SUCCESS) {
        /* success ensures valid data (so that memchr will not return NULL) */
        *host   = buf;
        *user   = (char *)memchr(buf,   '\0', bufsz) + 1;
        *domain = (char *)memchr(*user, '\0', bufsz - (*user - buf)) + 1;
        if (!**host)   *host   = NULL;
        if (!**user)   *user   = NULL;
        if (!**domain) *domain = NULL;
    }
    return status;
}

#endif


nss_status_t
_nss_mcdb_getnetent_r(struct netent * const restrict netbuf,
                      char * const restrict buf, const size_t bufsz,
                      int * const restrict errnop,
                      int * const restrict h_errnop)
{
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_netdb_netent_decode,
                                      .vstruct = netbuf,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop };
    const nss_status_t status = nss_mcdb_getent(NSS_DBTYPE_NETWORKS, &v);
    return (status == NSS_STATUS_SUCCESS)
      ? NSS_STATUS_SUCCESS
      : nss_mcdb_netdb_gethost_fill_h_errnop(status, h_errnop);
}

nss_status_t
_nss_mcdb_getnetbyname_r(const char * const restrict name,
                         struct netent * const restrict netbuf,
                         char * const restrict buf, const size_t bufsz,
                         int * const restrict errnop,
                         int * const restrict h_errnop)
{
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_netdb_netent_decode,
                                      .vstruct = netbuf,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop,
                                      .key     = name,
                                      .klen    = strlen(name),
                                      .tagc    = (unsigned char)'~' };
    const nss_status_t status =
      nss_mcdb_get_generic(NSS_DBTYPE_NETWORKS, &v);
    return (status == NSS_STATUS_SUCCESS)
      ? NSS_STATUS_SUCCESS
      : nss_mcdb_netdb_gethost_fill_h_errnop(status, h_errnop);
}

nss_status_t
_nss_mcdb_getnetbyaddr_r(const uint32_t net, const int type,
                         struct netent * const restrict netbuf,
                         char * const restrict buf, const size_t bufsz,
                         int * const restrict errnop,
                         int * const restrict h_errnop)
{
    uint32_t n[2];
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_netdb_netent_decode,
                                      .vstruct = netbuf,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop,
                                      .key     = (const char *)n,
                                      .klen    = sizeof(n),
                                      .tagc    = (unsigned char)'x' };
    nss_status_t status;
    n[0] = htonl(net);
    n[1] = htonl((uint32_t)type);
    status = nss_mcdb_get_generic(NSS_DBTYPE_NETWORKS, &v);
    return (status == NSS_STATUS_SUCCESS)
      ? NSS_STATUS_SUCCESS
      : nss_mcdb_netdb_gethost_fill_h_errnop(status, h_errnop);
}


nss_status_t
_nss_mcdb_getprotoent_r(struct protoent * const restrict protobuf,
                        char * const restrict buf, const size_t bufsz,
                        int * const restrict errnop)
{
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_netdb_protoent_decode,
                                      .vstruct = protobuf,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop };
    return nss_mcdb_getent(NSS_DBTYPE_PROTOCOLS, &v);
}

nss_status_t
_nss_mcdb_getprotobyname_r(const char * const restrict name,
                           struct protoent * const restrict protobuf,
                           char * const restrict buf, const size_t bufsz,
                           int * const restrict errnop)
{
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_netdb_protoent_decode,
                                      .vstruct = protobuf,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop,
                                      .key     = name,
                                      .klen    = strlen(name),
                                      .tagc    = (unsigned char)'~' };
    return nss_mcdb_get_generic(NSS_DBTYPE_PROTOCOLS, &v);
}

nss_status_t
_nss_mcdb_getprotobynumber_r(const int proto,
                             struct protoent * const restrict protobuf,
                             char * const restrict buf, const size_t bufsz,
                             int * const restrict errnop)
{
    const uint32_t n = htonl((uint32_t) proto);
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_netdb_protoent_decode,
                                      .vstruct = protobuf,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop,
                                      .key     = (const char *)&n,
                                      .klen    = sizeof(uint32_t),
                                      .tagc    = (unsigned char)'x' };
    return nss_mcdb_get_generic(NSS_DBTYPE_PROTOCOLS, &v);
}


nss_status_t
_nss_mcdb_getrpcent_r(struct rpcent * const restrict rpcbuf,
                      char * const restrict buf, const size_t bufsz,
                      int * const restrict errnop)
{
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_netdb_rpcent_decode,
                                      .vstruct = rpcbuf,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop };
    return nss_mcdb_getent(NSS_DBTYPE_RPC, &v);
}

nss_status_t
_nss_mcdb_getrpcbyname_r(const char * const restrict name,
                         struct rpcent * const restrict rpcbuf,
                         char * const restrict buf, const size_t bufsz,
                         int * const restrict errnop)
{
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_netdb_rpcent_decode,
                                      .vstruct = rpcbuf,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop,
                                      .key     = name,
                                      .klen    = strlen(name),
                                      .tagc    = (unsigned char)'~' };
    return nss_mcdb_get_generic(NSS_DBTYPE_RPC, &v);
}

nss_status_t
_nss_mcdb_getrpcbynumber_r(const int number,
                           struct rpcent * const restrict rpcbuf,
                           char * const restrict buf, const size_t bufsz,
                           int * const restrict errnop)
{
    const uint32_t n = htonl((uint32_t) number);
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_netdb_rpcent_decode,
                                      .vstruct = rpcbuf,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop,
                                      .key     = (const char *)&n,
                                      .klen    = sizeof(uint32_t),
                                      .tagc    = (unsigned char)'x' };
    return nss_mcdb_get_generic(NSS_DBTYPE_RPC, &v);
}


nss_status_t
_nss_mcdb_getservent_r(struct servent * const restrict servbuf,
                       char * const restrict buf, const size_t bufsz,
                       int * const restrict errnop)
{
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_netdb_servent_decode,
                                      .vstruct = servbuf,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop };
    if (bufsz > 0) {
        *buf = '\0';
        return nss_mcdb_getent(NSS_DBTYPE_SERVICES, &v);
    }
    else {
        *errnop = errno = ERANGE;
        return NSS_STATUS_TRYAGAIN;
    }
}

nss_status_t
_nss_mcdb_getservbyname_r(const char * const restrict name,
                          const char * const restrict proto,
                          struct servent * const restrict servbuf,
                          char * const restrict buf, const size_t bufsz,
                          int * const restrict errnop)
{
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_netdb_servent_decode,
                                      .vstruct = servbuf,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop,
                                      .key     = name,
                                      .klen    = strlen(name),
                                      .tagc    = (unsigned char)'~' };
    const size_t plen = proto != NULL ? strlen(proto) : 0;
    if (bufsz > plen) {
        /*copy proto for later match in nss_mcdb_netdb_servent_decode()*/
        *buf = '\0';
        if (plen)
            memcpy(buf, proto, plen+1);
        return nss_mcdb_get_generic(NSS_DBTYPE_SERVICES, &v);
    }
    else {
        *errnop = errno = ERANGE;
        return NSS_STATUS_TRYAGAIN;
    }
}

nss_status_t
_nss_mcdb_getservbyport_r(const int port, const char * const restrict proto,
                          struct servent * const restrict servbuf,
                          char * const restrict buf, const size_t bufsz,
                          int * const restrict errnop)
{
    /*(port argument is given in network byte order (e.g. via htons()))*/
    const struct nss_mcdb_vinfo v = { .decode  = nss_mcdb_netdb_servent_decode,
                                      .vstruct = servbuf,
                                      .buf     = buf,
                                      .bufsz   = bufsz,
                                      .errnop  = errnop,
                                      .key     = (const char *)&port,
                                      .klen    = sizeof(int),
                                      .tagc    = (unsigned char)'x' };
    const size_t plen = proto != NULL ? strlen(proto) : 0;
    if (bufsz > plen) {
        /*copy proto for later match in nss_mcdb_netdb_servent_decode()*/
        *buf = '\0';
        if (plen)
            memcpy(buf, proto, plen+1);
        return nss_mcdb_get_generic(NSS_DBTYPE_SERVICES, &v);
    }
    else {
        *errnop = errno = ERANGE;
        return NSS_STATUS_TRYAGAIN;
    }
}


static nss_status_t  __attribute_noinline__
nss_mcdb_netdb_gethost_fill_h_errnop(const nss_status_t status,
                                     int * const restrict h_errnop)
{
    if (NSS_STATUS_TRYAGAIN == status)
        *h_errnop = TRY_AGAIN;
    else if (NSS_STATUS_NOTFOUND == status)
        *h_errnop = HOST_NOT_FOUND;
    else if (NSS_STATUS_SUCCESS != status)
        *h_errnop = NO_RECOVERY;/*NSS_STATUS_UNAVAIL, NSS_STATUS_RETURN, other*/
    return status;
}

static nss_status_t
nss_mcdb_netdb_gethost_query(const uint32_t type,
                             const struct nss_mcdb_vinfo * const restrict v,
                             int * const restrict h_errnop)
{
    nss_status_t status;
    if (v->bufsz >= 4) {
        /*copy type for later match in nss_mcdb_netdb_hostent_decode()*/
        char * const restrict buf = v->buf; /*(masks redundant with char cast)*/
        buf[0] = (char)((type              ) >> 24);
        buf[1] = (char)((type & 0x00FF0000u) >> 16);
        buf[2] = (char)((type & 0x0000FF00u) >>  8);
        buf[3] = (char)((type & 0x000000FFu));/*network byte order; big-endian*/
        status = nss_mcdb_get_generic(NSS_DBTYPE_HOSTS, v);
    }
    else {
        *v->errnop = errno = ERANGE;
        status = NSS_STATUS_TRYAGAIN;
    }
    return (status == NSS_STATUS_SUCCESS)
      ? NSS_STATUS_SUCCESS
      : nss_mcdb_netdb_gethost_fill_h_errnop(status, h_errnop);
}

static nss_status_t
nss_mcdb_netdb_gethost_filladdr(const void * const restrict addr,
                                const int type,
                                const struct nss_mcdb_vinfo * const restrict v,
                                int * const restrict h_errnop)
{
    struct hostent * const restrict hostbuf = (struct hostent *)v->vstruct;
    const size_t bufsz = v->bufsz;
    char * const restrict buf = v->buf;
    char * const aligned = (char *)((uintptr_t)(buf+7) & ~0x7);
    /* supports only AF_INET and AF_INET6
     * (if expanding, might create static array of addr sizes indexed by AF_*)*/
    int h_length;
    switch (type) {
      case AF_INET:  h_length = sizeof(struct in_addr);  break;
      case AF_INET6: h_length = sizeof(struct in6_addr); break;
      default: *v->errnop = errno = ENOENT;
               return nss_mcdb_netdb_gethost_fill_h_errnop(NSS_STATUS_UNAVAIL,
                                                           h_errnop);
    }          /* other addr types not implemented */
    /* (pointers in struct hostent must be aligned; align to 8 bytes) */
    /* (align+(char **h_aliases)+(char **h_addr_list)+h_length+addrstr+'\0') */
    if ((aligned - buf) + 8 + 8 + h_length + v->klen + 1 >= bufsz) {
        *v->errnop = errno = ERANGE;
        return nss_mcdb_netdb_gethost_fill_h_errnop(NSS_STATUS_TRYAGAIN,
                                                    h_errnop);
    }
    hostbuf->h_name      = memcpy(aligned+16+h_length, v->key, v->klen+1);
    hostbuf->h_aliases   = (char **)aligned;
    hostbuf->h_addrtype  = type;
    hostbuf->h_length    = h_length;
    hostbuf->h_addr_list = (char **)(aligned+8);
    hostbuf->h_aliases[0]   = NULL;
    hostbuf->h_addr_list[0] = memcpy(aligned+16, addr, (uint32_t)h_length);
    return NSS_STATUS_SUCCESS;
}


/* Deserializing netdb entries shares much similar code -- some duplicated --
 * but each entry differs slightly so that factoring to common routine would
 * result in excess work.  Therefore, common template was copied and modified.
 *
 * The fixed format record header is parsed for numerical data and str offsets.
 * The buf is filled with str data.  For alias string lists, scan for '\0'
 * instead of precalculating array because names should be short and adding an
 * extra 2 bytes per name to store size takes more space and might not result
 * in any gain.
 */

static nss_status_t
nss_mcdb_netdb_hostent_decode(struct mcdb * const restrict m,
                              const struct nss_mcdb_vinfo * const restrict v)
{
    const char * restrict dptr = (char *)mcdb_dataptr(m);
    struct hostent * const he = (struct hostent *)v->vstruct;
    char *buf = v->buf;
    size_t he_mem_num;
    size_t he_lst_num;
    union { uint32_t u[NSS_HE_HDRSZ>>2]; uint16_t h[NSS_HE_HDRSZ>>1]; } hdr;
    const uint32_t type = (((uint32_t)((unsigned char *)buf)[0])<<24)
                         |(((uint32_t)((unsigned char *)buf)[1])<<16)
                         |(((uint32_t)((unsigned char *)buf)[2])<<8)
                         |            ((unsigned char *)buf)[3];

    /* match type (e.g. AF_INET, AF_INET6), if not zero (AF_UNSPEC) */
    if (type != 0) { /*network byte order; big-endian*/
        const unsigned char *atyp = (unsigned char *)dptr+NSS_H_ADDRTYPE;
        while (type != ((((uint32_t)atyp[0])<<24)
                       |(((uint32_t)atyp[1])<<16)
                       |(((uint32_t)atyp[2])<<8)
                       |            atyp[3]  )) {
            if (mcdb_findtagnext_h(m, v->key, v->klen, v->tagc)) {
                dptr = (char *)mcdb_dataptr(m);
                atyp = (unsigned char *)dptr+NSS_H_ADDRTYPE;
            }
            else {
                *v->errnop = errno = ENOENT;
                return NSS_STATUS_NOTFOUND;
            }
        }
    }

    memcpy(hdr.u, dptr, NSS_HE_HDRSZ);  /*(copy header for num data alignment)*/
    he->h_addrtype = (int)    ntohl( hdr.u[NSS_H_ADDRTYPE>>2] );
    he->h_length   = (int)    ntohl( hdr.u[NSS_H_LENGTH>>2] );
    he_mem_num     = (size_t) ntohs( hdr.h[NSS_HE_MEM_NUM>>1] );
    he_lst_num     = (size_t) ntohs( hdr.h[NSS_HE_LST_NUM>>1] );
    he->h_name     = buf;
    he->h_aliases  = /* align to 8-byte boundary for 64-bit */
      (char **)(((uintptr_t)(buf+ntohs(hdr.h[NSS_HE_MEM>>1])+0x7u)) & ~0x7u);
    if (((char *)he->h_aliases)-buf+((he_mem_num+1+he_lst_num+1)<<3)<=v->bufsz){
        char ** const restrict he_mem = he->h_aliases;    /* 8-byte aligned */
        char ** const restrict he_lst = he->h_addr_list = he_mem+he_mem_num+1;
        memcpy(buf, dptr+NSS_HE_HDRSZ, (size_t)mcdb_datalen(m)-NSS_HE_HDRSZ);
        he_mem[0] = (buf += ntohs(hdr.h[NSS_HE_MEM_STR>>1])); /*he_mem strings*/
        for (size_t i=1; i<he_mem_num; ++i) {/*(i=1; assigned first str above)*/
            while (*++buf != '\0')
                ;
            he_mem[i] = ++buf;
        }
        he_mem[he_mem_num] = NULL;         /* terminate (char **) he_mem array*/
        he_lst[0] = buf = v->buf+ntohs(hdr.h[NSS_HE_LST_STR>>1]);/*he_lst str*/
        for (size_t i=1; i<he_lst_num; ++i) {/*(i=1; assigned first str above)*/
            he_lst[i] = (buf += he->h_length);
        }
        he_lst[he_lst_num] = NULL;         /* terminate (char **) he_lst array*/
        return NSS_STATUS_SUCCESS;
    }
    else {
        *v->errnop = errno = ERANGE;
        return NSS_STATUS_TRYAGAIN;
    }
}


static nss_status_t
nss_mcdb_netdb_netent_decode(struct mcdb * const restrict m,
                             const struct nss_mcdb_vinfo * const restrict v)
{
    const char * const restrict dptr = (char *)mcdb_dataptr(m);
  #ifndef _AIX
    struct netent * const ne = (struct netent *)v->vstruct;
  #else
    struct nwent * const ne = (struct nwent *)v->vstruct;
  #endif
    char *buf;
    size_t ne_mem_num;
    union { uint32_t u[NSS_NE_HDRSZ>>2]; uint16_t h[NSS_NE_HDRSZ>>1]; } hdr;
    memcpy(hdr.u, dptr, NSS_NE_HDRSZ);  /*(copy header for num data alignment)*/
    ne->n_addrtype = (int)    ntohl( hdr.u[NSS_N_ADDRTYPE>>2] );
  #ifndef _AIX
    ne->n_net      =          ntohl( hdr.u[NSS_N_NET>>2] );
  #else /*(should get space from v->buf, but one-off handled in AIX-map below)*/
    *(uint32_t *)ne->n_addr = hdr.u[NSS_N_NET>>2]; /*(expects allocated space)*/
    ne->n_length   = (int)    ntohs( hdr.h[NSS_N_LENGTH>>1] );
  #endif
    ne_mem_num     = (size_t) ntohs( hdr.h[NSS_NE_MEM_NUM>>1] );
    ne->n_name     = buf = v->buf;
    ne->n_aliases  = /* align to 8-byte boundary for 64-bit */
      (char **)(((uintptr_t)(buf+ntohs(hdr.h[NSS_NE_MEM>>1])+0x7u)) & ~0x7u);
    if (((char *)ne->n_aliases)-buf+((ne_mem_num+1)<<3) <= v->bufsz) {
        char ** const restrict ne_mem = ne->n_aliases;
        memcpy(buf, dptr+NSS_NE_HDRSZ, (size_t)mcdb_datalen(m)-NSS_NE_HDRSZ);
        ne_mem[0] = (buf += ntohs(hdr.h[NSS_NE_MEM_STR>>1])); /*ne_mem strings*/
        for (size_t i=1; i<ne_mem_num; ++i) {/*(i=1; assigned first str above)*/
            while (*++buf != '\0')
                ;
            ne_mem[i] = ++buf;
        }
        ne_mem[ne_mem_num] = NULL;         /* terminate (char **) ne_mem array*/
        return NSS_STATUS_SUCCESS;
    }
    else {
        *v->errnop = errno = ERANGE;
        return NSS_STATUS_TRYAGAIN;
    }
}


static nss_status_t
nss_mcdb_netdb_protoent_decode(struct mcdb * const restrict m,
                               const struct nss_mcdb_vinfo * const restrict v)
{
    const char * const restrict dptr = (char *)mcdb_dataptr(m);
    struct protoent * const pe = (struct protoent *)v->vstruct;
    char *buf;
    size_t pe_mem_num;
    union { uint32_t u[NSS_PE_HDRSZ>>2]; uint16_t h[NSS_PE_HDRSZ>>1]; } hdr;
    memcpy(hdr.u, dptr, NSS_PE_HDRSZ);  /*(copy header for num data alignment)*/
    pe->p_proto   =          ntohl( hdr.u[NSS_P_PROTO>>2] );
    pe_mem_num    = (size_t) ntohs( hdr.h[NSS_PE_MEM_NUM>>1] );
    pe->p_name    = buf = v->buf;
    pe->p_aliases = /* align to 8-byte boundary for 64-bit */
      (char **)(((uintptr_t)(buf+ntohs(hdr.h[NSS_PE_MEM>>1])+0x7u)) & ~0x7u);
    if (((char *)pe->p_aliases)-buf+((pe_mem_num+1)<<3) <= v->bufsz) {
        char ** const restrict pe_mem = pe->p_aliases;
        memcpy(buf, dptr+NSS_PE_HDRSZ, (size_t)mcdb_datalen(m)-NSS_PE_HDRSZ);
        pe_mem[0] = (buf += ntohs(hdr.h[NSS_PE_MEM_STR>>1])); /*pe_mem strings*/
        for (size_t i=1; i<pe_mem_num; ++i) {/*(i=1; assigned first str above)*/
            while (*++buf != '\0')
                ;
            pe_mem[i] = ++buf;
        }
        pe_mem[pe_mem_num] = NULL;         /* terminate (char **) pe_mem array*/
        return NSS_STATUS_SUCCESS;
    }
    else {
        *v->errnop = errno = ERANGE;
        return NSS_STATUS_TRYAGAIN;
    }
}


static nss_status_t
nss_mcdb_netdb_rpcent_decode(struct mcdb * const restrict m,
                             const struct nss_mcdb_vinfo * const restrict v)
{
    const char * const restrict dptr = (char *)mcdb_dataptr(m);
    struct rpcent * const re = (struct rpcent *)v->vstruct;
    char *buf;
    size_t re_mem_num;
    union { uint32_t u[NSS_RE_HDRSZ>>2]; uint16_t h[NSS_RE_HDRSZ>>1]; } hdr;
    memcpy(hdr.u, dptr, NSS_RE_HDRSZ);  /*(copy header for num data alignment)*/
    re->r_number  =          ntohl( hdr.u[NSS_R_NUMBER>>2] );
    re_mem_num    = (size_t) ntohs( hdr.h[NSS_RE_MEM_NUM>>1] );
    re->r_name    = buf = v->buf;
    re->r_aliases = /* align to 8-byte boundary for 64-bit */
      (char **)(((uintptr_t)(buf+ntohs(hdr.h[NSS_RE_MEM>>1])+0x7u)) & ~0x7u);
    if (((char *)re->r_aliases)-buf+((re_mem_num+1)<<3) <= v->bufsz) {
        char ** const restrict re_mem = re->r_aliases;
        memcpy(buf, dptr+NSS_RE_HDRSZ, (size_t)mcdb_datalen(m)-NSS_RE_HDRSZ);
        re_mem[0] = (buf += ntohs(hdr.h[NSS_RE_MEM_STR>>1])); /*re_mem strings*/
        for (size_t i=1; i<re_mem_num; ++i) {/*(i=1; assigned first str above)*/
            while (*++buf != '\0')
                ;
            re_mem[i] = ++buf;
        }
        re_mem[re_mem_num] = NULL;         /* terminate (char **) re_mem array*/
        return NSS_STATUS_SUCCESS;
    }
    else {
        *v->errnop = errno = ERANGE;
        return NSS_STATUS_TRYAGAIN;
    }
}


static nss_status_t
nss_mcdb_netdb_servent_decode(struct mcdb * const restrict m,
                              const struct nss_mcdb_vinfo * const restrict v)
{
    const char * restrict dptr = (char *)mcdb_dataptr(m);
    struct servent * const se = (struct servent *)v->vstruct;
    char *buf = v->buf; /* contains proto string to match (if not "") */
    size_t se_mem_num;
    union { uint32_t u[NSS_SE_HDRSZ>>2]; uint16_t h[NSS_SE_HDRSZ>>1]; } hdr;

    /* match proto string (stored right after header), if not empty string
     * (future: should s_proto match be case-insensitive (strcasecmp())?)
     * (future: could possibly be further optimized for "tcp" "udp" "sctp"
     * (future: might add unique tag char db ents for tcp/udp by name/number) */
    if (*buf != '\0') {
        const size_t protolen = 1 + strlen(buf);
        while (dptr[NSS_SE_HDRSZ] != *buf  /* s_proto  e.g. "tcp" vs "udp" */
               || memcmp(dptr+NSS_SE_HDRSZ, buf, protolen) != 0) {
            if (mcdb_findtagnext_h(m, v->key, v->klen, v->tagc))
                dptr = (char *)mcdb_dataptr(m);
            else {
                *v->errnop = errno = ENOENT;
                return NSS_STATUS_NOTFOUND;
            }
        }
    }

    memcpy(hdr.u, dptr, NSS_SE_HDRSZ);  /*(copy header for num data alignment)*/
    se->s_port    =                 hdr.u[NSS_S_PORT>>2]; /*network byte order*/
    se_mem_num    = (size_t) ntohs( hdr.h[NSS_SE_MEM_NUM>>1] );
    se->s_proto   = buf;
    se->s_name    = buf + ntohs(hdr.h[NSS_S_NAME>>1]);
    se->s_aliases = /* align to 8-byte boundary for 64-bit */
      (char **)(((uintptr_t)(buf+ntohs(hdr.h[NSS_SE_MEM>>1])+0x7u)) & ~0x7u);
    if (((char *)se->s_aliases)-buf+((se_mem_num+1)<<3) <= v->bufsz) {
        char ** const restrict se_mem = se->s_aliases;
        memcpy(buf, dptr+NSS_SE_HDRSZ, (size_t)mcdb_datalen(m)-NSS_SE_HDRSZ);
        se_mem[0] = (buf += ntohs(hdr.h[NSS_SE_MEM_STR>>1])); /*se_mem strings*/
        for (size_t i=1; i<se_mem_num; ++i) {/*(i=1; assigned first str above)*/
            while (*++buf != '\0')
                ;
            se_mem[i] = ++buf;
        }
        se_mem[se_mem_num] = NULL;         /* terminate (char **) se_mem array*/
        return NSS_STATUS_SUCCESS;
    }
    else {
        *v->errnop = errno = ERANGE;
        return NSS_STATUS_TRYAGAIN;
    }
}
