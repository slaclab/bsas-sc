#include <registryRecordType.h>

extern "C" int stacker_registerRecordDeviceDriver(struct dbBase *pbase)
{
    return registerRecordDeviceDriver(pbase);
}
