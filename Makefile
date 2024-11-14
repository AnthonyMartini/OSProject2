vzip: serial.c
	gcc -pthread serial.c -lz -o vzip

test:
	rm -f video.vzip
	./vzip frames
	./check.sh

clean:
	rm -f vzip video.vzip

