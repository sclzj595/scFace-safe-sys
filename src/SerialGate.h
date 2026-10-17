// SerialGate.h —— 串口继电器闸机控制（业务无关的设备封装）
//
// 协议（可配置，缺省 = 常见 USB/串口继电器板 CH340 类 1 字节帧）：
//   开继电器：A0 01 01 A2 ；关继电器：A0 01 00 A1
//   （帧头 A0 + 地址 01 + 命令 + 通道 + 校验和低 8 位）
//   宿主从 exe 旁 gate.ini [Serial] 读取 GateProtocol 注入（4b：换板卡不改代码）。
//
// 模拟模式：无硬件演示用，pulseOpen 仅作状态时序（开 → hold → 关），
// 不触碰任何串口。默认模拟，连接成功后自动切真实模式。
#ifndef SERIALGATE_H
#define SERIALGATE_H

#pragma execution_character_set("utf-8")

#include <QObject>
#include <QSerialPort>
#include <QTimer>

// 继电器板协议参数（缺省 = 默认板卡 0xA0 帧）
struct GateProtocol {
    unsigned char head = 0xA0;      // 帧头
    unsigned char addr = 0x01;      // 板卡地址
    unsigned char cmdOn = 0x01;     // 开命令
    unsigned char cmdOff = 0x00;    // 关命令
    unsigned char channel = 0x01;   // 通道（接闸机开门线）
    int baud = 9600;                // 波特率
};

class SerialGate : public QObject
{
    Q_OBJECT
public:
    explicit SerialGate(QObject* parent = nullptr);

    void setProtocol(const GateProtocol& p) { _proto = p; }   // open() 前注入
    const GateProtocol& protocol() const { return _proto; }

    bool open(const QString& portName);   // 连接串口（退出模拟模式）
    void close();                          // 断开（回到模拟模式）
    bool isConnected() const { return _port.isOpen(); }
    bool isSimulated() const { return !_port.isOpen(); }
    bool isOpen() const { return _gateOpen; }   // 闸门当前状态（开/关）

    void pulseOpen(int holdMs = 3000);     // 开闸 → holdMs 后自动关闸

signals:
    void gateOpened();                     // 闸门开始放行
    void gateClosed();                     // 闸门关闭
    void errorText(const QString& msg);

private:
    bool sendFrame(unsigned char cmd, unsigned char data);

    QSerialPort _port;
    QTimer _holdTimer;
    bool _gateOpen = false;
    GateProtocol _proto;
};

#endif // SERIALGATE_H
