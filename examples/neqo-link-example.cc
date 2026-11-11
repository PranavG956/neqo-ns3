#include "ns3/neqo-link-test.h"

int
main()
{
    return ns3::NeqoRunInProcessHandshake() ? 0 : 1;
}
