/*
 * (c) 2017-2020 Copyright, Real-Time Innovations, Inc. (RTI)
 * All rights reserved.
 *
 * RTI grants Licensee a license to use, modify, compile, and create derivative
 * works of the Software solely in combination with RTI Connext DDS. Licensee
 * may redistribute copies of the Software provided that all such copies are
 * subject to this License. The Software is provided "as is", with no warranty
 * of any type, including any warranty for fitness for any purpose. RTI is
 * under no obligation to maintain or support the Software. RTI shall not be
 * liable for any incidental or consequential damages arising out of the use or
 * inability to use the Software. For purposes of clarity, nothing in this
 * License prevents Licensee from using alternate versions of DDS, provided
 * that Licensee may not combine or link such alternate versions of DDS with
 * the Software.
 */

#include "config/XmlEntities.hpp"
#include "config/XmlTransformationParams.hpp"
#include "plugins/adapters/DdsOpcUaAdapterProperty.hpp"
#include "plugins/adapters/OpcUaAttributeServiceStreamWriter.hpp"
#include "plugins/adapters/OpcUaSubscriptionStreamReader.hpp"
#include "plugins/adapters/OpcUaConnection.hpp"

namespace rti { namespace ddsopcua { namespace adapters {

OpcUaConnection::OpcUaConnection(
        const DdsOpcUaAdapterProperty& adapter_property,
        const rti::routing::PropertySet& connection_property)
        : adapter_property_(adapter_property),
          connection_property_(connection_property),
          async_stream_readers_(),
          opcua_attributeservice_streamreader_(nullptr),
          opcua_client_(),
          opcua_client_async_thread_(
                  opcua_client_,
                  "OPC UA Client Async Thread")
{
    // Fully-qualified name of the opcua server property associated with the
    // connection
    std::string opcua_server_xml_fqn = connection_property.at(
            config::XmlTransformationParams ::
                    DDSOPCUA_OPCUA_CONNECTION_FQN_PROPERTY);

    // Configure new OPC UA Client
    opcua::sdk::client::ClientProperty client_property;
    config::XmlOpcUaClient::get_client_property(
            client_property,
            adapter_property_.xml_root(),
            opcua_server_xml_fqn);
    opcua_client_.reset(client_property);
    opcua_client_async_thread_.timeout(client_property.run_async_timeout);

    // Connect to new OPC UA Client
    std::string server_uri = connection_property_.at(
            config::XmlTransformationParams ::
                    DDSOPCUA_OPCUA_CONNECTION_SERVER_URL_PROPERTY);
    opcua_client_.connect(server_uri);
}

OpcUaConnection::~OpcUaConnection()
{
}

rti::routing::adapter::StreamWriter* OpcUaConnection::create_stream_writer(
        rti::routing::adapter::Session* session,
        const rti::routing::StreamInfo& stream_info,
        const rti::routing::PropertySet& property)
{
    rti::routing::adapter::StreamWriter* stream_writer = nullptr;

    if (stream_info.stream_name() == "OpcUaAttributeServiceSet_Request") {
        stream_writer = new OpcUaAttributeServiceStreamWriter(
                opcua_client_,
                opcua_attributeservice_streamreader_);
    }

    return stream_writer;
}

void OpcUaConnection::delete_stream_writer(
        rti::routing::adapter::StreamWriter* stream_writer)
{
    delete stream_writer;
}

rti::routing::adapter::StreamReader* OpcUaConnection::create_stream_reader(
        rti::routing::adapter::Session*,
        const rti::routing::StreamInfo& stream_info,
        const rti::routing::PropertySet& stream_reader_property,
        rti::routing::adapter::StreamReaderListener* listener)
{
    rti::routing::adapter::StreamReader* stream_reader = nullptr;

    if (stream_info.stream_name() == "OpcUaAttributeServiceSet_Reply") {
        stream_reader = opcua_attributeservice_streamreader_ =
                new OpcUaAttributeServiceStreamReader(
                        stream_info,
                        opcua_client_);
    } else {
        OpcUaSubscriptionStreamReader* opcua_subs_sr = nullptr;
        try {
            opcua_subs_sr = new OpcUaSubscriptionStreamReader(
                    adapter_property_,
                    stream_info,
                    stream_reader_property,
                    listener,
                    opcua_client_);
            opcua_subs_sr->initialize_subscription();
        } catch (const std::exception& e) {
            GATEWAYLog_exception(&DDSOPCUA_LOG_ANY_s, e.what());
            opcua_subs_sr->finalize_subscription();
        }

        stream_reader = opcua_subs_sr;

        if (async_stream_readers_.size() == 0) {
            opcua_client_async_thread_.start();
        }
        async_stream_readers_.push_back(
                reinterpret_cast<uintptr_t>(stream_reader));
    }

    return stream_reader;
}

void OpcUaConnection::delete_stream_reader(
        rti::routing::adapter::StreamReader* stream_reader)
{
    // Stop opcua_client_async_thread_ before removing the stream reader if
    // this is the last OpcUaSubscriptionStreamReader to be removed
    std::vector<uintptr_t>::iterator it = std::find(
            async_stream_readers_.begin(),
            async_stream_readers_.end(),
            reinterpret_cast<uintptr_t>(stream_reader));
    if (it != async_stream_readers_.end()) {
        async_stream_readers_.erase(it);
    }

    delete stream_reader;

    if (async_stream_readers_.size() == 0) {
        opcua_client_async_thread_.stop();
    }
}

rti::opcua::sdk::client::Client& OpcUaConnection::connection_client()
{
    return opcua_client_;
}

}}}  // namespace rti::ddsopcua::adapters
