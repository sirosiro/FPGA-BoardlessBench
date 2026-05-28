CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -I../../src/include -I.
LDFLAGS = -lpthread

TARGET = test_bin
SRCS = main.cpp hal/imx_hal.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)
