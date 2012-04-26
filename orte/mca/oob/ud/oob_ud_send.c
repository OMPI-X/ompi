/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2011-2012 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 *
 */

#include "oob_ud.h"

#define min(a,b) ((a) < (b) ? (a) : (b))

#define MCA_OOB_UD_IOV_SIZE(iovec, count, size)                \
    do {                                                \
        int i;                                                \
        for (i = 0, (size) = 0 ; i < (count) ; ++i) {        \
            (size) += (iovec)[i].iov_len;                \
        }                                                \
    } while (0);

static void mca_oob_ud_send_cb (mca_oob_ud_msg_t *msg, int rc)
{
    mca_oob_ud_send_complete (msg->req, ORTE_SUCCESS == rc ? msg->hdr->msg_data.req.data_len : rc);
}

static int mca_oob_ud_send_self (struct iovec* iov, int count, int tag,
                                 int flags, orte_rml_callback_fn_t cbfunc,
                                 void* cbdata)
{
    unsigned int srco, dsto;
    mca_oob_ud_req_t *req;
    int srci, dsti;
    int rc, size;

    MCA_OOB_UD_IOV_SIZE(iov, count, size);

    OPAL_OUTPUT_VERBOSE((10, mca_oob_base_output, "%s mca_oob_ud_send_self: sending %d bytes to myself",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), size));

    rc = mca_oob_ud_get_recv_req (*ORTE_PROC_MY_NAME, tag, &req);
    if (ORTE_SUCCESS != rc) {
        return rc;
    }

    req->req_rem_data_len = size;
    req->req_is_eager     = true;

    rc = mca_oob_ud_recv_alloc (req);
    if (ORTE_SUCCESS != rc) {
        opal_output (0, "%s oob:ud:recv_start malloc failed!", ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
        if (MCA_OOB_UD_REQ_UNEX == req->type) {
            free (req->req_uiov);
            OBJ_RELEASE(req);
        }

        return rc;
    }

    srci = dsti = 0;
    srco = dsto = 0;

    do {
        size_t copy = min(iov[srci].iov_len - srco, 
                          req->req_uiov[dsti].iov_len - dsto);

        memmove ((unsigned char *) req->req_uiov[dsti].iov_base + dsto, 
                 (unsigned char *) iov[srci].iov_base + srco, copy);

        srco += copy;
        if (srco == iov[srci].iov_len) {
            srci++;
            srco = 0;
        }

        dsto += copy;
        if (dsto == req->req_uiov[dsti].iov_len) {
            dsti++;
            dsto = 0;
        }
    } while (srci < req->req_count && dsti < count);

    req->state = MCA_OOB_UD_REQ_COMPLETE;

    OPAL_OUTPUT_VERBOSE((10, mca_oob_base_output, "%s mca_oob_ud_send_self: complete. calling callbacks",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));

    /* queue up recv callback */
    mca_oob_ud_event_queue_completed (req);

    if (NULL != cbfunc) {
        cbfunc (ORTE_SUCCESS, ORTE_PROC_MY_NAME,
                iov, count, tag, cbdata);
    }

    return size;
}

int mca_oob_ud_send_nb(orte_process_name_t* target, 
                       orte_process_name_t* origin, 
                       struct iovec* iov, int count, int tag,
                       int flags, orte_rml_callback_fn_t cbfunc,
                       void* cbdata)
{
    mca_oob_ud_peer_t *peer;
    mca_oob_ud_port_t *port;
    mca_oob_ud_msg_t  *req_msg;
    mca_oob_ud_req_t  *send_req;
    bool send_eager = false;
    char *pack_ptr;
    int rc, size, i;

    if (OPAL_EQUAL == orte_util_compare_name_fields
        (ORTE_NS_CMP_ALL, ORTE_PROC_MY_NAME, target)) {
        return mca_oob_ud_send_self (iov, count, tag, flags,
                                     cbfunc, cbdata);
    }

    rc = mca_oob_ud_peer_lookup (target, &peer);
    if(ORTE_SUCCESS != rc || NULL == peer) {
        opal_output (0, "%s oob:ud:send_nb peer %s not found",
                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                     ORTE_NAME_PRINT(target));
        return (NULL == peer) ? ORTE_ERR_UNREACH : rc;
    }

    /* NTH: TODO -- get a random port? */
    port = (mca_oob_ud_port_t *) opal_list_get_first (&((mca_oob_ud_device_t *)peer->peer_context)->ports);

    send_req = OBJ_NEW(mca_oob_ud_req_t);
    if (!send_req) {
        opal_output(0, "oob:ud:send_nb malloc failed! errno = %d", errno);
        return ORTE_ERR_OUT_OF_RESOURCE;
    }

    MCA_OOB_UD_IOV_SIZE(iov, count, size);

    /* fill in request */
    send_req->req_target = *target;
    send_req->req_origin = *origin;
    send_req->req_uiov   = iov;
    send_req->req_count  = count;
    send_req->req_tag    = tag;
    send_req->req_flags  = flags;
    send_req->req_cbfunc = cbfunc;
    send_req->req_cbdata = cbdata;
    send_req->req_peer   = peer;
    send_req->req_mtu    = port->mtu;
    send_req->req_port   = port;
    send_req->req_rc     = 0;

    send_req->state      = MCA_OOB_UD_REQ_PENDING;
    send_req->type       = MCA_OOB_UD_REQ_SEND;

    OBJ_RETAIN(peer);

    if (size + sizeof (mca_oob_ud_msg_hdr_t) <= (unsigned int)port->mtu) {
        send_eager = true;
    }

    rc = mca_oob_ud_msg_get (port, send_req, &port->listen_qp, peer, false, &req_msg);
    if (ORTE_SUCCESS != rc) {
        OBJ_RELEASE (send_req);
        return rc;
    }

    /* fill in message header */
    req_msg->hdr->msg_type     = MCA_OOB_UD_MSG_REQUEST;
    req_msg->hdr->msg_rem_ctx  = send_req;

    req_msg->hdr->msg_origin   = *origin;

    req_msg->hdr->msg_data.req.data_len = size;
    req_msg->hdr->msg_data.req.mtu      = port->mtu;
    req_msg->hdr->msg_data.req.tag      = tag;
    OPAL_OUTPUT_VERBOSE((10, mca_oob_base_output, "%s-%s mca_oob_ud_send_nb: tag %d size %lu. msg: %p. peer = %p. req = %p."
                         "count = %d. uiov = %p. flags = %d\n",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(target),
                         tag, (unsigned long)size,
                         (void *) req_msg,
                         (void *) peer, (void *) send_req, count, (void *) iov, flags));

    if (!send_eager) {
        mca_oob_ud_req_append_to_list (send_req, &mca_oob_ud_component.ud_active_sends);

        /* send request */
        return mca_oob_ud_msg_post_send (req_msg);
    }

    pack_ptr = (char *)(req_msg->hdr + 1);

    for (i = 0 ; i < count ; ++i) {
        memcpy (pack_ptr, iov[i].iov_base, iov[i].iov_len);
        pack_ptr += iov[i].iov_len;
    }

    send_req->req_list = NULL;

    req_msg->hdr->msg_data.req.data_follows = true;

    req_msg->cbfunc = mca_oob_ud_send_cb;
    req_msg->req    = send_req;

    do {
        /* send request */
        rc = mca_oob_ud_msg_post_send (req_msg);
        if (ORTE_SUCCESS != rc) {
            opal_output (0, "msg send failed with status = %d", rc);
            break;
        }
    } while (0);

    return rc;
}

static void mca_oob_ud_send_try_to (int fd, short event, void *ctx)
{
    OPAL_THREAD_LOCK(&mca_oob_ud_component.ud_match_lock);
    (void) mca_oob_ud_send_try ((mca_oob_ud_req_t *) ctx);
    OPAL_THREAD_UNLOCK(&mca_oob_ud_component.ud_match_lock);
}

int mca_oob_ud_send_try (mca_oob_ud_req_t *send_req) {
    int wr_index, wr_count, sge_count, sge_index, iov_index;
    unsigned int iov_left, iov_offset, packet_size;
    const unsigned int mtu = send_req->req_mtu;
    const struct timeval aquire_timeout = {0, 500000};
    mca_oob_ud_msg_t *com_msg;
    int data_len, rc;

    OPAL_OUTPUT_VERBOSE((10, mca_oob_base_output, "%s oob:ud:send_try sending to %s, tag = %d, "
                         "count = %d. req = %p",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(&send_req->req_peer->peer_name),
                         send_req->req_tag, send_req->req_count,
                         (void *) send_req));

    do {
        if (NULL == send_req->req_qp) {
            rc = mca_oob_ud_qp_data_aquire (send_req->req_port, &send_req->req_qp);
            if (ORTE_SUCCESS != rc) {
                break;
            }
        }

        (void) mca_oob_ud_qp_purge (send_req->req_qp);

        rc = mca_oob_ud_msg_get (send_req->req_port, send_req, send_req->req_qp, send_req->req_peer, false,
                                 &com_msg);
        if (ORTE_SUCCESS != rc) {
            break;
        }

        if (NULL == send_req->req_mr) {
            /* allocate space for memory registers */
            send_req->req_mr = (struct ibv_mr **) calloc (send_req->req_count, sizeof (struct ibv_mr *));
            if (NULL == send_req->req_mr) {
                opal_output (0, "%s oob:ud:send_try error allocating space for memory registers. errno = %d",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), errno);
                rc = ORTE_ERR_OUT_OF_RESOURCE;
                break;
            }
        }

        rc = mca_oob_ud_register_iov (send_req->req_uiov, send_req->req_count,
                                      send_req->req_mr, send_req->req_port->device->ib_pd,
                                      mtu, &sge_count, &wr_count, &data_len);
        if (ORTE_SUCCESS != rc) {
            break;
        }

        wr_count = (data_len + mtu - 1) / mtu;

        if (data_len > 0) {
            data_len = data_len + 0;
        }

        OPAL_OUTPUT_VERBOSE((5, mca_oob_base_output, "%s oob:ud:send_try sending %d bytes in %d "
                             "work requests, %d sges. uiov = %p", ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), data_len,
                             wr_count, sge_count, (void *) send_req->req_uiov));

        if (wr_count && NULL == send_req->req_wr.send) {
            send_req->req_wr.send = (struct ibv_send_wr *) calloc (wr_count, sizeof (struct ibv_send_wr));
            if (NULL == send_req->req_wr.send) {
                opal_output (0, "%s oob:ud:send_try error allocating work requests. errno = %d",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), errno);
                rc = ORTE_ERR_OUT_OF_RESOURCE;
                break;
            }
        }

        if (wr_count && NULL == send_req->req_sge) {
            send_req->req_sge = (struct ibv_sge *) calloc (sge_count, sizeof (struct ibv_sge));

            if (NULL == send_req->req_sge) {
                opal_output (0, "%s oob:ud:send_try error allocating sges. errno = %d",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), errno);
                rc = ORTE_ERR_OUT_OF_RESOURCE;
                break;
            }
        }

        iov_left   = send_req->req_uiov[0].iov_len;
        iov_offset = 0;
        iov_index  = 0;

        for (wr_index = 0, sge_index = 0 ; wr_index < wr_count ; ++wr_index) {
            int sge_first = sge_index;

            packet_size = 0;

            do {
                int to_send = min (iov_left, mtu - packet_size);

                mca_oob_ud_fill_sge(send_req->req_sge + sge_index++,
                                    (char *)send_req->req_uiov[iov_index].iov_base + iov_offset,
                                    to_send, send_req->req_mr[iov_index]->lkey);

                iov_offset  += to_send;
                iov_left    -= to_send;
                packet_size += to_send;

                if (0 == iov_left) {
                    iov_index++;
                    iov_offset = 0;

                    if (iov_index < send_req->req_count) {
                        iov_left = send_req->req_uiov[iov_index].iov_len;
                    }
                }
            } while ((packet_size < mtu) && (iov_left > 0));

            mca_oob_ud_fill_send_wr(send_req->req_wr.send + wr_index,
                                    send_req->req_sge + sge_first,
                                    sge_index - sge_first, send_req->req_peer);

            /* we don't care about completions for data  */
            send_req->req_wr.send[wr_index].send_flags       = IBV_SEND_SOLICITED;

            /* sequence number */
            send_req->req_wr.send[wr_index].imm_data         = wr_index;
            send_req->req_wr.send[wr_index].wr.ud.remote_qpn = send_req->req_rem_qpn;
            send_req->req_wr.send[wr_index].opcode           = IBV_WR_SEND_WITH_IMM;

            if (wr_index + 1 < wr_count) {
                send_req->req_wr.send[wr_index].next = send_req->req_wr.send + wr_index + 1;
            }
        }

        /* send data */
        rc = mca_oob_ud_qp_post_send (send_req->req_qp, send_req->req_wr.send, 0);
        if (ORTE_SUCCESS != rc) {
            opal_output (0, "error posting send!");
            break;
        }

        OPAL_OUTPUT_VERBOSE((10, mca_oob_base_output, "%s oob:ud:send_try posting completion message",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));

        /* Fill in completion message. This message will go to the peers listen QP but
           must originate from our data qp to ensure that it is sent last. */
        com_msg->hdr->msg_type    = MCA_OOB_UD_MSG_COMPLETE;
        com_msg->hdr->msg_lcl_ctx = send_req->req_rem_ctx;
        com_msg->hdr->msg_rem_ctx = send_req;

        /* send message header */
        rc = mca_oob_ud_msg_post_send (com_msg);

        /* post_send already returned the message */
        com_msg = NULL;
    } while (0);

    if (ORTE_ERR_TEMP_OUT_OF_RESOURCE == rc) {
        /* set timer to retry post */
        mca_oob_ud_req_timer_set (send_req, &aquire_timeout, 1, mca_oob_ud_send_try_to);
        rc = ORTE_SUCCESS;
    }

    if (ORTE_SUCCESS != rc) {
        opal_output (0, "send error! rc = %d", rc);
        /* damn */
        return mca_oob_ud_send_complete (send_req, rc);
    }

    send_req->state = MCA_OOB_UD_REQ_ACTIVE;

    return rc;
}

int mca_oob_ud_send_complete (mca_oob_ud_req_t *send_req, int rc)
{
    mca_oob_ud_req_complete (send_req, (ORTE_SUCCESS == rc) ? send_req->req_rem_data_len : rc);

    return rc;
}
