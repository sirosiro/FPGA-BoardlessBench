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

template <typename T, typename = void> struct has_irq_out : std::false_type {};
template <typename T> struct has_irq_out<T, std::void_t<decltype(std::declval<T>().irq_out)>> : std::true_type {};

template <typename T, typename = void> struct has_l_pins_i : std::false_type {};
template <typename T> struct has_l_pins_i<T, std::void_t<decltype(std::declval<T>().l_pins_i)>> : std::true_type {};

// AXI Master/Slave Read Channel Handshakes
template <typename T, typename = void> struct has_m_axi_araddr : std::false_type {};
template <typename T> struct has_m_axi_araddr<T, std::void_t<decltype(std::declval<T>().m_axi_araddr)>> : std::true_type {};

template <typename T, typename = void> struct has_m_axi_arvalid : std::false_type {};
template <typename T> struct has_m_axi_arvalid<T, std::void_t<decltype(std::declval<T>().m_axi_arvalid)>> : std::true_type {};

template <typename T, typename = void> struct has_m_axi_arready : std::false_type {};
template <typename T> struct has_m_axi_arready<T, std::void_t<decltype(std::declval<T>().m_axi_arready)>> : std::true_type {};

template <typename T, typename = void> struct has_m_axi_rdata : std::false_type {};
template <typename T> struct has_m_axi_rdata<T, std::void_t<decltype(std::declval<T>().m_axi_rdata)>> : std::true_type {};

template <typename T, typename = void> struct has_m_axi_rvalid : std::false_type {};
template <typename T> struct has_m_axi_rvalid<T, std::void_t<decltype(std::declval<T>().m_axi_rvalid)>> : std::true_type {};

template <typename T, typename = void> struct has_m_axi_rready : std::false_type {};
template <typename T> struct has_m_axi_rready<T, std::void_t<decltype(std::declval<T>().m_axi_rready)>> : std::true_type {};

// AXI Master/Slave Write Channel Handshakes
template <typename T, typename = void> struct has_m_axi_awaddr : std::false_type {};
template <typename T> struct has_m_axi_awaddr<T, std::void_t<decltype(std::declval<T>().m_axi_awaddr)>> : std::true_type {};

template <typename T, typename = void> struct has_m_axi_awvalid : std::false_type {};
template <typename T> struct has_m_axi_awvalid<T, std::void_t<decltype(std::declval<T>().m_axi_awvalid)>> : std::true_type {};

template <typename T, typename = void> struct has_m_axi_awready : std::false_type {};
template <typename T> struct has_m_axi_awready<T, std::void_t<decltype(std::declval<T>().m_axi_awready)>> : std::true_type {};

template <typename T, typename = void> struct has_m_axi_wdata : std::false_type {};
template <typename T> struct has_m_axi_wdata<T, std::void_t<decltype(std::declval<T>().m_axi_wdata)>> : std::true_type {};

template <typename T, typename = void> struct has_m_axi_wvalid : std::false_type {};
template <typename T> struct has_m_axi_wvalid<T, std::void_t<decltype(std::declval<T>().m_axi_wvalid)>> : std::true_type {};

template <typename T, typename = void> struct has_m_axi_wready : std::false_type {};
template <typename T> struct has_m_axi_wready<T, std::void_t<decltype(std::declval<T>().m_axi_wready)>> : std::true_type {};

template <typename T, typename = void> struct has_m_axi_bvalid : std::false_type {};
template <typename T> struct has_m_axi_bvalid<T, std::void_t<decltype(std::declval<T>().m_axi_bvalid)>> : std::true_type {};

template <typename T, typename = void> struct has_m_axi_bready : std::false_type {};
template <typename T> struct has_m_axi_bready<T, std::void_t<decltype(std::declval<T>().m_axi_bready)>> : std::true_type {};
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
template <typename T> struct has_irq_out : std::false_type {};
template <typename T> struct has_l_pins_i : std::false_type {};
template <typename T> struct has_m_axi_araddr : std::false_type {};
template <typename T> struct has_m_axi_arvalid : std::false_type {};
template <typename T> struct has_m_axi_arready : std::false_type {};
template <typename T> struct has_m_axi_rdata : std::false_type {};
template <typename T> struct has_m_axi_rvalid : std::false_type {};
template <typename T> struct has_m_axi_rready : std::false_type {};
template <typename T> struct has_m_axi_awaddr : std::false_type {};
template <typename T> struct has_m_axi_awvalid : std::false_type {};
template <typename T> struct has_m_axi_awready : std::false_type {};
template <typename T> struct has_m_axi_wdata : std::false_type {};
template <typename T> struct has_m_axi_wvalid : std::false_type {};
template <typename T> struct has_m_axi_wready : std::false_type {};
template <typename T> struct has_m_axi_bvalid : std::false_type {};
template <typename T> struct has_m_axi_bready : std::false_type {};
#endif

#endif
