#!../../bin/linux-x86_64/simulator

dbLoadDatabase("/workspace/dbd/simulator.dbd",0,0)
simulator_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("/workspace/db/sim_table.db","P=SIM:,R=TABLE,SCAN=1 second,NUM_TABLES=1,NUM_SIGNALS=2,NUM_SAMPLES=1,CONFIG=0,TIME_STEP_SEC=0.001,NUM_ROWS=5")

iocInit()
