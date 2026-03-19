#!../../bin/linux-x86_64/simulator

dbLoadDatabase("/workspace/dbd/simulator.dbd",0,0)
simulator_registerRecordDeviceDriver(pdbbase)

#   NUM_SAMPLES: 1000 (number of "compressed" samples)
#    NUM_TABLES: 2 (number of individual tables to be simulated)
#   NUM_SIGNALS: 1000 (number of signals in each table, each signal has 'num_samp', 'value', 'min', 'max', 'avg' and 'rms' columns)
# TIME_STEP_SEC: 0.001 (time difference between rows, in seconds)
#                NUM_SAMPLES / TIME_STEP_SEC = simulated acquisition rate (1 MHz)
#      NUM_ROWS: 1000 (number of rows in each PV update = 1 second worth of data)
#dbLoadRecords("/workspace/db/sim_table.db","P=SIM:,R=STAT,SCAN=1 second,NUM_TABLES=2,NUM_SIGNALS=1000,NUM_SAMPLES=1000,CONFIG=16,TIME_STEP_SEC=0.001,NUM_ROWS=1000")

# 2 tables, each with 2 signal with 1000 compressed samples in each row, 100ms between rows, 1 sec per table update
dbLoadRecords("/workspace/db/sim_table.db","P=SIM:,R=STAT,SCAN=1 second,NUM_TABLES=2,NUM_SIGNALS=2,NUM_SAMPLES=1000,CONFIG=16,TIME_STEP_SEC=0.1,NUM_ROWS=10")

iocInit()
