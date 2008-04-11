#!/bin/sh

# Cleanup and create a testrun directory
rm -rf testrun &>/dev/null
mkdir testrun

# Create a test configuration file using udp test ports 13480/1
cat << END_CONFIG_FILE > $PWD/testrun/test.cfg
channelDispatcher = udp://localhost:13480
channelCollector = udp://localhost:13481
channelAgent = udp://localhost:13482
logFile = $PWD/testrun/test.log
debugFile = $PWD/testrun/test.dbg
debugLevel = 3
END_CONFIG_FILE

# Execute the dnx client as a daemon
../dnxClient -c $PWD/testrun/test.cfg -r $PWD/testrun

# Give it time to create its lock file
count=0
while ((count<30)) && [ ! -e $PWD/testrun/dnxClient.pid ]; do
	sleep 1; let count++
done

# Ensure we HAVE a lock file now
if [ ! -e $PWD/testrun/dnxClient.pid ]; then
	echo "dnxClient.pid NOT found - daemon didn't start!"
	exit 1
fi

# Hit it with a dnxstats GETVERSION request
../../stats/dnxstats -p 13482 -c GETVERSION
status=$?
if [ $status -ne 0 ]; then
	echo "dnxstats couldn't request client version!"
fi

# Send a SIGTERM - wait up to 15 seconds for it to die
# echo "dnxClient PID:" `cat $PWD/testrun/dnxClient.pid`
kill `cat $PWD/testrun/dnxClient.pid`

# Give it time to remove its lock file
count=0
while ((count<60)) && [ -e $PWD/testrun/dnxClient.pid ]; do
	sleep 1; let count++
done

# Ensure we DON'T have a lock file now
if [ -e $PWD/testrun/dnxClient.pid ]; then
	echo "dnxClient.pid still exists - daemon didn't stop!"
	exit 1
fi

# Remove all daemon droppings
rm -rf testrun

exit $status
