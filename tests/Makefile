
INC+=.
LIBS+=`pkg-config --libs cpputest`
CPPFLAGS+=-I$(INC)

run: runtests
	./runtests

runtests: hello.o hellotest.o TuningSystem.o
	g++ -o $@ $^ $(LIBS)

TuningSystem.o:
	g++ -I. -o $@ -c ../src/TuningSystem.cpp

.PHONY: clean
clean:
	rm -f runtests ./*.o


