// Copyright (C) 2018 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "receiver.h"
#include "command_options.h"

#include <dhcp/iface_mgr.h>

using namespace std;

namespace isc {
namespace perfdhcp {


void
Receiver::start() {
    if (single_threaded_) {
        return;
    }
    assert(run_flag_.test_and_set() == false);
    recv_thread_.reset(new thread{&Receiver::run, this});
}

void
Receiver::stop() {
    if (single_threaded_) {
        return;
    }
    // Clear flags to order the thread to stop its main loop.
    run_flag_.clear();

    // To be sure first check if thread is running and ready to be joined.
    if (recv_thread_->joinable()) {
        recv_thread_->join();
    }
}

Receiver::~Receiver() {
    if (single_threaded_) {
        return;
    }
    stop();
}


PktPtr
Receiver::getPkt() {
    if (single_threaded_) {
        // In single thread mode read packet directly from the socket and return it.
        auto pkt = readPktFromSocket();
        return pkt;
    } else {
        // In multi thread mode read packet from the queue which is feed by Receiver thread.
        unique_lock<mutex> lock(pkt_queue_mutex_);
        if (pkt_queue_.empty()) {
            if (CommandOptions::instance().getIpVersion() == 4) {
                return Pkt4Ptr{nullptr};
            } else {
                return Pkt6Ptr{nullptr};
            }
        }
        auto pkt = pkt_queue_.front();
        pkt_queue_.pop();
        return pkt;
    }
}

void
Receiver::run() {
    assert(single_threaded_ == false);
    try {
        // If the flag is still true receive packets.
        while (run_flag_.test_and_set()) {
            receivePackets();
        }
    } catch (...) {
        cout << "SOMETHING WENT WRONG" << endl;
    }
}

PktPtr
Receiver::readPktFromSocket() {
    PktPtr pkt;
    uint32_t timeout;
    if (single_threaded_) {
        // In case of single thread just check socket and if empty exit immediatelly
        // to not slow down sending part.
        timeout = 0;
    } else {
        // In case of multi thread wait for packets a little bit (1ms) as it is run
        // in separate thread and do not interefere with sending thread.
        timeout = 1000;
    }
    try {
        if (CommandOptions::instance().getIpVersion() == 4) {
            pkt = IfaceMgr::instance().receive4(0, timeout);
        } else {
            pkt = IfaceMgr::instance().receive6(0, timeout);
        }
    } catch (const Exception& e) {
        cerr << "Failed to receive DHCP packet: " << e.what() <<  endl;
    }
    if (!pkt) {
        return nullptr;
    }

    /// @todo: Add packet exception handling here. Right now any
    /// malformed packet will cause perfdhcp to abort.
    pkt->unpack();
    return pkt;
}

void
Receiver::receivePackets() {
    while (true) {
        PktPtr pkt = readPktFromSocket();
        if (!pkt) {
            break;
        }

        // Drop packet if not supported.
        if (pkt->getType() == DHCPOFFER || pkt->getType() == DHCPACK ||
            pkt->getType() == DHCPV6_ADVERTISE || pkt->getType() == DHCPV6_REPLY) {
            // Otherwise push to another thread.
            unique_lock<mutex> lock(pkt_queue_mutex_);
            pkt_queue_.push(pkt);
        }
    }
}

}
}
