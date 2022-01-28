all: crc crsd
	
crc: crc.cpp
	g++ crc.cpp -g -w -o crc -lpthread -lrt
	
crsd: crsd.cpp
	g++ crsd.cpp -g -w -o crsd -lpthread -lrt
	
clean:
	rm -f crc
	rm -f crsd