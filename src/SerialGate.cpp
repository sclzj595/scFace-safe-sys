// SerialGate.cpp
#include "SerialGate.h"
#include <QDebug>

namespace {
const unsigned char FRAME_HEAD = 0xA0;
const unsigned char ADDR = 0x01;          // 继电器板地址（默认 1）
const unsigned char CMD_ON = 0x01;
const unsigned char CMD_OFF = 0x00;
const unsigned char CHANNEL = 0x01;       // 通道 1 接闸机开门线

QString frameHex(const QByteArray& f)
{
    return QString::fromLatin1(f.toHex(' ').toUpper());
}
} // namespace

SerialGate::SerialGate(QObject* parent) : QObject(parent)
{
    _holdTimer.setSingleShot(true);
    connect(&_holdTimer, &QTimer::timeout, this, [this]() {
        if (_port.isOpen() && !sendFrame(CMD_OFF, CHANNEL))
            return;   // 关闸失败已发 errorText
        _gateOpen = false;
        emit gateClosed();
    });
}

bool SerialGate::sendFrame(unsigned char cmd, unsigned char data)
{
    QByteArray f;
    f.append(FRAME_HEAD);
    f.append(ADDR);
    f.append(cmd);
    f.append(data);
    unsigned char sum = 0;
    for (unsigned char c : f) sum += c;   // 校验和 = 前缀字节和的低 8 位
    f.append(sum);

    const qint64 n = _port.write(f);
    if (n != f.size() || !_port.flush()) {
        emit errorText(QStringLiteral("继电器指令写入失败: %1")
                           .arg(frameHex(f)));
        return false;
    }
    return true;
}

bool SerialGate::open(const QString& portName)
{
    close();
    _port.setPortName(portName);
    _port.setBaudRate(QSerialPort::Baud9600);
    _port.setDataBits(QSerialPort::Data8);
    _port.setParity(QSerialPort::NoParity);
    _port.setStopBits(QSerialPort::OneStop);
    if (!_port.open(QIODevice::ReadWrite)) {
        emit errorText(QStringLiteral("串口打开失败 %1: %2")
                           .arg(portName, _port.errorString()));
        return false;
    }
    return true;
}

void SerialGate::close()
{
    if (_holdTimer.isActive())
        _holdTimer.stop();
    if (_gateOpen) {           // 断开前尽量复位闸门
        if (isConnected())
            sendFrame(CMD_OFF, CHANNEL);
        _gateOpen = false;
        emit gateClosed();
    }
    if (_port.isOpen())
        _port.close();         // 之后 isSimulated() == true，回到模拟模式
}

void SerialGate::pulseOpen(int holdMs)
{
    if (_gateOpen)
        return;                // 已在开闸周期内，忽略重复触发
    _gateOpen = true;
    emit gateOpened();

    if (isConnected() && !sendFrame(CMD_ON, CHANNEL))
        return;                // 开闸失败：状态保持 open 由 holdTimer 关闭
    _holdTimer.start(holdMs);
}
