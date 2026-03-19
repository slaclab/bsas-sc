#!../../bin/linux-x86_64/simulator

dbLoadDatabase("/workspace/dbd/simulator.dbd",0,0)
simulator_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("/workspace/db/sim_0016.db","P=SIM:,R=SCALAR:,SCAN=100 Hz")

iocInit()
