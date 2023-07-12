
LIBS+=`pkg-config --libs cpputest`
CPPFLAGS+=-I. -O0 -coverage

UNITS = TuningSystem
MODULES=$(patsubst %,%.o,$(UNITS))
TESTS=$(patsubst %,%.test.o,$(UNITS))

run: cleancover runtests
	./runtests

runtests: runtests.o $(TESTS) $(MODULES)
	g++ -O0 -coverage -o $@ $^ $(LIBS)

%.o: ../src/%.cpp
	g++ $(CPPFLAGS) -o $@ -c $^

cover:
	gcov runtests
	lcov --ignore-errors mismatch --directory . --capture --output-file coverage.info
	genhtml --demangle-cpp -o coverage coverage.info

.PHONY: clean cleancover
cleancover:
	rm -f *.gc{da,no,ov}

clean: cleancover
	rm -f runtests ./*.o

