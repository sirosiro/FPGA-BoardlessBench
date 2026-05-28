#ifndef IMX_HAL_HPP
#define IMX_HAL_HPP

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>

enum class SocType {
    IMX8MP,
    IMX95
};

class ISerialPort {
public:
    virtual ~ISerialPort() = default;
    virtual bool open(const std::string& device_path) = 0;
    virtual void close() = 0;
    virtual int read(uint8_t* buf, size_t len) = 0;
    virtual int write(const uint8_t* buf, size_t len) = 0;
};

class IGpioController {
public:
    enum class Direction {
        INPUT,
        OUTPUT
    };

    virtual ~IGpioController() = default;
    virtual bool readPin(int pin_num) = 0;
    virtual void writePin(int pin_num, bool value) = 0;
    virtual void registerInterrupt(int pin_num, std::function<void(int, bool)> callback) = 0;
};

class HalFactory {
public:
    static SocType detectSocType();
    static std::string getDefaultUartPath(SocType soc_type);
    static std::unique_ptr<ISerialPort> createSerialPort(SocType soc_type, const std::string& path = "");
    static std::unique_ptr<IGpioController> createGpioController(
        SocType soc_type, 
        const std::unordered_map<int, IGpioController::Direction>& pin_config
    );
};

#endif // IMX_HAL_HPP
