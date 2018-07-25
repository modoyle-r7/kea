// Copyright (C) 2013-2018 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <config.h>
#include <asiolink/io_address.h>
#include <dhcp/option4_client_fqdn.h>
#include <dhcp/option_int_array.h>
#include <dhcp/tests/iface_mgr_test_config.h>
#include <dhcp4/tests/dhcp4_client.h>
#include <dhcp4/tests/dhcp4_test_utils.h>
#include <dhcp_ddns/ncr_msg.h>
#include <dhcpsrv/cfgmgr.h>
#include <dhcpsrv/lease_mgr.h>
#include <dhcpsrv/lease_mgr_factory.h>

#include <gtest/gtest.h>
#include <boost/scoped_ptr.hpp>

using namespace isc;
using namespace isc::asiolink;
using namespace isc::dhcp;
using namespace isc::dhcp::test;
using namespace isc::dhcp_ddns;

namespace {

/// @brief Set of JSON configurations used by the FQDN tests.
const char* CONFIGS[] = {
    // 0
    "{ \"interfaces-config\": {"
        "      \"interfaces\": [ \"*\" ]"
        "},"
        "\"valid-lifetime\": 3000,"
        "\"subnet4\": [ { "
        "    \"subnet\": \"10.0.0.0/24\", "
        "    \"id\": 1,"
        "    \"pools\": [ { \"pool\": \"10.0.0.10-10.0.0.100\" } ],"
        "    \"option-data\": [ {"
        "        \"name\": \"routers\","
        "        \"data\": \"10.0.0.200,10.0.0.201\""
        "    } ],"
        "    \"reservations\": ["
        "       {"
        "         \"hw-address\": \"aa:bb:cc:dd:ee:ff\","
        "         \"hostname\":   \"unique-host.example.org\""
        "       }"
        "    ]"
        " }],"
        "\"dhcp-ddns\": {"
            "\"enable-updates\": true,"
            "\"qualifying-suffix\": \"\""
        "}"
    "}",
    // 1
    "{ \"interfaces-config\": {"
        "      \"interfaces\": [ \"*\" ]"
        "},"
        "\"valid-lifetime\": 3000,"
        "\"subnet4\": [ { "
        "    \"subnet\": \"10.0.0.0/24\", "
        "    \"id\": 1,"
        "    \"pools\": [ { \"pool\": \"10.0.0.10-10.0.0.100\" } ],"
        "    \"option-data\": [ {"
        "        \"name\": \"routers\","
        "        \"data\": \"10.0.0.200,10.0.0.201\""
        "    } ],"
        "    \"reservations\": ["
        "       {"
        "         \"hw-address\": \"aa:bb:cc:dd:ee:ff\","
        "         \"hostname\":   \"foobar\""
        "       }"
        "    ]"
        " }],"
        "\"dhcp-ddns\": {"
            "\"enable-updates\": true,"
            "\"qualifying-suffix\": \"fake-suffix.isc.org.\""
        "}"
    "}",
    // 2
    // Simple config with DDNS updates disabled.  Note pool is one address
    // large to ensure we get a specific address back.
    "{ \"interfaces-config\": {"
        "      \"interfaces\": [ \"*\" ]"
        "},"
        "\"valid-lifetime\": 3000,"
        "\"subnet4\": [ { "
        "    \"subnet\": \"10.0.0.0/24\", "
        "    \"id\": 1,"
        "    \"pools\": [ { \"pool\": \"10.0.0.10-10.0.0.10\" } ]"
        " }],"
        "\"dhcp-ddns\": {"
            "\"enable-updates\": false,"
            "\"qualifying-suffix\": \"fake-suffix.isc.org.\""
        "}"
    "}",
    // 3
    // Simple config with DDNS updates enabled.  Note pool is one address
    // large to ensure we get a specific address back.
    "{ \"interfaces-config\": {"
        "      \"interfaces\": [ \"*\" ]"
        "},"
        "\"valid-lifetime\": 3000,"
        "\"subnet4\": [ { "
        "    \"subnet\": \"10.0.0.0/24\", "
        "    \"id\": 1,"
        "    \"pools\": [ { \"pool\": \"10.0.0.10-10.0.0.10\" } ]"
        " }],"
        "\"dhcp-ddns\": {"
            "\"enable-updates\": true,"
            "\"qualifying-suffix\": \"fake-suffix.isc.org.\""
        "}"
    "}",
    // 4
    // Configuration which disables DNS updates but contains a reservation
    // for a hostname. Reserved hostname should be assigned to a client if
    // the client includes it in the Parameter Request List option.
    "{ \"interfaces-config\": {"
        "      \"interfaces\": [ \"*\" ]"
        "},"
        "\"valid-lifetime\": 3000,"
        "\"subnet4\": [ { "
        "    \"subnet\": \"10.0.0.0/24\", "
        "    \"id\": 1,"
        "    \"pools\": [ { \"pool\": \"10.0.0.10-10.0.0.100\" } ],"
        "    \"option-data\": [ {"
        "        \"name\": \"routers\","
        "        \"data\": \"10.0.0.200,10.0.0.201\""
        "    } ],"
        "    \"reservations\": ["
        "       {"
        "         \"hw-address\": \"aa:bb:cc:dd:ee:ff\","
        "         \"hostname\":   \"reserved.example.org\""
        "       }"
        "    ]"
        " }],"
        "\"dhcp-ddns\": {"
            "\"enable-updates\": false,"
            "\"qualifying-suffix\": \"\""
        "}"
    "}",
    // 5
    // Configuration which disables DNS updates but contains a reservation
    // for a hostname and the qualifying-suffix which should be appended to
    // the reserved hostname in the Hostname option returned to a client.
    "{ \"interfaces-config\": {"
        "      \"interfaces\": [ \"*\" ]"
        "},"
        "\"valid-lifetime\": 3000,"
        "\"subnet4\": [ { "
        "    \"subnet\": \"10.0.0.0/24\", "
        "    \"id\": 1,"
        "    \"pools\": [ { \"pool\": \"10.0.0.10-10.0.0.100\" } ],"
        "    \"option-data\": [ {"
        "        \"name\": \"routers\","
        "        \"data\": \"10.0.0.200,10.0.0.201\""
        "    } ],"
        "    \"reservations\": ["
        "       {"
        "         \"hw-address\": \"aa:bb:cc:dd:ee:ff\","
        "         \"hostname\":   \"foo-bar\""
        "       }"
        "    ]"
        " }],"
        "\"dhcp-ddns\": {"
            "\"enable-updates\": false,"
            "\"qualifying-suffix\": \"example.isc.org\""
        "}"
    "}",
    // 6
    // Configuration which enables DNS updates and hostname sanitization
    "{ \"interfaces-config\": {"
        "      \"interfaces\": [ \"*\" ]"
        "},"
        "\"valid-lifetime\": 3000,"
        "\"subnet4\": [ { "
        "    \"subnet\": \"10.0.0.0/24\", "
        "    \"id\": 1,"
        "    \"pools\": [ { \"pool\": \"10.0.0.10-10.0.0.100\" } ],"
        "    \"option-data\": [ {"
        "        \"name\": \"routers\","
        "        \"data\": \"10.0.0.200,10.0.0.201\""
        "    } ],"
        "    \"reservations\": ["
        "       {"
        "         \"hw-address\": \"aa:bb:cc:dd:ee:ff\","
        "         \"hostname\":   \"unique-xxx-host.example.org\""
        "       }"
        "    ]"
        " }],"
        "\"dhcp-ddns\": {"
            "\"enable-updates\": true,"
            "\"hostname-char-set\" : \"[^A-Za-z0-9.-]\","
            "\"hostname-char-replacement\" : \"x\","
            "\"qualifying-suffix\": \"example.org\""
        "}"
    "}",
};

class NameDhcpv4SrvTest : public Dhcpv4SrvTest {
public:
    // Reference to D2ClientMgr singleton
    D2ClientMgr& d2_mgr_;

    /// @brief Pointer to the DHCP server instance.
    NakedDhcpv4Srv* srv_;

    /// @brief Interface Manager's fake configuration control.
    IfaceMgrTestConfig iface_mgr_test_config_;

    // Bit Constants for turning on and off DDNS configuration options.
    static const uint16_t OVERRIDE_NO_UPDATE = 1;
    static const uint16_t OVERRIDE_CLIENT_UPDATE = 2;
    static const uint16_t REPLACE_CLIENT_NAME = 4;

    // Enum used to specify whether a client (packet) should include
    // the hostname option
    enum ClientNameFlag {
        CLIENT_NAME_PRESENT,
        CLIENT_NAME_NOT_PRESENT
    };

    // Enum used to specify whether the server should replace/supply
    // the hostname or not
    enum ReplacementFlag {
        NAME_REPLACED,
        NAME_NOT_REPLACED
    };

    NameDhcpv4SrvTest()
        : Dhcpv4SrvTest(),
          d2_mgr_(CfgMgr::instance().getD2ClientMgr()),
          srv_(NULL),
          iface_mgr_test_config_(true)
    {
        srv_ = new NakedDhcpv4Srv(0);
        IfaceMgr::instance().openSockets4();
        // Config DDNS to be enabled, all controls off
        enableD2();
    }

    virtual ~NameDhcpv4SrvTest() {
        delete srv_;
        // CfgMgr singleton doesn't get wiped between tests, so  we'll
        // disable D2 explicitly between tests.
        disableD2();
    }

    /// @brief Sets the server's DDNS configuration to ddns updates disabled.
    void disableD2() {
        // Default constructor creates a config with DHCP-DDNS updates
        // disabled.
        D2ClientConfigPtr cfg(new D2ClientConfig());
        CfgMgr::instance().setD2ClientConfig(cfg);
    }

    /// @brief Enables DHCP-DDNS updates with the given options enabled.
    ///
    /// Replaces the current D2ClientConfiguration with a configuration
    /// which as updates enabled and the control options set based upon
    /// the bit mask of options.
    ///
    /// @param mask Bit mask of configuration options that should be enabled.
    void enableD2(const uint16_t mask = 0) {
        D2ClientConfigPtr cfg;

        ASSERT_NO_THROW(cfg.reset(new D2ClientConfig(true,
                                  isc::asiolink::IOAddress("127.0.0.1"), 53001,
                                  isc::asiolink::IOAddress("0.0.0.0"), 0, 1024,
                                  dhcp_ddns::NCR_UDP, dhcp_ddns::FMT_JSON,
                                  (mask & OVERRIDE_NO_UPDATE),
                                  (mask & OVERRIDE_CLIENT_UPDATE),
                                  ((mask & REPLACE_CLIENT_NAME) ?
                                    D2ClientConfig::RCM_WHEN_PRESENT
                                   : D2ClientConfig::RCM_NEVER),
                                  "myhost", "example.com", "", "")));
        ASSERT_NO_THROW(CfgMgr::instance().setD2ClientConfig(cfg));
        ASSERT_NO_THROW(srv_->startD2());
    }

    // Create a lease to be used by various tests.
    Lease4Ptr createLease(const isc::asiolink::IOAddress& addr,
                          const std::string& hostname,
                          const bool fqdn_fwd,
                          const bool fqdn_rev) {
        const uint8_t hwaddr_data[] = { 0, 1, 2, 3, 4, 5, 6 };
        HWAddrPtr hwaddr(new HWAddr(hwaddr_data, sizeof(hwaddr_data),
                                    HTYPE_ETHER));
        Lease4Ptr lease(new Lease4(addr, hwaddr,
                                   &generateClientId()->getData()[0],
                                   generateClientId()->getData().size(),
                                   100, 50, 75, time(NULL), subnet_->getID()));
        // @todo Set this through the Lease4 constructor.
        lease->hostname_ = hostname;
        lease->fqdn_fwd_ = fqdn_fwd;
        lease->fqdn_rev_ = fqdn_rev;

        return (lease);
    }

    // Create an instance of the DHCPv4 Client FQDN Option.
    Option4ClientFqdnPtr
    createClientFqdn(const uint8_t flags,
                     const std::string& fqdn_name,
                     Option4ClientFqdn::DomainNameType fqdn_type) {
        return (Option4ClientFqdnPtr(new Option4ClientFqdn(flags,
                                                           Option4ClientFqdn::
                                                           RCODE_CLIENT(),
                                                           fqdn_name,
                                                           fqdn_type)));
   }

    // Create an instance of the Hostname option.
    OptionStringPtr
    createHostname(const std::string& hostname) {
        OptionStringPtr opt_hostname(new OptionString(Option::V4,
                                                      DHO_HOST_NAME,
                                                      hostname));
        return (opt_hostname);
    }

    /// @brief Convenience method for generating an FQDN from an IP address.
    ///
    /// This is just a wrapper method around the D2ClientMgr's method for
    /// generating domain names from the configured prefix, suffix, and a
    /// given IP address.  This is useful for verifying that fully generated
    /// names are correct.
    ///
    /// @param addr IP address used in the lease.
    /// @param trailing_dot A boolean flag which indicates whether the
    /// trailing dot should be appended to the end of the hostname.
    /// The default value is "true" which means that it should.
    ///
    /// @return An std::string contained the generated FQDN.
    std::string generatedNameFromAddress(const IOAddress& addr,
                                         const bool trailing_dot = true) {
        return(CfgMgr::instance().getD2ClientMgr()
               .generateFqdn(addr, trailing_dot));
    }

    // Get the Client FQDN Option from the given message.
    Option4ClientFqdnPtr getClientFqdnOption(const Pkt4Ptr& pkt) {
        return (boost::dynamic_pointer_cast<
                Option4ClientFqdn>(pkt->getOption(DHO_FQDN)));
    }

    // get the Hostname option from the given message.
    OptionStringPtr getHostnameOption(const Pkt4Ptr& pkt) {
        return (boost::dynamic_pointer_cast<
                OptionString>(pkt->getOption(DHO_HOST_NAME)));
    }

    // Create a message holding DHCPv4 Client FQDN Option.
    Pkt4Ptr generatePktWithFqdn(const uint8_t msg_type,
                                const uint8_t fqdn_flags,
                                const std::string& fqdn_domain_name,
                                Option4ClientFqdn::DomainNameType fqdn_type,
                                const bool include_prl,
                                const bool include_clientid = true) {
        Pkt4Ptr pkt = Pkt4Ptr(new Pkt4(msg_type, 1234));
        pkt->setRemoteAddr(IOAddress("192.0.2.3"));
        pkt->setIface("eth1");
        // For DISCOVER we don't include server id, because client broadcasts
        // the message to all servers.
        if (msg_type != DHCPDISCOVER) {
            pkt->addOption(srv_->getServerID());
        }

        if (include_clientid) {
            pkt->addOption(generateClientId());
        }

        // Create Client FQDN Option with the specified flags and
        // domain-name.
        pkt->addOption(createClientFqdn(fqdn_flags, fqdn_domain_name,
                                        fqdn_type));

        // Control whether or not to request that server returns the FQDN
        // option. Server may be configured to always return it or return
        // only in case client requested it.
        if (include_prl) {
            OptionUint8ArrayPtr option_prl =
                OptionUint8ArrayPtr(new OptionUint8Array(Option::V4,
                                    DHO_DHCP_PARAMETER_REQUEST_LIST));
            option_prl->addValue(DHO_FQDN);
        }
        return (pkt);
    }

    // Create a message holding a Hostname option.
    Pkt4Ptr generatePktWithHostname(const uint8_t msg_type,
                                    const std::string& hostname) {

        Pkt4Ptr pkt = Pkt4Ptr(new Pkt4(msg_type, 1234));
        pkt->setRemoteAddr(IOAddress("192.0.2.3"));
        // For DISCOVER we don't include server id, because client broadcasts
        // the message to all servers.
        if (msg_type != DHCPDISCOVER) {
            pkt->addOption(srv_->getServerID());
        }

        pkt->addOption(generateClientId());


        // Create Client FQDN Option with the specified flags and
        // domain-name.
        pkt->addOption(createHostname(hostname));

        return (pkt);

    }

    // Create a message holding an empty Hostname option.
    Pkt4Ptr generatePktWithEmptyHostname(const uint8_t msg_type) {

        Pkt4Ptr pkt = Pkt4Ptr(new Pkt4(msg_type, 1234));
        pkt->setRemoteAddr(IOAddress("192.0.2.3"));
        // For DISCOVER we don't include server id, because client broadcasts
        // the message to all servers.
        if (msg_type != DHCPDISCOVER) {
            pkt->addOption(srv_->getServerID());
        }

        pkt->addOption(generateClientId());

        // Create Hostname option.
        std::string hostname(" ");
        OptionPtr opt = createHostname(hostname);
        opt->setData(hostname.begin(), hostname.begin());
        pkt->addOption(opt);

        return (pkt);

    }

    // Create a message holding of a given type
    Pkt4Ptr generatePkt(const uint8_t msg_type) {
        Pkt4Ptr pkt = Pkt4Ptr(new Pkt4(msg_type, 1234));
        pkt->setRemoteAddr(IOAddress("192.0.2.3"));
        // For DISCOVER we don't include server id, because client broadcasts
        // the message to all servers.
        if (msg_type != DHCPDISCOVER) {
            pkt->addOption(srv_->getServerID());
        }

        pkt->addOption(generateClientId());
        return (pkt);
    }

    // Test that server generates the appropriate FQDN option in response to
    // client's FQDN option.
    void testProcessFqdn(const Pkt4Ptr& query, const uint8_t exp_flags,
                         const std::string& exp_domain_name,
                         Option4ClientFqdn::DomainNameType
                         exp_domain_type = Option4ClientFqdn::FULL) {
        ASSERT_TRUE(getClientFqdnOption(query));

        Pkt4Ptr answer;
        if (query->getType() == DHCPDISCOVER) {
            answer.reset(new Pkt4(DHCPOFFER, 1234));

        } else {
            answer.reset(new Pkt4(DHCPACK, 1234));

        }
        Dhcpv4Exchange ex = createExchange(query);
        ASSERT_NO_THROW(srv_->processClientName(ex));

        Option4ClientFqdnPtr fqdn = getClientFqdnOption(ex.getResponse());
        ASSERT_TRUE(fqdn);

        checkFqdnFlags(ex.getResponse(), exp_flags);

        EXPECT_EQ(exp_domain_name, fqdn->getDomainName());
        EXPECT_EQ(exp_domain_type, fqdn->getDomainNameType());

    }

    // Test that the server's processes the hostname (or lack thereof)
    // in a client request correctly, according to the replace-client-name
    // mode configuration parameter.  We include hostname sanitizer to ensure
    // it does not interfere with name replacement.
    //
    // @param mode - value to use for replace-client-name
    // @param client_name_flag - specifies whether or not the client request
    // should contain a hostname option
    // @param exp_replacement_flag - specifies whether or not the server is
    // expected to replace (or supply) the hostname in its response
    void testReplaceClientNameMode(const char* mode,
                                   enum ClientNameFlag client_name_flag,
                                   enum ReplacementFlag exp_replacement_flag) {
        // Configuration "template" with a replaceable mode parameter
        const char* config_template =
            "{ \"interfaces-config\": {"
            "      \"interfaces\": [ \"*\" ]"
            "},"
            "\"valid-lifetime\": 3000,"
            "\"subnet4\": [ { "
            "    \"subnet\": \"10.0.0.0/24\", "
            "    \"id\": 1,"
            "    \"pools\": [ { \"pool\": \"10.0.0.10-10.0.0.10\" } ]"
            " }],"
            "\"dhcp-ddns\": {"
            "\"enable-updates\": true,"
            "\"qualifying-suffix\": \"fake-suffix.isc.org.\","
            "\"hostname-char-set\": \"[^A-Za-z0-9.-]\","
            "\"hostname-char-replacement\": \"x\","
            "\"replace-client-name\": \"%s\""
            "}}";

        // Create the configuration and configure the server
        char config_buf[1024];
        sprintf(config_buf, config_template, mode);
        ASSERT_NO_THROW(configure(config_buf, srv_)) << "configuration failed";

        // Build our client packet
        Pkt4Ptr query;
        if (client_name_flag == CLIENT_NAME_PRESENT) {
            ASSERT_NO_THROW(query = generatePktWithHostname(DHCPREQUEST,
                                                            "my.example.com."));
        } else {
            ASSERT_NO_THROW(query = generatePkt(DHCPREQUEST));
        }

        // Run the packet through the server, extracting the hostname option
        // from the response.  If the option isn't present the returned pointer
        // will be null.
        OptionStringPtr hostname;
        ASSERT_NO_THROW(
            hostname = processHostname(query,
                                       client_name_flag == CLIENT_NAME_PRESENT)
        ) << "processHostname throw an exception";

        // Verify the contents (or lack thereof) of the hostname
        if (exp_replacement_flag == NAME_REPLACED) {
            ASSERT_TRUE(hostname)
                << "No host name, it should have the replacement name \".\"";
            EXPECT_EQ(".", hostname->getValue());
        } else {
            if (client_name_flag == CLIENT_NAME_PRESENT) {
                ASSERT_TRUE(hostname)
                    << "No host name, expected original from client";
                EXPECT_EQ("my.example.com.", hostname->getValue());
            } else {
                ASSERT_FALSE(hostname)
                    << "Host name is: " << hostname
                    << ", it should have been null";
            }
        }
    }

    /// @brief Checks the packet's FQDN option flags against a given mask
    ///
    /// @param pkt IPv4 packet whose FQDN flags should be checked.
    /// @param exp_flags Bit mask of flags that are expected to be true.
    void checkFqdnFlags(const Pkt4Ptr& pkt, const uint8_t exp_flags) {
        Option4ClientFqdnPtr fqdn = getClientFqdnOption(pkt);
        ASSERT_TRUE(fqdn);

        const bool flag_n = (exp_flags & Option4ClientFqdn::FLAG_N) != 0;
        const bool flag_s = (exp_flags & Option4ClientFqdn::FLAG_S) != 0;
        const bool flag_o = (exp_flags & Option4ClientFqdn::FLAG_O) != 0;
        const bool flag_e = (exp_flags & Option4ClientFqdn::FLAG_E) != 0;

        EXPECT_EQ(flag_n, fqdn->getFlag(Option4ClientFqdn::FLAG_N));
        EXPECT_EQ(flag_s, fqdn->getFlag(Option4ClientFqdn::FLAG_S));
        EXPECT_EQ(flag_o, fqdn->getFlag(Option4ClientFqdn::FLAG_O));
        EXPECT_EQ(flag_e, fqdn->getFlag(Option4ClientFqdn::FLAG_E));
    }


    /// @brief  Invokes Dhcpv4Srv::processHostname on the given packet
    ///
    /// Processes the Hostname option in the client's message and returns
    /// the hostname option which would be sent to the client. It will
    /// return empty if the hostname option is not to be included
    /// server's response.
    /// @param query - client packet to process
    /// @param must_have_host - flag indicating whether or not the client
    /// packet must contain the hostname option
    ///
    /// @return a pointer to the hostname option constructed by the server
    OptionStringPtr processHostname(const Pkt4Ptr& query,
                                    bool must_have_host = true) {
        if (!getHostnameOption(query) && must_have_host) {
            ADD_FAILURE() << "Hostname option not carried in the query";
        }

        Pkt4Ptr answer;
        if (query->getType() == DHCPDISCOVER) {
            answer.reset(new Pkt4(DHCPOFFER, 1234));

        } else {
            answer.reset(new Pkt4(DHCPACK, 1234));

        }
        Dhcpv4Exchange ex = createExchange(query);
        srv_->processClientName(ex);

        OptionStringPtr hostname = getHostnameOption(ex.getResponse());
        return (hostname);

    }

    ///@brief Verify that NameChangeRequest holds valid values.
    ///
    /// Pulls the NCR from the top of the send queue and checks it's content
    ///  against a number of expected parameters.
    ///
    /// @param type - expected NCR change type, CHG_ADD or CHG_REMOVE
    /// @param reverse - flag indicating whether or not the NCR specifies
    /// reverse change
    /// @param forward - flag indication whether or not the NCR specifies
    /// forward change
    /// @param addr  - expected lease address in the NCR
    /// @param fqdn  - expected FQDN in the NCR
    /// @param dhcid - expected DHCID in the NCR (comparison is performed only
    /// if the value supplied is not empty):w
    /// @param cltt - cltt value from the lease the NCR for which the NCR
    /// was generated expected value for
    /// @param len - expected lease length in the NCR
    /// @param not_strict_expire_check - when true the comparison of the NCR
    /// lease expiration time is conducted as greater than or equal to rather
    /// equal to CLTT plus lease length.
    void verifyNameChangeRequest(const isc::dhcp_ddns::NameChangeType type,
                                 const bool reverse, const bool forward,
                                 const std::string& addr,
                                 const std::string& fqdn,
                                 const std::string& dhcid,
                                 const time_t cltt,
                                 const uint16_t len,
                                 const bool not_strict_expire_check = false) {
        NameChangeRequestPtr ncr;
        ASSERT_NO_THROW(ncr = d2_mgr_.peekAt(0));
        ASSERT_TRUE(ncr);

        EXPECT_EQ(type, ncr->getChangeType());
        EXPECT_EQ(forward, ncr->isForwardChange());
        EXPECT_EQ(reverse, ncr->isReverseChange());
        EXPECT_EQ(addr, ncr->getIpAddress());
        EXPECT_EQ(fqdn, ncr->getFqdn());
        // Compare dhcid if it is not empty. In some cases, the DHCID is
        // not known in advance and can't be compared.
        if (!dhcid.empty()) {
            EXPECT_EQ(dhcid, ncr->getDhcid().toStr());
        }
        // In some cases, the test doesn't have access to the last transmission
        // time for the particular client. In such cases, the test can use the
        // current time as cltt but the it may not check the lease expiration
        // time for equality but rather check that the lease expiration time
        // is not greater than the current time + lease lifetime.
        if (not_strict_expire_check) {
            EXPECT_GE(cltt + len, ncr->getLeaseExpiresOn());
        } else {
            EXPECT_EQ(cltt + len, ncr->getLeaseExpiresOn());
        }
        EXPECT_EQ(len, ncr->getLeaseLength());
        EXPECT_EQ(isc::dhcp_ddns::ST_NEW, ncr->getStatus());

        // Process the message off the queue
        ASSERT_NO_THROW(d2_mgr_.runReadyIO());
    }


    /// @brief Tests processing a request with the given client flags
    ///
    /// This method creates a request with its FQDN flags set to the given
    /// value and submits it to the server for processing.  It then checks
    /// the following:
    /// 1. Did the server generate an ACK with the correct FQDN flags
    /// 2. If the server should have generated an NCR, did it? and If
    /// so was it correct?
    ///
    /// @param client_flags Mask of client FQDN flags which are true
    /// @param response_flags Mask of expected FQDN flags in the response
    void flagVsConfigScenario(const uint8_t client_flags,
                       const uint8_t response_flags) {
        // Create fake interfaces and open fake sockets.
        IfaceMgrTestConfig iface_config(true);
        IfaceMgr::instance().openSockets4();

        Pkt4Ptr req = generatePktWithFqdn(DHCPREQUEST, client_flags,
                                          "myhost.example.com.",
                                          Option4ClientFqdn::FULL, true);

        // Process the request.
        Pkt4Ptr reply;
        ASSERT_NO_THROW(reply = srv_->processRequest(req));

        // Verify the response and flags.
        checkResponse(reply, DHCPACK, 1234);
        checkFqdnFlags(reply, response_flags);

        // NCRs cannot be sent to the d2_mgr unless updates are enabled.
        if (d2_mgr_.ddnsEnabled()) {
            // There should be an NCR if response S flag is 1 or N flag is 0.
            bool exp_fwd = (response_flags & Option4ClientFqdn::FLAG_S);
            bool exp_rev = (!(response_flags & Option4ClientFqdn::FLAG_N));
            if (!exp_fwd && !exp_rev) {
                ASSERT_EQ(0, d2_mgr_.getQueueSize());
            } else {
                // Verify that there is one NameChangeRequest as expected.
                ASSERT_EQ(1, d2_mgr_.getQueueSize());
                verifyNameChangeRequest(isc::dhcp_ddns::CHG_ADD,
                                        exp_rev, exp_fwd,
                                        reply->getYiaddr().toText(),
                                        "myhost.example.com.",
                                        "", // empty DHCID means don't check it
                                        time(NULL) + subnet_->getValid(),
                                        subnet_->getValid(), true);
            }
        }
    }
};

// Tests the following scenario:
//  - Updates are enabled
//  - All overrides are off
//  - Client requests forward update  (N = 0, S = 1)
//
//  Server should perform the update:
//  - Response flags should N = 0, S = 1, O = 0
//  - Should queue an NCR
TEST_F(NameDhcpv4SrvTest, updatesEnabled) {
    flagVsConfigScenario((Option4ClientFqdn::FLAG_E |
                          Option4ClientFqdn::FLAG_S),
                          (Option4ClientFqdn::FLAG_E |
                           Option4ClientFqdn::FLAG_S));
}

// Tests the following scenario
//  - Updates are disabled
//  - Client requests forward update  (N = 0, S = 1)
//
//  Server should NOT perform updates:
//   - Response flags should N = 1, S = 0, O = 1
//   - Should not queue any NCRs
TEST_F(NameDhcpv4SrvTest, updatesDisabled) {
    disableD2();
    flagVsConfigScenario((Option4ClientFqdn::FLAG_E |
                          Option4ClientFqdn::FLAG_S),
                          (Option4ClientFqdn::FLAG_E |
                           Option4ClientFqdn::FLAG_N |
                           Option4ClientFqdn::FLAG_O));
}

// Tests the following scenario:
//  - Updates are enabled
//  - All overrides are off.
//  - Client requests no updates  (N = 1, S = 0)
//
//  Server should NOT perform updates:
//  - Response flags should N = 1, S = 0, O = 0
//  - Should not queue any NCRs
TEST_F(NameDhcpv4SrvTest, respectNoUpdate) {
    flagVsConfigScenario((Option4ClientFqdn::FLAG_E |
                          Option4ClientFqdn::FLAG_N),
                          (Option4ClientFqdn::FLAG_E |
                           Option4ClientFqdn::FLAG_N));
}

// Tests the following scenario:
//  - Updates are enabled
//  - override-no-update is on
//  - Client requests no updates  (N = 1, S = 0)
//
// Server should override "no update" request and perform updates:
// - Response flags should be  N = 0, S = 1, O = 1
// - Should queue an NCR
TEST_F(NameDhcpv4SrvTest, overrideNoUpdate) {
    enableD2(OVERRIDE_NO_UPDATE);
    flagVsConfigScenario((Option4ClientFqdn::FLAG_E |
                          Option4ClientFqdn::FLAG_N),
                          (Option4ClientFqdn::FLAG_E |
                           Option4ClientFqdn::FLAG_S |
                           Option4ClientFqdn::FLAG_O));
}

// Tests the following scenario:
//  - Updates are enabled
//  - All overrides are off.
//  - Client requests delegation  (N = 0, S = 0)
//
// Server should respect client's delegation request and NOT do updates:

// - Response flags should be  N = 0, S = 0, O = 0
// - Should not queue any NCRs
TEST_F(NameDhcpv4SrvTest, respectClientDelegation) {

    flagVsConfigScenario(Option4ClientFqdn::FLAG_E,
                         Option4ClientFqdn::FLAG_E);
}

// Tests the following scenario:
//  - Updates are enabled
//  - override-client-update is on.
//  - Client requests delegation  (N = 0, S = 0)
//
// Server should override client's delegation request and do updates:
// - Response flags should be  N = 0, S = 1, O = 1
// - Should queue an NCR
TEST_F(NameDhcpv4SrvTest, overrideClientDelegation) {
    // Turn on override-client-update.
    enableD2(OVERRIDE_CLIENT_UPDATE);

    flagVsConfigScenario(Option4ClientFqdn::FLAG_E,
                         (Option4ClientFqdn::FLAG_E |
                          Option4ClientFqdn::FLAG_S |
                          Option4ClientFqdn::FLAG_O));
}

// Test that server processes the Hostname option sent by a client and
// responds with the Hostname option to confirm that the server has
// taken responsibility for the update.
TEST_F(NameDhcpv4SrvTest, serverUpdateHostname) {
    Pkt4Ptr query;
    ASSERT_NO_THROW(query = generatePktWithHostname(DHCPREQUEST,
                                                    "myhost.example.com."));
    OptionStringPtr hostname;
    ASSERT_NO_THROW(hostname = processHostname(query));

    ASSERT_TRUE(hostname);
    EXPECT_EQ("myhost.example.com.", hostname->getValue());

}

// Test that the server skips processing of a wrong Hostname option.
TEST_F(NameDhcpv4SrvTest, serverUpdateWrongHostname) {
    Pkt4Ptr query;
    ASSERT_NO_THROW(query = generatePktWithHostname(DHCPREQUEST,
                                                    "abc..example.com"));
    OptionStringPtr hostname;
    ASSERT_NO_THROW(hostname = processHostname(query));
    EXPECT_FALSE(hostname);
}

// Test that the server does not see an empty Hostname option.
// Suppressing the empty Hostname is done in libdhcp++ during
// unpackcing, so technically we don't need this test but,
// hey it's already written.
TEST_F(NameDhcpv4SrvTest, serverUpdateEmptyHostname) {
    Pkt4Ptr query;
    ASSERT_NO_THROW(query = generatePktWithEmptyHostname(DHCPREQUEST));
    OptionStringPtr hostname;
    ASSERT_NO_THROW(hostname = processHostname(query));
    EXPECT_FALSE(hostname);
}

// Test that server generates the fully qualified domain name for the client
// if client supplies the partial name.
TEST_F(NameDhcpv4SrvTest, serverUpdateForwardPartialNameFqdn) {
    Pkt4Ptr query = generatePktWithFqdn(DHCPREQUEST,
                                        Option4ClientFqdn::FLAG_E |
                                        Option4ClientFqdn::FLAG_S,
                                        "myhost",
                                        Option4ClientFqdn::PARTIAL,
                                        true);

    testProcessFqdn(query,
                    Option4ClientFqdn::FLAG_E | Option4ClientFqdn::FLAG_S,
                    "myhost.example.com.");

}

// Test that server generates the fully qualified domain name for the client
// if client supplies the unqualified name in the Hostname option.
TEST_F(NameDhcpv4SrvTest, serverUpdateUnqualifiedHostname) {
    Pkt4Ptr query;
    ASSERT_NO_THROW(query = generatePktWithHostname(DHCPREQUEST, "myhost"));
    OptionStringPtr hostname;
    ASSERT_NO_THROW(hostname =  processHostname(query));

    ASSERT_TRUE(hostname);
    EXPECT_EQ("myhost.example.com", hostname->getValue());

}

// Test that server sets empty domain-name in the FQDN option when client
// supplied no domain-name. The domain-name is supposed to be set after the
// lease is acquired. The domain-name is then generated from the IP address
// assigned to a client.
TEST_F(NameDhcpv4SrvTest, serverUpdateForwardNoNameFqdn) {
    Pkt4Ptr query = generatePktWithFqdn(DHCPREQUEST,
                                        Option4ClientFqdn::FLAG_E |
                                        Option4ClientFqdn::FLAG_S,
                                        "",
                                        Option4ClientFqdn::PARTIAL,
                                        true);

    testProcessFqdn(query,
                    Option4ClientFqdn::FLAG_E | Option4ClientFqdn::FLAG_S,
                    "", Option4ClientFqdn::PARTIAL);

}

// Test that exactly one NameChangeRequest is generated when the new lease
// has been acquired (old lease is NULL).
TEST_F(NameDhcpv4SrvTest, createNameChangeRequestsNewLease) {
    Lease4Ptr lease = createLease(IOAddress("192.0.2.3"), "myhost.example.com.",
                                  true, true);
    Lease4Ptr old_lease;

    ASSERT_NO_THROW(srv_->createNameChangeRequests(lease, old_lease));
    ASSERT_EQ(1, d2_mgr_.getQueueSize());

    verifyNameChangeRequest(isc::dhcp_ddns::CHG_ADD, true, true,
                            "192.0.2.3", "myhost.example.com.",
                            "00010132E91AA355CFBB753C0F0497A5A940436965"
                            "B68B6D438D98E680BF10B09F3BCF",
                            lease->cltt_, 100);
}

// Test that no NameChangeRequest is generated when a lease is renewed and
// the FQDN data hasn't changed.
TEST_F(NameDhcpv4SrvTest, createNameChangeRequestsRenewNoChange) {
    Lease4Ptr lease = createLease(IOAddress("192.0.2.3"), "myhost.example.com.",
                                  true, true);
    // Comparison should be case insensitive, so turning some of the
    // characters of the old lease hostname to upper case should not
    // trigger NCRs.
    Lease4Ptr old_lease = createLease(IOAddress("192.0.2.3"),
                                      "Myhost.Example.Com.", true, true);
    old_lease->valid_lft_ += 100;

    ASSERT_NO_THROW(srv_->createNameChangeRequests(lease, old_lease));
    ASSERT_EQ(0, d2_mgr_.getQueueSize());
}

// Test that the OFFER message generated as a result of the DISCOVER message
// processing will not result in generation of the NameChangeRequests.
TEST_F(NameDhcpv4SrvTest, processDiscover) {
    IfaceMgrTestConfig test_config(true);
    IfaceMgr::instance().openSockets4();

    Pkt4Ptr req = generatePktWithFqdn(DHCPDISCOVER, Option4ClientFqdn::FLAG_S |
                                      Option4ClientFqdn::FLAG_E,
                                      "myhost.example.com.",
                                      Option4ClientFqdn::FULL, true);

    Pkt4Ptr reply;
    ASSERT_NO_THROW(reply = srv_->processDiscover(req));
    checkResponse(reply, DHCPOFFER, 1234);

    EXPECT_EQ(0, d2_mgr_.getQueueSize());
}

// Test that server generates client's hostname from the IP address assigned
// to it when DHCPv4 Client FQDN option specifies an empty domain-name.
TEST_F(NameDhcpv4SrvTest, processRequestFqdnEmptyDomainName) {
    IfaceMgrTestConfig test_config(true);
    IfaceMgr::instance().openSockets4();

    Pkt4Ptr req = generatePktWithFqdn(DHCPREQUEST, Option4ClientFqdn::FLAG_S |
                                      Option4ClientFqdn::FLAG_E,
                                      "", Option4ClientFqdn::PARTIAL, true);

    Pkt4Ptr reply;
    ASSERT_NO_THROW(reply = srv_->processRequest(req));

    checkResponse(reply, DHCPACK, 1234);

    // Verify that there is one NameChangeRequest generated.
    ASSERT_EQ(1, d2_mgr_.getQueueSize());

    // The hostname is generated from the IP address acquired (yiaddr).
    std::string hostname = generatedNameFromAddress(reply->getYiaddr());

    verifyNameChangeRequest(isc::dhcp_ddns::CHG_ADD, true, true,
                            reply->getYiaddr().toText(), hostname,
                            "", // empty DHCID forces that it is not checked
                            time(NULL) + subnet_->getValid(),
                            subnet_->getValid(), true);

    req = generatePktWithFqdn(DHCPREQUEST, Option4ClientFqdn::FLAG_S |
                              Option4ClientFqdn::FLAG_E,
                              "", Option4ClientFqdn::PARTIAL, true);

    ASSERT_NO_THROW(reply = srv_->processRequest(req));

    checkResponse(reply, DHCPACK, 1234);

    // Verify that there are no NameChangeRequests generated.
    ASSERT_EQ(0, d2_mgr_.getQueueSize());
}

// Test that server generates client's hostname from the IP address assigned
// to it when DHCPv4 Client FQDN option specifies an empty domain-name  AND
// ddns updates are disabled.
TEST_F(NameDhcpv4SrvTest, processRequestEmptyDomainNameDisabled) {
    // Create fake interfaces and open fake sockets.
    IfaceMgrTestConfig test_config(true);
    IfaceMgr::instance().openSockets4();

    disableD2();
    Pkt4Ptr req = generatePktWithFqdn(DHCPREQUEST, Option4ClientFqdn::FLAG_S |
                                      Option4ClientFqdn::FLAG_E,
                                      "", Option4ClientFqdn::PARTIAL, true);
    Pkt4Ptr reply;
    ASSERT_NO_THROW(reply = srv_->processRequest(req));

    checkResponse(reply, DHCPACK, 1234);

    Option4ClientFqdnPtr fqdn = getClientFqdnOption(reply);
    ASSERT_TRUE(fqdn);

    // The hostname is generated from the IP address acquired (yiaddr).
    std::string hostname = generatedNameFromAddress(reply->getYiaddr());

    EXPECT_EQ(hostname, fqdn->getDomainName());
    EXPECT_EQ(Option4ClientFqdn::FULL, fqdn->getDomainNameType());
}


// Test that server generates client's hostname from the IP address assigned
// to it when Hostname option carries the top level domain-name.
TEST_F(NameDhcpv4SrvTest, processRequestTopLevelHostname) {
    IfaceMgrTestConfig test_config(true);
    IfaceMgr::instance().openSockets4();

    Pkt4Ptr req = generatePktWithHostname(DHCPREQUEST, ".");
    // Set interface for the incoming packet. The server requires it to
    // generate client id.
    req->setIface("eth1");

    Pkt4Ptr reply;
    ASSERT_NO_THROW(reply = srv_->processRequest(req));

    checkResponse(reply, DHCPACK, 1234);

    // Verify that there is one NameChangeRequest generated.
    ASSERT_EQ(1, d2_mgr_.getQueueSize());

    // The hostname is generated from the IP address acquired (yiaddr).
    std::string hostname = generatedNameFromAddress(reply->getYiaddr());

    verifyNameChangeRequest(isc::dhcp_ddns::CHG_ADD, true, true,
                            reply->getYiaddr().toText(), hostname,
                            "", // empty DHCID forces that it is not checked
                            time(NULL), subnet_->getValid(), true);
}

// Test that client may send two requests, each carrying FQDN option with
// a different domain-name. Server should use existing lease for the second
// request but modify the DNS entries for the lease according to the contents
// of the FQDN sent in the second request.
TEST_F(NameDhcpv4SrvTest, processTwoRequestsFqdn) {
    IfaceMgrTestConfig test_config(true);
    IfaceMgr::instance().openSockets4();

    Pkt4Ptr req1 = generatePktWithFqdn(DHCPREQUEST, Option4ClientFqdn::FLAG_S |
                                       Option4ClientFqdn::FLAG_E,
                                       "myhost.example.com.",
                                       Option4ClientFqdn::FULL, true);

    Pkt4Ptr reply;
    ASSERT_NO_THROW(reply = srv_->processRequest(req1));

    checkResponse(reply, DHCPACK, 1234);

    // Verify that there is one NameChangeRequest generated.
    ASSERT_EQ(1, d2_mgr_.getQueueSize());
    verifyNameChangeRequest(isc::dhcp_ddns::CHG_ADD, true, true,
                            reply->getYiaddr().toText(), "myhost.example.com.",
                            "00010132E91AA355CFBB753C0F0497A5A940436"
                            "965B68B6D438D98E680BF10B09F3BCF",
                            time(NULL), subnet_->getValid(), true);

    // Create another Request message but with a different FQDN. Server
    // should generate two NameChangeRequests: one to remove existing entry,
    // another one to add new entry with updated domain-name.
    Pkt4Ptr req2 = generatePktWithFqdn(DHCPREQUEST, Option4ClientFqdn::FLAG_S |
                                       Option4ClientFqdn::FLAG_E,
                                       "otherhost.example.com.",
                                       Option4ClientFqdn::FULL, true);

    ASSERT_NO_THROW(reply = srv_->processRequest(req2));

    checkResponse(reply, DHCPACK, 1234);

    // There should be two NameChangeRequests. Verify that they are valid.
    ASSERT_EQ(2, d2_mgr_.getQueueSize());
    verifyNameChangeRequest(isc::dhcp_ddns::CHG_REMOVE, true, true,
                            reply->getYiaddr().toText(),
                            "myhost.example.com.",
                            "00010132E91AA355CFBB753C0F0497A5A940436"
                            "965B68B6D438D98E680BF10B09F3BCF",
                            time(NULL), subnet_->getValid(), true);

    verifyNameChangeRequest(isc::dhcp_ddns::CHG_ADD, true, true,
                            reply->getYiaddr().toText(),
                            "otherhost.example.com.",
                            "000101A5AEEA7498BD5AD9D3BF600E49FF39A7E3"
                            "AFDCE8C3D0E53F35CC584DD63C89CA",
                            time(NULL), subnet_->getValid(), true);
}

// Test that client may send two requests, each carrying Hostname option with
// a different name. Server should use existing lease for the second request
// but modify the DNS entries for the lease according to the contents of the
// Hostname sent in the second request.
TEST_F(NameDhcpv4SrvTest, processTwoRequestsHostname) {
    IfaceMgrTestConfig test_config(true);
    IfaceMgr::instance().openSockets4();

    // Case in a hostname should be ignored.
    Pkt4Ptr req1 = generatePktWithHostname(DHCPREQUEST, "Myhost.Example.Com.");

    // Set interface for the incoming packet. The server requires it to
    // generate client id.
    req1->setIface("eth1");


    Pkt4Ptr reply;
    ASSERT_NO_THROW(reply = srv_->processRequest(req1));

    checkResponse(reply, DHCPACK, 1234);

    // Verify that there is one NameChangeRequest generated.
    ASSERT_EQ(1, d2_mgr_.getQueueSize());
    verifyNameChangeRequest(isc::dhcp_ddns::CHG_ADD, true, true,
                            reply->getYiaddr().toText(), "myhost.example.com.",
                            "00010132E91AA355CFBB753C0F0497A5A940436"
                            "965B68B6D438D98E680BF10B09F3BCF",
                            time(NULL), subnet_->getValid(), true);

    // Create another Request message but with a different Hostname. Server
    // should generate two NameChangeRequests: one to remove existing entry,
    // another one to add new entry with updated domain-name.
    Pkt4Ptr req2 = generatePktWithHostname(DHCPREQUEST, "otherhost");

    // Set interface for the incoming packet. The server requires it to
    // generate client id.
    req2->setIface("eth1");

    ASSERT_NO_THROW(reply = srv_->processRequest(req2));

    checkResponse(reply, DHCPACK, 1234);

    // There should be two NameChangeRequests. Verify that they are valid.
    ASSERT_EQ(2, d2_mgr_.getQueueSize());
    verifyNameChangeRequest(isc::dhcp_ddns::CHG_REMOVE, true, true,
                            reply->getYiaddr().toText(),
                            "myhost.example.com.",
                            "00010132E91AA355CFBB753C0F0497A5A940436"
                            "965B68B6D438D98E680BF10B09F3BCF",
                            time(NULL), subnet_->getValid(), true);

    verifyNameChangeRequest(isc::dhcp_ddns::CHG_ADD, true, true,
                            reply->getYiaddr().toText(),
                            "otherhost.example.com.",
                            "000101A5AEEA7498BD5AD9D3BF600E49FF39A7E3"
                            "AFDCE8C3D0E53F35CC584DD63C89CA",
                            time(NULL), subnet_->getValid(), true);
}

// Test that client may send two requests, each carrying the same FQDN option.
// Server should renew  existing lease for the second request without generating
// any NCRs.
TEST_F(NameDhcpv4SrvTest, processRequestRenewFqdn) {
    IfaceMgrTestConfig test_config(true);
    IfaceMgr::instance().openSockets4();

    Pkt4Ptr req1 = generatePktWithFqdn(DHCPREQUEST, Option4ClientFqdn::FLAG_S |
                                       Option4ClientFqdn::FLAG_E,
                                       "myhost.example.com.",
                                       Option4ClientFqdn::FULL, true);

    Pkt4Ptr reply;
    ASSERT_NO_THROW(reply = srv_->processRequest(req1));

    checkResponse(reply, DHCPACK, 1234);

    // Verify that there is one NameChangeRequest generated.
    ASSERT_EQ(1, d2_mgr_.getQueueSize());
    verifyNameChangeRequest(isc::dhcp_ddns::CHG_ADD, true, true,
                            reply->getYiaddr().toText(), "myhost.example.com.",
                            "00010132E91AA355CFBB753C0F0497A5A940436"
                            "965B68B6D438D98E680BF10B09F3BCF",
                            time(NULL), subnet_->getValid(), true);

    // Create another Request message with the same FQDN. Case changes in the
    // hostname should be ignored. Server should generate no NameChangeRequests.
    Pkt4Ptr req2 = generatePktWithFqdn(DHCPREQUEST, Option4ClientFqdn::FLAG_S |
                                       Option4ClientFqdn::FLAG_E,
                                       "Myhost.Example.Com.",
                                       Option4ClientFqdn::FULL, true);

    ASSERT_NO_THROW(reply = srv_->processRequest(req2));

    checkResponse(reply, DHCPACK, 1234);

    // There should be no NameChangeRequests.
    ASSERT_EQ(0, d2_mgr_.getQueueSize());
}

// Test that client may send two requests, each carrying the same hostname
// option.  Server should renew  existing lease for the second request without
// generating any NCRs.
TEST_F(NameDhcpv4SrvTest, processRequestRenewHostname) {
    IfaceMgrTestConfig test_config(true);
    IfaceMgr::instance().openSockets4();

    Pkt4Ptr req1 = generatePktWithHostname(DHCPREQUEST, "myhost.example.com.");

    // Set interface for the incoming packet. The server requires it to
    // generate client id.
    req1->setIface("eth1");

    Pkt4Ptr reply;
    ASSERT_NO_THROW(reply = srv_->processRequest(req1));

    checkResponse(reply, DHCPACK, 1234);

    // Verify that there is one NameChangeRequest generated.
    ASSERT_EQ(1, d2_mgr_.getQueueSize());
    verifyNameChangeRequest(isc::dhcp_ddns::CHG_ADD, true, true,
                            reply->getYiaddr().toText(), "myhost.example.com.",
                            "00010132E91AA355CFBB753C0F0497A5A940436"
                            "965B68B6D438D98E680BF10B09F3BCF",
                            time(NULL), subnet_->getValid(), true);

    // Create another Request message with the same Hostname. Case changes in the
    // hostname should be ignored. Server should generate no NameChangeRequests.
    Pkt4Ptr req2 = generatePktWithHostname(DHCPREQUEST, "Myhost.Example.Com.");

    // Set interface for the incoming packet. The server requires it to
    // generate client id.
    req2->setIface("eth1");

    ASSERT_NO_THROW(reply = srv_->processRequest(req2));

    checkResponse(reply, DHCPACK, 1234);

    // There should be no NameChangeRequests.
    ASSERT_EQ(0, d2_mgr_.getQueueSize());
}

// Test that when a release message is sent for a previously acquired lease,
// DDNS updates are enabled that the server generates a NameChangeRequest
// to remove entries corresponding to the released lease.
TEST_F(NameDhcpv4SrvTest, processRequestRelease) {
    IfaceMgrTestConfig test_config(true);
    IfaceMgr::instance().openSockets4();

    // Verify the updates are enabled.
    ASSERT_TRUE(CfgMgr::instance().ddnsEnabled());

    // Create and process a lease request so we have a lease to release.
    Pkt4Ptr req = generatePktWithFqdn(DHCPREQUEST, Option4ClientFqdn::FLAG_S |
                                      Option4ClientFqdn::FLAG_E,
                                      "myhost.example.com.",
                                      Option4ClientFqdn::FULL, true);
    Pkt4Ptr reply;
    ASSERT_NO_THROW(reply = srv_->processRequest(req));
    checkResponse(reply, DHCPACK, 1234);

    // Verify that there is one NameChangeRequest generated for lease.
    ASSERT_EQ(1, d2_mgr_.getQueueSize());
    verifyNameChangeRequest(isc::dhcp_ddns::CHG_ADD, true, true,
                            reply->getYiaddr().toText(), "myhost.example.com.",
                            "00010132E91AA355CFBB753C0F0497A5A940436"
                            "965B68B6D438D98E680BF10B09F3BCF",
                            time(NULL), subnet_->getValid(), true);

    // Create and process the Release message.
    Pkt4Ptr rel = Pkt4Ptr(new Pkt4(DHCPRELEASE, 1234));
    rel->setCiaddr(reply->getYiaddr());
    rel->setRemoteAddr(IOAddress("192.0.2.3"));
    rel->addOption(generateClientId());
    rel->addOption(srv_->getServerID());
    ASSERT_NO_THROW(srv_->processRelease(rel));

    // The lease has been removed, so there should be a NameChangeRequest to
    // remove corresponding DNS entries.
    ASSERT_EQ(1, d2_mgr_.getQueueSize());
    verifyNameChangeRequest(isc::dhcp_ddns::CHG_REMOVE, true, true,
                            reply->getYiaddr().toText(), "myhost.example.com.",
                            "00010132E91AA355CFBB753C0F0497A5A940436"
                            "965B68B6D438D98E680BF10B09F3BCF",
                            time(NULL), subnet_->getValid(), true);
}

// Test that when the Release message is sent for a previously acquired lease
// and DDNS updates are disabled that server does NOT generate a
// NameChangeRequest to remove entries corresponding to the released lease.
// Queue size is not available when updates are not enabled, however,
// attempting to send a NCR when updates disabled will result in a throw.
// If no throws are experienced then no attempt was made to send a NCR.
TEST_F(NameDhcpv4SrvTest, processRequestReleaseUpdatesDisabled) {
    // Create fake interfaces and open fake sockets.
    IfaceMgrTestConfig test_config(true);
    IfaceMgr::instance().openSockets4();

    // Disable DDNS.
    disableD2();
    ASSERT_FALSE(CfgMgr::instance().ddnsEnabled());

    // Create and process a lease request so we have a lease to release.
    Pkt4Ptr req = generatePktWithFqdn(DHCPREQUEST, Option4ClientFqdn::FLAG_S |
                                      Option4ClientFqdn::FLAG_E,
                                      "myhost.example.com.",
                                      Option4ClientFqdn::FULL, true);
    Pkt4Ptr reply;
    ASSERT_NO_THROW(reply = srv_->processRequest(req));
    checkResponse(reply, DHCPACK, 1234);

    // Create and process the Release message.
    Pkt4Ptr rel = Pkt4Ptr(new Pkt4(DHCPRELEASE, 1234));
    rel->setCiaddr(reply->getYiaddr());
    rel->setRemoteAddr(IOAddress("192.0.2.3"));
    rel->addOption(generateClientId());
    rel->addOption(srv_->getServerID());
    ASSERT_NO_THROW(srv_->processRelease(rel));
}

// This test verifies that the server sends the FQDN option to the client
// with the reserved hostname.
TEST_F(NameDhcpv4SrvTest, fqdnReservation) {
    Dhcp4Client client(Dhcp4Client::SELECTING);
    // Use HW address that matches the reservation entry in the configuration.
    client.setHWAddress("aa:bb:cc:dd:ee:ff");
    // Configure DHCP server.
    configure(CONFIGS[0], *client.getServer());
    // Make sure that DDNS is enabled.
    ASSERT_TRUE(CfgMgr::instance().ddnsEnabled());
    ASSERT_NO_THROW(client.getServer()->startD2());
    // Include the Client FQDN option.
    ASSERT_NO_THROW(client.includeFQDN(Option4ClientFqdn::FLAG_S | Option4ClientFqdn::FLAG_E,
                                       "client-name", Option4ClientFqdn::PARTIAL));
    // Send the DHCPDISCOVER.
    ASSERT_NO_THROW(client.doDiscover());

    // Make sure that the server responded.
    Pkt4Ptr resp = client.getContext().response_;
    ASSERT_TRUE(resp);
    ASSERT_EQ(DHCPOFFER, static_cast<int>(resp->getType()));

    // Obtain the FQDN option sent in the response and make sure that the server
    // has used the hostname reserved for this client.
    Option4ClientFqdnPtr fqdn;
    fqdn = boost::dynamic_pointer_cast<Option4ClientFqdn>(resp->getOption(DHO_FQDN));
    ASSERT_TRUE(fqdn);
    EXPECT_EQ("unique-host.example.org.", fqdn->getDomainName());

    // When receiving DHCPDISCOVER, no NCRs should be generated.
    EXPECT_EQ(0, d2_mgr_.getQueueSize());

    // Now send the DHCPREQUEST with including the FQDN option.
    ASSERT_NO_THROW(client.doRequest());
    resp = client.getContext().response_;
    ASSERT_TRUE(resp);
    ASSERT_EQ(DHCPACK, static_cast<int>(resp->getType()));

    // Once again check that the FQDN is as expected.
    fqdn = boost::dynamic_pointer_cast<Option4ClientFqdn>(resp->getOption(DHO_FQDN));
    ASSERT_TRUE(fqdn);
    EXPECT_EQ("unique-host.example.org.", fqdn->getDomainName());

    {
        SCOPED_TRACE("Verify the correctness of the NCR for the"
                     "unique-host.example.org");

        // Because this is a new lease, there should be one NCR which adds the
        // new DNS entry.
        ASSERT_EQ(1, CfgMgr::instance().getD2ClientMgr().getQueueSize());
        verifyNameChangeRequest(isc::dhcp_ddns::CHG_ADD, true, true,
                                resp->getYiaddr().toText(),
                                "unique-host.example.org.",
                                "000001ACB52196C8F3BCC1DF3BA1F40BAC39BF23"
                                "0D280858B1ED7696E174C4479E3372",
                                time(NULL), subnet_->getValid(), true);
    }

    // And that this FQDN has been stored in the lease database.
    Lease4Ptr lease = LeaseMgrFactory::instance().getLease4(client.config_.lease_.addr_);
    ASSERT_TRUE(lease);
    EXPECT_EQ("unique-host.example.org.", lease->hostname_);

    // Reconfigure DHCP server to use a different hostname for the client.
    configure(CONFIGS[1], *client.getServer());
    // Make sure that DDNS is enabled.
    ASSERT_TRUE(CfgMgr::instance().ddnsEnabled());
    ASSERT_NO_THROW(client.getServer()->startD2());

    // Client is in the renewing state.
    client.setState(Dhcp4Client::RENEWING);
    client.doRequest();
    resp = client.getContext().response_;
    ASSERT_TRUE(resp);
    ASSERT_EQ(DHCPACK, static_cast<int>(resp->getType()));

    // The new FQDN should contain a different name this time.
    fqdn = boost::dynamic_pointer_cast<Option4ClientFqdn>(resp->getOption(DHO_FQDN));
    ASSERT_TRUE(fqdn);
    EXPECT_EQ("foobar.fake-suffix.isc.org.", fqdn->getDomainName());

    // And the lease in the lease database should also contain this new FQDN.
    lease = LeaseMgrFactory::instance().getLease4(client.config_.lease_.addr_);
    ASSERT_TRUE(lease);
    EXPECT_EQ("foobar.fake-suffix.isc.org.", lease->hostname_);

    // Now there should be two name NCRs. One that removes the previous entry
    // and the one that adds a new entry for the new hostname.
    ASSERT_EQ(2, CfgMgr::instance().getD2ClientMgr().getQueueSize());

    {
        SCOPED_TRACE("Verify the correctness of the CHG_REMOVE NCR for the "
                     "unique-host.example.org");

        verifyNameChangeRequest(isc::dhcp_ddns::CHG_REMOVE, true, true,
                                resp->getYiaddr().toText(),
                                "unique-host.example.org.",
                                "000001ACB52196C8F3BCC1DF3BA1F40BAC39BF23"
                                "0D280858B1ED7696E174C4479E3372",
                                time(NULL), subnet_->getValid(), true);
    }

    {
        SCOPED_TRACE("Verify the correctness of the CHG_ADD NCR for the "
                     "foobar.fake-suffix.isc.org");

        verifyNameChangeRequest(isc::dhcp_ddns::CHG_ADD, true, true,
                                resp->getYiaddr().toText(),
                                "foobar.fake-suffix.isc.org.",
                                "0000017C29B3C236344924E448E247F3FD56C7E9"
                                "167B3397B1305FB664C160B967CE1F",
                                time(NULL), subnet_->getValid(), true);
    }
}

// This test verifies that the server sends the Hostname option to the client
// with the reserved hostname.
TEST_F(NameDhcpv4SrvTest, hostnameReservation) {
    Dhcp4Client client(Dhcp4Client::SELECTING);
    // Use HW address that matches the reservation entry in the configuration.
    client.setHWAddress("aa:bb:cc:dd:ee:ff");
    // Configure DHCP server.
    configure(CONFIGS[0], *client.getServer());
    // Make sure that DDNS is enabled.
    ASSERT_TRUE(CfgMgr::instance().ddnsEnabled());
    ASSERT_NO_THROW(client.getServer()->startD2());
    // Include the Hostname option.
    ASSERT_NO_THROW(client.includeHostname("client-name"));

    // Send the DHCPDISCOVER
    ASSERT_NO_THROW(client.doDiscover());

    // Make sure that the server responded.
    Pkt4Ptr resp = client.getContext().response_;
    ASSERT_TRUE(resp);
    ASSERT_EQ(DHCPOFFER, static_cast<int>(resp->getType()));

    // Obtain the Hostname option sent in the response and make sure that the server
    // has used the hostname reserved for this client.
    OptionStringPtr hostname;
    hostname = boost::dynamic_pointer_cast<OptionString>(resp->getOption(DHO_HOST_NAME));
    ASSERT_TRUE(hostname);
    EXPECT_EQ("unique-host.example.org", hostname->getValue());

    // Now send the DHCPREQUEST with including the Hostname option.
    ASSERT_NO_THROW(client.doRequest());
    resp = client.getContext().response_;
    ASSERT_TRUE(resp);
    ASSERT_EQ(DHCPACK, static_cast<int>(resp->getType()));

    // Once again check that the Hostname is as expected.
    hostname = boost::dynamic_pointer_cast<OptionString>(resp->getOption(DHO_HOST_NAME));
    ASSERT_TRUE(hostname);
    EXPECT_EQ("unique-host.example.org", hostname->getValue());

    // And that this hostname has been stored in the lease database.
    Lease4Ptr lease = LeaseMgrFactory::instance().getLease4(client.config_.lease_.addr_);
    ASSERT_TRUE(lease);
    EXPECT_EQ("unique-host.example.org", lease->hostname_);

    // Because this is a new lease, there should be one NCR which adds the
    // new DNS entry.
    ASSERT_EQ(1, CfgMgr::instance().getD2ClientMgr().getQueueSize());
    {
        SCOPED_TRACE("Verify the correctness of the NCR for the"
                     "unique-host.example.org");

        verifyNameChangeRequest(isc::dhcp_ddns::CHG_ADD, true, true,
                                resp->getYiaddr().toText(),
                                "unique-host.example.org.",
                                "000001ACB52196C8F3BCC1DF3BA1F40BAC39BF23"
                                "0D280858B1ED7696E174C4479E3372",
                                time(NULL), subnet_->getValid(), true);
    }

    // Reconfigure DHCP server to use a different hostname for the client.
    configure(CONFIGS[1], *client.getServer());
    // Make sure that DDNS is enabled.
    ASSERT_TRUE(CfgMgr::instance().ddnsEnabled());
    ASSERT_NO_THROW(client.getServer()->startD2());

    // Client is in the renewing state.
    client.setState(Dhcp4Client::RENEWING);
    client.doRequest();
    resp = client.getContext().response_;
    ASSERT_TRUE(resp);
    ASSERT_EQ(DHCPACK, static_cast<int>(resp->getType()));

    // The new hostname should be different than previously.
    hostname = boost::dynamic_pointer_cast<OptionString>(resp->getOption(DHO_HOST_NAME));
    ASSERT_TRUE(hostname);
    EXPECT_EQ("foobar.fake-suffix.isc.org", hostname->getValue());

    // And the lease in the lease database should also contain this new FQDN.
    lease = LeaseMgrFactory::instance().getLease4(client.config_.lease_.addr_);
    ASSERT_TRUE(lease);
    EXPECT_EQ("foobar.fake-suffix.isc.org", lease->hostname_);

    // Now there should be two name NCRs. One that removes the previous entry
    // and the one that adds a new entry for the new hostname.
    ASSERT_EQ(2, CfgMgr::instance().getD2ClientMgr().getQueueSize());
    {
        SCOPED_TRACE("Verify the correctness of the CHG_REMOVE NCR for the "
                     "unique-host.example.org");

        verifyNameChangeRequest(isc::dhcp_ddns::CHG_REMOVE, true, true,
                                resp->getYiaddr().toText(),
                                "unique-host.example.org.",
                                "000001ACB52196C8F3BCC1DF3BA1F40BAC39BF23"
                                "0D280858B1ED7696E174C4479E3372",
                                time(NULL), subnet_->getValid(), true);
    }

    {
        SCOPED_TRACE("Verify the correctness of the CHG_ADD NCR for the "
                     "foobar.fake-suffix.isc.org");

        verifyNameChangeRequest(isc::dhcp_ddns::CHG_ADD, true, true,
                                resp->getYiaddr().toText(),
                                "foobar.fake-suffix.isc.org.",
                                "0000017C29B3C236344924E448E247F3FD56C7E9"
                                "167B3397B1305FB664C160B967CE1F",
                                time(NULL), subnet_->getValid(), true);
    }
}

// This test verifies that the server sends the Hostname option to the client
// with hostname reservation and which included hostname option code in the
// Parameter Request List.
TEST_F(NameDhcpv4SrvTest, hostnameReservationPRL) {
    Dhcp4Client client(Dhcp4Client::SELECTING);
    // Use HW address that matches the reservation entry in the configuration.
    client.setHWAddress("aa:bb:cc:dd:ee:ff");
    // Configure DHCP server.
    configure(CONFIGS[4], *client.getServer());
    // Make sure that DDNS is enabled.
    ASSERT_FALSE(CfgMgr::instance().ddnsEnabled());
    // Request Hostname option.
    ASSERT_NO_THROW(client.requestOption(DHO_HOST_NAME));

    // Send the DHCPDISCOVER
    ASSERT_NO_THROW(client.doDiscover());

    // Make sure that the server responded.
    Pkt4Ptr resp = client.getContext().response_;
    ASSERT_TRUE(resp);
    ASSERT_EQ(DHCPOFFER, static_cast<int>(resp->getType()));

    // Obtain the Hostname option sent in the response and make sure that the server
    // has used the hostname reserved for this client.
    OptionStringPtr hostname;
    hostname = boost::dynamic_pointer_cast<OptionString>(resp->getOption(DHO_HOST_NAME));
    ASSERT_TRUE(hostname);
    EXPECT_EQ("reserved.example.org", hostname->getValue());

    // Now send the DHCPREQUEST with including the Hostname option.
    ASSERT_NO_THROW(client.doRequest());
    resp = client.getContext().response_;
    ASSERT_TRUE(resp);
    ASSERT_EQ(DHCPACK, static_cast<int>(resp->getType()));

    // Once again check that the Hostname is as expected.
    hostname = boost::dynamic_pointer_cast<OptionString>(resp->getOption(DHO_HOST_NAME));
    ASSERT_TRUE(hostname);
    EXPECT_EQ("reserved.example.org", hostname->getValue());
}

// This test verifies that the server sends the Hostname option to the client
// with partial hostname reservation and with the global qualifying-suffix set.
TEST_F(NameDhcpv4SrvTest, hostnameReservationNoDNSQualifyingSuffix) {
    Dhcp4Client client(Dhcp4Client::SELECTING);
    // Use HW address that matches the reservation entry in the configuration.
    client.setHWAddress("aa:bb:cc:dd:ee:ff");
    // Configure DHCP server.
    configure(CONFIGS[5], *client.getServer());
    // Make sure that DDNS is enabled.
    ASSERT_FALSE(CfgMgr::instance().ddnsEnabled());
    // Include the Hostname option.
    ASSERT_NO_THROW(client.includeHostname("client-name"));

    // Send the DHCPDISCOVER
    ASSERT_NO_THROW(client.doDiscover());

    // Make sure that the server responded.
    Pkt4Ptr resp = client.getContext().response_;
    ASSERT_TRUE(resp);
    ASSERT_EQ(DHCPOFFER, static_cast<int>(resp->getType()));

    // Obtain the Hostname option sent in the response and make sure that the server
    // has used the hostname reserved for this client.
    OptionStringPtr hostname;
    hostname = boost::dynamic_pointer_cast<OptionString>(resp->getOption(DHO_HOST_NAME));
    ASSERT_TRUE(hostname);
    EXPECT_EQ("foo-bar.example.isc.org", hostname->getValue());

    // Now send the DHCPREQUEST with including the Hostname option.
    ASSERT_NO_THROW(client.doRequest());
    resp = client.getContext().response_;
    ASSERT_TRUE(resp);
    ASSERT_EQ(DHCPACK, static_cast<int>(resp->getType()));

    // Once again check that the Hostname is as expected.
    hostname = boost::dynamic_pointer_cast<OptionString>(resp->getOption(DHO_HOST_NAME));
    ASSERT_TRUE(hostname);
    EXPECT_EQ("foo-bar.example.isc.org", hostname->getValue());
}

// Test verifies that the server properly generates a FQDN when the client
// FQDN name is blank, whether or not DDNS updates are enabled.  It also
// verifies that the lease is only in the database following a DHCPREQUEST and
// that the lease contains the generated FQDN.
TEST_F(NameDhcpv4SrvTest, emptyFqdn) {
    Dhcp4Client client(Dhcp4Client::SELECTING);
    isc::asiolink::IOAddress expected_address("10.0.0.10");
    std::string expected_fqdn("myhost-10-0-0-10.fake-suffix.isc.org.");

    // Load a configuration with DDNS updates disabled
    configure(CONFIGS[2], *client.getServer());
    ASSERT_FALSE(CfgMgr::instance().ddnsEnabled());

    // Include the Client FQDN option.
    ASSERT_NO_THROW(client.includeFQDN((Option4ClientFqdn::FLAG_S
                                        | Option4ClientFqdn::FLAG_E),
                                       "", Option4ClientFqdn::PARTIAL));

    // Send the DHCPDISCOVER
    ASSERT_NO_THROW(client.doDiscover());

    // Make sure that the server responded.
    Pkt4Ptr resp = client.getContext().response_;
    ASSERT_TRUE(resp);
    ASSERT_EQ(DHCPOFFER, static_cast<int>(resp->getType()));

    // Make sure the response FQDN has the generated name and FQDN flags are
    // correct for updated disabled.
    Option4ClientFqdnPtr fqdn;
    fqdn = boost::dynamic_pointer_cast<Option4ClientFqdn>(resp->getOption(DHO_FQDN));
    ASSERT_TRUE(fqdn);
    EXPECT_EQ(expected_fqdn, fqdn->getDomainName());
    checkFqdnFlags(resp, (Option4ClientFqdn::FLAG_N |
                          Option4ClientFqdn::FLAG_E |
                          Option4ClientFqdn::FLAG_O));

    // Make sure the lease is NOT in the database.
    Lease4Ptr lease = LeaseMgrFactory::instance().getLease4(IOAddress(expected_address));
    ASSERT_FALSE(lease);

    // Now test with updates enabled
    configure(CONFIGS[3], *client.getServer());
    ASSERT_TRUE(CfgMgr::instance().ddnsEnabled());
    ASSERT_NO_THROW(client.getServer()->startD2());

    // Send the DHCPDISCOVER
    ASSERT_NO_THROW(client.doDiscover());

    // Make sure that the server responded.
    resp = client.getContext().response_;
    ASSERT_TRUE(resp);
    ASSERT_EQ(DHCPOFFER, static_cast<int>(resp->getType()));

    // Make sure the response FQDN has the generated name and FQDN flags are
    // correct for updates enabled.
    fqdn = boost::dynamic_pointer_cast<Option4ClientFqdn>(resp->getOption(DHO_FQDN));
    ASSERT_TRUE(fqdn);
    EXPECT_EQ(expected_fqdn, fqdn->getDomainName());
    checkFqdnFlags(resp, (Option4ClientFqdn::FLAG_E |
                          Option4ClientFqdn::FLAG_S));

    // Make sure the lease is NOT in the database.
    lease = LeaseMgrFactory::instance().getLease4(IOAddress(expected_address));
    ASSERT_FALSE(lease);

    // Do a DORA and verify that the lease exists and the name is correct.
    ASSERT_NO_THROW(client.doDORA());

    // Make sure that the server responded.
    resp = client.getContext().response_;
    ASSERT_TRUE(resp);
    ASSERT_EQ(DHCPACK, static_cast<int>(resp->getType()));

    // Make sure the response FQDN has the generated name and FQDN flags are
    // correct for updates enabled.
    fqdn = boost::dynamic_pointer_cast<Option4ClientFqdn>(resp->getOption(DHO_FQDN));
    ASSERT_TRUE(fqdn);
    EXPECT_EQ(expected_fqdn, fqdn->getDomainName());
    checkFqdnFlags(resp, (Option4ClientFqdn::FLAG_E |
                          Option4ClientFqdn::FLAG_S));

    // Make sure the lease is in the database and hostname is correct.
    lease = LeaseMgrFactory::instance().getLease4(IOAddress(expected_address));
    ASSERT_TRUE(lease);
    EXPECT_EQ(expected_fqdn, lease->hostname_);

}

// Verifies that the replace-client-name behavior is correct for each of
// the supported modes.
TEST_F(NameDhcpv4SrvTest, replaceClientNameModeTest) {

    testReplaceClientNameMode("never",
                              CLIENT_NAME_NOT_PRESENT, NAME_NOT_REPLACED);
    testReplaceClientNameMode("never",
                              CLIENT_NAME_PRESENT, NAME_NOT_REPLACED);

    testReplaceClientNameMode("always",
                              CLIENT_NAME_NOT_PRESENT, NAME_REPLACED);
    testReplaceClientNameMode("always",
                              CLIENT_NAME_PRESENT, NAME_REPLACED);

    testReplaceClientNameMode("when-present",
                              CLIENT_NAME_NOT_PRESENT, NAME_NOT_REPLACED);
    testReplaceClientNameMode("when-present",
                              CLIENT_NAME_PRESENT, NAME_REPLACED);

    testReplaceClientNameMode("when-not-present",
                              CLIENT_NAME_NOT_PRESENT, NAME_REPLACED);
    testReplaceClientNameMode("when-not-present",
                              CLIENT_NAME_PRESENT, NAME_NOT_REPLACED);
}

// Verifies that setting hostname-char-set sanitizes Hostname option
// values received from clients.
TEST_F(NameDhcpv4SrvTest, sanitizeHost) {
    Dhcp4Client client(Dhcp4Client::SELECTING);

    // Configure DHCP server.
    configure(CONFIGS[6], *client.getServer());

    // Make sure that DDNS is enabled.
    ASSERT_TRUE(CfgMgr::instance().ddnsEnabled());
    ASSERT_NO_THROW(client.getServer()->startD2());

    struct Scenario {
        std::string description_;
        std::string original_;
        std::string sanitized_;
    };

    std::vector<Scenario> scenarios = {
        {
            "unqualified host name with invalid characters",
            "one-&$_-host",
            "one-xxx-host.example.org"
        },
        {
            "qualified host name with invalid characters",
            "two-&$_-host.other.org",
            "two-xxx-host.other.org"
        },
        {
            "unqualified host name with all valid characters",
            "three-ok-host",
            "three-ok-host.example.org"
        },
        {
            "qualified host name with valid characters",
            "four-ok-host.other.org",
            "four-ok-host.other.org"
        }
    };

    Pkt4Ptr resp;
    OptionStringPtr hostname;
    for (auto scenario : scenarios) {
        SCOPED_TRACE((scenario).description_);
        {
            // Set the hostname option.
            ASSERT_NO_THROW(client.includeHostname((scenario).original_));

            // Send the DHCPDISCOVER and make sure that the server responded.
            ASSERT_NO_THROW(client.doDiscover());
            resp = client.getContext().response_;
            ASSERT_TRUE(resp);
            ASSERT_EQ(DHCPOFFER, static_cast<int>(resp->getType()));

            // Make sure the response hostname is what we expect.
            hostname = boost::dynamic_pointer_cast<OptionString>(resp->getOption(DHO_HOST_NAME));
            ASSERT_TRUE(hostname);
            EXPECT_EQ((scenario).sanitized_, hostname->getValue());
        }
    }
}

// Verifies that setting hostname-char-set sanitizes FQDN option
// values received from clients.
TEST_F(NameDhcpv4SrvTest, sanitizeFqdn) {
    Dhcp4Client client(Dhcp4Client::SELECTING);

    // Configure DHCP server.
    configure(CONFIGS[6], *client.getServer());

    // Make sure that DDNS is enabled.
    ASSERT_TRUE(CfgMgr::instance().ddnsEnabled());
    ASSERT_NO_THROW(client.getServer()->startD2());

    struct Scenario {
        std::string description_;
        std::string original_;
        Option4ClientFqdn::DomainNameType name_type_;
        std::string sanitized_;
    };

    std::vector<Scenario> scenarios = {
        {
            "unqualified FQDN with invalid characters",
            "one-&*_-host",
            Option4ClientFqdn::PARTIAL,
            "one-xxx-host.example.org."
        },
        {
            "qualified FQDN with invalid characters",
            "two-&*_-host.other.org",
            Option4ClientFqdn::FULL,
            "two-xxx-host.other.org."
        },
        {
            "unqualified FQDN name with all valid characters",
            "three-ok-host",
            Option4ClientFqdn::PARTIAL,
            "three-ok-host.example.org."
        },
        {
            "qualified FQDN name with valid characters",
            "four-ok-host.other.org",
            Option4ClientFqdn::FULL,
            "four-ok-host.other.org."
        }
    };

    Pkt4Ptr resp;
    Option4ClientFqdnPtr fqdn;
    for (auto scenario = scenarios.begin(); scenario != scenarios.end(); ++scenario) {
        SCOPED_TRACE((*scenario).description_);
        {
        // Set the hostname option.
        ASSERT_NO_THROW(client.includeHostname((*scenario).original_));
        ASSERT_NO_THROW(client.includeFQDN(0, (*scenario).original_, (*scenario).name_type_));

        // Send the DHCPDISCOVER and make sure that the server responded.
        ASSERT_NO_THROW(client.doDiscover());
        resp = client.getContext().response_;
        ASSERT_TRUE(resp);
        ASSERT_EQ(DHCPOFFER, static_cast<int>(resp->getType()));

        // Make sure the response fqdn is what we expect.
        fqdn = boost::dynamic_pointer_cast<Option4ClientFqdn>(resp->getOption(DHO_FQDN));
        ASSERT_TRUE(fqdn);
        EXPECT_EQ((*scenario).sanitized_, fqdn->getDomainName());
        }
    }
}


} // end of anonymous namespace
