CXXFLAGS=-Wall -O3 -g -fno-strict-aliasing
BINARIES=marioclock

# Re-use the rgb-matrix library that ships with the matrixclock project.
RGB_INCDIR=../matrixclock/include
RGB_LIBDIR=../matrixclock/lib
LDFLAGS+=-L$(RGB_LIBDIR) -lrgbmatrix -lrt -lm -lpthread

all : $(BINARIES)

marioclock : marioclock.o
	$(CXX) $(CXXFLAGS) marioclock.o -o $@ $(LDFLAGS)

%.o : %.cc
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) -DADAFRUIT_RGBMATRIX_HAT -c -o $@ $<

clean:
	rm -f *.o $(BINARIES)
