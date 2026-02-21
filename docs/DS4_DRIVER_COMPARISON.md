# DS4 驱动与标准对比 (ControllersInfo + Linux hid-sony)

参考：
- [ControllersInfo/dualshock4](https://github.com/DJm00n/ControllersInfo/tree/master/dualshock4) — `dualshock4_hid_report_descriptor.txt`
- Linux `hid-sony` / `hid-playstation` — DS4 报告解析与校准

---

## 已对齐部分

### Input Report 0x01（64 字节）
- **Report ID + 前 10 字节**：与 ControllersInfo 一致  
  - 1 字节 Report ID 0x01  
  - 4 字节 X/Y/Z/Rz（左/右摇杆）  
  - 4 bit Hat + 14 bit Buttons + 6 bit Report Counter  
  - 2 字节 Rx/Ry（扳机）
- **54 字节 Vendor 块（0xFF000021）**：布局已按标准修正  
  - 字节 10–11：Timestamp（axisTiming）  
  - 字节 12：Temperature（1 字节，填 0）  
  - 字节 13–18：陀螺 X/Z/Y（int16 LE）  
  - 字节 19–24：加速度 X/Y/Z（int16 LE）  
  - 字节 25–29：ExtData[5] + 电源/状态位  
  - 后续：触摸与 padding（TouchpadData + mystery2）

### Output Report 0x05
- 31 字节（0x1F），与 ControllersInfo 一致。

### HID Report Descriptor — Input/Output 部分
- Report ID 1：4+4+14+6+2+54 与标准一致。  
- Report ID 5：31 字节 Output 与标准一致。

---

## 与标准不一致之处（已修复）

### 1. Feature Report 顺序与 Report ID 4（已修复）

**标准顺序（ControllersInfo）：** 0x05 (Output 31) → **0x04 (Feature 36)** → 0x02 (Feature 36) → 0x08, …

**当前实现：** 已在描述符中在 0x05 之后插入 **Report ID 0x04（36 字节，Usage 0xFF000023）**，再为 0x03、0x02。`get_report` 对 report_id 0x04 返回 36 字节（零填充），PS4Driver 与 PS4BDriver 均已处理。

---

### 2. Feature Report 0x88 (136) 长度（已修复）

**标准：** Report ID 0x88 — 63 字节 (0x95, 0x3F)。  
**当前：** 描述符已改为 0x95, 0x3F（63 字节）；`get_report` 对 report_id 0x88 返回 63 字节（零填充）。

---

### 3. 6 bit Report Counter 的 Logical Maximum

**ControllersInfo 描述符：**  
- Usage 0x20：Report Size 6，Logical Maximum **0x7F (127)**  
- 描述符注释已注明：6 bit 无法表示 127，为描述符错误。

**当前项目：**  
- Report Size 6，未显式设 Logical Maximum，实际有效范围 0–63，与 6 bit 一致，**无问题**。

---

## 小结

| 项目           | 状态 | 说明 |
|----------------|------|------|
| Input 0x01 结构 | 已对齐 | 54 字节块内传感器/时间戳/触摸布局已按标准修正 |
| 传感器字节序/顺序 | 已对齐 | LE，陀螺 X/Z/Y，加速度 X/Y/Z |
| Output 0x05     | 已对齐 | 31 字节 |
| Feature Report 4 | 已对齐 | 描述符 0x05 后增加 0x04（36 字节）；get_report 返回 36 字节 |
| Feature Report 0x88 长度 | 已对齐 | 描述符改为 63 字节；get_report 返回 63 字节 |
| Report Counter 逻辑范围 | 已对齐 | 实际 0–63，无问题 |

驱动已按 ControllersInfo 与 Linux hid-sony 的 DS4 标准补全上述项。

---

## 严格复查（与 ControllersInfo 逐项对照）

### Report 描述符顺序（标准 vs 本项目）

| 顺序 | 标准 (ControllersInfo) | 本项目 | 说明 |
|------|------------------------|--------|------|
| 1 | 0x01 Input (4+4+14+6+2+54) | 同左 | 一致 |
| 2 | 0x05 Output 31 | 同左 | 一致 |
| 3 | **0x04** Feature 36 (0x23) | **0x04** Feature 36 | 一致 |
| 4 | **0x02** Feature 36 (0x24) | **0x03** Feature 47 (0x2721) | 本项目多 Report 3（控制器定义，PS4 认证用） |
| 5 | 0x08 Feature 3 | 0x02 Feature 36 | 标准无 0x03，故 0x02 在标准中为第 4 项 |
| 6 | 0x10, 0x11, 0x12, 0x13, 0x14, 0x15 | 0x08, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15 | 0x02 之后与标准一致 |
| 7 | 0x80～0xD4 等 (FF80 页) | 同左，且 0x88 为 63 字节 | 0x88 已修正为 63 字节 |

**结论：** 标准中无 Report ID 0x03；本项目中 0x03（47 字节，Usage 0x2721）为**有意扩展**，用于 PS4 主机认证时的控制器定义，顺序为 0x05 → 0x04 → **0x03** → 0x02 → 0x08…，其余与标准一致。

### Get_Report 行为

- **Report 0x04**：`reqlen < 36` 时返回 `(uint16_t)-1`（错误）；否则写入 36 字节 0，返回 36。
- **Report 0x88**：`reqlen < 63` 时返回 `(uint16_t)-1`；否则写入 63 字节 0，返回 63。
- 与其余 Feature 报告错误时返回 `-1` 的约定一致。

### 未实现的 Feature Report

标准中 0x80～0xD4 等大量 Feature 仅描述符声明，主机 Get_Report 时若未在驱动中单独处理会走 default 并返回 -1。0x04 与 0x88 已按标准实现；其余多为校准/扩展，游戏通常不依赖。
