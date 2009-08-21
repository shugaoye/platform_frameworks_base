/*
 * Copyright 2008, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Yi Sun(beyounn@gmail.com)
 */

#define LOG_TAG "ethernet"

#include "jni.h"
#include <utils/misc.h>
#include <android_runtime/AndroidRuntime.h>
#include <utils/Log.h>
#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <net/if_arp.h>

#define ETH_PKG_NAME "android/net/ethernet/EthernetNative"

namespace android {
    static struct fieldIds {
        jclass dhcpInfoClass;
        jmethodID constructorId;
        jfieldID ipaddress;
        jfieldID gateway;
        jfieldID netmask;
        jfieldID dns1;
        jfieldID dns2;
        jfieldID serverAddress;
        jfieldID leaseDuration;
    } dhcpInfoFieldIds;

    typedef struct _interface_info_t {
        int i;                            /* interface index        */
        char *name;                       /* name (eth0, eth1, ...) */
        struct in_addr ip_addr;           /* IPv4 address (0=none)  */
        struct in6_addr ip6_addr;         /* IPv6 address (0=none)  */
        unsigned char mac[8];             /* MAC address            */
        struct _interface_info_t *next;
    } interface_info_t;

    interface_info_t *interfaces = NULL;
    int total_int = 0;
#define NL_SOCK_INV      -1
#define RET_STR_SZ       4096
#define NL_POLL_MSG_SZ   8*1024
    static int nl_socket_msg = NL_SOCK_INV;
    static struct sockaddr_nl addr_msg;
    static int nl_socket_poll = NL_SOCK_INV;
    static struct sockaddr_nl addr_poll;
    static int getinterfacename(int index, char *name, size_t len);

    static interface_info_t *find_info_by_index(int index){
        interface_info_t *info = interfaces;
        while( info) {
            if (info->i == index)
                return info;
            info = info->next;
        }
        return NULL;
    }

    static jstring android_net_ethernet_waitForEvent(JNIEnv *env,
                                                     jobject clazz)
    {
        struct pollfd pfd;
        char *buff;
        struct nlmsghdr *nh;
        struct ifinfomsg *einfo;
        struct iovec iov;
        struct msghdr msg;
        char *result = NULL;
        char rbuf[4096];
        unsigned int left;
        interface_info_t *info;

        LOGI("Poll events from ethernet devices");
        /*
         *wait on uevent netlink socket for the ethernet device
         */
        buff = (char *)malloc(NL_POLL_MSG_SZ);
        if (!buff) {
            LOGE("Allocate poll buffer failed");
            goto error;
        }

        iov.iov_base = buff;
        iov.iov_len = NL_POLL_MSG_SZ;
        pfd.events = POLLIN;
        pfd.fd = nl_socket_poll;
        if (poll(&pfd,1, -1) != -1) {
            int len;
            msg.msg_name = (void *)&addr_msg;
            msg.msg_namelen =  sizeof(addr_msg);
            msg.msg_iov =  &iov;
            msg.msg_iovlen =  1;
            msg.msg_control =  NULL;
            msg.msg_controllen =  0;
            msg.msg_flags =  0;
            len = recvmsg(pfd.fd, &msg, 0);
            if (len <= 0 ) {
                LOGE("Can not receive data from netlink socket");
                goto error;
            }
            if (len == -1) {
                LOGE("Can not read from netlink socket for if state");
                goto error;
            }
            result = rbuf;
            left = 4096;
            for (nh = (struct nlmsghdr *) buff; NLMSG_OK (nh, len);
                 nh = NLMSG_NEXT (nh, len))
            {

                if (nh->nlmsg_type == NLMSG_DONE){
                    LOGE("Did not find useful eth interface information");
                    goto error;
                }

                if (nh->nlmsg_type == NLMSG_ERROR){

                    /* Do some error handling. */
                    LOGE("Read device name failed");
                    goto error;
                }

                if (nh->nlmsg_type == RTM_DELLINK ||
                    nh->nlmsg_type == RTM_NEWLINK ||
                    nh->nlmsg_type == RTM_DELADDR ||
                    nh->nlmsg_type == RTM_NEWADDR) {
                  if ( (info = find_info_by_index
                        (((struct ifinfomsg*)
                          NLMSG_DATA(nh))->ifi_index))!=NULL)
                    snprintf(result,left, "%s:%d:",info->name,
                             nh->nlmsg_type);
                    left = left - strlen(result);
                    result =(char *)(result+ strlen(result));
                }

            }
        }
        LOGI("poll state :%s",rbuf);
    error:
        if(buff)
            free(buff);
        return env->NewStringUTF(rbuf);
    }

    static int netlink_send_dump_request(int sock, int type, int family) {
        int ret;
        char buf[4096];
        struct sockaddr_nl snl;
        struct nlmsghdr *nlh;
        struct rtgenmsg *g;

        memset(&snl, 0, sizeof(snl));
        snl.nl_family = AF_NETLINK;

        memset(buf, 0, sizeof(buf));
        nlh = (struct nlmsghdr *)buf;
        g = (struct rtgenmsg *)(buf + sizeof(struct nlmsghdr));

        nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg));
        nlh->nlmsg_flags = NLM_F_REQUEST|NLM_F_DUMP;
        nlh->nlmsg_type = type;
        g->rtgen_family = family;

        ret = sendto(sock, buf, nlh->nlmsg_len, 0, (struct sockaddr *)&snl,
                     sizeof(snl));
        if (ret < 0) {
            perror("netlink_send_dump_request sendto");
            return -1;
        }

        return ret;
    }

    static void free_int_list() {
        interface_info_t *tmp = interfaces;
        while(tmp) {
            if (tmp->name) free(tmp->name);
            interfaces = tmp->next;
            free(tmp);
            tmp = interfaces;
            total_int--;
        }
        if (total_int != 0 )
        {
            LOGE("Wrong interface count found");
            total_int = 0;
        }
    }

    static void add_int_to_list(interface_info_t *node) {
        /*
         *Todo: Lock here!!!!
         */
        node->next = interfaces;
        interfaces = node;
        total_int ++;
    }

    /**
     * Most of the code is copied from anaconda
     * Initialize the interfaces linked list with the interface name, MAC
     * address, and IP addresses.  This function is only called once to
     * initialize the structure, but may be called again if the structure
     * should be reinitialized.
     *
     * @return 0 on succes, -1 on error.
     */
    static int netlink_init_interfaces_list(void) {
        int sock, len, alen, r;
        char buf[4096];
        struct nlmsghdr *nlh;
        struct ifinfomsg *ifi;
        struct rtattr *rta;
        struct rtattr *tb[IFLA_MAX+1];
        interface_info_t *intfinfo;
        int ret = -1;

        LOGI("==>%s",__FUNCTION__);
        if (interfaces) {
            free_int_list();
        }

        /* send dump request */
        if (netlink_send_dump_request(nl_socket_msg,
                                      RTM_GETLINK, AF_NETLINK) == -1) {
            LOGE("netlink_send_dump_request "
                   "in netlink_init_interfaces_table");
            goto error;
        }

        /* read back messages */
        memset(buf, 0, sizeof(buf));
        ret = recvfrom(nl_socket_msg, buf, sizeof(buf), 0, NULL, 0);
        if (ret < 0) {
            LOGE("recvfrom in netlink_init_interfaces_table");
            goto error;
        }

        nlh = (struct nlmsghdr *) buf;
        while (NLMSG_OK(nlh, ret)) {
            switch (nlh->nlmsg_type) {
            case NLMSG_DONE:
                break;
            case RTM_NEWLINK:
                break;
            default:
                nlh = NLMSG_NEXT(nlh, ret);
                continue;
            }

            /* RTM_NEWLINK */
            memset(tb, 0, sizeof(tb));
            memset(tb, 0, sizeof(struct rtattr *) * (IFLA_MAX + 1));

            ifi = (struct ifinfomsg *)NLMSG_DATA(nlh);
            rta = IFLA_RTA(ifi);
            len = IFLA_PAYLOAD(nlh);

            /* void and none are bad */
            if (ifi->ifi_type != ARPHRD_ETHER) {
                nlh = NLMSG_NEXT(nlh, ret);
                continue;
            }

            while (RTA_OK(rta, len)) {
                if (rta->rta_type <= len)
                    tb[rta->rta_type] = rta;
                rta = RTA_NEXT(rta, len);
            }

            alen = RTA_PAYLOAD(tb[IFLA_ADDRESS]);

            /* we have an ethernet MAC addr if alen=6 */
            if (alen == 6) {
                /* make some room! */
                intfinfo = (interface_info_t *)
                    malloc(sizeof(struct _interface_info_t));
                if (intfinfo == NULL) {
                    LOGE("malloc in netlink_init_interfaces_table");
                    ret = -1;
                    goto error;
                }

                /* copy the interface index */
                intfinfo->i = ifi->ifi_index;

                /* copy the interface name (eth0, eth1, ...) */
                intfinfo->name = strndup((char *) RTA_DATA(tb[IFLA_IFNAME]),
                                         sizeof(RTA_DATA(tb[IFLA_IFNAME])));
                LOGI("interface %s found,type:%d",intfinfo->name,
                     ifi->ifi_type);
#if 0
                /* copy the MAC addr */
                memcpy(&intfinfo->mac, RTA_DATA(tb[IFLA_ADDRESS]), alen);

                /* get the IPv4 address of this interface (if any) */
                r = netlink_get_interface_ip(intfinfo->i, AF_INET, &intfinfo->ip_addr);
                if (r == -1)
                    intfinfo->ip_addr.s_addr = 0;

                /* get the IPv6 address of this interface (if any) */
                r = netlink_get_interface_ip(intfinfo->i,AF_INET6,
                                             &intfinfo->ip6_addr);
                if (r == -1)
                    memset(intfinfo->ip6_addr.s6_addr, 0,
                           sizeof(intfinfo->ip6_addr.s6_addr));
#endif
                add_int_to_list(intfinfo);

            }

            /* next netlink msg */
            nlh = NLMSG_NEXT(nlh, ret);
        }
        LOGI("%s exit with success",__FUNCTION__);
        return 0;
    error:
        LOGE("%s exit with error",__FUNCTION__);
        free_int_list();
        return ret;
    }


    /*
     * The netlink socket
     */

    static jint android_net_ethernet_initEthernetNative(JNIEnv *env,
                                                        jobject clazz)
    {
        int ret = -1;

        LOGI("==>%s",__FUNCTION__);
        memset(&addr_msg, 0, sizeof(sockaddr_nl));
        addr_msg.nl_family = AF_NETLINK;
        memset(&addr_poll, 0, sizeof(sockaddr_nl));
        addr_poll.nl_family = AF_NETLINK;
        addr_poll.nl_pid = 0;//getpid();
        addr_poll.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR;

        /*
         *Create connection to netlink socket
         */
        nl_socket_msg = socket(AF_NETLINK,SOCK_RAW,NETLINK_ROUTE);
        if (nl_socket_msg <= 0) {
            LOGE("Can not create netlink msg socket");
            goto error;
        }
        if (bind(nl_socket_msg, (struct sockaddr *)(&addr_msg),
                 sizeof(struct sockaddr_nl))) {
            LOGE("Can not bind to netlink msg socket");
            goto error;
        }

        nl_socket_poll = socket(AF_NETLINK,SOCK_RAW,NETLINK_ROUTE);
        if (nl_socket_poll <= 0) {
            LOGE("Can not create netlink poll socket");
            goto error;
        }

        errno = 0;
        if(bind(nl_socket_poll, (struct sockaddr *)(&addr_poll),
                sizeof(struct sockaddr_nl))) {
            LOGE("Can not bind to netlink poll socket,%s",strerror(errno));

            goto error;
        }

        if ((ret = netlink_init_interfaces_list()) < 0) {
            LOGE("Can not collect the interface list");
            goto error;
        }
        LOGE("%s exited with success",__FUNCTION__);
        return ret;
    error:
        LOGE("%s exited with error",__FUNCTION__);
        if (nl_socket_msg >0)
            close(nl_socket_msg);
        if (nl_socket_poll >0)
            close(nl_socket_poll);
        return ret;
    }

    static jstring android_net_ethernet_getInterfaceName(JNIEnv *env,
                                                         jobject clazz,
                                                         jint index)
    {
        int i = 0;
        interface_info_t *info;
        LOGI("User ask for device name on %d, list:%X, total:%d",index,
             (unsigned int)interfaces, total_int);
        info= interfaces;
        if (total_int != 0 && index <= (total_int -1)) {
            while (info != NULL) {
                if (index == i) {
                    LOGI("Found :%s",info->name);
                    return env->NewStringUTF(info->name);
                }
                info = info->next;
                i ++;
            }
        }
        LOGI("No device name found");
        return env->NewStringUTF(NULL);
    }


    static jint android_net_ethernet_getInterfaceCnt() {
        return total_int;
    }

    static JNINativeMethod gEthernetMethods[] = {
        {"waitForEvent", "()Ljava/lang/String;",
         (void *)android_net_ethernet_waitForEvent},
        {"getInterfaceName", "(I)Ljava/lang/String;",
         (void *)android_net_ethernet_getInterfaceName},
        {"initEthernetNative", "()I",
         (void *)android_net_ethernet_initEthernetNative},
        {"getInterfaceCnt","()I",
         (void *)android_net_ethernet_getInterfaceCnt}
    };

    int register_android_net_ethernet_EthernetManager(JNIEnv* env)
    {
        jclass eth = env->FindClass(ETH_PKG_NAME);
        LOGI("Loading ethernet jni class");
        LOG_FATAL_IF( eth== NULL, "Unable to find class " ETH_PKG_NAME);

        dhcpInfoFieldIds.dhcpInfoClass =
            env->FindClass("android/net/DhcpInfo");
        if (dhcpInfoFieldIds.dhcpInfoClass != NULL) {
            dhcpInfoFieldIds.constructorId =
                env->GetMethodID(dhcpInfoFieldIds.dhcpInfoClass,
                                 "<init>", "()V");
            dhcpInfoFieldIds.ipaddress =
                env->GetFieldID(dhcpInfoFieldIds.dhcpInfoClass,
                                "ipAddress", "I");
            dhcpInfoFieldIds.gateway =
                env->GetFieldID(dhcpInfoFieldIds.dhcpInfoClass,
                                "gateway", "I");
            dhcpInfoFieldIds.netmask =
                env->GetFieldID(dhcpInfoFieldIds.dhcpInfoClass,
                                "netmask", "I");
            dhcpInfoFieldIds.dns1 =
                env->GetFieldID(dhcpInfoFieldIds.dhcpInfoClass, "dns1", "I");
            dhcpInfoFieldIds.dns2 =
                env->GetFieldID(dhcpInfoFieldIds.dhcpInfoClass, "dns2", "I");
            dhcpInfoFieldIds.serverAddress =
                env->GetFieldID(dhcpInfoFieldIds.dhcpInfoClass,
                                "serverAddress", "I");
            dhcpInfoFieldIds.leaseDuration =
                env->GetFieldID(dhcpInfoFieldIds.dhcpInfoClass,
                                "leaseDuration", "I");
        }

        return AndroidRuntime::registerNativeMethods(env,
                                                     ETH_PKG_NAME,
                                                     gEthernetMethods,
                                                     NELEM(gEthernetMethods));
    }

};
