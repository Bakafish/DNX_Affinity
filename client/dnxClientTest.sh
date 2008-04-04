#!/bin/sh

# Cleanup and create a testrun directory
rm -rf testrun &>/dev/null
mkdir testrun

# Create a test configuration file using udp test ports 13480/1
cat << END_CONFIG_FILE > $PWD/testrun/test.cfg
channelDispatcher = udp://localhost:13480
channelCollector = udp://localhost:13481
END_CONFIG_FILE

# Execute the dnx client as a daemon
./dnxClient -c $PWD/testrun/test.cfg -r $PWD/testrun -l $PWD/testrun/test.log -D $PWD/testrun/test.dbg

# Give it time to create its lock file
count=0
while ((count<15)) && [ ! -e $PWD/testrun/dnxClient.pid ]; do
	sleep 1; let count++
done

# Ensure we HAVE a lock file now
if [ ! -e $PWD/testrun/dnxClient.pid ]; then
	echo "dnxClient.pid NOT found - daemon didn't start!"
	exit 1
fi

# Send a SIGTERM - wait up to 15 seconds for it to die
# echo "dnxClient PID:" `cat $PWD/testrun/dnxClient.pid`
kill `cat $PWD/testrun/dnxClient.pid`

# Give it time to remove its lock file
count=0
while ((count<15)) && [ -e $PWD/testrun/dnxClient.pid ]; do
	sleep 1; let count++
done

# Ensure we DON'T have a lock file now
if [ -e $PWD/testrun/dnxClient.pid ]; then
	echo "dnxClient.pid still exists - daemon didn't stop!"
	exit 1
fi

# Remove all daemon droppings
rm -rf testrun

exit 0
