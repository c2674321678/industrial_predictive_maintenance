### 基于 FreeRTOS 与 AI 的工业设备智能预维护系统

项目简介

工业设备长期运行中因轴承磨损、转子不平衡等原因会产生异常振动和温升，传统人工巡检难以及时发现早期故障。
本项目设计了一套端云协同的设备状态监测与智能预警系统，通过嵌入式终端实时采集设备运行参数，经 MQTT 协议上传至云端 AI 引擎进行异常检测，
并将推理结果下发至终端执行分级报警，实现从"事后维修"到"预测性维护"的转变。

核心功能

- 嵌入式终端：基于 FreeRTOS 多任务架构采集温度（DS18B20）和振动（MPU6050）数据
- 无线通信：ESP-01S 模块通过 AT 指令实现 WiFi 联网与 MQTT 通信
- 云端推理：百度云天工 IoT 平台 + Isolation Forest 异常检测模型
- 分级报警：终端根据云端推理结果控制 LED 与蜂鸣器执行不同级别报警
- 断网恢复：五阶段容错状态机，断网 15 秒内自动恢复连接

技术栈

- 嵌入式端：STM32F103ZET6、FreeRTOS、C 语言、HAL 库
- 传感器：DS18B20（温度）、MPU6050（六轴）
- 通信协议：MQTT 3.1.1、UART、AT 指令集
- 无线模块：ESP-01S（WiFi）
- 云平台：百度云天工 IoT
- 服务端：Python、Flask、scikit-learn（Isolation Forest）
- 工具链：Keil MDK v5、STM32CubeMX、ESP Flash Download Tool

目录结构
'''
industrial_predictive_maintenance/
├── STM32_Code/ 
│ ├── Drivers/ 
│ ├── Projects/
│ └── User/ 
├── Cloud_Code/ 
│ └── server.py 
└── Docs/ 
├── 基于FreeRTOS与AI的工业设备智能预维护系统-项目介绍.docx
├── 基于FreeRTOS与AI的工业设备智能预维护系统-项目介绍.pdf
└── 基于FreeRTOS与AI的工业设备智能预维护系统演示视频.mp4
'''


核心算法与关键技术

FreeRTOS 多任务架构

设计三个独立任务，通过 xQueueOverwrite 实现数据传递：

| 任务 | 优先级 | 功能 |
|------|--------|------|
| MQTT 通信任务 | 最高 | 处理网络连接与云端通信，确保实时响应 |
| 传感器采集任务 | 中等 | 周期性采集温度和振动数据 |
| LCD 显示任务 | 最低 | 更新本地显示信息 |

五阶段容错状态机

ESP-01S 连接流程分为五个阶段，任一阶段失败自动回退重连，重连间隔指数退避（1s -> 2s -> 4s -> ... -> 60s 上限）：

WiFi 连接 -> TCP 连接 -> MQTT 连接 -> 订阅 Topic -> 数据收发

DS18B20 时序保护

使用 DWT 硬件计数器实现微秒级延时，配合 vTaskSuspendAll() 在关键时序段挂起任务调度，解决 FreeRTOS 中断打断传感器读取导致数据异常的问题。

 轻量级 JSON 解析器

手写 JSON 解析器提取云端推理结果，不依赖 cJSON 等第三方库，代码量约 200 行，适合资源受限的嵌入式环境。

项目成果

- 端到端延迟：<= 2 秒
- 温度精度：0.1℃
- 振动检测灵敏度：0.01g
- 72 小时连续运行无宕机
- 网络中断后 15 秒内自动恢复连接

运行方式

云端服务

```bash
cd Cloud_Code
pip install flask scikit-learn numpy paho-mqtt
python server.py

后续优化方向
增加更多传感器（电流、电压）扩展监测维度

接入更多云平台（阿里云、AWS IoT）提升兼容性

在终端侧部署轻量级 AI 模型，实现边缘端实时推理

作者
GitHub: c2674321678

License
MIT License
