#include <registryRecordType.h>

extern "C" int simulator_registerRecordDeviceDriver(struct dbBase *pbase)
{
    return registerRecordDeviceDriver(pbase);
}
