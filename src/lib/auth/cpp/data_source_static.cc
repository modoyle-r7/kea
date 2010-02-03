

#include "data_source_static.h"

#include <dns/name.h>
#include <dns/rdata.h>
#include <dns/rdataclass.h>
#include <dns/rrclass.h>
#include <dns/rrset.h>
#include <dns/rrtype.h>
#include <dns/rrttl.h>

#include <iostream>

namespace isc {
namespace dns {

using namespace isc::dns::rdata;

StaticDataSrc::StaticDataSrc() : authors_name("authors.bind"),
                                 version_name("version.bind")
{
    authors = RRsetPtr(new RRset(authors_name, RRClass::CH(),
                                          RRType::TXT(), RRTTL(0)));
    authors->addRdata(generic::TXT("Evan Hunt"));
    authors->addRdata(generic::TXT("Han Feng"));
    authors->addRdata(generic::TXT("Jelte Jansen"));
    authors->addRdata(generic::TXT("Jeremy C. Reed")); 
    authors->addRdata(generic::TXT("Jin Jian"));
    authors->addRdata(generic::TXT("JINMEI Tatuya"));
    authors->addRdata(generic::TXT("Kazunori Fujiwara"));
    authors->addRdata(generic::TXT("Michael Graff"));
    authors->addRdata(generic::TXT("Naoki Kambe"));
    authors->addRdata(generic::TXT("Shane Kerr"));
    authors->addRdata(generic::TXT("Zhang Likun"));

    authors_ns = RRsetPtr(new RRset(authors_name, RRClass::CH(),
                                    RRType::NS(), RRTTL(0)));
    authors_ns->addRdata(generic::NS(authors_name));

    version = RRsetPtr(new RRset(version_name, RRClass::CH(),
                                          RRType::TXT(), RRTTL(0)));
    version->addRdata(generic::TXT("BIND10 0.0.0 (pre-alpha)"));

    version_ns = RRsetPtr(new RRset(version_name, RRClass::CH(),
                                    RRType::NS(), RRTTL(0)));
    version_ns->addRdata(generic::NS(version_name));
}

const DataSrc*
StaticDataSrc::findClosestEnclosure(const Name& qname, Name& container, bool& found) const {
    NameComparisonResult::NameRelation version_cmp = 
        qname.compare(version_name).getRelation();

    if (version_cmp == NameComparisonResult::EQUAL ||
        version_cmp == NameComparisonResult::SUBDOMAIN) {
        NameComparisonResult::NameRelation sub_cmp = 
           version_name.compare(container).getRelation();

        if (sub_cmp == NameComparisonResult::SUBDOMAIN) {
            container = authors_name;
            found = true;
            return this;
        } else if (!found && sub_cmp == NameComparisonResult::EQUAL) {
            found = true;
            return this;
        } else {
            return NULL;
        }
    }

    NameComparisonResult::NameRelation authors_cmp = 
        qname.compare(authors_name).getRelation();

    if (authors_cmp == NameComparisonResult::EQUAL ||
        authors_cmp == NameComparisonResult::SUBDOMAIN) {
        NameComparisonResult::NameRelation sub_cmp = 
            authors_name.compare(container).getRelation();

        if (sub_cmp == NameComparisonResult::SUBDOMAIN) {
            container = authors_name;
            found = true;
            return this;
        } else if (!found && sub_cmp == NameComparisonResult::EQUAL) {
            found = true;
            return this;
        } else {
            return NULL;
        }
    }

    return NULL;
}

DSResult
StaticDataSrc::findRRset(const Name& qname,
                         const RRClass& qclass,
                         const RRType& qtype,
                         RRsetList& target) const
{
    if (qname == version_name &&
        qclass == version->getClass() && qtype == version->getType()) {
        target.push_back(version);
        return SUCCESS;
    } else if (qname == version_name &&
               qclass == version_ns->getClass() &&
               qtype == version_ns->getType()) {
        target.push_back(version_ns);
        return SUCCESS;
    } else if (qname == authors_name &&
               qclass == authors->getClass() && qtype == authors->getType()) {
        target.push_back(authors);
        return SUCCESS;
    } else if (qname == authors_name &&
               qclass == authors_ns->getClass() &&
               qtype == authors_ns->getType()) {
        target.push_back(authors_ns);
        return SUCCESS;
    }
    // XXX: this is not 100% correct.
    // We should also support the nodata/noerror case.
    return NAME_NOT_FOUND;
}

DSResult
StaticDataSrc::findRRset(const Name& qname,
                         const RRClass& qclass,
                         const RRType& qtype,
                         RRsetList& target, RRsetList& sigs) const
{
    return findRRset(qname, qclass, qtype, target);
}

}
}
