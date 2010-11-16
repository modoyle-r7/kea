// Copyright (C) 2010  Internet Systems Consortium, Inc. ("ISC")
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
// REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
// LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
// OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

// $Id$

#include <algorithm>
#include <functional>
#include <cassert>
#include <iostream>

#include <ctype.h>
#include <strings.h>

#include <config.h>

#include <exceptions/exceptions.h>
#include <dns/name.h>
#include <dns/rrclass.h>
#include <dns/rrttl.h>
#include <dns/question.h>

#include "address_entry.h"
#include "nameserver_entry.h"
#include "resolver_interface.h"

using namespace asiolink;
using namespace isc::nsas;
using namespace isc::dns;
using namespace std;
using namespace boost;

namespace isc {
namespace nsas {


// Constructor, initialized with the list of addresses associated with this
// nameserver.
NameserverEntry::NameserverEntry(const AbstractRRset* v4Set,
    const AbstractRRset* v6Set, time_t curtime) : expiration_(0)
{
    // TODO: Use pseudo-random RTT
    uint32_t rtt = 0;       // Round-trip time for an address
    string v4name = "";     // Name from the V4 RRset
    string v6name = "";     // Name from the v6 RRset
    uint16_t v4class = 0;   // Class of V4 RRset
    uint16_t v6class = 0;   // Class for V6 RRset

    // Get the time for setting the expiration time.
    if (curtime == 0) {
        curtime = time(NULL);
    }

    // Add the v4 addresses to the list of addresses.  Each address is assigned
    // a small RTT that ensures that each server is used at least once (in a
    // random order).


    // Do the V4 addresses first
    // XXX: Do we need to check that these are V4 addresses?
    if (v4Set) {
        RdataIteratorPtr i = v4Set->getRdataIterator();
        // TODO Remove at merge with #410
        i->first();
        while (! i->isLast()) {
            address_.push_back(AddressEntry(IOAddress(i->getCurrent().toText()),
            ++rtt));
            i->next();
        }

        // Set the expiration time and extract the owner name and class
        expiration_ = curtime + v4Set->getTTL().getValue();
        v4name = v4Set->getName().toText(false);    // Ensure trailing dot
        v4class = v4Set->getClass().getCode();
    }

    // Now the v6 addresses
    // XXX: Do we need to check that these are V6 addresses?
    if (v6Set) {
        RdataIteratorPtr i = v6Set->getRdataIterator();
        // TODO Remove at merge with #410
        i->first();
        while (! i->isLast()) {
            address_.push_back(AddressEntry(IOAddress(i->getCurrent().toText()),
            ++rtt));
            i->next();
        }

        // Update the expiration time of the data
        time_t v6expiration = curtime + v6Set->getTTL().getValue();
        if (expiration_ == 0) {
            expiration_ = v6expiration;
        }
        else {
            expiration_ = min(expiration_, v6expiration);
        }

        // Extract the name of the v6 set and its class
        v6name = v6Set->getName().toText(false);    // Ensure trailing dot
        v6class = v6Set->getClass().getCode();
    }

    // TODO: Log a problem if both V4 and V6 address were null.

    if (v4Set && v6Set) {

        // If two owner names were specified and they were different, something
        // has gone wrong with the logic that created this object.
       if (strcasecmp(v4name.c_str(), v6name.c_str()) != 0) {
           isc_throw(InconsistentOwnerNames,
               "Owner names for NameserverEntry RRsets are different");
       }

       // Likewise with the class
       if (v4class != v6class) {
           isc_throw(InconsistentClass,
               "Class codes for NameserverEntry RRsets are different");
       }
    }

    // Otherwise set the owner name
    name_ = v4Set ? v4name : v6name;
    classCode_ = v4Set ? v4class : v6class;
}

// Returns the list of addresses matching the given family
void NameserverEntry::getAddresses(AddressVector& addresses, short family) const {

    // Quick check that allows validation of the assumption in the header file
    // that a family value of 0 will select all address families.
    assert(AF_INET != 0);
    assert(AF_INET6 != 0);
    
    // Now copy all entries that meet the criteria.  Since remove_copy_if
    // does the inverse (copies all entries that do not meet the criteria),
    // the predicate for address selection is negated.
    remove_copy_if(address_.begin(), address_.end(), back_inserter(addresses),
        bind1st(AddressSelection(), family));
}

asiolink::IOAddress NameserverEntry::getAddressAtIndex(uint32_t index) const
{
    assert(index < address_.size());

    return address_[index].getAddress();
}

// Set the address RTT to a specific value
void NameserverEntry::setAddressRTT(const IOAddress& address, uint32_t rtt) {

    // Search through the list of addresses for a match
    for (AddressVectorIterator i = address_.begin(); i != address_.end(); ++i) {
        if (i->getAddress().equal(address)) {
            i->setRTT(rtt);
        }
    }
}

// Update the address's rtt 
void NameserverEntry::updateAddressRTTAtIndex(uint32_t rtt, uint32_t index) {
    //make sure it is a valid index
    if(index >= address_.size()) return;

    //update the rtt
    address_[index].setRTT(rtt);
}

// Sets the address to be unreachable
void NameserverEntry::setAddressUnreachable(const IOAddress& address) {
    setAddressRTT(address, AddressEntry::UNREACHABLE);
}

void NameserverEntry::ensureHasCallback(shared_ptr<ZoneEntry> zone,
    NameserverEntry::Callback& callback)
{
    if (getState() != Fetchable::IN_PROGRESS) {
        isc_throw(BadValue,
            "Callbacks can be added only to IN_PROGRESS nameserver entries");
    }
    if (ipCallbacks_.find(zone) == ipCallbacks_.end()) {
        ipCallbacks_[zone] = &callback;
    }
}

class NameserverEntry::ResolverCallback : public ResolverInterface::Callback {
    public:
        ResolverCallback(shared_ptr<NameserverEntry> entry) :
            entry_(entry),
            rtt(0)
        { }
        virtual void success(const Message& response) {
            map<shared_ptr<ZoneEntry>, NameserverEntry::Callback*> callbacks;
            bool has_address(false);
            {
                mutex::scoped_lock lock(entry_->mutex_);
                for (RRsetIterator set(
                    // TODO Trunk does Section::ANSWER() by constant
                    response.beginSection(Section::ANSWER()));
                    set != response.endSection(Section::ANSWER()); ++ set)
                {
                    /**
                     * TODO Move to common function, this is similar to
                     * what is in constructor.
                     */
                    RdataIteratorPtr i((*set)->getRdataIterator());
                    // TODO Remove at merge with #410
                    i->first();
                    while (! i->isLast()) {
                        has_address = true;
                        entry_->address_.push_back(AddressEntry(IOAddress(
                        i->getCurrent().toText()), ++rtt));
                        i->next();
                    }
                }
                if (has_address) {
                    callbacks.swap(entry_->ipCallbacks_);
                    entry_->waiting_responses_ --;
                    entry_->setState(Fetchable::READY);
                }
            } // Unlock
            if (has_address) {
                dispatchCallbacks(callbacks);
            } else {
                // No address there, so we take it as a failure
                failure();
            }
        }
        virtual void failure() {
            map<shared_ptr<ZoneEntry>, NameserverEntry::Callback*> callbacks;
            {
                mutex::scoped_lock lock(entry_->mutex_);
                entry_->waiting_responses_ --;
                // Do we still have a chance to get the answer?
                // Or did we already?
                if (entry_->waiting_responses_ ||
                    entry_->getState() != Fetchable::IN_PROGRESS)
                {
                    return;
                }
                // Remove the callbacks and call them now
                callbacks.swap(entry_->ipCallbacks_);
                entry_->setState(Fetchable::UNREACHABLE);
            } // Unlock
            dispatchCallbacks(callbacks);
        }
    private:
        shared_ptr<NameserverEntry> entry_;
        int rtt;
        void dispatchCallbacks(map<shared_ptr<ZoneEntry>,
            NameserverEntry::Callback*>& callbacks)
        {
            /*
             * FIXME This approach is not completely exception safe.
             * If we get an exception from callback, we lose the other
             * callbacks.
             */
            for (map<shared_ptr<ZoneEntry>, NameserverEntry::Callback*>::
                iterator i(callbacks.begin()); i != callbacks.end(); ++ i)
            {
                shared_ptr<ZoneEntry> zone(i->first);
                (*i->second)(zone);
            }
        }
};

void NameserverEntry::askIP(ResolverInterface& resolver,
    shared_ptr<ZoneEntry> zone, NameserverEntry::Callback& callback,
    shared_ptr<NameserverEntry> self)
{
    if (getState() != Fetchable::NOT_ASKED) {
        isc_throw(BadValue,
            "Asking to resolve an IP address, but it was asked before");
    }
    setState(Fetchable::IN_PROGRESS);
    ipCallbacks_[zone] = &callback;

    shared_ptr<ResolverCallback> resolver_callback(new ResolverCallback(self));
    waiting_responses_ = 2;
    // TODO Should we ask for both A and AAAA in all occations?
    resolver.resolve(QuestionPtr(new Question(Name(getName()),
        RRClass(getClass()), RRType::A())), resolver_callback);
    resolver.resolve(QuestionPtr(new Question(Name(getName()),
        RRClass(getClass()), RRType::AAAA())), resolver_callback);
}

} // namespace dns
} // namespace isc
