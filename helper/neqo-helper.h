#ifndef NEQO_HELPER_H
#define NEQO_HELPER_H

#include "ns3/application-container.h"
#include "ns3/inet-socket-address.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"

namespace ns3
{
namespace neqo_ns3
{

class NeqoServerHelper
{
  public:
    explicit NeqoServerHelper(const InetSocketAddress& listenAddr);
    void SetAttribute(const std::string& name, const AttributeValue& value);
    ApplicationContainer Install(NodeContainer nodes) const;
    ApplicationContainer Install(Ptr<Node> node) const;

  private:
    ObjectFactory m_factory;
};

class NeqoClientHelper
{
  public:
    explicit NeqoClientHelper(const InetSocketAddress& remoteAddr);
    void SetAttribute(const std::string& name, const AttributeValue& value);
    ApplicationContainer Install(NodeContainer nodes) const;
    ApplicationContainer Install(Ptr<Node> node) const;

  private:
    ObjectFactory m_factory;
};

} // namespace neqo_ns3
} // namespace ns3

#endif