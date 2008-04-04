gcc -Wall -g -c -I../../include plugtest.c
gcc -Wall -g -o plugtest plugtest.o ../dnxPlugin.o ../pfopen.o ../../nrpe-2.6/src/mod_nrpe.o ../../nrpe-2.6/src/utils.o -lcrypt -lssl
