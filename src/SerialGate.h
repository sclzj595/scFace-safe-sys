// SerialGate.h —— 串口继电器闸机控制（业务无关的设备封装）
//
// 协议：常见 USB/串口继电器板（CH340 类），1 字节帧：
//   开继电器1：A0 01 01 A2 ；关继电器1：A0 01 00 A1
//   （帧头 A0 + 地址 01 + 命令 + 校验和低 8 位）
//
// 模拟模式：无硬件演示用，pulseOpen 仅作状态时序（开 → hold → 关），
// 不触碰任何串口。默认模拟，连接成功后自动切真实模式。
#ifndef SERIALGATE_H
#define SERIALGATE_H

#pragma execution_character_set("utf-8")

#include <QObject>
#include <QSerialPort>
#include <QTimer>

class SerialGate : public QObject
{
    Q_OBJECT
public:
    explicit SerialGate(QObject* parent = nullptr);

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
};

#endif // SERIALGATE_H
