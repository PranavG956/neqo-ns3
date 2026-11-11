// neqo-helper.cc
#include "neqo-helper.h"

#include "ns3/address.h"
#include "ns3/log.h"
#include "ns3/node.h"

namespace ns3
{
NS_LOG_COMPONENT_DEFINE("NeqoHelper");
namespace neqo_ns3
{

NeqoServerHelper::NeqoServerHelper(const InetSocketAddress& listenAddr)
{
    m_factory.SetTypeId("ns3::neqo_ns3::NeqoServerApp");
    m_factory.Set("ListenAddress", AddressValue(listenAddr));
}

void
NeqoServerHelper::SetAttribute(const std::string& name, const AttributeValue& value)
{
    m_factory.Set(name, value);
}

ApplicationContainer
NeqoServerHelper::Install(NodeContainer nodes) const
{
    ApplicationContainer apps;
    for (auto it = nodes.Begin(); it != nodes.End(); ++it)
    {
        apps.Add(Install(*it));
    }
    return apps;
}

ApplicationContainer
NeqoServerHelper::Install(Ptr<Node> node) const
{
    Ptr<Application> app = m_factory.Create<Application>();
    node->AddApplication(app);
    return ApplicationContainer(app);
}

NeqoClientHelper::NeqoClientHelper(const InetSocketAddress& remoteAddr)
{
    m_factory.SetTypeId("ns3::neqo_ns3::NeqoClientApp");
    m_factory.Set("RemoteAddress", AddressValue(remoteAddr));
}

void
NeqoClientHelper::SetAttribute(const std::string& name, const AttributeValue& value)
{
    m_factory.Set(name, value);
}

ApplicationContainer
NeqoClientHelper::Install(NodeContainer nodes) const
{
    ApplicationContainer apps;
    for (auto it = nodes.Begin(); it != nodes.End(); ++it)
    {
        apps.Add(Install(*it));
    }
    return apps;
}

ApplicationContainer
NeqoClientHelper::Install(Ptr<Node> node) const
{
    Ptr<Application> app = m_factory.Create<Application>();
    node->AddApplication(app);
    return ApplicationContainer(app);
}

} // namespace neqo_ns3
} // namespace ns3