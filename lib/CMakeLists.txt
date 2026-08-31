add_subdirectory(CRC32)
add_subdirectory(FlashPROM)
# 接收端无 WebConfig/RNDIS：httpd 与 lwip-port 零消费者（map 验证符号不进固件），跳过编译
add_subdirectory(nanopb)
add_subdirectory(NeoPico)
add_subdirectory(OneBitDisplay)
add_subdirectory(PicoPeripherals)
# GPReceiverHML 接收端禁用 WebConfig/RNDIS，跳过 rndis 库（CFG_TUD_ECM_RNDIS=0 时缺少 CFG_TUD_NET_MTU 宏）
if(NOT GP2040_BOARDCONFIG STREQUAL "GPReceiverHML")
  add_subdirectory(rndis)
endif()
add_subdirectory(WiiExtension)
add_subdirectory(SNESpad)
