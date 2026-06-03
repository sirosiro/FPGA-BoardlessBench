CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -I../../src/include -I.
LDFLAGS = -lpthread

# F-BBホストと実機クロスビルドの分離
ifeq ($(ARCH),)
    # F-BBホスト環境: Mesaライブラリをリンク
    LDFLAGS += -lEGL -lGLESv2
else
    # 実機クロス環境: ベンダー固有GPUライブラリをリンク (Sysroot想定)
    LDFLAGS += -lEGL -lGLESv2
endif

TARGET = test_bin
SRCS = main.cpp hal/imx_hal.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)
