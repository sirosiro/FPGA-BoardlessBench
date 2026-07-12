#ifndef SIM_TRAITS_H
#define SIM_TRAITS_H

#include <type_traits>

#ifndef __VERILATOR__
// GCC/Clang: Real SFINAE detection
template <typename T, typename = void> struct has_clk : std::false_type {};
template <typename T> struct has_clk<T, std::void_t<decltype(std::declval<T>().clk)>> : std::true_type {};

template <typename T, typename = void> struct has_rst_n : std::false_type {};
template <typename T> struct has_rst_n<T, std::void_t<decltype(std::declval<T>().rst_n)>> : std::true_type {};

template <typename T, typename = void> struct has_addr : std::false_type {};
template <typename T> struct has_addr<T, std::void_t<decltype(std::declval<T>().addr)>> : std::true_type {};

template <typename T, typename = void> struct has_w_data : std::false_type {};
template <typename T> struct has_w_data<T, std::void_t<decltype(std::declval<T>().w_data)>> : std::true_type {};

template <typename T, typename = void> struct has_w_en : std::false_type {};
template <typename T> struct has_w_en<T, std::void_t<decltype(std::declval<T>().w_en)>> : std::true_type {};

template <typename T, typename = void> struct has_r_data : std::false_type {};
template <typename T> struct has_r_data<T, std::void_t<decltype(std::declval<T>().r_data)>> : std::true_type {};

template <typename T, typename = void> struct has_pl_spi_sclk : std::false_type {};
template <typename T> struct has_pl_spi_sclk<T, std::void_t<decltype(std::declval<T>().pl_spi_sclk)>> : std::true_type {};

template <typename T, typename = void> struct has_pl_spi_mosi : std::false_type {};
template <typename T> struct has_pl_spi_mosi<T, std::void_t<decltype(std::declval<T>().pl_spi_mosi)>> : std::true_type {};

template <typename T, typename = void> struct has_pl_spi_miso : std::false_type {};
template <typename T> struct has_pl_spi_miso<T, std::void_t<decltype(std::declval<T>().pl_spi_miso)>> : std::true_type {};

template <typename T, typename = void> struct has_pl_spi_cs_n : std::false_type {};
template <typename T> struct has_pl_spi_cs_n<T, std::void_t<decltype(std::declval<T>().pl_spi_cs_n)>> : std::true_type {};
#else
// Verilator internal parser: Dummy definitions to prevent parsing overhead
template <typename T> struct has_clk : std::false_type {};
template <typename T> struct has_rst_n : std::false_type {};
template <typename T> struct has_addr : std::false_type {};
template <typename T> struct has_w_data : std::false_type {};
template <typename T> struct has_w_en : std::false_type {};
template <typename T> struct has_r_data : std::false_type {};
template <typename T> struct has_pl_spi_sclk : std::false_type {};
template <typename T> struct has_pl_spi_mosi : std::false_type {};
template <typename T> struct has_pl_spi_miso : std::false_type {};
template <typename T> struct has_pl_spi_cs_n : std::false_type {};
#endif

#endif
